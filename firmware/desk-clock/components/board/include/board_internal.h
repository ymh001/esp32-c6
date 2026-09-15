#pragma once

#include <stdint.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t board_i2c_init(void);
esp_err_t board_i2c_add_device(uint8_t address, uint32_t speed_hz,
                               i2c_master_dev_handle_t *out);
esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t device, uint8_t reg,
                              const uint8_t *data, size_t len);
esp_err_t board_i2c_read_reg(i2c_master_dev_handle_t device, uint8_t reg,
                             uint8_t *data, size_t len);

esp_err_t board_power_init(void);
esp_err_t board_display_init(board_display_t *display);
esp_err_t board_display_reset(void);
esp_err_t board_display_set_rotation(board_rotation_t rotation);
esp_err_t board_imu_init(void);
esp_err_t board_rtc_init(void);
esp_err_t board_buttons_init(void);

i2c_master_bus_handle_t board_i2c_bus(void);
bool board_i2c_ready(void);
