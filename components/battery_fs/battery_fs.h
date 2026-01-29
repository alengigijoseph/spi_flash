/**
 * @file battery_fs.h
 * @brief Battery Data File System Component
 * 
 * This component manages battery data storage on SPI NAND Flash.
 * Each battery gets its own file based on serial number.
 */

#ifndef BATTERY_FS_H
#define BATTERY_FS_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Battery filesystem configuration
 */
typedef struct {
    int spi_host;           ///< SPI host (SPI2_HOST or SPI3_HOST)
    int pin_mosi;           ///< MOSI pin
    int pin_miso;           ///< MISO pin
    int pin_sclk;           ///< SCLK pin
    int pin_cs;             ///< CS pin
    int pin_wp;             ///< WP pin (optional, -1 to disable)
    int pin_hd;             ///< HD pin (optional, -1 to disable)
    uint32_t clock_speed_hz; ///< SPI clock speed in Hz
    const char *mount_point; ///< Filesystem mount point (e.g., "/nandflash")
    bool format_if_failed;   ///< Format filesystem if mount fails
} battery_fs_config_t;

/**
 * @brief Battery data entry structure
 */
typedef struct {
    uint32_t log_number;    ///< Log entry number
    uint8_t *binary_data;   ///< Pointer to binary data
    size_t data_len;        ///< Length of binary data
} battery_data_t;

/**
 * @brief Callback function for reading battery data
 * 
 * Called for each log entry when reading battery file.
 * 
 * @param log_number The log entry number
 * @param binary_data Pointer to binary data (valid only during callback)
 * @param data_len Length of binary data
 * @param user_ctx User context pointer passed to battery_fs_read_data
 * @return true to continue reading, false to stop
 */
typedef bool (*battery_data_read_cb_t)(uint32_t log_number, const uint8_t *binary_data, size_t data_len, void *user_ctx);

/**
 * @brief Initialize battery filesystem
 * 
 * @param config Pointer to configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_init(const battery_fs_config_t *config);

/**
 * @brief Write battery data to file
 * 
 * Creates a new file if battery serial number doesn't exist,
 * or appends to existing file.
 * 
 * @param battery_serial Battery serial number (will be used as filename)
 * @param data Pointer to battery data structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_write_data(const char *battery_serial, const battery_data_t *data);

/**
 * @brief Write multiple battery data entries in one operation (bulk write)
 * 
 * Efficiently writes multiple log entries by opening the file once.
 * Much faster than calling battery_fs_write_data() multiple times.
 * 
 * @param battery_serial Battery serial number (will be used as filename)
 * @param data_array Array of battery data structures
 * @param count Number of entries in the array
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_write_bulk(const char *battery_serial, const battery_data_t *data_array, size_t count);

/**
 * @brief Smart sync: Write only new/changed entries by comparing with existing data
 * 
 * Compares incoming ring buffer data against the last N entries in flash.
 * Only writes entries that don't exist or have changed data (based on CRC32 hash).
 * This prevents duplicate writes and handles ring buffer wraparound automatically.
 * 
 * Process:
 * 1. Loads last N entries from flash (where N = count of incoming data)
 * 2. Compares log_number and CRC32 hash of each incoming entry
 * 3. Writes only new or modified entries
 * 
 * @param battery_serial Battery serial number (will be used as filename)
 * @param data_array Array of battery data structures from ring buffer
 * @param count Number of entries in the array
 * @param written_count Pointer to store number of entries actually written (optional, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_sync_from_ring(const char *battery_serial, const battery_data_t *data_array, size_t count, size_t *written_count);

/**
 * @brief Check if a battery file exists
 * 
 * @param battery_serial Battery serial number
 * @param exists Pointer to store result (true if exists)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_file_exists(const char *battery_serial, bool *exists);

/**
 * @brief Get the last log number for a battery
 * 
 * @param battery_serial Battery serial number
 * @param last_log Pointer to store the last log number
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file doesn't exist, error code otherwise
 */
esp_err_t battery_fs_get_last_log(const char *battery_serial, uint32_t *last_log);

/**
 * @brief Read all battery data entries from file
 * 
 * Reads the battery file and calls the callback function for each log entry.
 * The callback receives the log number, binary data pointer, and data length.
 * Binary data pointer is only valid during the callback.
 * 
 * @param battery_serial Battery serial number
 * @param callback Callback function called for each entry
 * @param user_ctx User context pointer passed to callback
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file doesn't exist, error code otherwise
 */
esp_err_t battery_fs_read_data(const char *battery_serial, battery_data_read_cb_t callback, void *user_ctx);

/**
 * @brief Read all battery data into RAM in one operation (bulk read)
 * 
 * Reads all log entries from file into a pre-allocated array.
 * User must provide array with enough space. Use battery_fs_get_entry_count() first.
 * Each entry's binary_data will be allocated with malloc() - caller must free.
 * 
 * @param battery_serial Battery serial number
 * @param data_array Pre-allocated array to store entries
 * @param max_count Maximum number of entries the array can hold
 * @param actual_count Pointer to store actual number of entries read
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file doesn't exist, error code otherwise
 */
esp_err_t battery_fs_read_bulk(const char *battery_serial, battery_data_t *data_array, size_t max_count, size_t *actual_count);

/**
 * @brief Get the number of log entries in a battery file
 * 
 * Useful before calling battery_fs_read_bulk() to allocate the right array size.
 * 
 * @param battery_serial Battery serial number
 * @param count Pointer to store entry count
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file doesn't exist, error code otherwise
 */
esp_err_t battery_fs_get_entry_count(const char *battery_serial, size_t *count);

/**
 * @brief Clear all battery log files
 * 
 * Deletes all .bin files in the filesystem mount point.
 * Useful for clearing old test data or resetting the system.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_clear_all_logs(void);

/**
 * @brief Get filesystem information
 * 
 * @param total_kb Pointer to store total space in KB
 * @param free_kb Pointer to store free space in KB
 * @param used_kb Pointer to store used space in KB
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_get_info(uint64_t *total_kb, uint64_t *free_kb, uint64_t *used_kb);

/**
 * @brief Get flash wear leveling statistics
 * 
 * Provides information about bad blocks for monitoring flash health.
 * Bad block count is fast and suitable for routine monitoring.
 * 
 * @param bad_block_count Pointer to store the number of bad blocks
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_get_wear_info(uint32_t *bad_block_count);

/**
 * @brief Get detailed ECC statistics (WARNING: SLOW operation)
 * 
 * Scans all pages to collect ECC error statistics. This operation
 * takes ~5 seconds and should only be used for detailed diagnostics,
 * not routine monitoring.
 * 
 * Displays: Total ECC errors, uncorrectable errors, errors exceeding threshold
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_get_ecc_stats(void);

/**
 * @brief Deinitialize battery filesystem
 * 
 * Unmounts filesystem and frees resources
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_FS_H
