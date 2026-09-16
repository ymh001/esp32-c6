#include "main_navigation.h"
#include "clock_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "energy_service.h"
#include "energy_view.h"
#include "xiaozhi_view.h"
#include "voice_service.h"
#include "ha_devices.h"
#include "devices_view.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "lunar.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "time_service.h"
#include "wifi_manager.h"

static const char *TAG = "clock_ui";

// sdkconfig.defaults does not override an existing sdkconfig. Refuse a build
// with the old pool size instead of shipping a known UI allocation failure.
static_assert(CONFIG_LV_MEM_SIZE_KILOBYTES >= 96,
              "Set CONFIG_LV_MEM_SIZE_KILOBYTES=96 in sdkconfig");

LV_FONT_DECLARE(clock_cjk_16);
LV_FONT_DECLARE(clock_cjk_24);
LV_FONT_DECLARE(clock_cjk_32);
LV_FONT_DECLARE(clock_cjk_96);

typedef enum {
    MAIN_PAGE_CLOCK = 0,
    MAIN_PAGE_XIAOZHI,
    MAIN_PAGE_DEVICES,
    MAIN_PAGE_ENERGY,
    MAIN_PAGE_COUNT,
} main_page_t;

typedef struct {
    lv_obj_t *clock_screen;
    lv_obj_t *xiaozhi_screen;
    lv_obj_t *energy_screen;
    devices_view_t devices;
    lv_obj_t *control_screen;
    lv_obj_t *time_label;
    lv_obj_t *seconds_label;
    lv_obj_t *date_label;
    lv_obj_t *lunar_label;
    lv_obj_t *status_label;
    lv_obj_t *week_cells[7];
    lv_obj_t *week_name_labels[7];
    lv_obj_t *week_day_labels[7];
    lv_obj_t *week_lunar_labels[7];
    energy_view_t energy;
    lv_obj_t *control_panel;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value_label;
    lv_obj_t *battery_label;
    main_page_t current_page;
    main_page_t previous_page;
    int applied_brightness;
    int week_anchor_year;
    int week_anchor_month;
    int week_anchor_day;
} ui_state_t;

static ui_state_t s_ui;
static lv_display_t *s_lv_display;
static clock_settings_t s_ui_settings;
static const lv_font_t *s_cjk_font = &clock_cjk_16;
static const lv_font_t *s_cjk_title_font = &clock_cjk_24;
static const lv_font_t *s_cjk_date_font = &clock_cjk_32;
static const lv_font_t *s_cjk_clock_font = &clock_cjk_96;
static uint32_t s_last_view_switch_tick;

// LVGL renders in strips because this ESP32-C6 has no PSRAM. A 90/270-degree
// pose turns a 32-row strip into a 32-column QSPI transfer. Keep one DMA-safe
// scratch strip; LVGL does not submit the next partial flush until the panel IO
// completion callback marks the current one ready.
#define ROTATION_STRIP_ROWS 32
#define ROTATION_GUARD_ROWS 4
#define ROTATION_MAX_PIXELS \
    (BOARD_LCD_H_RES * (ROTATION_STRIP_ROWS + ROTATION_GUARD_ROWS))

static uint16_t *s_rotation_buffer;
static size_t s_rotation_buffer_pixels;
static uint32_t s_flush_count;
static SemaphoreHandle_t s_flush_done;

static bool panel_transfer_done(esp_lcd_panel_io_handle_t,
                                esp_lcd_panel_io_event_data_t *, void *context)
{
    BaseType_t wake = pdFALSE;
    lv_display_flush_ready((lv_display_t *)context);
    xSemaphoreGiveFromISR(s_flush_done, &wake);
    return wake == pdTRUE;
}

static void wait_for_panel_transfer(lv_display_t *)
{
    // LVGL's default wait is a busy loop. Block so the idle task, network and
    // application can run while DMA transfers a strip on this single core.
    if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Panel DMA completion timed out");
        abort(); // Never reuse a buffer whose DMA completion was not observed.
    }
}

