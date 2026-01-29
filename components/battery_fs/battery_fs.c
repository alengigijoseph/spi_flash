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
#include "esp_log.h"
#include "esp_vfs_fat_nand.h"
#include "spi_nand_flash.h"
#include "nand_diag_api.h"
#include "driver/spi_master.h"

static const char *TAG = "battery_fs";

// Internal state
static struct {
    bool initialized;
    spi_nand_flash_device_t *flash_handle;
    spi_device_handle_t spi_handle;
    char mount_point[32];
} g_fs_state = {0};

/**
 * @brief Build full file path from battery serial number
 */
static void build_file_path(const char *battery_serial, char *path, size_t path_size) {
    // Use .bin extension and remove special characters for FAT compatibility
    snprintf(path, path_size, "%s/%s.bin", g_fs_state.mount_point, battery_serial);
}

/**
 * @brief Build metadata file path from battery serial number
 */
static void build_meta_path(const char *battery_serial, char *path, size_t path_size) {
    // Use .met extension (3 chars for FAT compatibility)
    snprintf(path, path_size, "%s/%s.met", g_fs_state.mount_point, battery_serial);
}

/**
 * @brief Load last file position from metadata
 */
static long load_last_position(const char *battery_serial) {
    char metapath[128];
    build_meta_path(battery_serial, metapath, sizeof(metapath));
    
    FILE *f = fopen(metapath, "r");
    if (f == NULL) {
        ESP_LOGD(TAG, "No metadata file for %s, starting from position 0", battery_serial);
        return 0;  // No metadata, start from beginning
    }
    
    long position = 0;
    fscanf(f, "%ld", &position);
    fclose(f);
    
    ESP_LOGI(TAG, "Loaded position %ld from metadata for %s", position, battery_serial);
    return position;
}

/**
 * @brief Save current file position to metadata
 */
static void save_last_position(const char *battery_serial, long position) {
    char metapath[128];
    build_meta_path(battery_serial, metapath, sizeof(metapath));
    
    ESP_LOGI(TAG, "Attempting to save position %ld to %s", position, metapath);
    
    FILE *f = fopen(metapath, "w");
    if (f != NULL) {
        fprintf(f, "%ld", position);
        fclose(f);
        ESP_LOGI(TAG, "✓ Saved position %ld to metadata for %s", position, battery_serial);
    } else {
        ESP_LOGE(TAG, "Failed to save metadata to %s (errno: %d - %s)", 
                 metapath, errno, strerror(errno));
    }
}

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
    ESP_LOGI(TAG, "Battery filesystem initialized at %s", config->mount_point);

    return ESP_OK;
}

esp_err_t battery_fs_file_exists(const char *battery_serial, bool *exists) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || exists == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[128];
    build_file_path(battery_serial, filepath, sizeof(filepath));

    struct stat st;
    *exists = (stat(filepath, &st) == 0);

    return ESP_OK;
}

