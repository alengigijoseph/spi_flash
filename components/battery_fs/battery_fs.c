/**
 * @file battery_fs.c
 * @brief Battery Data File System Component Implementation
 */

#include "battery_fs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include "esp_log.h"
#include "esp_vfs_fat_nand.h"
#include "spi_nand_flash.h"
#include "nand_diag_api.h"
#include "driver/spi_master.h"
#include "esp_crc.h"

static const char *TAG = "battery_fs";

// Internal state
static struct {
    bool initialized;
    spi_nand_flash_device_t *flash_handle;
    spi_device_handle_t spi_handle;
    char mount_point[32];
} g_fs_state = {0};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Build data file path from serial number
 */
static void build_data_path(const char *serial_number, char *path, size_t path_size) {
    snprintf(path, path_size, "%s/%s.bin", g_fs_state.mount_point, serial_number);
}

/**
 * @brief Build metadata file path from serial number
 */
static void build_meta_path(const char *serial_number, char *path, size_t path_size) {
    snprintf(path, path_size, "%s/%s.met", g_fs_state.mount_point, serial_number);
}

// ============================================================================
// Core Functions
// ============================================================================

esp_err_t battery_fs_init(const battery_fs_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_fs_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing battery filesystem");

    // Store mount point
    strncpy(g_fs_state.mount_point, config->mount_point, sizeof(g_fs_state.mount_point) - 1);

    // Configure SPI bus
    spi_bus_config_t bus_config = {
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = config->pin_miso,
        .sclk_io_num = config->pin_sclk,
        .quadhd_io_num = config->pin_hd,
        .quadwp_io_num = config->pin_wp,
        .max_transfer_sz = 4096 * 2,
    };

    esp_err_t ret = spi_bus_initialize(config->spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->clock_speed_hz,
        .mode = 0,
        .spics_io_num = config->pin_cs,
        .queue_size = 10,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_bus_add_device(config->spi_host, &devcfg, &g_fs_state.spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        spi_bus_free(config->spi_host);
        return ret;
    }

    // Initialize NAND Flash
    spi_nand_flash_config_t nand_config = {
        .device_handle = g_fs_state.spi_handle,
        .io_mode = SPI_NAND_IO_MODE_SIO,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_nand_flash_init_device(&nand_config, &g_fs_state.flash_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NAND Flash: %s", esp_err_to_name(ret));
        spi_bus_remove_device(g_fs_state.spi_handle);
        spi_bus_free(config->spi_host);
        return ret;
    }

    // Mount FAT filesystem
    esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 20,
        .format_if_mount_failed = config->format_if_failed,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_nand_mount(config->mount_point, g_fs_state.flash_handle, &mount_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(ret));
        spi_nand_flash_deinit_device(g_fs_state.flash_handle);
        spi_bus_remove_device(g_fs_state.spi_handle);
        spi_bus_free(config->spi_host);
        return ret;
    }

    g_fs_state.initialized = true;
    ESP_LOGI(TAG, "✓ Battery filesystem initialized at %s", config->mount_point);

    return ESP_OK;
}

esp_err_t battery_fs_deinit(void) {
    if (!g_fs_state.initialized) {
        return ESP_OK;
    }

    // Unmount filesystem
    esp_vfs_fat_nand_unmount(g_fs_state.mount_point, g_fs_state.flash_handle);
    
    // Cleanup NAND flash
    if (g_fs_state.flash_handle) {
        spi_nand_flash_deinit_device(g_fs_state.flash_handle);
        g_fs_state.flash_handle = NULL;
    }
    
    // Cleanup SPI
    if (g_fs_state.spi_handle) {
        spi_bus_remove_device(g_fs_state.spi_handle);
        g_fs_state.spi_handle = NULL;
    }

    g_fs_state.initialized = false;
    ESP_LOGI(TAG, "✓ Battery filesystem deinitialized");

    return ESP_OK;
}

// ============================================================================
// Metadata Functions
// ============================================================================

bool battery_fs_exists(const char *serial_number) {
    if (!g_fs_state.initialized || serial_number == NULL) {
        return false;
    }

    char filepath[128];
    build_data_path(serial_number, filepath, sizeof(filepath));

    struct stat st;
    return (stat(filepath, &st) == 0);
}

esp_err_t battery_fs_read_metadata(const char *serial_number, battery_metadata_t *metadata) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (serial_number == NULL || metadata == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    char metapath[128];
    build_meta_path(serial_number, metapath, sizeof(metapath));

    FILE *f = fopen(metapath, "rb");
    if (f == NULL) {
        ESP_LOGD(TAG, "No metadata file for %s", serial_number);
        return ESP_ERR_NOT_FOUND;
    }

    size_t read = fread(metadata, sizeof(battery_metadata_t), 1, f);
    fclose(f);

    if (read != 1) {
        ESP_LOGE(TAG, "Failed to read metadata");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Read metadata for %s: last_index=%lu, records=%lu", 
             serial_number, (unsigned long)metadata->last_memory_index, 
             (unsigned long)metadata->record_count);

    return ESP_OK;
}

esp_err_t battery_fs_write_metadata(const char *serial_number, const battery_metadata_t *metadata) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (serial_number == NULL || metadata == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    char metapath[128];
    build_meta_path(serial_number, metapath, sizeof(metapath));

    FILE *f = fopen(metapath, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create metadata file %s (errno: %d - %s)", 
                 metapath, errno, strerror(errno));
        return ESP_FAIL;
    }

    size_t written = fwrite(metadata, sizeof(battery_metadata_t), 1, f);
    fclose(f);

    if (written != 1) {
        ESP_LOGE(TAG, "Failed to write metadata");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✓ Wrote metadata for %s: last_index=%lu, records=%lu", 
             serial_number, (unsigned long)metadata->last_memory_index, 
             (unsigned long)metadata->record_count);

    return ESP_OK;
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Calculate CRC32 hash of data
 */
static uint32_t calculate_data_hash(const uint8_t *data, size_t len) {
    return esp_crc32_le(0, data, len);
}

esp_err_t battery_fs_identify_new_records(const battery_metadata_t *metadata, 
                                          const battery_log_t *logs, 
                                          size_t log_count,
                                          battery_log_t *new_logs, 
                                          size_t *new_count) {
    if (metadata == NULL || logs == NULL || new_logs == NULL || new_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *new_count = 0;

    // If no existing data, all logs are new
    if (metadata->record_count == 0) {
        memcpy(new_logs, logs, log_count * sizeof(battery_log_t));
        *new_count = log_count;
        ESP_LOGI(TAG, "No existing records, all %u logs are new", log_count);
        return ESP_OK;
    }

    // Find the log with memory_index matching last_memory_index
    const battery_log_t *matching_log = NULL;
    for (size_t i = 0; i < log_count; i++) {
        if (logs[i].memory_index == metadata->last_memory_index) {
            matching_log = &logs[i];
            break;
        }
    }

    // Check if data at last_memory_index has been overwritten
    if (matching_log != NULL) {
        uint32_t current_hash = calculate_data_hash(matching_log->data, matching_log->data_len);
        
        if (current_hash != metadata->last_data_hash) {
            // Data has been overwritten - ring buffer wrapped around
            ESP_LOGW(TAG, "Ring buffer overwrite detected! Hash mismatch at index %lu (stored: 0x%08lX, current: 0x%08lX)",
                     (unsigned long)metadata->last_memory_index,
                     (unsigned long)metadata->last_data_hash,
                     (unsigned long)current_hash);
            ESP_LOGI(TAG, "Appending ALL %u memory logs", log_count);
            
            // Append all logs
            memcpy(new_logs, logs, log_count * sizeof(battery_log_t));
            *new_count = log_count;
            return ESP_OK;
        }
    }

    // Hash matches or log not found - check if wraparound condition
    if (metadata->last_memory_index >= 256) {
        // Wraparound condition - leave blank for now
        ESP_LOGW(TAG, "Last memory index >= 256, potential wraparound.");
        *new_count = 0;
        return ESP_OK;
    }

    // Normal case: last_memory_index < 256 and hash matches
    // Only take records after last_memory_index
    ESP_LOGI(TAG, "Hash matches, identifying new records after index %lu", 
             (unsigned long)metadata->last_memory_index);
    
    for (size_t i = 0; i < log_count; i++) {
        if (logs[i].memory_index > metadata->last_memory_index) {
            new_logs[*new_count] = logs[i];
            (*new_count)++;
        }
    }

    ESP_LOGI(TAG, "Identified %u new records (last_index was %lu)", 
             *new_count, (unsigned long)metadata->last_memory_index);

    return ESP_OK;
}

// ============================================================================
// Data Write Functions
// ============================================================================

esp_err_t battery_fs_write_data(const char *serial_number, const battery_log_t *logs, size_t log_count) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (serial_number == NULL || logs == NULL || log_count == 0) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Writing battery data for %s (%u logs)", serial_number, log_count);

    // Step 1: Check if battery file exists
    bool exists = battery_fs_exists(serial_number);

    battery_log_t *logs_to_write = NULL;
    size_t write_count = 0;
    battery_metadata_t metadata = {0};
    bool free_logs = false;

    if (exists) {
        ESP_LOGI(TAG, "Battery %s exists, checking for new records...", serial_number);
        
        // Step 2: Read existing metadata
        esp_err_t ret = battery_fs_read_metadata(serial_number, &metadata);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read metadata, treating as new battery");
            exists = false;
        } else {
            // Step 3: Identify new records
            battery_log_t *new_logs = malloc(log_count * sizeof(battery_log_t));
            if (new_logs == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for new logs");
                return ESP_ERR_NO_MEM;
            }

            ret = battery_fs_identify_new_records(&metadata, logs, log_count, new_logs, &write_count);
            if (ret != ESP_OK) {
                free(new_logs);
                return ret;
            }

            if (write_count == 0) {
                ESP_LOGI(TAG, "No new records to write for %s", serial_number);
                free(new_logs);
                return ESP_OK;
            }

            logs_to_write = new_logs;
            free_logs = true;
        }
    }

    if (!exists) {
        // New battery, write all logs
        ESP_LOGI(TAG, "New battery %s, writing all %u logs", serial_number, log_count);
        logs_to_write = (battery_log_t *)logs;
        write_count = log_count;
    }

    // Step 4: Write data to file
    char filepath[128];
    build_data_path(serial_number, filepath, sizeof(filepath));

    const char *mode = exists ? "ab" : "wb";  // Append if exists, write if new
    FILE *f = fopen(filepath, mode);
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s (errno: %d - %s)", 
                 filepath, errno, strerror(errno));
        if (free_logs) free(logs_to_write);
        return ESP_FAIL;
    }

    // Write each log entry
    for (size_t i = 0; i < write_count; i++) {
        // Write memory index
        if (fwrite(&logs_to_write[i].memory_index, sizeof(uint32_t), 1, f) != 1) {
            ESP_LOGE(TAG, "Failed to write memory index");
            fclose(f);
            if (free_logs) free(logs_to_write);
            return ESP_FAIL;
        }

        // Write data length
        if (fwrite(&logs_to_write[i].data_len, sizeof(size_t), 1, f) != 1) {
            ESP_LOGE(TAG, "Failed to write data length");
            fclose(f);
            if (free_logs) free(logs_to_write);
            return ESP_FAIL;
        }

        // Write binary data
        if (fwrite(logs_to_write[i].data, 1, logs_to_write[i].data_len, f) != logs_to_write[i].data_len) {
            ESP_LOGE(TAG, "Failed to write binary data");
            fclose(f);
            if (free_logs) free(logs_to_write);
            return ESP_FAIL;
        }
    }

    fclose(f);
    if (free_logs) free(logs_to_write);

    ESP_LOGI(TAG, "✓ Wrote %u records to %s", write_count, serial_number);

    // Step 5: Update metadata after successful write
    // Find the highest memory_index and its corresponding data hash
    uint32_t last_index = logs[0].memory_index;
    const battery_log_t *last_log = &logs[0];
    
    for (size_t i = 0; i < log_count; i++) {
        if (logs[i].memory_index > last_index) {
            last_index = logs[i].memory_index;
            last_log = &logs[i];
        }
    }

    metadata.last_memory_index = last_index;
    metadata.last_data_hash = calculate_data_hash(last_log->data, last_log->data_len);
    metadata.record_count = exists ? (metadata.record_count + write_count) : write_count;
    metadata.last_timestamp = time(NULL);
    
    ESP_LOGI(TAG, "Updating metadata: index=%lu, hash=0x%08lX, records=%lu",
             (unsigned long)last_index, (unsigned long)metadata.last_data_hash,
             (unsigned long)metadata.record_count);

    esp_err_t ret = battery_fs_write_metadata(serial_number, &metadata);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update metadata (data was written successfully)");
    }

    return ESP_OK;
}