static void rotation_flush(lv_display_t *display, const lv_area_t *area,
                           uint8_t *pixel_map)
{
    ++s_flush_count;
    const board_display_t *board = board_display();
    if (board == NULL || board->panel == NULL) {
        lv_display_flush_ready(display);
        return;
    }

    const int32_t source_width = lv_area_get_width(area);
    const int32_t source_height = lv_area_get_height(area);
    if (source_width <= 0 || source_height <= 0) {
        lv_display_flush_ready(display);
        return;
    }

    lv_area_t output_area = *area;
    uint8_t *output = pixel_map;
    const lv_display_rotation_t rotation = lv_display_get_rotation(display);
    if (rotation != LV_DISPLAY_ROTATION_0) {
        const size_t pixels = (size_t)source_width * (size_t)source_height;
        if (s_rotation_buffer == NULL || pixels > s_rotation_buffer_pixels) {
            ESP_LOGE(TAG,
                     "Rotation strip overflow: %dx%d=%u pixels, capacity=%u",
                     (int)source_width, (int)source_height, (unsigned)pixels,
                     (unsigned)s_rotation_buffer_pixels);
            lv_display_flush_ready(display);
            return;
        }

        lv_display_rotate_area(display, &output_area);
        const uint32_t source_stride =
            lv_draw_buf_width_to_stride(source_width, LV_COLOR_FORMAT_RGB565);
        const uint32_t output_stride = lv_draw_buf_width_to_stride(
            lv_area_get_width(&output_area), LV_COLOR_FORMAT_RGB565);
        lv_draw_sw_rotate(pixel_map, s_rotation_buffer, source_width,
                          source_height, source_stride, output_stride, rotation,
                          LV_COLOR_FORMAT_RGB565);
        output = (uint8_t *)s_rotation_buffer;
    }

    // The panel expects byte-swapped RGB565. The buffer remains owned until the
    // panel-IO completion callback calls flush_ready.
    lv_draw_sw_rgb565_swap(output, lv_area_get_size(&output_area));
    xSemaphoreTake(s_flush_done, 0); // Discard the preceding transfer token.
    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        board->panel, output_area.x1, output_area.y1, output_area.x2 + 1,
        output_area.y2 + 1, output);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Panel flush failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(display);
    }
}

static lv_display_rotation_t lv_rotation_for_pose(board_rotation_t rotation)
{
    switch (rotation) {
    case BOARD_ROTATION_90:
        return LV_DISPLAY_ROTATION_90;
    case BOARD_ROTATION_180:
        return LV_DISPLAY_ROTATION_180;
    case BOARD_ROTATION_270:
        return LV_DISPLAY_ROTATION_270;
    default:
        return LV_DISPLAY_ROTATION_0;
    }
}

static void rounder_event_cb(lv_event_t *event)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(event);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static void apply_font(lv_obj_t *label, const lv_font_t *font)
{
    lv_obj_set_style_text_font(label, font, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    apply_font(label, font);
    return label;
}

static void show_main_page(main_page_t page)
{
    s_ui.current_page = page;
    ha_devices_set_visible(page==MAIN_PAGE_DEVICES);
    if (page == MAIN_PAGE_CLOCK) {
        lv_screen_load(s_ui.clock_screen);
    } else if (page == MAIN_PAGE_XIAOZHI) {
        lv_screen_load(s_ui.xiaozhi_screen);
    } else if (page == MAIN_PAGE_DEVICES) {
        lv_screen_load(s_ui.devices.screen);
    } else {
        lv_screen_load(s_ui.energy_screen);
    }
}

static void switch_main_page(int delta)
{
    int page = (int)s_ui.current_page + delta;
    while (page < 0) {
        page += MAIN_PAGE_COUNT;
    }
    while (page >= MAIN_PAGE_COUNT) {
        page -= MAIN_PAGE_COUNT;
    }
    show_main_page((main_page_t)page);
}

static void show_control_screen(void)
{
    ha_devices_set_visible(false);
    if (lv_screen_active() != s_ui.control_screen) {
        s_ui.previous_page = s_ui.current_page;
    }
    lv_anim_delete(s_ui.control_panel, NULL);
    lv_obj_align(s_ui.control_panel, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_opa(s_ui.control_panel, LV_OPA_COVER, 0);
    lv_screen_load(s_ui.control_screen);
}

static void close_control_screen(void)
{
    show_main_page(s_ui.previous_page);
}

static void screen_gesture_cb(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }
    if(lv_screen_active()==s_ui.devices.screen)return;
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    if (lv_screen_active() == s_ui.control_screen) {
        if (direction == LV_DIR_TOP) {
            close_control_screen();
        }
        return;
    }
    if (direction == LV_DIR_BOTTOM) {
        show_control_screen();
        return;
    }
    if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT) {
        return;
    }
    if (s_last_view_switch_tick != 0 &&
        lv_tick_elaps(s_last_view_switch_tick) < 300) {
        return;
    }
    s_last_view_switch_tick = lv_tick_get();

    switch_main_page(direction == LV_DIR_LEFT ? 1 : -1);
}

