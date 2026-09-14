#include "clock_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "energy_service.h"
#include "esp_check.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "lunar.h"
#include "lvgl.h"
#include "time_service.h"
#include "wifi_manager.h"

static const char *TAG = "clock_ui";

#define CALENDAR_FIXED_CHILD_COUNT 3

LV_FONT_DECLARE(clock_cjk_16);
LV_FONT_DECLARE(clock_cjk_24);
LV_FONT_DECLARE(clock_cjk_32);
LV_FONT_DECLARE(clock_cjk_96);

typedef enum {
    MAIN_PAGE_CLOCK = 0,
    MAIN_PAGE_CALENDAR,
    MAIN_PAGE_ENERGY,
    MAIN_PAGE_COUNT,
} main_page_t;

typedef struct {
    lv_obj_t *clock_screen;
    lv_obj_t *calendar_screen;
    lv_obj_t *energy_screen;
    lv_obj_t *control_screen;
    lv_obj_t *time_label;
    lv_obj_t *seconds_label;
    lv_obj_t *date_label;
    lv_obj_t *lunar_label;
    lv_obj_t *status_label;
    lv_obj_t *month_label;
    lv_obj_t *week_cells[7];
    lv_obj_t *week_name_labels[7];
    lv_obj_t *week_day_labels[7];
    lv_obj_t *week_lunar_labels[7];
    lv_obj_t *energy_value_labels[4];
    lv_obj_t *energy_status_label;
    lv_obj_t *control_panel;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value_label;
    lv_obj_t *battery_label;
    main_page_t current_page;
    main_page_t previous_page;
    int displayed_year;
    int displayed_month;
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

void clock_ui_render_calendar(int year, int month);

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
    if (month == 2) {
        return ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29
                                                                      : 28;
    }
    return days[month - 1];
}

static void normalize_month(int *year, int *month)
{
    while (*month < 1) {
        --*year;
        *month += 12;
    }
    while (*month > 12) {
        ++*year;
        *month -= 12;
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
    if (page == MAIN_PAGE_CLOCK) {
        lv_screen_load(s_ui.clock_screen);
    } else if (page == MAIN_PAGE_CALENDAR) {
        time_t now = time(NULL);
        struct tm local = {0};
        localtime_r(&now, &local);
        s_ui.displayed_year = local.tm_year + 1900;
        s_ui.displayed_month = local.tm_mon + 1;
        clock_ui_render_calendar(s_ui.displayed_year, s_ui.displayed_month);
        lv_screen_load(s_ui.calendar_screen);
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
    if (lv_screen_active() != s_ui.control_screen) {
        s_ui.previous_page = s_ui.current_page;
    }
    lv_anim_delete(s_ui.control_panel, NULL);
    lv_obj_set_y(s_ui.control_panel, 28);
    lv_obj_set_style_opa(s_ui.control_panel, LV_OPA_TRANSP, 0);
    lv_screen_load(s_ui.control_screen);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_ui.control_panel);
    lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&animation, 28, 0);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_ui.control_panel);
    lv_anim_set_exec_cb(
        &animation, [](void *object, int32_t opacity) {
            lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)opacity, 0);
        });
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void close_control_screen(void)
{
    lv_screen_load(s_ui.previous_page == MAIN_PAGE_CALENDAR
                       ? s_ui.calendar_screen
                       : s_ui.previous_page == MAIN_PAGE_ENERGY
                             ? s_ui.energy_screen
                             : s_ui.clock_screen);
    s_ui.current_page = s_ui.previous_page;
}

static void screen_gesture_cb(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }
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

