/**
 * @file main.c
 * @brief Example application using SPI NAND Flash (W25N01GV) with ESP32-S3
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "spiflash.h"

static const char *TAG = "MAIN";

// SPI Configuration for ESP32-S3
#define SPI_HOST            SPI2_HOST
#define PIN_MOSI            5       // MOSI pin
#define PIN_MISO            4       // MISO pin
#define PIN_SCLK            6       // SCLK pin
#define PIN_CS              17      // CS pin
#define SPI_CLOCK_SPEED     10000000  // 10 MHz

/**
 * @brief Test basic read/write operations
 */
static void test_basic_operations(spiflash_handle_t *flash) {
    ESP_LOGI(TAG, "=== Testing Basic Operations ===");
    
    // Allocate buffers on heap instead of stack
    uint8_t *write_buffer = malloc(SPIFLASH_PAGE_SIZE);
    uint8_t *read_buffer = malloc(SPIFLASH_PAGE_SIZE);
    
    if (write_buffer == NULL || read_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        free(write_buffer);
        free(read_buffer);
        return;
    }
    
    // Fill with test pattern
    for (int i = 0; i < SPIFLASH_PAGE_SIZE; i++) {
        write_buffer[i] = (i & 0xFF);
    }
    
    uint32_t test_page = 100;  // Use page 100
    
    // Erase the block containing this page (block 1 contains pages 64-127)
    uint32_t test_block = test_page / SPIFLASH_PAGES_PER_BLOCK;
    ESP_LOGI(TAG, "Erasing block %" PRIu32 "...", test_block);
    esp_err_t ret = spiflash_erase_block(flash, test_block);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(ret));
        free(write_buffer);
        free(read_buffer);
        return;
    }
    
    // Write page
    ESP_LOGI(TAG, "Writing page %" PRIu32 " (%d bytes)...", test_page, SPIFLASH_PAGE_SIZE);
    ESP_LOG_BUFFER_HEX_LEVEL("WRITE", write_buffer, 16, ESP_LOG_INFO);
    ret = spiflash_write_page(flash, test_page, write_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(ret));
        return;
    }

    // Read page back
    ESP_LOGI(TAG, "Reading page %" PRIu32 "...", test_page);
    ret = spiflash_read_page(flash, test_page, read_buffer);
    ESP_LOG_BUFFER_HEX_LEVEL("READ", read_buffer, 16, ESP_LOG_INFO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Verify data
    if (memcmp(write_buffer, read_buffer, SPIFLASH_PAGE_SIZE) == 0) {
        ESP_LOGI(TAG, "✓ Read/Write test PASSED");
    } else {
        ESP_LOGE(TAG, "✗ Read/Write test FAILED - Data mismatch");
        // Print first differences
        for (int i = 0; i < 32; i++) {
            if (write_buffer[i] != read_buffer[i]) {
                ESP_LOGE(TAG, "First mismatch at byte %d: wrote 0x%02X, read 0x%02X", 
                         i, write_buffer[i], read_buffer[i]);
                break;
            }
        }
    }
    
    free(write_buffer);
    free(read_buffer);
}

/**
 * @brief Test multiple page writes
 */