static void update_week_strip(const struct tm *local)
{
    if (s_ui.week_cells[0] == NULL ||
        (s_ui.week_anchor_year == local->tm_year + 1900 &&
         s_ui.week_anchor_month == local->tm_mon + 1 &&
         s_ui.week_anchor_day == local->tm_mday)) {
        return;
    }

    s_ui.week_anchor_year = local->tm_year + 1900;
    s_ui.week_anchor_month = local->tm_mon + 1;
    s_ui.week_anchor_day = local->tm_mday;

    struct tm anchor = *local;
    anchor.tm_hour = 12;
    anchor.tm_min = 0;
    anchor.tm_sec = 0;
    anchor.tm_isdst = -1;
    const time_t anchor_time = mktime(&anchor);

    static const char *short_weekdays[] = {
        "日", "一", "二", "三", "四", "五", "六",
    };

    for (int i = 0; i < 7; ++i) {
        const time_t day_time =
            anchor_time + (time_t)(i - local->tm_wday) * 86400;
        struct tm day = {0};
        localtime_r(&day_time, &day);

        const bool today = day.tm_year == local->tm_year &&
                           day.tm_mon == local->tm_mon &&
                           day.tm_mday == local->tm_mday;
        lv_obj_set_style_border_width(
            s_ui.week_cells[i], today ? 2 : 1, 0);
        lv_obj_set_style_border_color(
            s_ui.week_cells[i],
            today ? lv_color_hex(0xF3A712) : lv_color_hex(0x252C37), 0);
        lv_obj_set_style_bg_color(
            s_ui.week_cells[i],
            today ? lv_color_hex(0x2A2110) : lv_color_hex(0x10141B), 0);

        lv_label_set_text(s_ui.week_name_labels[i],
                          short_weekdays[day.tm_wday]);

        char day_text[8];
        snprintf(day_text, sizeof(day_text), "%d", day.tm_mday);
        lv_label_set_text(s_ui.week_day_labels[i], day_text);

        char lunar_text[16] = {0};
        lunar_calendar_cell_text(day.tm_year + 1900, day.tm_mon + 1,
                                 day.tm_mday, lunar_text, sizeof(lunar_text));
        lv_label_set_text(s_ui.week_lunar_labels[i], lunar_text);
    }
}