static void month_button_clicked(lv_event_t *event)
{
    const intptr_t delta = (intptr_t)lv_event_get_user_data(event);
    s_ui.displayed_month += (int)delta;
    normalize_month(&s_ui.displayed_year, &s_ui.displayed_month);
    clock_ui_render_calendar(s_ui.displayed_year, s_ui.displayed_month);
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

static void render_calendar(int year, int month)
{
    char title[32];
    snprintf(title, sizeof(title), "%d年%d月", year, month);
    lv_label_set_text(s_ui.month_label, title);

    while (lv_obj_get_child_count(s_ui.calendar_screen) >
           CALENDAR_FIXED_CHILD_COUNT) {
        lv_obj_t *child = lv_obj_get_child(s_ui.calendar_screen, -1);
        lv_obj_delete(child);
    }

    static const char *weekdays = "日 一 二 三 四 五 六";
    lv_obj_t *weekday = make_label(s_ui.calendar_screen, weekdays,
                                   s_cjk_font,
                                   lv_color_hex(0x8E98A8));
    lv_obj_set_width(weekday, 444);
    lv_obj_set_style_text_align(weekday, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(weekday, 18, 56);

    const int first_weekday = lunar_day_of_week(year, month, 1);
    int cell_year = year;
    int cell_month = month;
    int cell_day = 1 - first_weekday;

    for (int index = 0; index < 42; ++index, ++cell_day) {
        int current_year = cell_year;
        int current_month = cell_month;
        int current_day = cell_day;
        while (current_day < 1) {
            --current_month;
            normalize_month(&current_year, &current_month);
            current_day += days_in_month(current_year, current_month);
        }
        while (current_day > days_in_month(current_year, current_month)) {
            current_day -= days_in_month(current_year, current_month);
            ++current_month;
            normalize_month(&current_year, &current_month);
        }

        const int column = index % 7;
        const int row = index / 7;
        lv_obj_t *cell = lv_obj_create(s_ui.calendar_screen);
        lv_obj_set_size(cell, 60, 57);
        lv_obj_set_pos(cell, 18 + column * 64, 78 + row * 59);
        lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(cell, 6, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x252C37), 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x10141B), 0);

        const bool in_current_month = current_month == month;
        const time_t now = time(NULL);
        struct tm local = {0};
        localtime_r(&now, &local);
        const bool today = current_year == local.tm_year + 1900 &&
                           current_month == local.tm_mon + 1 &&
                           current_day == local.tm_mday;
        if (today) {
            lv_obj_set_style_border_width(cell, 2, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(0xF3A712), 0);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x2A2110), 0);
        }

        char day_text[16];
        snprintf(day_text, sizeof(day_text), "%d", current_day);
        lv_obj_t *day_label = make_label(
            cell, day_text, s_cjk_title_font,
            in_current_month ? lv_color_white() : lv_color_hex(0x5B6472));
        lv_obj_align(day_label, LV_ALIGN_TOP_MID, 0, 3);

        char lunar_text[16] = {0};
        lunar_calendar_cell_text(current_year, current_month, current_day,
                                 lunar_text, sizeof(lunar_text));
        lv_obj_t *lunar_label =
            make_label(cell, lunar_text, s_cjk_font,
                       in_current_month ? lv_color_hex(0xAAB3C2)
                                        : lv_color_hex(0x444B56));
        lv_obj_align(lunar_label, LV_ALIGN_BOTTOM_MID, 0, -3);
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

    if (s_ui.calendar_screen != NULL && lv_screen_active() == s_ui.calendar_screen &&
        (s_ui.displayed_year != local.tm_year + 1900 ||
         s_ui.displayed_month != local.tm_mon + 1)) {
        clock_ui_render_calendar(s_ui.displayed_year, s_ui.displayed_month);
    }

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
        board_set_backlight((uint8_t)brightness);
        s_ui.applied_brightness = brightness;
    }
}

static void update_energy_view(void)
{
    if (s_ui.energy_value_labels[0] == NULL) {
        return;
    }

    energy_snapshot_t snapshot = {};
    energy_service_get_snapshot(&snapshot);
    if (snapshot.loaded) {
        char text[32];
        snprintf(text, sizeof(text), "%.2f kWh", snapshot.today_kwh);
        lv_label_set_text(s_ui.energy_value_labels[0], text);
        snprintf(text, sizeof(text), "%.2f kWh", snapshot.week_kwh);
        lv_label_set_text(s_ui.energy_value_labels[1], text);
        snprintf(text, sizeof(text), "%.2f kWh", snapshot.remaining_kwh);
        lv_label_set_text(s_ui.energy_value_labels[3], text);
    }
    if (snapshot.month_loaded) {
        char text[32];
        snprintf(text, sizeof(text), "%.2f kWh", snapshot.month_kwh);
        lv_label_set_text(s_ui.energy_value_labels[2], text);
    } else if (snapshot.refreshing || snapshot.month_refreshing) {
        lv_label_set_text(s_ui.energy_value_labels[2], "--");
    }

    lv_label_set_text(s_ui.energy_status_label,
                      snapshot.refreshing
                          ? "正在更新"
                          : snapshot.message[0] != '\0' ? snapshot.message
                                                        : "等待更新");
}

