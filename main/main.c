/**
 * @file main.c
 * @brief Example application using SPI NAND Flash (W25N01GV) with FAT Filesystem
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/spi_pins.h"
#include "spi_nand_flash.h"
#include "esp_vfs_fat_nand.h"

static const char *TAG = "MAIN";

// SPI Configuration for ESP32-S3
#define SPI_HOST            SPI2_HOST
#define PIN_MOSI            5       // MOSI pin
#define PIN_MISO            4       // MISO pin
#define PIN_SCLK            6       // SCLK pin
#define PIN_CS              17      // CS pin
#define PIN_WP              2       // WP pin (optional, can be -1)
#define PIN_HD              4       // HD pin (optional, can be -1)
#define SPI_CLOCK_SPEED     40000000  // 40 MHz

// Mount path for the file system
const char *base_path = "/nandflash";

// Sample data to test with
const char *sample_text = "ESP32-S3 SPI NAND Flash File System Test\n"
                         "This data is stored on W25N01GV NAND Flash.\n"
                         "Demonstrating FAT filesystem operations.\n";

/**
 * @brief Initialize SPI NAND Flash device
 */
static esp_err_t init_nand_flash(spi_nand_flash_device_t **out_handle, spi_device_handle_t *spi_handle) {
    // Configure SPI bus
    spi_bus_config_t bus_config = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadhd_io_num = PIN_HD,
        .quadwp_io_num = PIN_WP,
        .max_transfer_sz = 4096 * 2,
    };

    ESP_LOGI(TAG, "Initializing SPI bus on SPI%d", SPI_HOST + 1);
    esp_err_t ret = spi_bus_initialize(SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_CLOCK_SPEED,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 10,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    spi_device_handle_t spi;
    ret = spi_bus_add_device(SPI_HOST, &devcfg, &spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        spi_bus_free(SPI_HOST);
        return ret;
    }

    // Initialize NAND Flash
    spi_nand_flash_config_t nand_config = {
        .device_handle = spi,
        .io_mode = SPI_NAND_IO_MODE_SIO,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    spi_nand_flash_device_t *nand_device;
    ret = spi_nand_flash_init_device(&nand_config, &nand_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NAND Flash: %s", esp_err_to_name(ret));
        spi_bus_remove_device(spi);
        spi_bus_free(SPI_HOST);
        return ret;
    }

    *out_handle = nand_device;
    *spi_handle = spi;

    ESP_LOGI(TAG, "SPI NAND Flash initialized successfully");
    return ESP_OK;
}

/**
 * @brief Deinitialize SPI NAND Flash device
 */
static void deinit_nand_flash(spi_nand_flash_device_t *flash, spi_device_handle_t spi) {
    spi_nand_flash_deinit_device(flash);
    spi_bus_remove_device(spi);
    spi_bus_free(SPI_HOST);
    ESP_LOGI(TAG, "SPI NAND Flash deinitialized");
}

/**
 * @brief Display chip and filesystem information
 */
static void display_info(spi_nand_flash_device_t *flash) {
    ESP_LOGI(TAG, "=== Flash Chip Information ===");
    
    // Get flash capacity info
    uint32_t sector_num, sector_size;
    if (spi_nand_flash_get_capacity(flash, &sector_num) == ESP_OK) {
        ESP_LOGI(TAG, "Total Sectors: %" PRIu32, sector_num);
    }
    if (spi_nand_flash_get_sector_size(flash, &sector_size) == ESP_OK) {
        ESP_LOGI(TAG, "Sector Size: %" PRIu32 " bytes", sector_size);
        ESP_LOGI(TAG, "Total Capacity: %" PRIu32 " MB", (sector_num * sector_size) / (1024 * 1024));
    }
    
    // Get filesystem info
    uint64_t bytes_total, bytes_free;
    esp_err_t ret = esp_vfs_fat_info(base_path, &bytes_total, &bytes_free);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "=== Filesystem Information ===");
        ESP_LOGI(TAG, "Total space: %" PRIu64 " KB", bytes_total / 1024);
        ESP_LOGI(TAG, "Free space: %" PRIu64 " KB", bytes_free / 1024);
        ESP_LOGI(TAG, "Used space: %" PRIu64 " KB", (bytes_total - bytes_free) / 1024);
    }
}

/**
 * @brief Test file creation and write operations
 */
static void test_file_write(void) {
    ESP_LOGI(TAG, "=== Testing File Write ===");
    
    // Create and write to a text file
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/test.txt", base_path);
    
    ESP_LOGI(TAG, "Creating file: %s", filepath);
    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    
    fprintf(f, "%s", sample_text);
    fprintf(f, "ESP-IDF Version: %s\n", esp_get_idf_version());
    fclose(f);
    
    ESP_LOGI(TAG, "✓ File written successfully");
}

/**
 * @brief Test file read operations
 */
