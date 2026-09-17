#pragma once
/* Host test fixture ONLY; cross-build always uses real ESP-IDF headers. */
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
typedef int esp_err_t;
typedef int i2c_port_t;
typedef int i2c_mode_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define I2C_NUM_0 0
#define I2C_MODE_MASTER 1
#define GPIO_PULLUP_ENABLE 1
typedef struct {
    i2c_mode_t mode;
    int sda_io_num;
    int scl_io_num;
    int sda_pullup_en;
    int scl_pullup_en;
    struct { uint32_t clk_speed; } master;
} i2c_config_t;
esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *config);
esp_err_t i2c_driver_install(i2c_port_t port, i2c_mode_t mode,
                             size_t rx, size_t tx, int intr_flags);
esp_err_t i2c_driver_delete(i2c_port_t port);
esp_err_t i2c_master_write_read_device(i2c_port_t port, uint8_t address,
                     const uint8_t *wr, size_t nwr, uint8_t *rd, size_t nrd,
                     TickType_t ticks);
esp_err_t i2c_master_write_to_device(i2c_port_t port, uint8_t address,
                     const uint8_t *wr, size_t nwr, TickType_t ticks);
esp_err_t i2c_master_read_from_device(i2c_port_t port, uint8_t address,
                     uint8_t *rd, size_t nrd, TickType_t ticks);
