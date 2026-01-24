/**
 * @file spiflash.c
 * @brief SPI NAND Flash Memory Driver Implementation (W25N01GV)
 */

#include "spiflash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "SPIFLASH";

#define SPIFLASH_TIMEOUT_MS         5000
#define SPIFLASH_ERASE_TIMEOUT_MS   10000

/**
 * @brief Send a command with optional data
 */
static esp_err_t spiflash_send_command(spiflash_handle_t *handle, 
                                       const uint8_t *cmd_buf, size_t cmd_len,
                                       uint8_t *rx_buf, size_t rx_len) {
    if (cmd_len > 0 && rx_len > 0) {
        // Combined transaction: send command and receive data
        uint8_t *tx_buf = malloc(cmd_len + rx_len);
        if (tx_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        
        memcpy(tx_buf, cmd_buf, cmd_len);
        memset(tx_buf + cmd_len, 0xFF, rx_len);  // Don't care bytes
        
        spi_transaction_t trans = {
            .length = (cmd_len + rx_len) * 8,
            .tx_buffer = tx_buf,
            .rx_buffer = rx_buf,
        };
        
        esp_err_t ret = spi_device_polling_transmit(handle->spi_handle, &trans);
        free(tx_buf);
        
        if (ret == ESP_OK && rx_len > 0) {
            // Shift data: copy from offset cmd_len to beginning
            memmove(rx_buf, rx_buf + cmd_len, rx_len);
        }
        
        return ret;
    } else if (cmd_len > 0) {
        // Only send command
        spi_transaction_t trans = {
            .length = cmd_len * 8,
            .tx_buffer = cmd_buf,
            .rx_buffer = NULL,
        };
        return spi_device_polling_transmit(handle->spi_handle, &trans);
    }
    
    return ESP_ERR_INVALID_ARG;
}

/**
 * @brief Reset the flash device
 */
static esp_err_t spiflash_reset(spiflash_handle_t *handle) {
    uint8_t cmd = SPIFLASH_CMD_RESET;
    return spiflash_send_command(handle, &cmd, 1, NULL, 0);
}

/**
 * @brief Enable write operations
 */
static esp_err_t spiflash_write_enable(spiflash_handle_t *handle) {
    uint8_t cmd = SPIFLASH_CMD_WRITE_ENABLE;
    return spiflash_send_command(handle, &cmd, 1, NULL, 0);
}

/**
 * @brief Disable write operations
 */
static esp_err_t spiflash_write_disable(spiflash_handle_t *handle) {
    uint8_t cmd = SPIFLASH_CMD_WRITE_DISABLE;
    return spiflash_send_command(handle, &cmd, 1, NULL, 0);
}

/**
 * @brief Write status register (to clear block protection)
 */
static esp_err_t spiflash_write_status_register(spiflash_handle_t *handle, uint8_t status_reg, uint8_t value) {
    // First enable writes
    esp_err_t ret = spiflash_write_enable(handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Write status register: 01h followed by register address and value
    uint8_t cmd[3];
    cmd[0] = SPIFLASH_CMD_WRITE_STATUS;
    cmd[1] = status_reg;  // 0xA0 for SR1, 0xB0 for SR2, 0xC0 for SR3
    cmd[2] = value;
    
    ret = spiflash_send_command(handle, cmd, 3, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write status register failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for write to complete
    return spiflash_wait_ready(handle, SPIFLASH_TIMEOUT_MS);
}

/**
 * @brief Clear block protection (set BP bits to 0)
 */
static esp_err_t spiflash_clear_block_protection(spiflash_handle_t *handle) {
    ESP_LOGI(TAG, "Clearing block protection...");
    
    // Write Status Register 1 with BP bits cleared (0xA0 register, 0x00 value)
    esp_err_t ret = spiflash_write_status_register(handle, 0xA0, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear block protection: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Block protection cleared");
    return ESP_OK;
}

/**
 * @brief Verify Write Enable Latch is set
 */
static esp_err_t spiflash_verify_wel(spiflash_handle_t *handle) {
    uint8_t status;
    esp_err_t ret = spiflash_read_status(handle, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Check WEL bit (bit 1) in status register
    if (!(status & SPIFLASH_STATUS_WEL)) {
        ESP_LOGE(TAG, "WEL not set! Status: 0x%02X", status);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t spiflash_read_status(spiflash_handle_t *handle, uint8_t *status) {
    if (handle == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t cmd[2];
    uint8_t rx[1];
    cmd[0] = SPIFLASH_CMD_READ_STATUS;
    cmd[1] = 0xC0;  // Read status register C0 (OIP bit)
    
    esp_err_t ret = spiflash_send_command(handle, cmd, 2, rx, 1);
    if (ret == ESP_OK) {
        *status = rx[0];
    }
    
    return ret;
}

esp_err_t spiflash_wait_ready(spiflash_handle_t *handle, uint32_t timeout_ms) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t start = xTaskGetTickCount();
    uint8_t status;
    
    while (1) {
        esp_err_t ret = spiflash_read_status(handle, &status);
        if (ret != ESP_OK) {
            return ret;
        }
        
        // Bit 0 is OIP (operation in progress)
        if ((status & SPIFLASH_STATUS_BUSY) == 0) {
            return ESP_OK;
        }
        
        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > timeout_ms) {
            ESP_LOGE(TAG, "Timeout waiting for flash ready");
            return ESP_ERR_TIMEOUT;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t spiflash_read_jedec_id(spiflash_handle_t *handle, uint8_t *id) {
    if (handle == NULL || id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Combined transaction: command (1 byte) + read ID (3 bytes)
    uint8_t tx_buf[4];
    uint8_t rx_buf[4];
    
    tx_buf[0] = SPIFLASH_CMD_READ_ID;
    tx_buf[1] = 0xFF;  // Don't care bytes for read phase
    tx_buf[2] = 0xFF;
    tx_buf[3] = 0xFF;
    
    spi_transaction_t trans = {
        .length = 32,  // 4 bytes * 8 bits
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    
    esp_err_t ret = spi_device_polling_transmit(handle->spi_handle, &trans);
    if (ret == ESP_OK) {
        // ID bytes are at rx_buf[1], rx_buf[2], rx_buf[3]
        id[0] = rx_buf[1];
        id[1] = rx_buf[2];
        id[2] = rx_buf[3];
    }
    
    return ret;
}

esp_err_t spiflash_read_page(spiflash_handle_t *handle, uint32_t page_num, 
                             uint8_t *buffer) {
    if (handle == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Wait for flash to be ready
    esp_err_t ret = spiflash_wait_ready(handle, SPIFLASH_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Step 1: Send PAGE READ command to load page into buffer
    uint8_t cmd[4];
    cmd[0] = SPIFLASH_CMD_PAGE_READ;
    cmd[1] = (page_num >> 16) & 0xFF;
    cmd[2] = (page_num >> 8) & 0xFF;
    cmd[3] = page_num & 0xFF;
    
    ret = spiflash_send_command(handle, cmd, 4, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Page read command failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for page to be loaded into internal buffer
    ret = spiflash_wait_ready(handle, SPIFLASH_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Step 2: Read data from buffer (0x03 + 2-byte column address + 1 dummy byte, then read data)
    uint8_t tx_buf[4] = {0x03, 0x00, 0x00, 0x00};
    size_t rx_len = 4 + SPIFLASH_PAGE_SIZE;
    uint8_t *rx_buf = malloc(rx_len);
    if (!rx_buf) {
        return ESP_ERR_NO_MEM;
    }

    spi_transaction_t trans = {
        .length = rx_len * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    ret = spi_device_polling_transmit(handle->spi_handle, &trans);
    if (ret == ESP_OK) {
        // Copy only the data portion (skip first 4 bytes: command + address + dummy)
        memcpy(buffer, rx_buf + 4, SPIFLASH_PAGE_SIZE);
    } else {
        ESP_LOGE(TAG, "Page read data failed: %s", esp_err_to_name(ret));
    }

    free(rx_buf);
    return ret;
}

esp_err_t spiflash_write_page(spiflash_handle_t *handle, uint32_t page_num, 
                              const uint8_t *data) {
    if (handle == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Wait for flash to be ready
    esp_err_t ret = spiflash_wait_ready(handle, SPIFLASH_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Step 1: Write enable (0x06)
    ret = spiflash_write_enable(handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Verify WEL is set before proceeding
    ret = spiflash_verify_wel(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WEL verification failed before PROGRAM_LOAD");
        return ret;
    }
    
    // Step 2: Load program data into buffer (command 0x02 + 2 address bytes + data)
    uint8_t *tx_buf = malloc(3 + SPIFLASH_PAGE_SIZE);
    if (tx_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    tx_buf[0] = SPIFLASH_CMD_PROGRAM_LOAD;
    tx_buf[1] = 0x00;  // Column address high byte
    tx_buf[2] = 0x00;  // Column address low byte
    memcpy(tx_buf + 3, data, SPIFLASH_PAGE_SIZE);
    
    spi_transaction_t trans = {
        .length = (3 + SPIFLASH_PAGE_SIZE) * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = NULL,
    };
    
    ret = spi_device_polling_transmit(handle->spi_handle, &trans);
    free(tx_buf);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Program load failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 3: Execute program (load data to NAND)
    // NOTE: Must execute immediately after PROGRAM_LOAD while WEL is still set
    uint8_t cmd[4];
    cmd[0] = SPIFLASH_CMD_PROGRAM_EXECUTE;
    cmd[1] = (page_num >> 16) & 0xFF;
    cmd[2] = (page_num >> 8) & 0xFF;
    cmd[3] = page_num & 0xFF;
    
    ret = spiflash_send_command(handle, cmd, 4, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Program execute failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for program to complete (OIP bit will be 1 while programming)
    ret = spiflash_wait_ready(handle, SPIFLASH_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Timeout waiting for program complete");
        return ret;
    }
    
    // Check for program failure
    uint8_t status;
    ret = spiflash_read_status(handle, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (status & SPIFLASH_STATUS_PFAIL) {
        ESP_LOGE(TAG, "Program failed: status=0x%02X", status);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Wrote page %" PRIu32, page_num);
    return spiflash_write_disable(handle);
}

esp_err_t spiflash_erase_block(spiflash_handle_t *handle, uint32_t block_num) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Wait for flash to be ready
    esp_err_t ret = spiflash_wait_ready(handle, SPIFLASH_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Write enable
    ret = spiflash_write_enable(handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Verify WEL is set before erase
    ret = spiflash_verify_wel(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WEL verification failed before BLOCK_ERASE");
        return ret;
    }
    
    // Send block erase command
    uint8_t cmd[4];
    cmd[0] = SPIFLASH_CMD_BLOCK_ERASE;
    // Block address: blocks are identified by page address with lower 6 bits = 0
    // Block N starts at page N*64
    uint32_t page_addr = block_num << 6;  // Shift by 6 to get page address
    cmd[1] = (page_addr >> 16) & 0xFF;
    cmd[2] = (page_addr >> 8) & 0xFF;
    cmd[3] = page_addr & 0xFF;
    
    ret = spiflash_send_command(handle, cmd, 4, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Block erase command failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for erase to complete
    ret = spiflash_wait_ready(handle, SPIFLASH_ERASE_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Check for erase failure
    uint8_t status;
    ret = spiflash_read_status(handle, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (status & SPIFLASH_STATUS_EFAIL) {
        ESP_LOGE(TAG, "Erase failed: status=0x%02X", status);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Erased block %" PRIu32, block_num);
    return spiflash_write_disable(handle);
}

esp_err_t spiflash_init(const spiflash_config_t *config, spiflash_handle_t **handle) {
    if (config == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate handle
    *handle = (spiflash_handle_t *)malloc(sizeof(spiflash_handle_t));
    if (*handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for handle");
        return ESP_ERR_NO_MEM;
    }
    
    memset(*handle, 0, sizeof(spiflash_handle_t));
    (*handle)->total_size = SPIFLASH_TOTAL_BLOCKS * SPIFLASH_BLOCK_SIZE;  // 1GB
    
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = config->pin_miso,
        .sclk_io_num = config->pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    
    esp_err_t ret = spi_bus_initialize(config->host_id, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        free(*handle);
        *handle = NULL;
        return ret;
    }
    
    // Configure SPI device
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,  // SPI mode 0 (CPOL=0, CPHA=0)
        .clock_speed_hz = config->clock_speed_hz,
        .spics_io_num = config->pin_cs,
        .queue_size = 7,
        .flags = 0,
    };
    
    ret = spi_bus_add_device(config->host_id, &dev_cfg, &(*handle)->spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        spi_bus_free(config->host_id);
        free(*handle);
        *handle = NULL;
        return ret;
    }
    
    // Reset flash
    ret = spiflash_reset(*handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reset failed: %s", esp_err_to_name(ret));
        spi_bus_remove_device((*handle)->spi_handle);
        spi_bus_free(config->host_id);
        free(*handle);
        *handle = NULL;
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for reset to complete
    
    // Clear block protection - CRITICAL for W25N01GV
    ret = spiflash_clear_block_protection(*handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear block protection");
        spi_bus_remove_device((*handle)->spi_handle);
        spi_bus_free(config->host_id);
        free(*handle);
        *handle = NULL;
        return ret;
    }
    
    // Read JEDEC ID
    ret = spiflash_read_jedec_id(*handle, (*handle)->jedec_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read JEDEC ID");
        spi_bus_remove_device((*handle)->spi_handle);
        spi_bus_free(config->host_id);
        free(*handle);
        *handle = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "SPI NAND Flash initialized");
    ESP_LOGI(TAG, "JEDEC ID: %02X %02X %02X", 
             (*handle)->jedec_id[0], (*handle)->jedec_id[1], (*handle)->jedec_id[2]);
    
    // Validate W25N01GV
    if ((*handle)->jedec_id[0] == 0xEF && (*handle)->jedec_id[1] == 0xAA && 
        (*handle)->jedec_id[2] == 0x21) {
        ESP_LOGI(TAG, "Detected: Winbond W25N01GV (1Gb NAND Flash)");
        ESP_LOGI(TAG, "Size: 1GB, Page: 2KB, Block: 128KB, Pages/Block: 64");
    }
    
    return ESP_OK;
}

esp_err_t spiflash_deinit(spiflash_handle_t *handle) {
    if (handle != NULL) {
        spi_bus_remove_device(handle->spi_handle);
        free(handle);
    }
    return ESP_OK;
}
