#include "board.h"

#include "board_internal.h"

#define PCF85063A_ADDRESS 0x51

static i2c_master_dev_handle_t s_rtc;

static uint8_t bcd_to_dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0f));
}

static uint8_t dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

esp_err_t board_rtc_init(void)
{
    if (!board_i2c_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    return board_i2c_add_device(PCF85063A_ADDRESS, 100000, &s_rtc);
}

esp_err_t board_rtc_read(struct tm *out)
{
    if (out == NULL || s_rtc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[7] = {0};
    esp_err_t err = board_i2c_read_reg(s_rtc, 0x04, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    // Oscillator-stop flag means the calendar cannot be trusted.
    if (data[0] & 0x80) return ESP_ERR_INVALID_STATE;
    out->tm_sec = bcd_to_dec(data[0] & 0x7f);
    out->tm_min = bcd_to_dec(data[1] & 0x7f);
    out->tm_hour = bcd_to_dec(data[2] & 0x3f);
    out->tm_mday = bcd_to_dec(data[3] & 0x3f);
    out->tm_wday = data[4] & 0x07;
    out->tm_mon = bcd_to_dec(data[5] & 0x1f) - 1;
    out->tm_year = bcd_to_dec(data[6]) + 100;
    out->tm_isdst = -1;

    if (out->tm_year < 120 || out->tm_mon < 0 || out->tm_mon > 11 ||
        out->tm_mday < 1 || out->tm_mday > 31 || out->tm_hour > 23 ||
        out->tm_min > 59 || out->tm_sec > 59) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t board_rtc_write(const struct tm *value)
{
    if (value == NULL || s_rtc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t data[7] = {
        (uint8_t)(dec_to_bcd(value->tm_sec) & 0x7f),
        (uint8_t)(dec_to_bcd(value->tm_min) & 0x7f),
        (uint8_t)(dec_to_bcd(value->tm_hour) & 0x3f),
        (uint8_t)(dec_to_bcd(value->tm_mday) & 0x3f),
        (uint8_t)(value->tm_wday & 0x07),
        (uint8_t)(dec_to_bcd((uint8_t)(value->tm_mon + 1)) & 0x1f),
        dec_to_bcd(value->tm_year % 100),
    };
    return board_i2c_write_reg(s_rtc, 0x04, data, sizeof(data));
}