esp_err_t battery_fs_clear_all_logs(void) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Clearing all battery log files from %s...", g_fs_state.mount_point);

    DIR *dir = opendir(g_fs_state.mount_point);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory %s (errno: %d - %s)", 
                 g_fs_state.mount_point, errno, strerror(errno));
        return ESP_FAIL;
    }

    struct dirent *entry;
    size_t deleted_count = 0;
    size_t failed_count = 0;
    size_t total_files = 0;

    while ((entry = readdir(dir)) != NULL) {
        total_files++;
        ESP_LOGI(TAG, "Found file: %s", entry->d_name);
        
        // Delete .bin and .meta/.met files (case-insensitive check)
        const char *ext = strrchr(entry->d_name, '.');
        if (ext != NULL && (strcasecmp(ext, ".bin") == 0 || 
                           strcasecmp(ext, ".meta") == 0 || 
                           strcasecmp(ext, ".met") == 0)) {
            char filepath[512];
            int len = snprintf(filepath, sizeof(filepath), "%s/%s", g_fs_state.mount_point, entry->d_name);
            if (len < 0 || (size_t)len >= sizeof(filepath)) {
                ESP_LOGE(TAG, "Filepath too long: %s", entry->d_name);
                failed_count++;
                continue;
            }
            
            ESP_LOGI(TAG, "Deleting: %s", filepath);
            if (remove(filepath) == 0) {
                ESP_LOGI(TAG, "✓ Deleted: %s", entry->d_name);
                deleted_count++;
            } else {
                ESP_LOGE(TAG, "✗ Failed to delete: %s (errno: %d - %s)", 
                         entry->d_name, errno, strerror(errno));
                failed_count++;
            }
        }
    }

    closedir(dir);

    ESP_LOGI(TAG, "Directory scan complete: %u total files, %u deleted, %u failed", 
             total_files, deleted_count, failed_count);
    return (failed_count == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t battery_fs_write_data(const char *battery_serial, const battery_data_t *data) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || data == NULL || data->binary_data == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[128];
    build_file_path(battery_serial, filepath, sizeof(filepath));

    // Check if file exists to determine mode
    bool exists = false;
    battery_fs_file_exists(battery_serial, &exists);

    const char *mode = exists ? "a" : "w";  // Use text mode for FAT compatibility
    
    ESP_LOGI(TAG, "%s data to %s (log: %lu, size: %u bytes)",
             exists ? "Appending" : "Creating", filepath, data->log_number, data->data_len);

    FILE *f = fopen(filepath, mode);
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s (errno: %d - %s)", filepath, errno, strerror(errno));
        return ESP_FAIL;
    }

    // Write log number (4 bytes) - write as binary data in text mode
    size_t written = fwrite(&data->log_number, 1, sizeof(uint32_t), f);
    if (written != sizeof(uint32_t)) {
        ESP_LOGE(TAG, "Failed to write log number");
        fclose(f);
        return ESP_FAIL;
    }

    // Write data length (4 bytes)
    uint32_t len = data->data_len;
    written = fwrite(&len, 1, sizeof(uint32_t), f);
    if (written != sizeof(uint32_t)) {
        ESP_LOGE(TAG, "Failed to write data length");
        fclose(f);
        return ESP_FAIL;
    }

    // Write binary data
    written = fwrite(data->binary_data, 1, data->data_len, f);
    if (written != data->data_len) {
        ESP_LOGE(TAG, "Failed to write binary data (wrote %u of %u bytes)", written, data->data_len);
        fclose(f);
        return ESP_FAIL;
    }

    fclose(f);

    ESP_LOGI(TAG, "Successfully wrote log %lu to %s", data->log_number, battery_serial);
    return ESP_OK;
}

esp_err_t battery_fs_write_bulk(const char *battery_serial, const battery_data_t *data_array, size_t count) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || data_array == NULL || count == 0) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[128];
    build_file_path(battery_serial, filepath, sizeof(filepath));

    // Check if file exists to determine mode
    bool exists = false;
    battery_fs_file_exists(battery_serial, &exists);

    const char *mode = exists ? "a" : "w";
    
    ESP_LOGI(TAG, "Bulk writing %u entries to %s", count, battery_serial);

    FILE *f = fopen(filepath, mode);
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s (errno: %d - %s)", filepath, errno, strerror(errno));
        return ESP_FAIL;
    }

    size_t written_count = 0;
    
    // Write all entries in one file open/close cycle
    for (size_t i = 0; i < count; i++) {
        const battery_data_t *data = &data_array[i];
        
        if (data->binary_data == NULL) {
            ESP_LOGW(TAG, "Skipping entry %u: NULL data", i);
            continue;
        }

        // Write log number
        size_t written = fwrite(&data->log_number, 1, sizeof(uint32_t), f);
        if (written != sizeof(uint32_t)) {
            ESP_LOGE(TAG, "Failed to write log number at entry %u", i);
            break;
        }

        // Write data length
        uint32_t len = data->data_len;
        written = fwrite(&len, 1, sizeof(uint32_t), f);
        if (written != sizeof(uint32_t)) {
            ESP_LOGE(TAG, "Failed to write data length at entry %u", i);
            break;
        }

        // Write binary data
        written = fwrite(data->binary_data, 1, data->data_len, f);
        if (written != data->data_len) {
            ESP_LOGE(TAG, "Failed to write binary data at entry %u", i);
            break;
        }

        written_count++;
    }

    fclose(f);

    ESP_LOGI(TAG, "Successfully bulk wrote %u/%u entries to %s", written_count, count, battery_serial);
    return (written_count == count) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Calculate CRC32 hash of binary data
 */
static uint32_t calculate_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

/**
 * @brief Entry index for fast lookup
 */
typedef struct {
    uint32_t log_number;
    uint32_t hash;
    bool valid;
} entry_index_t;