static void update_clock(void)
{
    struct tm local = {0};
    time_service_get_local(&local);

    int hour = local.tm_hour;
    const char *suffix = "";
    if (!s_ui_settings.use_24_hour) {
        suffix = hour >= 12 ? " PM" : " AM";
        hour %= 12;
        if (hour == 0) {
            hour = 12;
        }
    }

    char time_text[16];
    snprintf(time_text, sizeof(time_text), "%02d:%02d", hour, local.tm_min);
    lv_label_set_text(s_ui.time_label, time_text);

    char seconds_text[8];
    snprintf(seconds_text, sizeof(seconds_text), ":%02d%s", local.tm_sec,
             suffix);
    lv_label_set_text(s_ui.seconds_label, seconds_text);

    char date_text[64];
    snprintf(date_text, sizeof(date_text), "%d年%d月%d日 %s",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             weekday_name(local.tm_wday));
    lv_label_set_text(s_ui.date_label, date_text);

    char lunar_text[64];
    lunar_format_full(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      lunar_text, sizeof(lunar_text));
    const char *special = festival_for_date(local.tm_year + 1900,
                                            local.tm_mon + 1, local.tm_mday);
    if (special == NULL) {
        special = solar_term_for_date(local.tm_year + 1900,
                                      local.tm_mon + 1, local.tm_mday);
    }
    if (special != NULL) {
        char combined[96];
        snprintf(combined, sizeof(combined), "%s · %s", lunar_text, special);
        lv_label_set_text(s_ui.lunar_label, combined);
    } else {
        lv_label_set_text(s_ui.lunar_label, lunar_text);
    }

    char status[96];
    if (wifi_manager_is_connected()) {
        char ip[16];
        wifi_manager_get_ip(ip, sizeof(ip));
        snprintf(status, sizeof(status), "%s  %s",
                 time_service_is_synced() ? "NTP 已同步" : "等待校时", ip);
    } else if (wifi_manager_state() == WIFI_MANAGER_CONNECTING) {
        snprintf(status, sizeof(status), "WiFi 连接中");
    } else {
        snprintf(status, sizeof(status), "WiFi 未连接");
    }
    lv_label_set_text(s_ui.status_label, status);
    update_week_strip(&local);

    int brightness = s_ui_settings.brightness;
    const bool night =
        s_ui_settings.night_start_hour <= s_ui_settings.night_end_hour
            ? (local.tm_hour >= s_ui_settings.night_start_hour &&
               local.tm_hour < s_ui_settings.night_end_hour)
            : (local.tm_hour >= s_ui_settings.night_start_hour ||
               local.tm_hour < s_ui_settings.night_end_hour);
    if (night) {
        brightness = s_ui_settings.night_brightness;
    }
    if (brightness != s_ui.applied_brightness) {
        if (board_set_backlight((uint8_t)brightness) == ESP_OK) {
            s_ui.applied_brightness = brightness;
        }
    }
}

static void update_energy_view(void)
{
    energy_snapshot_t snapshot = {};
    energy_service_get_snapshot(&snapshot);
    const energy_view_data_t data = {
        .loaded = snapshot.loaded,
        .refreshing = snapshot.refreshing,
        .failed = snapshot.refresh_failed,
        .stale = snapshot.stale,
        .today = snapshot.today_kwh,
        .today_cost = snapshot.today_cost,
        .remaining_cost = snapshot.remaining_cost,
        .price_per_kwh = snapshot.price_per_kwh,
        .updated_at = snapshot.updated_at,
    };
    energy_view_update(&s_ui.energy, &data);
}

void clock_ui_poll_battery(void)
{
    static TickType_t last_poll;
    const TickType_t now = xTaskGetTickCount();
    if (now - last_poll < pdMS_TO_TICKS(5000)) return;
    last_poll = now;
    static uint8_t last_percent = UINT8_MAX;
    uint8_t percent = 0;
    uint16_t voltage_mv = 0;
    const esp_err_t err = board_power_get_battery(&percent, &voltage_mv);
    char text[16] = "--";
    if (err == ESP_OK) snprintf(text, sizeof(text), "%u%%", percent);
    if (esp_lv_adapter_lock(100) == ESP_OK) {
        lv_label_set_text(s_ui.battery_label, text);
        esp_lv_adapter_unlock();
    }
    if (err != ESP_OK) return;
    if (percent != last_percent) {
        last_percent = percent;
        ESP_LOGI(TAG, "Battery: %u%%, %u mV", percent, voltage_mv);
    }
}

static void clock_timer(lv_timer_t *timer)
{
    (void)timer;
    update_clock();
    update_energy_view();

    static int memory_countdown;
    if (memory_countdown-- <= 0) {
        memory_countdown = 30;
        lv_mem_monitor_t monitor = {};
        lv_mem_monitor(&monitor);
        ESP_LOGI(TAG, "LVGL memory: %u%% used, %u free, %u largest, %u%% fragmented",
                 monitor.used_pct, (unsigned)monitor.free_size,
                 (unsigned)monitor.free_biggest_size, monitor.frag_pct);
    }
}

static void xiaozhi_nav(lv_event_t *event);

