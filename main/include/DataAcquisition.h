#ifndef DATA_AQUISITION_H
#define DATA_AQUISITION_H


#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "BATMON.h"

#define NO_DBR 4
#define NO_BATMON 9

typedef enum {
    SYS_IDLE,
    SYS_CHARGING,
    SYS_FAULT,
    SYS_ESTOP
} system_state_t;

// Battery state tracking
typedef struct {
    bool is_connected;
    uint8_t address;
} battery_state_t;

// Global variables
extern i2c_master_bus_handle_t SMBus_handle;
extern batmon_handle_t BATMON_handle[NO_BATMON];
extern battery_state_t battery_state[NO_BATMON];
extern const uint8_t BATMON_addresses[NO_BATMON];

// function prototypes
esp_err_t init_i2c_bus(void);
void init_batmon_devices(void);
void get_battery_log(int batmon_index);
void SMBUS_update(void *arg);

#endif