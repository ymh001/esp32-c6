#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "orientation_math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_LCD_H_RES 480
#define BOARD_LCD_V_RES 480

#define BOARD_I2C_SCL_GPIO 7
#define BOARD_I2C_SDA_GPIO 8

#define BOARD_LCD_CS_GPIO 15
#define BOARD_LCD_PCLK_GPIO 0
#define BOARD_LCD_D0_GPIO 1
#define BOARD_LCD_D1_GPIO 2
#define BOARD_LCD_D2_GPIO 3
#define BOARD_LCD_D3_GPIO 4

#define BOARD_TOUCH_RST_GPIO 11
#define BOARD_TOUCH_INT_GPIO 5

#define BOARD_KEY_GPIO 10

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_touch_handle_t touch;
} board_display_t;

esp_err_t board_init(void);
const board_display_t *board_display(void);

void board_set_backlight(uint8_t percent);
esp_err_t board_power_get_battery(uint8_t *percent, uint16_t *voltage_mv);
esp_err_t board_rtc_read(struct tm *out);
esp_err_t board_rtc_write(const struct tm *value);

bool board_key_pressed(void);
esp_err_t board_auto_rotation_update(board_rotation_t *rotation, bool *changed);

#ifdef __cplusplus
}
#endif