// ============================================================================
// Delete Functions
// ============================================================================

esp_err_t battery_fs_delete_battery(const char *serial_number) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (serial_number == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[128];
    char metapath[128];
    build_data_path(serial_number, filepath, sizeof(filepath));
    build_meta_path(serial_number, metapath, sizeof(metapath));

    bool data_deleted = false;
    bool meta_deleted = false;

    // Delete data file
    if (remove(filepath) == 0) {
        ESP_LOGI(TAG, "✓ Deleted data file: %s", serial_number);
        data_deleted = true;
    } else if (errno != ENOENT) {
        ESP_LOGE(TAG, "Failed to delete data file %s (errno: %d - %s)", 
                 filepath, errno, strerror(errno));
    }

    // Delete metadata file
    if (remove(metapath) == 0) {
        ESP_LOGI(TAG, "✓ Deleted metadata file: %s", serial_number);
        meta_deleted = true;
    } else if (errno != ENOENT) {
        ESP_LOGE(TAG, "Failed to delete metadata file %s (errno: %d - %s)", 
                 metapath, errno, strerror(errno));
    }

    if (!data_deleted && !meta_deleted) {
        ESP_LOGW(TAG, "Battery %s not found", serial_number);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t battery_fs_delete_all(void) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deleting all battery files from %s...", g_fs_state.mount_point);

    DIR *dir = opendir(g_fs_state.mount_point);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory %s (errno: %d - %s)", 
                 g_fs_state.mount_point, errno, strerror(errno));
        return ESP_FAIL;
    }

    struct dirent *entry;
    size_t deleted_count = 0;
    size_t failed_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. directory entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char filepath[256];
        int len = snprintf(filepath, sizeof(filepath), "%s/%s", g_fs_state.mount_point, entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(filepath)) {
            ESP_LOGE(TAG, "Filepath too long: %s", entry->d_name);
            failed_count++;
            continue;
        }

        if (remove(filepath) == 0) {
            ESP_LOGI(TAG, "✓ Deleted: %s", entry->d_name);
            deleted_count++;
        } else {
            ESP_LOGE(TAG, "✗ Failed to delete: %s (errno: %d - %s)", 
                     entry->d_name, errno, strerror(errno));
            failed_count++;
        }
    }

    closedir(dir);

    ESP_LOGI(TAG, "Delete complete: %u deleted, %u failed", deleted_count, failed_count);
    return (failed_count == 0) ? ESP_OK : ESP_FAIL;
}
