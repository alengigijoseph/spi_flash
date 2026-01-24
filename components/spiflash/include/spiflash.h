/**
 * @file spiflash.h
 * @brief SPI Flash Memory Driver for ESP-IDF
 * 
 * Supports SPI NAND flash chips (W25N, GD5F, etc.)
 */

#ifndef SPIFLASH_H
#define SPIFLASH_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI NAND Flash commands (W25N01GV compatible)
 */
#define SPIFLASH_CMD_RESET              0xFF
#define SPIFLASH_CMD_READ_ID            0x9F
#define SPIFLASH_CMD_READ_STATUS        0x05
#define SPIFLASH_CMD_WRITE_STATUS       0x01
#define SPIFLASH_CMD_WRITE_ENABLE       0x06
#define SPIFLASH_CMD_WRITE_DISABLE      0x04
#define SPIFLASH_CMD_BB_MGMT            0xA1  // Bad block management
#define SPIFLASH_CMD_READ_BBM           0xA5  // Read bad block markers
#define SPIFLASH_CMD_BLOCK_ERASE        0xD8
#define SPIFLASH_CMD_PAGE_READ          0x13  // Read from NAND to internal buffer
#define SPIFLASH_CMD_PAGE_READ_CACHE    0x3F  // Cache read
#define SPIFLASH_CMD_PROGRAM_LOAD       0x02  // Load data into program cache
#define SPIFLASH_CMD_PROGRAM_EXECUTE    0x10  // Execute program from cache
#define SPIFLASH_CMD_PROGRAM_LOAD_RND   0x84  // Random program load

/**
 * @brief SPI NAND Flash memory parameters
 */
#define SPIFLASH_PAGE_SIZE              2048
#define SPIFLASH_OOB_SIZE               64
#define SPIFLASH_PAGES_PER_BLOCK        64
#define SPIFLASH_BLOCK_SIZE             (SPIFLASH_PAGE_SIZE * SPIFLASH_PAGES_PER_BLOCK)
#define SPIFLASH_TOTAL_BLOCKS           1024  // 1GB chip

/**
 * @brief Status register bits
 */
#define SPIFLASH_STATUS_BUSY            (1 << 0)
#define SPIFLASH_STATUS_WEL             (1 << 1)  // Write Enable Latch
#define SPIFLASH_STATUS_EFAIL           (1 << 4)  // Erase fail
#define SPIFLASH_STATUS_PFAIL           (1 << 3)  // Program fail

/**
 * @brief SPI Flash configuration structure
 */
typedef struct {
    spi_host_device_t host_id;      // SPI host (SPI2_HOST or SPI3_HOST)
    int pin_mosi;                   // MOSI pin
    int pin_miso;                   // MISO pin
    int pin_sclk;                   // SCLK pin
    int pin_cs;                     // CS pin
    int clock_speed_hz;             // SPI clock speed (Hz)
} spiflash_config_t;

/**
 * @brief SPI NAND Flash device handle
 */
typedef struct {
    spi_device_handle_t spi_handle;
    uint8_t jedec_id[3];            // Manufacturer ID, Memory Type, Capacity
    uint32_t total_size;            // Total flash size in bytes (1GB = 1024 * 128KB blocks)
} spiflash_handle_t;

/**
 * @brief Initialize SPI Flash
 * 
 * @param config Pointer to configuration structure
 * @param handle Pointer to device handle (will be allocated)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spiflash_init(const spiflash_config_t *config, spiflash_handle_t **handle);

/**
 * @brief Deinitialize SPI Flash
 * 
 * @param handle Device handle
 * @return ESP_OK on success
 */
esp_err_t spiflash_deinit(spiflash_handle_t *handle);

/**
 * @brief Read JEDEC ID (Manufacturer ID, Memory Type, Capacity)
 * 
 * @param handle Device handle
 * @param id Buffer to store 3-byte ID
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spiflash_read_jedec_id(spiflash_handle_t *handle, uint8_t *id);

/**
 * @brief Read a page from flash
 * 
 * @param handle Device handle
 * @param page_num Page number to read
 * @param buffer Buffer to store read data (2048 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spiflash_read_page(spiflash_handle_t *handle, uint32_t page_num, 
                             uint8_t *buffer);

/**
 * @brief Write a page to flash
 * 
 * @param handle Device handle
 * @param page_num Page number to write to
 * @param data Data to write (2048 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spiflash_write_page(spiflash_handle_t *handle, uint32_t page_num, 
                              const uint8_t *data);

/**
 * @brief Erase a 128KB block
 * 
 * @param handle Device handle
 * @param block_num Block number to erase
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spiflash_erase_block(spiflash_handle_t *handle, uint32_t block_num);

/**
 * @brief Read status register
 * 
 * @param handle Device handle
 * @param status Pointer to store status value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t spiflash_read_status(spiflash_handle_t *handle, uint8_t *status);

/**
 * @brief Wait until flash is ready (not busy)
 * 
 * @param handle Device handle
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t spiflash_wait_ready(spiflash_handle_t *handle, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // SPIFLASH_H