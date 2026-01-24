/**
 * @file main.c
 * @brief Example application using Battery Filesystem Component
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/spi_common.h"
#include "battery_fs.h"
#include "battery_mock_data.h"

static const char *TAG = "MAIN";

// SPI Configuration for ESP32-S3
#define SPI_HOST            SPI2_HOST
#define PIN_MOSI            5       // MOSI pin
#define PIN_MISO            4       // MISO pin
#define PIN_SCLK            6       // SCLK pin
#define PIN_CS              17      // CS pin
#define PIN_WP              2       // WP pin (optional)
#define PIN_HD              4       // HD pin (optional)
#define SPI_CLOCK_SPEED     40000000  // 40 MHz

// Mount path for the file system
const char *base_path = "/nandflash";

/**
 * @brief Parse hex string to byte array
 */
static size_t parse_hex_string(const char *hex_str, uint8_t *output, size_t max_len) {
    size_t count = 0;
    const char *p = hex_str;
    
    while (*p && count < max_len) {
        // Skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        
        // Read two hex digits
        if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
            char byte_str[3] = {p[0], p[1], '\0'};
            output[count++] = (uint8_t)strtol(byte_str, NULL, 16);
            p += 2;
        } else {
            break;
        }
    }
    
    return count;
}

/**
 * @brief Write mock battery data to flash using bulk write
 */
static void write_mock_data(const char *battery_serial, const mock_battery_entry_t *mock_data, size_t count) {
    ESP_LOGI(TAG, "Writing %d log entries for %s using BULK write", count, battery_serial);
    
    // Allocate array for battery_data_t
    battery_data_t *data_array = malloc(count * sizeof(battery_data_t));
    if (data_array == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for data array");
        return;
    }
    
    // Parse all hex strings to binary
    for (size_t i = 0; i < count; i++) {
        uint8_t *binary_data = malloc(128);  // Max binary data size
        if (binary_data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate binary buffer");
            // Free previously allocated buffers
            for (size_t j = 0; j < i; j++) {
                free(data_array[j].binary_data);
            }
            free(data_array);
            return;
        }
        
        size_t data_len = parse_hex_string(mock_data[i].hex_data, binary_data, 128);
        
        data_array[i].log_number = mock_data[i].log_number;
        data_array[i].binary_data = binary_data;
        data_array[i].data_len = data_len;
    }
    
    // Write all entries in one operation
    uint32_t start_time = esp_log_timestamp();
    esp_err_t ret = battery_fs_write_bulk(battery_serial, data_array, count);
    uint32_t elapsed = esp_log_timestamp() - start_time;
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Bulk write completed in %lu ms", elapsed);
    } else {
        ESP_LOGE(TAG, "Bulk write failed");
    }
    
    // Free allocated memory
    for (size_t i = 0; i < count; i++) {
        free(data_array[i].binary_data);
    }
    free(data_array);
}

/**
 * @brief Test writing battery data from mock data
 */
static void test_battery_logging(void) {
    ESP_LOGI(TAG, "=== Loading Battery Mock Data ===");
    
    // Write Battery 01945 data
    write_mock_data("BAT01945", battery_01945_data, BATTERY_01945_COUNT);
    
    // Write Battery 62521 data
    write_mock_data("BAT62521", battery_62521_data, BATTERY_62521_COUNT);
    
    ESP_LOGI(TAG, "✓ Battery logging completed");
}

/**
 * @brief Test file existence check and last log number
 */
static void test_file_check(void) {
    ESP_LOGI(TAG, "=== Testing File Existence and Last Log ===");
    
    bool exists = false;
    uint32_t last_log = 0;
    
    // Check BAT01945
    battery_fs_file_exists("BAT01945", &exists);
    ESP_LOGI(TAG, "BAT01945 file exists: %s", exists ? "YES" : "NO");
    if (exists && battery_fs_get_last_log("BAT01945", &last_log) == ESP_OK) {
        ESP_LOGI(TAG, "BAT01945 last log: %lu", last_log);
    }
    
    // Check BAT62521
    battery_fs_file_exists("BAT62521", &exists);
    ESP_LOGI(TAG, "BAT62521 file exists: %s", exists ? "YES" : "NO");
    if (exists && battery_fs_get_last_log("BAT62521", &last_log) == ESP_OK) {
        ESP_LOGI(TAG, "BAT62521 last log: %lu", last_log);
    }
    
    // Check non-existent battery
    battery_fs_file_exists("BAT99999", &exists);
    ESP_LOGI(TAG, "BAT99999 file exists: %s", exists ? "YES" : "NO");
}

/**
 * @brief Test reading battery data using bulk read
 */
