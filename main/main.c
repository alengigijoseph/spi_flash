#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_common.h"
#include "battery_fs.h"
#include "DataAcquisition.h"

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
 * @brief Main application entry point
 */
void app_main(void) {
    esp_err_t ret;
    bool filesystem_available = false;

    ESP_LOGI(TAG, "=== Battery Monitoring System Starting ===");

    // ========================================
    // Step 1: Initialize I2C Bus and BATMON
    // ========================================
    ESP_LOGI(TAG, "Initializing I2C and BATMON...");
    ret = init_i2c_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        // Don't return - continue with filesystem init
    } else {
        ESP_LOGI(TAG, "✓ I2C bus initialized");

        init_batmon_devices();
        ESP_LOGI(TAG, "✓ BATMON devices initialized");

        // Start battery monitoring task
        xTaskCreate(SMBUS_update, "SMBUS_update", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "✓ Battery monitoring task started");
    }

    // ========================================
    // Step 2: Initialize Battery Filesystem (Optional)
    // ========================================
    ESP_LOGI(TAG, "Initializing battery filesystem...");
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
    
    ret = battery_fs_init(&fs_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery filesystem not available (NAND flash not detected)");
        ESP_LOGW(TAG, "Continuing without filesystem support...");
        filesystem_available = false;
    } else {
        ESP_LOGI(TAG, "✓ Battery filesystem initialized at %s", base_path);
        filesystem_available = true;

        // ========================================
        // Step 3: Run Filesystem Tests (if available)
        // ========================================
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Clear all existing log files
        battery_fs_delete_all();
        ESP_LOGI(TAG, "✓ Cleared existing logs");
    }

    // ========================================
    // Step 4: Main Loop
    // ========================================
    ESP_LOGI(TAG, "\n=== System Running ===");
    ESP_LOGI(TAG, "BATMON Monitoring: %s", ret == ESP_OK ? "ACTIVE" : "INACTIVE");
    ESP_LOGI(TAG, "Filesystem: %s", filesystem_available ? "AVAILABLE" : "NOT AVAILABLE");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // Periodic status update every 5 seconds
        if (filesystem_available) {
            // Could add filesystem health checks here
        }
    }
}