static void test_file_read(void) {
    ESP_LOGI(TAG, "=== Testing File Read ===");
    
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/test.txt", base_path);
    
    ESP_LOGI(TAG, "Reading file: %s", filepath);
    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    
    char line[128];
    ESP_LOGI(TAG, "File contents:");
    while (fgets(line, sizeof(line), f) != NULL) {
        // Remove newline
        char *pos = strchr(line, '\n');
        if (pos) *pos = '\0';
        ESP_LOGI(TAG, "  %s", line);
    }
    fclose(f);
    
    ESP_LOGI(TAG, "✓ File read successfully");
}

/**
 * @brief Test battery data file operations
 */
static void test_battery_data(void) {
    ESP_LOGI(TAG, "=== Testing Battery Data Storage ===");
    
    // Sample battery data (simulating CSV data)
    const char *battery_data[] = {
        "Log,Min SOC,Max SOC,SoH,shutdown min cell,shutdown max cell,boot min cell,boot max cell",
        "154,84,99,0,9,5,10,8,23,34,29,12,0,0,3940,3980,4280,4360",
        "153,65,82,0,9,4,10,5,24,33,33,12,0,0,3900,3920,3900,3940",
        "152,56,65,0,10,5,9,5,23,27,15,12,0,0,3920,3960,3840,3860",
        "151,56,90,0,3,5,10,8,34,47,24,12,0,0,3780,3820,4260,4300",
        NULL
    };
    
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/battery.csv", base_path);
    
    ESP_LOGI(TAG, "Creating battery data file: %s", filepath);
    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create battery data file (errno: %d)", errno);
        return;
    }
    
    int line_count = 0;
    for (int i = 0; battery_data[i] != NULL; i++) {
        int written = fprintf(f, "%s\n", battery_data[i]);
        if (written < 0) {
            ESP_LOGE(TAG, "Failed to write line %d", i);
            fclose(f);
            return;
        }
        line_count++;
    }
    fclose(f);
    
    ESP_LOGI(TAG, "✓ Written %d lines of battery data", line_count);
    
    // Read back and verify
    f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open battery data file for reading (errno: %d)", errno);
        return;
    }
    
    ESP_LOGI(TAG, "Reading battery data (first 3 lines):");
    char line[256];
    for (int i = 0; i < 3 && fgets(line, sizeof(line), f) != NULL; i++) {
        char *pos = strchr(line, '\n');
        if (pos) *pos = '\0';
        ESP_LOGI(TAG, "  %s", line);
    }
    fclose(f);
    
    ESP_LOGI(TAG, "✓ Battery data verified");
}

/**
 * @brief Test multiple file operations
 */
static void test_multiple_files(void) {
    ESP_LOGI(TAG, "=== Testing Multiple Files ===");
    
    // Create several files
    for (int i = 1; i <= 3; i++) {
        char filepath[64];
        snprintf(filepath, sizeof(filepath), "%s/data_%d.txt", base_path, i);
        
        FILE *f = fopen(filepath, "w");
        if (f != NULL) {
            fprintf(f, "File number %d\n", i);
            fprintf(f, "Contains test data for file operations\n");
            fclose(f);
            ESP_LOGI(TAG, "✓ Created file: data_%d.txt", i);
        }
    }
    
    // Create a binary file
    char binpath[64];
    snprintf(binpath, sizeof(binpath), "%s/binary.dat", base_path);
    FILE *f = fopen(binpath, "wb");
    if (f != NULL) {
        uint8_t data[256];
        for (int i = 0; i < 256; i++) {
            data[i] = i;
        }
        fwrite(data, 1, sizeof(data), f);
        fclose(f);
        ESP_LOGI(TAG, "✓ Created binary file: binary.dat (256 bytes)");
    }
}


void app_main(void) {
    ESP_LOGI(TAG, "Starting SPI NAND Flash File System example for ESP32-S3");
    
    // Initialize SPI NAND Flash
    spi_device_handle_t spi;
    spi_nand_flash_device_t *flash = NULL;
    
    esp_err_t ret = init_nand_flash(&flash, &spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NAND Flash, aborting");
        return;
    }
    
    // Mount FAT filesystem
    ESP_LOGI(TAG, "=== Mounting FAT Filesystem ===");
    esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 10,
        .format_if_mount_failed = true,  // Format if mount fails
        .allocation_unit_size = 16 * 1024  // 16KB allocation units
    };
    
    ret = esp_vfs_fat_nand_mount(base_path, flash, &mount_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(ret));
        deinit_nand_flash(flash, spi);
        return;
    }
    
    ESP_LOGI(TAG, "✓ Filesystem mounted at %s", base_path);
    
    // Display information
    vTaskDelay(pdMS_TO_TICKS(500));
    display_info(flash);
    
    // Run file system tests
    vTaskDelay(pdMS_TO_TICKS(1000));
    test_file_write();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    test_file_read();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    test_battery_data();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    test_multiple_files();
    
    // Display final filesystem info
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "=== Final Filesystem Status ===");
    display_info(flash);
    
    ESP_LOGI(TAG, "=== All tests complete ===");
    
    // Unmount and cleanup
    esp_vfs_fat_nand_unmount(base_path, flash);
    deinit_nand_flash(flash, spi);
    
    ESP_LOGI(TAG, "Example finished successfully");
}