static void update_battery_view(void)
{
    static uint8_t last_percent = UINT8_MAX;
    uint8_t percent = 0;
    uint16_t voltage_mv = 0;
    const esp_err_t err = board_power_get_battery(&percent, &voltage_mv);
    if (err != ESP_OK) {
        lv_label_set_text(s_ui.battery_label, "--");
        return;
    }
    char text[16];
    snprintf(text, sizeof(text), "%u%%", percent);
    lv_label_set_text(s_ui.battery_label, text);
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

    static int battery_countdown;
    if (battery_countdown-- <= 0) {
        battery_countdown = 5;
        update_battery_view();
    }
}

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
        lv_obj_set_size(cell, 64, 152);
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
}

static void refresh_button_clicked(lv_event_t *event)
{
    (void)event;
    energy_service_request_refresh();
}

static void control_button_clicked(lv_event_t *event)
{
    (void)event;
    show_control_screen();
}

static void brightness_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    const int value = lv_slider_get_value(slider);
    char text[16];
    s_ui_settings.brightness = (uint8_t)value;
    s_ui_settings.night_brightness = (uint8_t)value;
    board_set_backlight((uint8_t)value);
    snprintf(text, sizeof(text), "%d%%", value);
    lv_label_set_text(s_ui.brightness_value_label, text);
}

static void update_brightness_from_pointer(lv_event_t *event)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == NULL || s_ui.brightness_slider == NULL) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t slider_area;
    lv_obj_get_coords(s_ui.brightness_slider, &slider_area);
    const int32_t width = lv_area_get_width(&slider_area);
    if (width <= 0) {
        return;
    }

    int32_t position = point.x - slider_area.x1;
    if (position < 0) {
        position = 0;
    } else if (position > width) {
        position = width;
    }

    const int minimum = lv_slider_get_min_value(s_ui.brightness_slider);
    const int maximum = lv_slider_get_max_value(s_ui.brightness_slider);
    const int value =
        minimum + (int)((int64_t)(maximum - minimum) * position / width);
    if (value != lv_slider_get_value(s_ui.brightness_slider)) {
        lv_slider_set_value(s_ui.brightness_slider, value, LV_ANIM_OFF);
    }
}

static void brightness_touch_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
        code == LV_EVENT_RELEASED) {
        update_brightness_from_pointer(event);
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ESP_LOGI(TAG, "Brightness: %d%%",
                 lv_slider_get_value(s_ui.brightness_slider));
        clock_settings_save(&s_ui_settings);
    }
}

