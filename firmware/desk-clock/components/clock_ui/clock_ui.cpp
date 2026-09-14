#include "clock_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "esp_check.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "lunar.h"
#include "lvgl.h"
#include "net_config.h"
#include "time_service.h"

static const char *TAG = "clock_ui";

LV_FONT_DECLARE(clock_cjk_16);
LV_FONT_DECLARE(clock_cjk_24);
LV_FONT_DECLARE(clock_cjk_32);
LV_FONT_DECLARE(clock_dot_24);
LV_FONT_DECLARE(clock_dot_32);
LV_FONT_DECLARE(clock_dot_48);
LV_FONT_DECLARE(clock_dot_96);

typedef struct {
    lv_obj_t *clock_screen;
    lv_obj_t *calendar_screen;
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

static void show_clock_screen(void)
{
    lv_screen_load_anim(s_ui.clock_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0,
                        false);
}

static void show_calendar_screen(void)
{
    time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);
    s_ui.displayed_year = local.tm_year + 1900;
    s_ui.displayed_month = local.tm_mon + 1;
    clock_ui_render_calendar(s_ui.displayed_year, s_ui.displayed_month);
    lv_screen_load_anim(s_ui.calendar_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180,
                        0, false);
}

static void clock_clicked(lv_event_t *event)
{
    (void)event;
    show_calendar_screen();
}

static void month_button_clicked(lv_event_t *event)
{
    const intptr_t delta = (intptr_t)lv_event_get_user_data(event);
    s_ui.displayed_month += (int)delta;
    normalize_month(&s_ui.displayed_year, &s_ui.displayed_month);
    clock_ui_render_calendar(s_ui.displayed_year, s_ui.displayed_month);
}

static void back_to_clock_clicked(lv_event_t *event)
{
    (void)event;
    show_clock_screen();
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

    while (lv_obj_get_child_count(s_ui.calendar_screen) > 17) {
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
            cell, day_text, &clock_dot_32,
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
    if (net_config_is_portal()) {
        snprintf(status, sizeof(status), "配网热点 192.168.4.1");
    } else if (net_config_is_connected()) {
        char ip[16];
        net_config_get_ip(ip, sizeof(ip));
        snprintf(status, sizeof(status), "%s  %s",
                 time_service_is_synced() ? "NTP 已同步" : "等待校时", ip);
    } else {
        snprintf(status, sizeof(status), "网络未连接");
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

static void clock_timer(lv_timer_t *timer)
{
    (void)timer;
    update_clock();
}

static void build_clock_screen(void)
{
    s_ui.clock_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.clock_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.clock_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.clock_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.clock_screen, clock_clicked, LV_EVENT_CLICKED, NULL);

    s_ui.time_label =
        make_label(s_ui.clock_screen, "00:00", &clock_dot_96,
                   lv_color_white());
    lv_obj_align(s_ui.time_label, LV_ALIGN_TOP_MID, -34, 50);

    s_ui.seconds_label =
        make_label(s_ui.clock_screen, ":00", &clock_dot_32,
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
            make_label(cell, "", &clock_dot_32, lv_color_white());
        lv_obj_align(s_ui.week_day_labels[i], LV_ALIGN_CENTER, 0, 0);

        s_ui.week_lunar_labels[i] =
            make_label(cell, "", s_cjk_font, lv_color_hex(0xAAB3C2));
        lv_obj_align(s_ui.week_lunar_labels[i], LV_ALIGN_BOTTOM_MID, 0, -8);
    }
}

static void build_calendar_screen(void)
{
    s_ui.calendar_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.calendar_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.calendar_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.calendar_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *previous = lv_button_create(s_ui.calendar_screen);
    lv_obj_set_size(previous, 48, 36);
    lv_obj_set_pos(previous, 18, 16);
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
    lv_obj_set_style_bg_color(next, lv_color_hex(0x1A2029), 0);
    lv_obj_add_event_cb(next, month_button_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);
    lv_obj_t *next_label =
        make_label(next, ">", s_cjk_title_font, lv_color_white());
    lv_obj_center(next_label);

    lv_obj_t *back = lv_button_create(s_ui.calendar_screen);
    lv_obj_set_size(back, 74, 32);
    lv_obj_set_pos(back, 203, 443);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1A2029), 0);
    lv_obj_add_event_cb(back, back_to_clock_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label =
        make_label(back, "时钟", s_cjk_font, lv_color_white());
    lv_obj_center(back_label);
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
        if (esp_lv_adapter_register_touch(&touch_config) == NULL) {
            ESP_LOGW(TAG, "Touch registration failed");
        }
    }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "LVGL adapter start");

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        build_clock_screen();
        build_calendar_screen();
        lv_screen_load(s_ui.clock_screen);
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
    if (lv_screen_active() == s_ui.clock_screen) {
        show_calendar_screen();
    } else {
        show_clock_screen();
    }
    esp_lv_adapter_unlock();
}

void clock_ui_show_clock(void)
{
    if (esp_lv_adapter_lock(100) != ESP_OK) {
        return;
    }
    if (s_ui.clock_screen != NULL) {
        show_clock_screen();
    }
    esp_lv_adapter_unlock();
}
