#pragma once

#include "driver/i2c_master.h"
#include "Batmon_struct.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t i2c_handle;
    uint8_t address;
    uint8_t numTherms;
} batmon_handle_t;

/**
 * @brief Initialize BATMON device
 * 
 * @param bus_handle I2C master bus handle
 * @param address I2C address of the BATMON device
 * @param numTherms Number of thermistors
 * @param out_handle Pointer to store the created handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t BATMON_init(i2c_master_bus_handle_t bus_handle, uint8_t address, uint8_t numTherms, batmon_handle_t *out_handle);

/**
 * @brief Read cell voltages
 * 
 * @param handle BATMON device handle
 * @param cv Pointer to CVolts structure to store data
 * @return byte 0: No error, 1: CRC error, 2: i2c error, 3: status error
 */
uint8_t BATMON_readCellVoltages(batmon_handle_t *handle, Batmon_cellVoltages *cv);

/**
 * @brief Read status
 * 
 * @param handle BATMON device handle
 * @param st Pointer to store status byte
 * @return byte 0: No error, 1: CRC error, 2: i2c error, 3: status error without CRC error, 4: status error with CRC error
 */
uint8_t BATMON_readStatus(batmon_handle_t *handle, uint8_t *st);

/**
 * @brief Read total voltage
 * 
 * @param handle BATMON device handle
 * @param tv Pointer to TotVolt structure
 * @return byte 0: No error, 1: CRC error, 2: i2c error, 3: status error
 */
uint8_t BATMON_readTotalVoltage(batmon_handle_t *handle, Batmon_totalVoltage *tv);

/**
 * @brief Read thermistors
 * 
 * @param handle BATMON device handle
 * @param ts Pointer to Therms structure
 * @param num Thermistor index (0: internal, 1: external 1, 2: external 2)
 * @return byte 0: No error, 1: CRC error, 2: i2c error, 3: status error
 */
uint8_t BATMON_readTherms(batmon_handle_t *handle, Batmon_thermistors *ts, uint8_t num);

/**
 * @brief Get current in mA
 * 
 * @param handle BATMON device handle
 * @param current Pointer to store current
 * @return esp_err_t 
 */
esp_err_t BATMON_getCur(batmon_handle_t *handle, int16_t *current);

/**
 * @brief Get State of Charge
 * 
 * @param handle BATMON device handle
 * @param soc Pointer to store SOC
 * @return esp_err_t 
 */
esp_err_t BATMON_getSOC(batmon_handle_t *handle, uint16_t *soc);

/**
 * @brief Get Cell Count
 * 
 * @param handle BATMON device handle
 * @param cellCount Pointer to store cell count
 * @return esp_err_t 
 */
esp_err_t BATMON_getCellCount(batmon_handle_t *handle, uint16_t *cellCount);

esp_err_t BATMON_getDeciCur(batmon_handle_t *handle, int *current);
esp_err_t BATMON_getTInt(batmon_handle_t *handle, int *temp);
esp_err_t BATMON_getTExt(batmon_handle_t *handle, uint8_t extThermNum, int *temp);
esp_err_t BATMON_read_mAh_discharged(batmon_handle_t *handle, int16_t *discharged);
esp_err_t BATMON_readRemainCap(batmon_handle_t *handle, uint16_t *cap);
esp_err_t BATMON_getHash(batmon_handle_t *handle, uint16_t *hash);
bool BATMON_getSN(batmon_handle_t *handle, uint16_t sn[8]);
esp_err_t BATMON_getBattStatus(batmon_handle_t *handle, uint16_t *battStatus);
esp_err_t BATMON_getMan(batmon_handle_t *handle, uint8_t *buf, size_t len);
esp_err_t BATMON_getMemoryInfo(batmon_handle_t *handle, BATMON_Mem_Info *mem_info);
bool BATMON_getMemory(batmon_handle_t *handle, BatmonMemory *batmem, const BATMON_Mem_Info *mem_info);

#ifdef __cplusplus
}
#endif
