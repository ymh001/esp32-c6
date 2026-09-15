#include "board.h"

#include "board_internal.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c_bus;
static board_display_t s_display;
static bool s_i2c_ready;

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c_bus;
}

esp_err_t board_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    const gpio_config_t probe_config = {
        .pin_bit_mask = (1ULL << BOARD_I2C_SCL_GPIO) |
                        (1ULL << BOARD_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&probe_config);
    vTaskDelay(pdMS_TO_TICKS(5));
    const int scl_level = gpio_get_level((gpio_num_t)BOARD_I2C_SCL_GPIO);
    const int sda_level = gpio_get_level((gpio_num_t)BOARD_I2C_SDA_GPIO);
    s_i2c_ready = scl_level != 0 && sda_level != 0;
    ESP_LOGI(TAG, "I2C idle levels: SCL=%d SDA=%d (%s)", scl_level, sda_level,
             s_i2c_ready ? "ready" : "held low");

    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = (gpio_num_t)BOARD_I2C_SDA_GPIO;
    config.scl_io_num = (gpio_num_t)BOARD_I2C_SCL_GPIO;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&config, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(err));
    }
    return err;
}

bool board_i2c_ready(void)
{
    return s_i2c_ready;
}

esp_err_t board_i2c_add_device(uint8_t address, uint32_t speed_hz,
                               i2c_master_dev_handle_t *out)
{
    if (s_i2c_bus == NULL || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = speed_hz,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &config, out);
}

esp_err_t board_i2c_write_reg(i2c_master_dev_handle_t device, uint8_t reg,
                              const uint8_t *data, size_t len)
{
    uint8_t buffer[16];
    if (len + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[0] = reg;
    for (size_t i = 0; i < len; ++i) {
        buffer[i + 1] = data[i];
    }
    return i2c_master_transmit(device, buffer, len + 1, 1000);
}

esp_err_t board_i2c_read_reg(i2c_master_dev_handle_t device, uint8_t reg,
                             uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, len, 1000);
}

bool board_key_pressed(void)
{
    return gpio_get_level((gpio_num_t)BOARD_KEY_GPIO) == 0;
}

esp_err_t board_buttons_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_KEY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

esp_err_t board_init(void)
{
    esp_err_t err = board_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    err = board_power_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PMIC init failed, continuing in degraded mode: %s",
                 esp_err_to_name(err));
    }

    err = board_display_init(&s_display);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display init failed: %s", esp_err_to_name(err));
    }

    err = board_rtc_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC init failed, continuing without RTC: %s",
                 esp_err_to_name(err));
    }

    err = board_buttons_init();
    if (err != ESP_OK) {
        return err;
    }

    err = board_imu_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IMU init failed, auto rotation disabled: %s",
                 esp_err_to_name(err));
    }

    if (s_display.panel_io != NULL) {
        board_set_backlight(80);
    }
    ESP_LOGI(TAG, "Board initialized");
    return ESP_OK;
}

const board_display_t *board_display(void)
{
    return &s_display;
}