static void test_multiple_pages(spiflash_handle_t *flash) {
    ESP_LOGI(TAG, "=== Testing Multiple Pages ===");
    
    // Allocate buffers on heap instead of stack
    uint8_t *write_buffer = malloc(SPIFLASH_PAGE_SIZE);
    uint8_t *read_buffer = malloc(SPIFLASH_PAGE_SIZE);
    
    if (write_buffer == NULL || read_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        free(write_buffer);
        free(read_buffer);
        return;
    }
    
    uint32_t test_block = 2;
    uint32_t start_page = test_block * SPIFLASH_PAGES_PER_BLOCK;
    
    // Erase block
    ESP_LOGI(TAG, "Erasing block %" PRIu32 "...", test_block);
    esp_err_t ret = spiflash_erase_block(flash, test_block);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(ret));
        free(write_buffer);
        free(read_buffer);
        return;
    }
    
    // Write and verify multiple pages
    int num_pages = 4;
    for (int i = 0; i < num_pages; i++) {
        uint32_t page = start_page + i;
        
        // Create unique pattern for each page
        for (int j = 0; j < SPIFLASH_PAGE_SIZE; j++) {
            write_buffer[j] = (i + j) & 0xFF;
        }
        
        // Write
        ret = spiflash_write_page(flash, page, write_buffer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write page %" PRIu32 " failed: %s", page, esp_err_to_name(ret));
            free(write_buffer);
            free(read_buffer);
            return;
        }
        
        // Read back
        ret = spiflash_read_page(flash, page, read_buffer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Read page %" PRIu32 " failed: %s", page, esp_err_to_name(ret));
            free(write_buffer);
            free(read_buffer);
            return;
        }
        
        // Verify
        if (memcmp(write_buffer, read_buffer, SPIFLASH_PAGE_SIZE) == 0) {
            ESP_LOGI(TAG, "✓ Page %" PRIu32 " OK", page);
        } else {
            ESP_LOGE(TAG, "✗ Page %" PRIu32 " FAILED", page);
            free(write_buffer);
            free(read_buffer);
            return;
        }
    }
    
    ESP_LOGI(TAG, "✓ Multiple page test PASSED");
    free(write_buffer);
    free(read_buffer);
}

/**
 * @brief Display chip information
 */
static void display_chip_info(spiflash_handle_t *flash) {
    ESP_LOGI(TAG, "=== Flash Chip Information ===");
    
    ESP_LOGI(TAG, "JEDEC ID: %02X %02X %02X", 
             flash->jedec_id[0], flash->jedec_id[1], flash->jedec_id[2]);
    
    if (flash->jedec_id[0] == 0xEF && flash->jedec_id[1] == 0xAA && 
        flash->jedec_id[2] == 0x21) {
        ESP_LOGI(TAG, "Manufacturer: Winbond");
        ESP_LOGI(TAG, "Device: W25N01GV (1Gb SPI NAND)");
    }
    
    ESP_LOGI(TAG, "Page Size: %" PRIu32 " bytes", (uint32_t)SPIFLASH_PAGE_SIZE);
    ESP_LOGI(TAG, "Pages per Block: %" PRIu32, (uint32_t)SPIFLASH_PAGES_PER_BLOCK);
    ESP_LOGI(TAG, "Block Size: %" PRIu32 " bytes (128 KB)", (uint32_t)SPIFLASH_BLOCK_SIZE);
    ESP_LOGI(TAG, "Total Blocks: %" PRIu32, (uint32_t)SPIFLASH_TOTAL_BLOCKS);
    ESP_LOGI(TAG, "Total Size: 1 GB");
    
    uint8_t status;
    spiflash_read_status(flash, &status);
    ESP_LOGI(TAG, "Status Register: 0x%02X", status);
    ESP_LOGI(TAG, "Busy: %s", (status & 0x01) ? "Yes" : "No");
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting SPI NAND Flash example for ESP32-S3");
    
    // Configure SPI Flash
    spiflash_config_t flash_cfg = {
        .host_id = SPI_HOST,
        .pin_mosi = PIN_MOSI,
        .pin_miso = PIN_MISO,
        .pin_sclk = PIN_SCLK,
        .pin_cs = PIN_CS,
        .clock_speed_hz = SPI_CLOCK_SPEED,
    };
    
    // Initialize SPI Flash
    spiflash_handle_t *flash = NULL;
    esp_err_t ret = spiflash_init(&flash_cfg, &flash);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI Flash: %s", esp_err_to_name(ret));
        return;
    }
    
    // Display chip information
    display_chip_info(flash);
    
    // Run tests
    vTaskDelay(pdMS_TO_TICKS(1000));
    test_basic_operations(flash);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    test_multiple_pages(flash);
    
    ESP_LOGI(TAG, "=== All tests complete ===");
    
    // Cleanup
    spiflash_deinit(flash);
    
    ESP_LOGI(TAG, "Example finished");
}