static void build_energy_screen(void)
{
    s_ui.energy_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.energy_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.energy_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.energy_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.energy_screen, screen_gesture_cb, LV_EVENT_GESTURE,
                        NULL);

    lv_obj_t *title =
        make_label(s_ui.energy_screen, "用电情况", s_cjk_title_font,
                   lv_color_white());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *refresh = lv_button_create(s_ui.energy_screen);
    lv_obj_set_size(refresh, 60, 36);
    lv_obj_set_pos(refresh, 18, 16);
    lv_obj_clear_flag(refresh, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(0x1A2029), 0);
    lv_obj_add_event_cb(refresh, refresh_button_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label =
        make_label(refresh, "刷新", s_cjk_font, lv_color_white());
    lv_obj_center(refresh_label);

    lv_obj_t *control = lv_button_create(s_ui.energy_screen);
    lv_obj_set_size(control, 60, 36);
    lv_obj_set_pos(control, 402, 16);
    lv_obj_clear_flag(control, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(control, lv_color_hex(0x1A2029), 0);
    lv_obj_add_event_cb(control, control_button_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *control_label =
        make_label(control, "控制", s_cjk_font, lv_color_white());
    lv_obj_center(control_label);

    static const char *titles[] = {"当日用电", "本周用电", "本月用电",
                                   "剩余电量"};
    const int positions[][2] = {
        {18, 68},
        {246, 68},
        {18, 226},
        {246, 226},
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *card = lv_obj_create(s_ui.energy_screen);
        lv_obj_set_size(card, 216, 136);
        lv_obj_set_pos(card, positions[i][0], positions[i][1]);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x10141B), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x252C37), 0);
        lv_obj_set_style_pad_all(card, 0, 0);

        lv_obj_t *label =
            make_label(card, titles[i], s_cjk_font, lv_color_hex(0x8E98A8));
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 14);

        s_ui.energy_value_labels[i] =
            make_label(card, "-- kWh", s_cjk_date_font, lv_color_white());
        lv_obj_align(s_ui.energy_value_labels[i], LV_ALIGN_CENTER, 0, 8);
    }

    s_ui.energy_status_label =
        make_label(s_ui.energy_screen, "等待更新", s_cjk_font,
                   lv_color_hex(0x778293));
    lv_obj_align(s_ui.energy_status_label, LV_ALIGN_BOTTOM_MID, 0, -24);
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
    lv_obj_set_size(s_ui.brightness_slider, 360, 40);
    lv_obj_align(s_ui.brightness_slider, LV_ALIGN_TOP_MID, 0, 138);
    lv_obj_set_ext_click_area(s_ui.brightness_slider, 12);
    lv_slider_set_range(s_ui.brightness_slider, 10, 100);
    lv_slider_set_value(s_ui.brightness_slider, s_ui_settings.brightness,
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.brightness_slider,
                              lv_color_hex(0x3A4352), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.brightness_slider, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.brightness_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brightness_slider,
                              lv_color_hex(0xF3A712), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.brightness_slider, LV_RADIUS_CIRCLE,
                            LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.brightness_slider, LV_OPA_TRANSP,
                            LV_PART_KNOB);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_touch_event,
                        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_touch_event,
                        LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_touch_event,
                        LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_ui.brightness_slider, brightness_touch_event,
                        LV_EVENT_PRESS_LOST, NULL);

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

static void build_calendar_screen(void)
{
    s_ui.calendar_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.calendar_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.calendar_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.calendar_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.calendar_screen, screen_gesture_cb,
                        LV_EVENT_GESTURE, NULL);

    lv_obj_t *previous = lv_button_create(s_ui.calendar_screen);
    lv_obj_set_size(previous, 48, 36);
    lv_obj_set_pos(previous, 18, 16);
    lv_obj_clear_flag(previous, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(previous, lv_color_hex(0x1A2029), 0);
    lv_obj_add_event_cb(previous, month_button_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)-1);
    lv_obj_t *prev_label =
        make_label(previous, "<", s_cjk_title_font, lv_color_white());
    lv_obj_center(prev_label);

    s_ui.month_label =
        make_label(s_ui.calendar_screen, "2026年1月",
                   s_cjk_title_font, lv_color_white());
    lv_obj_align(s_ui.month_label, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *next = lv_button_create(s_ui.calendar_screen);
    lv_obj_set_size(next, 48, 36);
    lv_obj_set_pos(next, 414, 16);
    lv_obj_clear_flag(next, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(next, lv_color_hex(0x1A2029), 0);
    lv_obj_add_event_cb(next, month_button_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);
    lv_obj_t *next_label =
        make_label(next, ">", s_cjk_title_font, lv_color_white());
    lv_obj_center(next_label);

}

void clock_ui_render_calendar(int year, int month)
{
    render_calendar(year, month);
}

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
    esp_lv_adapter_display_config_t display_config =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
            display->panel, display->panel_io, BOARD_LCD_H_RES,
            BOARD_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = 80;
    s_lv_display = esp_lv_adapter_register_display(&display_config);
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
        build_calendar_screen();
        build_energy_screen();
        build_control_screen();
        show_main_page(MAIN_PAGE_CLOCK);
        update_clock();
        lv_timer_create(clock_timer, 1000, NULL);
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
