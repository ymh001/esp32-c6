#include "board_internal.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu";

#define QMI8658_ADDRESS 0x6B
#define QMI8658_REG_WHO_AM_I 0x00
#define QMI8658_REG_CTRL1 0x02
#define QMI8658_REG_CTRL2 0x03
#define QMI8658_REG_CTRL7 0x08
#define QMI8658_REG_AX_L 0x35
#define QMI8658_CTRL1_AUTO_INCREMENT 0x60
#define QMI8658_ACCEL_2G_125HZ 0x06
#define QMI8658_ENABLE_ACCEL 0x01
#define QMI8658_ACCEL_LSB_PER_G 16384.0f

#define ORIENTATION_FILTER_ALPHA 0.25f
#define ORIENTATION_STABLE_SAMPLES 5

static i2c_master_dev_handle_t s_imu;
static float s_filtered_x;
static float s_filtered_y;
static float s_filtered_z;
static bool s_filter_initialized;
static board_rotation_t s_candidate_rotation = BOARD_ROTATION_0;
static int s_stable_samples;
static board_rotation_t s_applied_rotation = BOARD_ROTATION_0;

static esp_err_t read_acceleration(float *x, float *y, float *z)
{
    uint8_t data[6] = {0};
    ESP_RETURN_ON_ERROR(
        board_i2c_read_reg(s_imu, QMI8658_REG_AX_L, data, sizeof(data)), TAG,
        "Read accelerometer");

    const int16_t raw_x = (int16_t)((data[1] << 8) | data[0]);
    const int16_t raw_y = (int16_t)((data[3] << 8) | data[2]);
    const int16_t raw_z = (int16_t)((data[5] << 8) | data[4]);
    *x = (float)raw_x / QMI8658_ACCEL_LSB_PER_G;
    *y = (float)raw_y / QMI8658_ACCEL_LSB_PER_G;
    *z = (float)raw_z / QMI8658_ACCEL_LSB_PER_G;
    return ESP_OK;
}

esp_err_t board_imu_init(void)
{
    if (!board_i2c_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        board_i2c_add_device(QMI8658_ADDRESS, 400000, &s_imu), TAG,
        "Add QMI8658 device");

    uint8_t who_am_i = 0;
    ESP_RETURN_ON_ERROR(
        board_i2c_read_reg(s_imu, QMI8658_REG_WHO_AM_I, &who_am_i, 1), TAG,
        "Read WHO_AM_I");
    if (who_am_i != 0x05) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I 0x%02X", who_am_i);
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t ctrl1 = QMI8658_CTRL1_AUTO_INCREMENT;
    const uint8_t ctrl2 = QMI8658_ACCEL_2G_125HZ;
    const uint8_t ctrl7 = QMI8658_ENABLE_ACCEL;
    ESP_RETURN_ON_ERROR(
        board_i2c_write_reg(s_imu, QMI8658_REG_CTRL1, &ctrl1, 1), TAG,
        "Configure CTRL1");
    ESP_RETURN_ON_ERROR(
        board_i2c_write_reg(s_imu, QMI8658_REG_CTRL2, &ctrl2, 1), TAG,
        "Configure CTRL2");
    ESP_RETURN_ON_ERROR(
        board_i2c_write_reg(s_imu, QMI8658_REG_CTRL7, &ctrl7, 1), TAG,
        "Enable accelerometer");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "QMI8658 ready; 0 degrees is buttons up / USB-C down");
    return ESP_OK;
}

esp_err_t board_auto_rotation_update(board_rotation_t *rotation, bool *changed)
{
    if (rotation == NULL || changed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *rotation = s_applied_rotation;
    *changed = false;
    if (s_imu == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    ESP_RETURN_ON_ERROR(read_acceleration(&x, &y, &z), TAG,
                        "Read acceleration");
    if (!s_filter_initialized) {
        s_filtered_x = x;
        s_filtered_y = y;
        s_filtered_z = z;
        s_filter_initialized = true;
    } else {
        s_filtered_x += ORIENTATION_FILTER_ALPHA * (x - s_filtered_x);
        s_filtered_y += ORIENTATION_FILTER_ALPHA * (y - s_filtered_y);
        s_filtered_z += ORIENTATION_FILTER_ALPHA * (z - s_filtered_z);
    }

    board_rotation_t candidate;
    if (!board_rotation_from_acceleration(s_filtered_x, s_filtered_y,
                                          s_filtered_z, &candidate)) {
        s_stable_samples = 0;
        return ESP_OK;
    }

    if (candidate != s_candidate_rotation) {
        s_candidate_rotation = candidate;
        s_stable_samples = 1;
        return ESP_OK;
    }
    if (s_stable_samples < ORIENTATION_STABLE_SAMPLES) {
        ++s_stable_samples;
    }
    if (s_stable_samples < ORIENTATION_STABLE_SAMPLES ||
        candidate == s_applied_rotation) {
        return ESP_OK;
    }

    s_applied_rotation = candidate;
    *rotation = candidate;
    *changed = true;
    ESP_LOGI(TAG, "Physical orientation %s degrees (x=%.2f y=%.2f z=%.2f)",
             board_rotation_name(candidate), s_filtered_x, s_filtered_y,
             s_filtered_z);
    return ESP_OK;
}
