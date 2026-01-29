#include "DataAcquisition.h"
#include "Batmon_struct.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>


#define TAG "DATA_ACQ"

// GPIO pins for I2C
typedef struct {
    gpio_num_t sda;
    gpio_num_t scl;
} i2c_pins_t;

static const i2c_pins_t smbus_pins = {
    .sda = GPIO_NUM_21,
    .scl = GPIO_NUM_18,
};

// BATMON addresses
const uint8_t BATMON_addresses[NO_BATMON] = {
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x14
};

// Global handles and state
i2c_master_bus_handle_t SMBus_handle;
batmon_handle_t BATMON_handle[NO_BATMON];
battery_state_t battery_state[NO_BATMON] = {0};

/**
 * @brief Initialize I2C bus
 */
esp_err_t init_i2c_bus(void)
{
    i2c_master_bus_config_t SMBus_cfg = {
        .i2c_port = 1,
        .sda_io_num = smbus_pins.sda,
        .scl_io_num = smbus_pins.scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };

    esp_err_t ret = i2c_new_master_bus(&SMBus_cfg, &SMBus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SMBus: %s", esp_err_to_name(ret));
        return ret;
    } else {
        ESP_LOGI(TAG, "SMBus initialized successfully");
    }
    
    return ESP_OK;
}

/**
 * @brief Initialize all BATMON devices
 */
void init_batmon_devices(void)
{
    // Suppress I2C NACK error logs (expected when batteries not connected)
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    
    for(int i = 0; i < NO_BATMON; i++)
    {
        ESP_LOGI(TAG, "Initializing BATMON %d at address 0x%02X", i, BATMON_addresses[i]);
        esp_err_t ret = BATMON_init(SMBus_handle, BATMON_addresses[i], NUM_THERM_TO_READ, &BATMON_handle[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize BATMON %d: %s", i, esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "BATMON %d initialized successfully", i);
        }
        
        // Initialize battery state
        battery_state[i].is_connected = false;
        battery_state[i].address = BATMON_addresses[i];
    }
}

/**
 * @brief Get battery log from BATMON via I2C
 * Reads battery memory data including memoryIndex
 */
void get_battery_log(int batmon_index)
{
    esp_err_t ret;
    BATMON_Mem_Info mem_info;
    BatmonMemory batmem;

    ESP_LOGI(TAG, "Reading battery log from BATMON %d...", batmon_index);

    // Get memory info
    ret = BATMON_getMemoryInfo(&BATMON_handle[batmon_index], &mem_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get memory info: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Memory Info:");
    ESP_LOGI(TAG, "  Bytes per record: %d", mem_info.data.bytesPerRecord);
    ESP_LOGI(TAG, "  Number of partitions: %d", mem_info.data.numPartitionsPerRecord);
    ESP_LOGI(TAG, "  Total memory records: %d", mem_info.data.totalMemoryRecords);

    // Read battery memory data
    bool success = BATMON_getMemory(&BATMON_handle[batmon_index], &batmem, &mem_info);
    if (!success) {
        ESP_LOGE(TAG, "Failed to read battery memory");
        return;
    }

    // Print raw hex data
    ESP_LOGI(TAG, "Raw Hex Data (%d bytes):", sizeof(BatmonMemory));
    uint8_t *raw_data = (uint8_t *)&batmem;
    for (int i = 0; i < sizeof(BatmonMemory); i += 16) {
        char hex_line[80];
        char ascii_line[20];
        int offset = 0;
        int ascii_offset = 0;
        
        offset += sprintf(hex_line + offset, "%04X: ", i);
        
        for (int j = 0; j < 16 && (i + j) < sizeof(BatmonMemory); j++) {
            offset += sprintf(hex_line + offset, "%02X ", raw_data[i + j]);
            ascii_line[ascii_offset++] = (raw_data[i + j] >= 32 && raw_data[i + j] <= 126) ? raw_data[i + j] : '.';
        }
        ascii_line[ascii_offset] = '\0';
        
        ESP_LOGI(TAG, "%s  %s", hex_line, ascii_line);
    }

    // Display battery log data
    ESP_LOGI(TAG, "Battery Log:");
    ESP_LOGI(TAG, "  Memory Index: %d", batmem.data.memoryIndex);
    ESP_LOGI(TAG, "  Min SOC: %d%%", batmem.data.minSOC);
    ESP_LOGI(TAG, "  Max SOC: %d%%", batmem.data.maxSOC);
    ESP_LOGI(TAG, "  SOH: %d%%", batmem.data.SOH);
    ESP_LOGI(TAG, "  Battery Cycle: %d", batmem.data.log.battCycle);
    ESP_LOGI(TAG, "  Min Temp Cycle: %d°C", batmem.data.minTempCycle + MEMORY_TEMP_OFFSET);
    ESP_LOGI(TAG, "  Max Temp Cycle: %d°C", batmem.data.maxTempCycle + MEMORY_TEMP_OFFSET);
    ESP_LOGI(TAG, "  Max Internal Temp: %d°C", batmem.data.maxIntTempCycle + MEMORY_TEMP_OFFSET);
    ESP_LOGI(TAG, "  Max Drained Current: %d A", batmem.data.maxDrainedCurrentCycle);
    ESP_LOGI(TAG, "  Shutdown Remain Cap: %d mAh", batmem.data.shutdownRemainCap);
    ESP_LOGI(TAG, "  Accumulated Charged: %lu mAh", (unsigned long)batmem.data.accumulatedCharged);
    ESP_LOGI(TAG, "  Accumulated Discharged: %lu mAh", (unsigned long)batmem.data.accumulatedDischarged);
    ESP_LOGI(TAG, "  New Cycle: %d", batmem.data.log.REC_NEW_CYCLE);
    ESP_LOGI(TAG, "  Logged Without Sleep: %d", batmem.data.log.LOGGED_WITHOUT_SLEEP);
}

/**
 * @brief Continuous SMBUS update task
 * Monitors battery connections and reads logs when newly connected
 */
void SMBUS_update(void *arg)
{
    ESP_LOGI(TAG, "SMBUS_update task started");
    const TickType_t xFrequency = pdMS_TO_TICKS(1000); // Update every 1 second
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (1)
    {
        xTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        for (int i = 0; i < NO_BATMON; i++)
        {
            // Check if battery is connected by trying to read SOC
            uint16_t soc;
            esp_err_t ret = BATMON_getSOC(&BATMON_handle[i], &soc);
            bool currently_connected = (ret == ESP_OK);
            
            // Detect new connection or reconnection
            if (currently_connected && !battery_state[i].is_connected)
            {
                // Battery just connected or reconnected
                ESP_LOGI(TAG, "\n========== BATMON %d (0x%02X) CONNECTED ==========", i, BATMON_addresses[i]);
                ESP_LOGI(TAG, "SOC: %d%%", soc);
                get_battery_log(i);
                battery_state[i].is_connected = true;
            }
            else if (!currently_connected && battery_state[i].is_connected)
            {
                // Battery disconnected
                ESP_LOGW(TAG, "BATMON %d (0x%02X) DISCONNECTED", i, BATMON_addresses[i]);
                battery_state[i].is_connected = false;
            }
            // If still connected, do nothing (don't print again)
        }
    }
}