esp_err_t battery_fs_sync_from_ring(const char *battery_serial, const battery_data_t *data_array, size_t count, size_t *written_count) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || data_array == NULL || count == 0) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Smart sync: Comparing %u entries from ring buffer with flash data", count);
    uint32_t start_time = esp_log_timestamp();

    // Check if file exists
    bool file_exists = false;
    battery_fs_file_exists(battery_serial, &file_exists);

    // Build index of existing entries from flash
    entry_index_t *flash_index = calloc(count, sizeof(entry_index_t));
    if (flash_index == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for flash index");
        return ESP_ERR_NO_MEM;
    }

    size_t flash_entries = 0;

    if (file_exists) {
        // Read last N entries from flash to build comparison index
        ESP_LOGI(TAG, "Loading last %u entries from flash for comparison", count);
        
        char filepath[128];
        build_file_path(battery_serial, filepath, sizeof(filepath));
        
        FILE *f = fopen(filepath, "r");
        if (f != NULL) {
            // Get file size
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            
            // Estimate position to start reading last N entries
            // Each entry is ~78 bytes (4+4+70), seek back N*85 with margin
            long estimated_bytes = count * 85;
            long start_pos = (file_size > estimated_bytes) ? (file_size - estimated_bytes) : 0;
            fseek(f, start_pos, SEEK_SET);
            
            ESP_LOGI(TAG, "Reading last %u entries from file (size: %ld, seeking from: %ld)", 
                     count, file_size, start_pos);
            
            // Read all entries from this point, keep last N in circular buffer
            uint8_t *buffer = malloc(4096);
            if (buffer != NULL) {
                size_t write_idx = 0;
                
                while (ftell(f) < file_size) {
                    uint32_t log_num, data_len;
                    
                    if (fread(&log_num, 1, sizeof(uint32_t), f) != sizeof(uint32_t)) break;
                    if (fread(&data_len, 1, sizeof(uint32_t), f) != sizeof(uint32_t)) break;
                    
                    if (data_len > 0 && data_len <= 4096) {
                        size_t read_bytes = fread(buffer, 1, data_len, f);
                        if (read_bytes == data_len) {
                            // Store in circular buffer - keeps only last N
                            size_t idx = write_idx % count;
                            flash_index[idx].log_number = log_num;
                            flash_index[idx].hash = calculate_crc32(buffer, data_len);
                            flash_index[idx].valid = true;
                            write_idx++;
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                
                flash_entries = (write_idx < count) ? write_idx : count;
                free(buffer);
            }
            
            fclose(f);
            ESP_LOGI(TAG, "Loaded %u entries from flash for comparison", flash_entries);
        }
    } else {
        ESP_LOGI(TAG, "File doesn't exist - will write all entries");
    }

    // Compare incoming data against flash index
    battery_data_t *new_entries = malloc(count * sizeof(battery_data_t));
    if (new_entries == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for new entries");
        free(flash_index);
        return ESP_ERR_NO_MEM;
    }

    size_t new_count = 0;
    size_t duplicates = 0;

    for (size_t i = 0; i < count; i++) {
        const battery_data_t *incoming = &data_array[i];
        uint32_t incoming_hash = calculate_crc32(incoming->binary_data, incoming->data_len);
        bool is_duplicate = false;

        // Check if this entry exists in flash with same hash
        for (size_t j = 0; j < flash_entries; j++) {
            if (flash_index[j].valid && 
                flash_index[j].log_number == incoming->log_number &&
                flash_index[j].hash == incoming_hash) {
                is_duplicate = true;
                duplicates++;
                break;
            }
        }

        // Add to write list if not duplicate
        if (!is_duplicate) {
            new_entries[new_count++] = *incoming;
        }
    }

    ESP_LOGI(TAG, "Comparison complete: %u new/changed, %u duplicates", new_count, duplicates);

    // Write only new/changed entries using bulk write
    esp_err_t ret = ESP_OK;
    if (new_count > 0) {
        ret = battery_fs_write_bulk(battery_serial, new_entries, new_count);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✓ Wrote %u new/changed entries", new_count);
        }
    } else {
        ESP_LOGI(TAG, "No new data to write - all entries already exist in flash");
    }

    uint32_t elapsed = esp_log_timestamp() - start_time;
    ESP_LOGI(TAG, "Smart sync completed in %lu ms", elapsed);

    // Return written count if requested
    if (written_count != NULL) {
        *written_count = new_count;
    }

    free(new_entries);
    free(flash_index);

    return ret;
}

esp_err_t battery_fs_get_last_log(const char *battery_serial, uint32_t *last_log) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || last_log == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if file exists
    bool exists = false;
    battery_fs_file_exists(battery_serial, &exists);
    if (!exists) {
        ESP_LOGW(TAG, "Battery file %s does not exist", battery_serial);
        return ESP_ERR_NOT_FOUND;
    }

    char filepath[128];
    build_file_path(battery_serial, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for reading (errno: %d - %s)", 
                 filepath, errno, strerror(errno));
        return ESP_FAIL;
    }

    uint32_t log_num = 0;
    uint32_t last_valid_log = 0;
    bool found_entry = false;

    // Read through all entries to find the last one
    while (true) {
        // Read log number
        size_t read_bytes = fread(&log_num, 1, sizeof(uint32_t), f);
        if (read_bytes != sizeof(uint32_t)) {
            break;  // End of file or read error
        }

        // Read data length
        uint32_t data_len = 0;
        read_bytes = fread(&data_len, 1, sizeof(uint32_t), f);
        if (read_bytes != sizeof(uint32_t)) {
            ESP_LOGW(TAG, "Incomplete entry found, stopping read");
            break;
        }

        // Skip the binary data
        if (fseek(f, data_len, SEEK_CUR) != 0) {
            ESP_LOGW(TAG, "Failed to skip data, stopping read");
            break;
        }

        // Update last valid log number
        last_valid_log = log_num;
        found_entry = true;
    }

    fclose(f);

    if (!found_entry) {
        ESP_LOGW(TAG, "No valid entries found in %s", battery_serial);
        return ESP_ERR_NOT_FOUND;
    }

    *last_log = last_valid_log;
    ESP_LOGI(TAG, "Last log number for %s: %lu", battery_serial, last_valid_log);
    return ESP_OK;
}