static void build_clock_screen(void)
{
    s_ui.clock_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.clock_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.clock_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.clock_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.clock_screen, screen_gesture_cb, LV_EVENT_GESTURE,
                        NULL);

    s_ui.time_label =
        make_label(s_ui.clock_screen, "00:00", s_cjk_clock_font,
                   lv_color_white());
    lv_obj_align(s_ui.time_label, LV_ALIGN_TOP_MID, -34, 50);

    s_ui.seconds_label =
        make_label(s_ui.clock_screen, ":00", s_cjk_date_font,
                   lv_color_hex(0xF3A712));
    lv_obj_align_to(s_ui.seconds_label, s_ui.time_label, LV_ALIGN_OUT_RIGHT_BOTTOM,
                    6, -12);

    s_ui.date_label =
        make_label(s_ui.clock_screen, "2026年1月1日 星期四",
                   s_cjk_date_font, lv_color_hex(0xD7DDE7));
    lv_obj_align(s_ui.date_label, LV_ALIGN_TOP_MID, 0, 184);

    s_ui.lunar_label =
        make_label(s_ui.clock_screen, "农历正月初一",
                   s_cjk_title_font, lv_color_hex(0xAAB3C2));
    lv_obj_align(s_ui.lunar_label, LV_ALIGN_TOP_MID, 0, 230);

    lv_obj_t *rule = lv_obj_create(s_ui.clock_screen);
    lv_obj_set_size(rule, 300, 1);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 277);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x303846), 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.status_label =
        make_label(s_ui.clock_screen, "启动中", s_cjk_font,
                   lv_color_hex(0x778293));
    lv_obj_align(s_ui.status_label, LV_ALIGN_TOP_MID, 0, 13);

    for (int i = 0; i < 7; ++i) {
        lv_obj_t *cell = lv_obj_create(s_ui.clock_screen);
        lv_obj_set_size(cell, 64, 128);
        lv_obj_set_pos(cell, 16 + i * 64, 286);
        lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(cell, 5, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x252C37), 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x10141B), 0);
        s_ui.week_cells[i] = cell;

        s_ui.week_name_labels[i] =
            make_label(cell, "", s_cjk_font, lv_color_hex(0x8E98A8));
        lv_obj_align(s_ui.week_name_labels[i], LV_ALIGN_TOP_MID, 0, 9);

        s_ui.week_day_labels[i] =
            make_label(cell, "", s_cjk_date_font, lv_color_white());
        lv_obj_align(s_ui.week_day_labels[i], LV_ALIGN_CENTER, 0, 0);

        s_ui.week_lunar_labels[i] =
            make_label(cell, "", s_cjk_font, lv_color_hex(0xAAB3C2));
        lv_obj_align(s_ui.week_lunar_labels[i], LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    main_navigation_create(s_ui.clock_screen,MAIN_PAGE_CLOCK,xiaozhi_nav);
}

static void refresh_button_clicked(lv_event_t *event)
{
    (void)event;
    energy_service_request_refresh();
    update_energy_view();
}

static void brightness_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    const int value = lv_slider_get_value(slider);
    char text[16];
    s_ui_settings.brightness = (uint8_t)value;
    s_ui_settings.night_brightness = (uint8_t)value;
    if (board_set_backlight((uint8_t)value) == ESP_OK) {
        s_ui.applied_brightness = value;
    }
    snprintf(text, sizeof(text), "%d%%", value);
    lv_label_set_text(s_ui.brightness_value_label, text);
}

static void brightness_released(lv_event_t *event)
{
    (void)event;
    ESP_LOGI(TAG, "Brightness: %d%%",
             lv_slider_get_value(s_ui.brightness_slider));
    clock_settings_request_save(&s_ui_settings);
}