static void test_read_data(void) {
    ESP_LOGI(TAG, "=== Testing Bulk Read Battery Data ===");
    
    // Get entry count first
    size_t count = 0;
    esp_err_t ret = battery_fs_get_entry_count("BAT01945", &count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get entry count");
        return;
    }
    
    ESP_LOGI(TAG, "BAT01945 has %u entries", count);
    
    if (count == 0) {
        ESP_LOGW(TAG, "No entries to read");
        return;
    }
    
    // Allocate array for bulk read
    battery_data_t *data_array = malloc(count * sizeof(battery_data_t));
    if (data_array == NULL) {
        ESP_LOGE(TAG, "Failed to allocate array for %u entries", count);
        return;
    }
    
    // Bulk read all entries into RAM
    size_t actual_count = 0;
    uint32_t start_time = esp_log_timestamp();
    ret = battery_fs_read_bulk("BAT01945", data_array, count, &actual_count);
    uint32_t elapsed = esp_log_timestamp() - start_time;
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Bulk read %u entries in %lu ms", actual_count, elapsed);
        
        // Display first entry details
        if (actual_count > 0) {
            ESP_LOGI(TAG, "First entry: Log=%lu, Size=%u bytes", 
                     data_array[0].log_number, data_array[0].data_len);
            ESP_LOGI(TAG, "  First 16 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                     data_array[0].binary_data[0], data_array[0].binary_data[1],
                     data_array[0].binary_data[2], data_array[0].binary_data[3],
                     data_array[0].binary_data[4], data_array[0].binary_data[5],
                     data_array[0].binary_data[6], data_array[0].binary_data[7],
                     data_array[0].binary_data[8], data_array[0].binary_data[9],
                     data_array[0].binary_data[10], data_array[0].binary_data[11],
                     data_array[0].binary_data[12], data_array[0].binary_data[13],
                     data_array[0].binary_data[14], data_array[0].binary_data[15]);
        }
        
        // Calculate RAM usage
        size_t ram_used = actual_count * sizeof(battery_data_t);
        for (size_t i = 0; i < actual_count; i++) {
            ram_used += data_array[i].data_len;
        }
        ESP_LOGI(TAG, "Total RAM used: %u bytes (~%u KB)", ram_used, ram_used / 1024);
        
        // Free allocated memory
        for (size_t i = 0; i < actual_count; i++) {
            free(data_array[i].binary_data);
        }
    } else {
        ESP_LOGE(TAG, "Bulk read failed");
    }
    
    free(data_array);
}

/**
 * @brief Display filesystem information
 */
static void display_fs_info(void) {
    uint64_t total_kb, free_kb, used_kb;
    
    esp_err_t ret = battery_fs_get_info(&total_kb, &free_kb, &used_kb);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "=== Filesystem Information ===");
        ESP_LOGI(TAG, "Total space: %" PRIu64 " KB", total_kb);
        ESP_LOGI(TAG, "Free space: %" PRIu64 " KB", free_kb);
        ESP_LOGI(TAG, "Used space: %" PRIu64 " KB", used_kb);
    }
}

static void display_wear_info(void) {
    uint32_t bad_block_count = 0;
    
    ESP_LOGI(TAG, "=== Flash Wear Leveling Information ===");
    esp_err_t ret = battery_fs_get_wear_info(&bad_block_count);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Bad block count: %lu", (unsigned long)bad_block_count);
    } else {
        ESP_LOGE(TAG, "Failed to get wear info: %s", esp_err_to_name(ret));
    }
}

static void display_ecc_stats(void) {
    ESP_LOGI(TAG, "=== Detailed ECC Statistics (Slow Operation) ===");
    esp_err_t ret = battery_fs_get_ecc_stats();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get ECC stats: %s", esp_err_to_name(ret));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Battery Filesystem Example for ESP32-S3");
    
    // Configure battery filesystem
    battery_fs_config_t fs_config = {
        .spi_host = SPI_HOST,
        .pin_mosi = PIN_MOSI,
        .pin_miso = PIN_MISO,
        .pin_sclk = PIN_SCLK,
        .pin_cs = PIN_CS,
        .pin_wp = PIN_WP,
        .pin_hd = PIN_HD,
        .clock_speed_hz = SPI_CLOCK_SPEED,
        .mount_point = base_path,
        .format_if_failed = true,
    };
    
    // Initialize filesystem
    esp_err_t ret = battery_fs_init(&fs_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize battery filesystem");
        return;
    }
    
    // Display filesystem info
    vTaskDelay(pdMS_TO_TICKS(500));
    display_fs_info();
    
    // Display wear leveling info
    vTaskDelay(pdMS_TO_TICKS(500));
    display_wear_info();
    
    // Test battery data logging
    vTaskDelay(pdMS_TO_TICKS(1000));
    test_battery_logging();
    
    // Test file existence check
    vTaskDelay(pdMS_TO_TICKS(500));
    test_file_check();
    
    // Test reading battery data
    vTaskDelay(pdMS_TO_TICKS(500));
    test_read_data();
    
    // Display final filesystem status
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "=== Final Status ===");
    display_fs_info();
    display_wear_info();
    
    // Optional: Display detailed ECC stats (slow - uncomment if needed)
    // ESP_LOGI(TAG, "Fetching detailed ECC statistics...");
    // display_ecc_stats();
    
    ESP_LOGI(TAG, "=== All tests complete ===");
    
    // Cleanup
    battery_fs_deinit();
    
    ESP_LOGI(TAG, "Example finished successfully");
}