esp_err_t battery_fs_read_data(const char *battery_serial, battery_data_read_cb_t callback, void *user_ctx) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || callback == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if file exists
    bool exists = false;
    battery_fs_file_exists(battery_serial, &exists);
    if (!exists) {
        ESP_LOGW(TAG, "Battery file %s does not exist", battery_serial);
        return ESP_ERR_NOT_FOUND;
    }

    char filepath[128];
    build_file_path(battery_serial, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for reading (errno: %d - %s)", 
                 filepath, errno, strerror(errno));
        return ESP_FAIL;
    }

    uint32_t log_num = 0;
    uint32_t data_len = 0;
    uint8_t *buffer = NULL;
    size_t buffer_size = 0;
    int entries_read = 0;
    bool continue_reading = true;

    ESP_LOGI(TAG, "Reading data from %s", battery_serial);

    // Read through all entries
    while (continue_reading) {
        // Read log number
        size_t read_bytes = fread(&log_num, 1, sizeof(uint32_t), f);
        if (read_bytes != sizeof(uint32_t)) {
            break;  // End of file or read error
        }

        // Read data length
        read_bytes = fread(&data_len, 1, sizeof(uint32_t), f);
        if (read_bytes != sizeof(uint32_t)) {
            ESP_LOGW(TAG, "Incomplete entry found, stopping read");
            break;
        }

        // Allocate/resize buffer if needed
        if (data_len > buffer_size) {
            uint8_t *new_buffer = realloc(buffer, data_len);
            if (new_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate %lu bytes for data", data_len);
                break;
            }
            buffer = new_buffer;
            buffer_size = data_len;
        }

        // Read binary data
        read_bytes = fread(buffer, 1, data_len, f);
        if (read_bytes != data_len) {
            ESP_LOGW(TAG, "Incomplete data read (expected %lu, got %u)", data_len, read_bytes);
            break;
        }

        // Call callback
        continue_reading = callback(log_num, buffer, data_len, user_ctx);
        entries_read++;
    }

    // Cleanup
    if (buffer != NULL) {
        free(buffer);
    }
    fclose(f);

    ESP_LOGI(TAG, "Read %d entries from %s", entries_read, battery_serial);
    return ESP_OK;
}

esp_err_t battery_fs_get_entry_count(const char *battery_serial, size_t *count) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || count == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if file exists
    bool exists = false;
    battery_fs_file_exists(battery_serial, &exists);
    if (!exists) {
        *count = 0;
        return ESP_ERR_NOT_FOUND;
    }

    char filepath[128];
    build_file_path(battery_serial, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s (errno: %d - %s)", filepath, errno, strerror(errno));
        return ESP_FAIL;
    }

    size_t entry_count = 0;
    uint32_t log_num, data_len;

    // Count entries by reading headers
    while (true) {
        if (fread(&log_num, 1, sizeof(uint32_t), f) != sizeof(uint32_t)) break;
        if (fread(&data_len, 1, sizeof(uint32_t), f) != sizeof(uint32_t)) break;
        if (fseek(f, data_len, SEEK_CUR) != 0) break;
        entry_count++;
    }

    fclose(f);
    *count = entry_count;
    ESP_LOGI(TAG, "Battery %s has %u entries", battery_serial, entry_count);
    return ESP_OK;
}