static void build_energy_screen(void)
{
    s_ui.energy = energy_view_create(refresh_button_clicked,xiaozhi_nav);
    s_ui.energy_screen = s_ui.energy.screen;
    lv_obj_add_event_cb(s_ui.energy_screen, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
}

static void build_control_screen(void)
{
    s_ui.control_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.control_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.control_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.control_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.control_screen, screen_gesture_cb,
                        LV_EVENT_GESTURE, NULL);

    s_ui.control_panel = lv_obj_create(s_ui.control_screen);
    lv_obj_set_size(s_ui.control_panel, 440, 300);
    lv_obj_align(s_ui.control_panel, LV_ALIGN_CENTER, 0, 20);
    lv_obj_clear_flag(s_ui.control_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_ui.control_panel, 10, 0);
    lv_obj_set_style_bg_opa(s_ui.control_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.control_panel, 0, 0);
    lv_obj_set_style_pad_all(s_ui.control_panel, 0, 0);

    lv_obj_t *title =
        make_label(s_ui.control_panel, "控制", s_cjk_title_font,
                   lv_color_white());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *brightness_label =
        make_label(s_ui.control_panel, "屏幕亮度", s_cjk_title_font,
                   lv_color_hex(0xD7DDE7));
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 86);

    s_ui.brightness_slider = lv_slider_create(s_ui.control_panel);
    // Rounded bar clipping creates an ARGB8888 layer near the minimum value.
    // Use a flat, opaque track: dragging must never need an offscreen layer.
    lv_obj_remove_style_all(s_ui.brightness_slider);
    lv_obj_set_style_bg_opa(s_ui.brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_size(s_ui.brightness_slider, 360, 40);
    lv_obj_align(s_ui.brightness_slider, LV_ALIGN_TOP_MID, 0, 138);
    lv_obj_set_ext_click_area(s_ui.brightness_slider, 12);
    lv_slider_set_range(s_ui.brightness_slider, 10, 100);
    lv_slider_set_value(s_ui.brightness_slider, s_ui_settings.brightness,
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.brightness_slider,
                              lv_color_hex(0x3A4352), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.brightness_slider, 0,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.brightness_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brightness_slider,
                              lv_color_hex(0xF3A712), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.brightness_slider, 0,
                            LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.brightness_slider, LV_OPA_TRANSP,
                            LV_PART_KNOB);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_clear_flag(s_ui.brightness_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_released,
                        LV_EVENT_RELEASED, NULL);

    s_ui.brightness_value_label =
        make_label(s_ui.control_panel, "80%", s_cjk_title_font,
                   lv_color_white());
    lv_obj_align(s_ui.brightness_value_label, LV_ALIGN_TOP_MID, 0, 200);
    char text[16];
    snprintf(text, sizeof(text), "%u%%", s_ui_settings.brightness);
    lv_label_set_text(s_ui.brightness_value_label, text);

    lv_obj_t *battery_title =
        make_label(s_ui.control_panel, "设备电量", s_cjk_font,
                   lv_color_hex(0x8E98A8));
    lv_obj_align(battery_title, LV_ALIGN_BOTTOM_LEFT, 34, -26);

    s_ui.battery_label =
        make_label(s_ui.control_panel, "--", s_cjk_title_font,
                   lv_color_white());
    lv_obj_align(s_ui.battery_label, LV_ALIGN_BOTTOM_RIGHT, -34, -22);
}

static void xiaozhi_nav(lv_event_t *event)
{
    show_main_page((main_page_t)(intptr_t)lv_event_get_user_data(event));
}

static void speak_clicked(lv_event_t *) { voice_service_click(); }
static void device_clicked(lv_event_t *event){ha_devices_toggle((unsigned)(intptr_t)lv_event_get_user_data(event));}
static void devices_refresh_clicked(lv_event_t *){ha_devices_refresh();}

static void voice_timer(lv_timer_t *)
{
    static uint32_t last_revision = UINT32_MAX;
    voice_snapshot_t state;
    voice_service_snapshot(&state);
    if (last_revision != state.revision) {
        last_revision = state.revision;
        xiaozhi_view_update(state.state, state.text);
    }
    static uint32_t devices_revision=UINT32_MAX;
    ha_devices_snapshot_t devices;ha_devices_snapshot(&devices);
    if(devices_revision!=devices.revision){
        devices_revision=devices.revision;
        devices_view_data_t data={};data.message=devices.message;data.refreshing=devices.refreshing;
        for(size_t i=0;i<HA_DEVICE_COUNT;++i)data.cards[i]={devices.devices[i].power,devices.devices[i].busy,devices.devices[i].error};
        devices_view_update(&s_ui.devices,&data);
    }
}

