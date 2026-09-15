#include "board_internal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AXP2101_ADDRESS 0x34

static i2c_master_dev_handle_t s_pmic;

static uint8_t battery_percent_from_voltage(uint16_t voltage_mv)
{
    static const uint16_t voltages[] = {
        3300, 3500, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4200,
    };
    static const uint8_t percents[] = {
        0, 10, 20, 30, 40, 50, 70, 80, 90, 100,
    };

    if (voltage_mv <= voltages[0]) {
        return 0;
    }
    for (size_t i = 1; i < sizeof(voltages) / sizeof(voltages[0]); ++i) {
        if (voltage_mv <= voltages[i]) {
            const uint16_t low_mv = voltages[i - 1];
            const uint16_t high_mv = voltages[i];
            const uint8_t low_percent = percents[i - 1];
            const uint8_t high_percent = percents[i];
            const uint32_t span = high_mv - low_mv;
            return (uint8_t)(
                low_percent +
                (uint32_t)(voltage_mv - low_mv) *
                    (high_percent - low_percent) / span);
        }
    }
    return 100;
}

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
    const struct {
        uint8_t reg;
        uint8_t value;
    } writes[] = {
        {0x22, 0x06}, {0x27, 0x10}, {0x80, 0x01}, {0x90, 0x00},
        {0x91, 0x00}, {0x82, 18},   {0x92, 28},   {0x93, 28},
        {0x94, 28},   {0x95, 28},   {0x90, 0x0f}, {0x64, 0x03},
        {0x61, 0x02}, {0x62, 0x0a}, {0x63, 0x01},
    };
    for (const auto &write : writes) {
        err = axp_write(write.reg, write.value);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = axp_update(0x30, 0x01, 0x01);
    if (err != ESP_OK) {
        return err;
    }
    err = axp_update(0x68, 0x01, 0x01);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t board_power_get_battery(uint8_t *percent, uint16_t *voltage_mv)
{
    if (s_pmic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t status = 0;
    esp_err_t err = board_i2c_read_reg(s_pmic, 0x00, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    if ((status & 0x08) == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t high = 0;
    uint8_t low = 0;
    esp_err_t voltage_err = board_i2c_read_reg(s_pmic, 0x34, &high, 1);
    if (voltage_err == ESP_OK) {
        voltage_err = board_i2c_read_reg(s_pmic, 0x35, &low, 1);
    }

    if (voltage_err != ESP_OK) {
        return voltage_err;
    }
    const uint16_t voltage =
        (uint16_t)(((uint16_t)(high & 0x1F) << 8) | low);
    if (percent != NULL) {
        *percent = voltage >= 3000 ? battery_percent_from_voltage(voltage)
                                   : 0;
    }
    if (voltage_mv != NULL) {
        *voltage_mv = voltage;
    }
    return ESP_OK;
}

esp_err_t board_display_reset(void)
{
    // There is no dedicated LCD reset GPIO on this board.
    esp_err_t err = axp_update(0x90, 0x04, 0x04);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    err = axp_update(0x90, 0x04, 0x00);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    err = axp_update(0x90, 0x04, 0x04);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}
