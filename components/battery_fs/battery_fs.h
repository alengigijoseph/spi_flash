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
 * @brief Battery memory log entry
 */
typedef struct {
    uint32_t memory_index;  ///< Memory index from battery
    uint8_t *data;          ///< Pointer to binary data
    size_t data_len;        ///< Length of binary data
} battery_log_t;

/**
 * @brief Battery metadata structure
 * Stores information about the last recorded log
 */
typedef struct {
    uint32_t last_memory_index; ///< Last memory index written
    uint32_t record_count;      ///< Total number of records
    uint32_t last_timestamp;    ///< Last update timestamp (optional)
    uint32_t last_data_hash;    ///< CRC32 hash of last record's data (for ring buffer detection)
} battery_metadata_t;

// ============================================================================
// Core Functions
// ============================================================================

/**
 * @brief Initialize battery filesystem
 * 
 * @param config Pointer to configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_init(const battery_fs_config_t *config);

/**
 * @brief Deinitialize battery filesystem
 * 
 * Unmounts filesystem and frees resources
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_deinit(void);

// ============================================================================
// Data Write Functions
// ============================================================================

/**
 * @brief Write battery data to flash
 * 
 * This function handles:
 * 1. Check if battery file exists
 * 2. If exists: Read metadata, identify new records, append only new data
 * 3. If not exists: Create file and write all data
 * 4. Update metadata after successful write
 * 
 * @param serial_number Battery serial number (used as filename)
 * @param logs Array of battery memory logs
 * @param log_count Number of logs in the array
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_write_data(const char *serial_number, const battery_log_t *logs, size_t log_count);

// ============================================================================
// Metadata Functions
// ============================================================================

/**
 * @brief Check if battery file exists
 * 
 * @param serial_number Battery serial number
 * @return true if file exists, false otherwise
 */
bool battery_fs_exists(const char *serial_number);

/**
 * @brief Read battery metadata
 * 
 * @param serial_number Battery serial number
 * @param metadata Pointer to store metadata
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file doesn't exist
 */
esp_err_t battery_fs_read_metadata(const char *serial_number, battery_metadata_t *metadata);

/**
 * @brief Write battery metadata
 * 
 * @param serial_number Battery serial number
 * @param metadata Pointer to metadata structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_write_metadata(const char *serial_number, const battery_metadata_t *metadata);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Identify new records to append
 * 
 * Compares incoming logs with existing metadata to determine which records
 * are new and need to be written.
 * 
 * @param metadata Existing battery metadata
 * @param logs Array of incoming memory logs
 * @param log_count Number of logs in the array
 * @param new_logs Output array of new logs (must be pre-allocated)
 * @param new_count Output: number of new logs identified
 * @return ESP_OK on success
 */
esp_err_t battery_fs_identify_new_records(const battery_metadata_t *metadata, 
                                          const battery_log_t *logs, 
                                          size_t log_count,
                                          battery_log_t *new_logs, 
                                          size_t *new_count);

// ============================================================================
// Delete Functions
// ============================================================================

/**
 * @brief Delete all battery files (data and metadata)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_fs_delete_all(void);

/**
 * @brief Delete specific battery data and metadata
 * 
 * @param serial_number Battery serial number
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file doesn't exist
 */
esp_err_t battery_fs_delete_battery(const char *serial_number);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_FS_H
