#include "board.h"

#include "board_internal.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"

static const char *TAG = "display";

static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 600},
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x36, (uint8_t[]){0x30}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

esp_err_t board_display_init(board_display_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_bus_config_t bus_config = {};
    bus_config.data0_io_num = BOARD_LCD_D0_GPIO;
    bus_config.data1_io_num = BOARD_LCD_D1_GPIO;
    bus_config.sclk_io_num = BOARD_LCD_PCLK_GPIO;
    bus_config.data2_io_num = BOARD_LCD_D2_GPIO;
    bus_config.data3_io_num = BOARD_LCD_D3_GPIO;
    bus_config.max_transfer_sz =
        BOARD_LCD_H_RES * 84 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG,
        "SPI init failed");

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = BOARD_LCD_CS_GPIO;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 1;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                 &io_config, &display->panel_io),
        TAG, "Panel IO init failed");

    sh8601_vendor_config_t vendor_config = {};
    vendor_config.init_cmds = s_lcd_init_cmds;
    vendor_config.init_cmds_size =
        sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]);
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_sh8601(display->panel_io, &panel_config,
                                 &display->panel),
        TAG, "Panel init failed");
    const esp_err_t reset_error = board_display_reset();
    if (reset_error != ESP_OK) {
        ESP_LOGW(TAG, "Panel power reset failed: %s",
                 esp_err_to_name(reset_error));
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display->panel), TAG,
                        "Panel controller init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), TAG,
                        "Panel display on failed");

    if (!board_i2c_ready()) {
        display->touch = NULL;
        ESP_LOGW(TAG, "Skipping touch because the I2C bus is held low");
        return ESP_OK;
    }

    esp_lcd_touch_config_t touch_config = {};
    touch_config.x_max = BOARD_LCD_H_RES - 1;
    touch_config.y_max = BOARD_LCD_V_RES - 1;
    touch_config.rst_gpio_num =
        (gpio_num_t)BOARD_TOUCH_RST_GPIO;
    touch_config.int_gpio_num =
        (gpio_num_t)BOARD_TOUCH_INT_GPIO;
    touch_config.levels.reset = 0;
    touch_config.levels.interrupt = 0;
    touch_config.flags.swap_xy = 1;
    touch_config.flags.mirror_x = 0;
    touch_config.flags.mirror_y = 1;
    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t touch_io_config =
        ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    touch_io_config.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(board_i2c_bus(), &touch_io_config, &touch_io),
        TAG, "Touch IO init failed");
    const esp_err_t touch_error =
        esp_lcd_touch_new_i2c_cst9217(touch_io, &touch_config, &display->touch);
    if (touch_error != ESP_OK) {
        display->touch = NULL;
        ESP_LOGW(TAG, "Touch init failed: %s", esp_err_to_name(touch_error));
    }

    ESP_LOGI(TAG, "Display and touch initialized");
    return ESP_OK;
}

void board_set_backlight(uint8_t percent)
{
    const board_display_t *display = board_display();
    if (display == NULL || display->panel_io == NULL) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint8_t value = (uint8_t)((percent * 255U) / 100U);
    uint32_t command = ((uint32_t)0x51 << 8) | ((uint32_t)0x02 << 24);
    esp_lcd_panel_io_tx_param(display->panel_io, command, &value, 1);
}