esp_err_t battery_fs_read_bulk(const char *battery_serial, battery_data_t *data_array, size_t max_count, size_t *actual_count) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_serial == NULL || data_array == NULL || actual_count == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if file exists
    bool exists = false;
    battery_fs_file_exists(battery_serial, &exists);
    if (!exists) {
        ESP_LOGW(TAG, "Battery file %s does not exist", battery_serial);
        *actual_count = 0;
        return ESP_ERR_NOT_FOUND;
    }

    char filepath[128];
    build_file_path(battery_serial, filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for reading (errno: %d - %s)", 
                 filepath, errno, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Bulk reading from %s (max %u entries)", battery_serial, max_count);

    size_t entries_read = 0;
    uint32_t log_num, data_len;

    // Read all entries into array
    while (entries_read < max_count) {
        // Read log number
        if (fread(&log_num, 1, sizeof(uint32_t), f) != sizeof(uint32_t)) break;

        // Read data length
        if (fread(&data_len, 1, sizeof(uint32_t), f) != sizeof(uint32_t)) {
            ESP_LOGW(TAG, "Incomplete entry found at index %u", entries_read);
            break;
        }

        // Allocate memory for binary data
        uint8_t *binary_data = malloc(data_len);
        if (binary_data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %lu bytes at entry %u", data_len, entries_read);
            break;
        }

        // Read binary data
        size_t read_bytes = fread(binary_data, 1, data_len, f);
        if (read_bytes != data_len) {
            ESP_LOGW(TAG, "Incomplete data at entry %u", entries_read);
            free(binary_data);
            break;
        }

        // Store in array
        data_array[entries_read].log_number = log_num;
        data_array[entries_read].binary_data = binary_data;
        data_array[entries_read].data_len = data_len;
        entries_read++;
    }

    fclose(f);

    *actual_count = entries_read;
    ESP_LOGI(TAG, "Bulk read %u entries from %s", entries_read, battery_serial);
    return ESP_OK;
}

esp_err_t battery_fs_get_info(uint64_t *total_kb, uint64_t *free_kb, uint64_t *used_kb) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t bytes_total, bytes_free;
    esp_err_t ret = esp_vfs_fat_info(g_fs_state.mount_point, &bytes_total, &bytes_free);
    if (ret != ESP_OK) {
        return ret;
    }

    if (total_kb) *total_kb = bytes_total / 1024;
    if (free_kb) *free_kb = bytes_free / 1024;
    if (used_kb) *used_kb = (bytes_total - bytes_free) / 1024;

    return ESP_OK;
}

esp_err_t battery_fs_get_wear_info(uint32_t *bad_block_count) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (bad_block_count == NULL) {
        ESP_LOGE(TAG, "bad_block_count pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Get bad block statistics using the diagnostic API (fast operation)
    esp_err_t ret = nand_get_bad_block_stats(g_fs_state.flash_handle, bad_block_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get bad block statistics: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Bad blocks: %lu", (unsigned long)*bad_block_count);

    return ESP_OK;
}

esp_err_t battery_fs_get_ecc_stats(void) {
    if (!g_fs_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Getting ECC statistics (this takes ~5 seconds)...");
    
    // Get and display detailed ECC statistics (SLOW - scans all pages)
    esp_err_t ret = nand_get_ecc_stats(g_fs_state.flash_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get ECC statistics: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t battery_fs_deinit(void) {
    if (!g_fs_state.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing battery filesystem");

    esp_vfs_fat_nand_unmount(g_fs_state.mount_point, g_fs_state.flash_handle);
    spi_nand_flash_deinit_device(g_fs_state.flash_handle);
    spi_bus_remove_device(g_fs_state.spi_handle);
    spi_bus_free(SPI2_HOST);  // Assumes SPI2_HOST, could be made configurable

    memset(&g_fs_state, 0, sizeof(g_fs_state));

    ESP_LOGI(TAG, "Battery filesystem deinitialized");
    return ESP_OK;
}