static void build_xiaozhi_screen(void)
{
    s_ui.xiaozhi_screen = xiaozhi_view_create(xiaozhi_nav, speak_clicked);
    lv_obj_add_event_cb(s_ui.xiaozhi_screen, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
}
static void build_devices_screen(void)
{
    s_ui.devices=devices_view_create(xiaozhi_nav,device_clicked,devices_refresh_clicked);
    lv_obj_add_event_cb(s_ui.devices.screen,screen_gesture_cb,LV_EVENT_GESTURE,nullptr);
}

static lv_display_t *register_display(const board_display_t *display)
{
    s_flush_done = xSemaphoreCreateBinary();
    if (s_flush_done == NULL) return NULL;
    s_rotation_buffer_pixels = ROTATION_MAX_PIXELS;
    s_rotation_buffer = (uint16_t *)heap_caps_malloc(
        s_rotation_buffer_pixels * sizeof(uint16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (s_rotation_buffer == NULL) {
        ESP_LOGE(TAG, "Unable to allocate %u-byte rotation strip",
                 (unsigned)(s_rotation_buffer_pixels * sizeof(uint16_t)));
        return NULL;
    }

    esp_lv_adapter_display_config_t display_config =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
            display->panel, display->panel_io, BOARD_LCD_H_RES,
            BOARD_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = ROTATION_STRIP_ROWS;
    lv_display_t *lv_display = esp_lv_adapter_register_display(&display_config);
    if (lv_display == NULL) {
        heap_caps_free(s_rotation_buffer);
        s_rotation_buffer = NULL;
        s_rotation_buffer_pixels = 0;
        return NULL;
    }

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = panel_transfer_done,
    };
    if (esp_lcd_panel_io_register_event_callbacks(display->panel_io, &callbacks,
                                                  lv_display) != ESP_OK) {
        return NULL;
    }
    lv_display_set_flush_cb(lv_display, rotation_flush);
    lv_display_set_flush_wait_cb(lv_display, wait_for_panel_transfer);
    ESP_LOGI(TAG, "Display strip: %u rows; rotation scratch: %u bytes",
             ROTATION_STRIP_ROWS,
             (unsigned)(s_rotation_buffer_pixels * sizeof(uint16_t)));
    return lv_display;
}

#if CONFIG_DESK_CLOCK_UI_SELF_TEST
static void ui_self_test(lv_timer_t *timer)
{
    static unsigned step;
    static clock_settings_t original;
    static uint32_t initial_flush_count;
    constexpr unsigned steps_per_pose = 182 + 24 + MAIN_PAGE_COUNT;
    if (step == 0) {
        original = s_ui_settings;
        initial_flush_count = s_flush_count;
        ESP_LOGI(TAG, "UI SELF TEST START: 728 brightness changes, 96 page changes, four rotations");
    }
    if (step == 4 * steps_per_pose) {
        s_ui_settings = original;
        clock_settings_request_save(&original);
        lv_slider_set_value(s_ui.brightness_slider, original.brightness, LV_ANIM_OFF);
        char text[16];
        snprintf(text, sizeof(text), "%u%%", original.brightness);
        lv_label_set_text(s_ui.brightness_value_label, text);
        lv_display_set_rotation(s_lv_display, LV_DISPLAY_ROTATION_0);
        show_main_page(MAIN_PAGE_CLOCK);
        s_ui.applied_brightness = -1;
        update_clock();
        assert(s_flush_count > initial_flush_count + 100);
        assert(lv_mem_test() == LV_RESULT_OK);
        assert(heap_caps_check_integrity_all(true));
        ESP_LOGI(TAG, "UI SELF TEST PASS: %lu flushes; heap free=%u, largest=%u",
                 (unsigned long)(s_flush_count - initial_flush_count),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        lv_timer_delete(timer);
        return;
    }
    const unsigned pose = step / steps_per_pose;
    const unsigned action = step % steps_per_pose;
    if (action == 0) {
        lv_display_set_rotation(s_lv_display, (lv_display_rotation_t)pose);
        show_control_screen();
        lv_obj_add_state(s_ui.brightness_slider, LV_STATE_PRESSED);
        ESP_LOGI(TAG, "UI SELF TEST: pose %u, slider sweep", pose * 90);
    }
    if (action < 182) {
        const int value = action < 91 ? 100 - action : 10 + (action - 91);
        lv_slider_set_value(s_ui.brightness_slider, value, LV_ANIM_OFF);
        lv_obj_send_event(s_ui.brightness_slider, LV_EVENT_VALUE_CHANGED, NULL);
        if (action == 181) lv_obj_remove_state(s_ui.brightness_slider, LV_STATE_PRESSED);
    } else if (action < 206) {
        show_main_page(MAIN_PAGE_XIAOZHI);
        show_main_page((main_page_t)(action % MAIN_PAGE_COUNT));
    } else {
        show_main_page((main_page_t)(action - 206));
    }
    lv_refr_now(s_lv_display);
    assert(lv_mem_test() == LV_RESULT_OK);
    assert(heap_caps_check_integrity_all(true));
    ++step;
}
#endif

esp_err_t clock_ui_start(const clock_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_ui_settings = *settings;
    s_ui.applied_brightness = -1;

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 16 * 1024;
    adapter_config.task_priority = 10;
    adapter_config.task_core_id = 0;
    adapter_config.stack_in_psram = false;
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG,
                        "LVGL adapter init");

    const board_display_t *display = board_display();
    s_lv_display = register_display(display);
    if (s_lv_display == NULL) {
        return ESP_FAIL;
    }
    lv_display_add_event_cb(s_lv_display, rounder_event_cb,
                            LV_EVENT_INVALIDATE_AREA, NULL);

    if (display->touch != NULL) {
        esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_lv_display, display->touch);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        if (touch == NULL) {
            ESP_LOGW(TAG, "Touch registration failed");
        } else {
            lv_indev_set_gesture_min_distance(touch, 36);
            lv_indev_set_gesture_min_velocity(touch, 4);
        }
    }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "LVGL adapter start");

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        build_clock_screen();
        build_xiaozhi_screen();
        build_devices_screen();
        build_energy_screen();
        build_control_screen();
#if CONFIG_DESK_CLOCK_VOICE_SELF_TEST
        show_main_page(MAIN_PAGE_XIAOZHI);
#else
        show_main_page(MAIN_PAGE_CLOCK);
#endif
        update_clock();
        lv_timer_create(clock_timer, 1000, NULL);
        lv_timer_create(voice_timer, 100, NULL);
#if CONFIG_DESK_CLOCK_UI_SELF_TEST
        lv_timer_create(ui_self_test, 60, NULL);
#endif
        esp_lv_adapter_unlock();
    } else {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void clock_ui_toggle_view(void)
{
    if (esp_lv_adapter_lock(100) != ESP_OK) {
        return;
    }
    if (lv_screen_active() == s_ui.control_screen) {
        close_control_screen();
    } else {
        switch_main_page(1);
    }
    esp_lv_adapter_unlock();
}

void clock_ui_show_clock(void)
{
    if (esp_lv_adapter_lock(100) != ESP_OK) {
        return;
    }
    if (s_ui.clock_screen != NULL) {
        show_main_page(MAIN_PAGE_CLOCK);
    }
    esp_lv_adapter_unlock();
}

void clock_ui_auto_rotate_update(void)
{
#if CONFIG_DESK_CLOCK_UI_SELF_TEST
    return; // The test owns rotation while it exercises each pose.
#endif
    board_rotation_t rotation = BOARD_ROTATION_0;
    bool changed = false;
    const esp_err_t err = board_auto_rotation_update(&rotation, &changed);
    if (err != ESP_OK || s_lv_display == NULL) {
        return;
    }

    if (esp_lv_adapter_lock(100) != ESP_OK) {
        return;
    }
    const lv_display_rotation_t desired = lv_rotation_for_pose(rotation);
    if (lv_display_get_rotation(s_lv_display) == desired) {
        esp_lv_adapter_unlock();
        return;
    }
    lv_display_set_rotation(s_lv_display, desired);
    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI(TAG, "UI orientation set to %s degrees",
             board_rotation_name(rotation));
    esp_lv_adapter_unlock();
}
