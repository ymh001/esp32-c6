#include "board_internal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AXP2101_ADDRESS 0x34

static i2c_master_dev_handle_t s_pmic;

static esp_err_t axp_write(uint8_t reg, uint8_t value)
{
    return board_i2c_write_reg(s_pmic, reg, &value, 1);
}

static esp_err_t axp_update(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    esp_err_t err = board_i2c_read_reg(s_pmic, reg, &current, 1);
    if (err != ESP_OK) {
        return err;
    }
    current = (current & ~mask) | (value & mask);
    return axp_write(reg, current);
}

esp_err_t board_power_init(void)
{
    if (!board_i2c_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = board_i2c_add_device(AXP2101_ADDRESS, 100000, &s_pmic);
    if (err != ESP_OK) {
        return err;
    }

    // ALDO3 powers the panel, ALDO2 the speaker amplifier, ALDO1 the mics.
    err |= axp_write(0x22, 0x06);
    err |= axp_write(0x27, 0x10);
    err |= axp_write(0x80, 0x01);
    err |= axp_write(0x90, 0x00);
    err |= axp_write(0x91, 0x00);
    err |= axp_write(0x82, 18);
    err |= axp_write(0x92, 28);
    err |= axp_write(0x93, 28);
    err |= axp_write(0x94, 28);
    err |= axp_write(0x95, 28);
    err |= axp_write(0x90, 0x0f);
    err |= axp_write(0x64, 0x02);
    err |= axp_write(0x61, 0x02);
    err |= axp_write(0x62, 0x0a);
    err |= axp_write(0x63, 0x01);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t board_display_reset(void)
{
    // There is no dedicated LCD reset GPIO on this board.
    esp_err_t err = axp_update(0x90, 0x04, 0x04);
    vTaskDelay(pdMS_TO_TICKS(100));
    err |= axp_update(0x90, 0x04, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));
    err |= axp_update(0x90, 0x04, 0x04);
    vTaskDelay(pdMS_TO_TICKS(100));
    return err;
}
