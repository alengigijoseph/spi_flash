/**
 * @file battery_tests.h
 * @brief Test utilities for battery filesystem
 */

#ifndef BATTERY_TESTS_H
#define BATTERY_TESTS_H

#include <stddef.h>
#include "battery_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse hex string to byte array
 * @param hex_str Input hex string
 * @param output Output byte array
 * @param max_len Maximum output length
 * @return Number of bytes parsed
 */
size_t parse_hex_string(const char *hex_str, uint8_t *output, size_t max_len);

/**
 * @brief Test writing battery data from mock data
 */
void test_battery_logging(void);

/**
 * @brief Stress test - run battery logging 500 times
 */
void test_stress_write_500(void);

/**
 * @brief Test file existence check and last log number
 */
void test_file_check(void);

/**
 * @brief Test reading battery data using bulk read
 */
void test_read_data(void);

/**
 * @brief Display filesystem information
 */
void display_fs_info(void);

/**
 * @brief Display wear leveling information
 */
void display_wear_info(void);

/**
 * @brief Display detailed ECC statistics
 */
void display_ecc_stats(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_TESTS_H
