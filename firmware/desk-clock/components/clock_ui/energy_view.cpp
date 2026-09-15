#include "energy_view.h"

#include <math.h>
#include <stdio.h>

LV_FONT_DECLARE(clock_cjk_16);
LV_FONT_DECLARE(clock_cjk_24);
LV_FONT_DECLARE(clock_cjk_32);
LV_FONT_DECLARE(energy_digits_64);

static constexpr uint32_t GREEN = 0x42D98A;
static constexpr uint32_t BLUE = 0x7DAEE8;

static lv_obj_t *label(lv_obj_t *parent, const char *text,
                       const lv_font_t *font, int x, int y, uint32_t color = 0xFFFFFF)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_pos(obj, x, y);
    return obj;
}

static void line(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x34383C), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

energy_view_t energy_view_create(lv_event_cb_t refresh_callback)
{
    energy_view_t view = {};
    view.screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(view.screen);
    lv_obj_set_style_bg_color(view.screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view.screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(view.screen, LV_OBJ_FLAG_SCROLLABLE);

    label(view.screen, "用电概览", &clock_cjk_24, 24, 24);
    view.status = label(view.screen, "● 等待更新", &clock_cjk_16, 0, 0, BLUE);
    lv_obj_align(view.status, LV_ALIGN_TOP_RIGHT, -24, 30);
    label(view.screen, "今日耗电", &clock_cjk_24, 24, 92, GREEN);
    view.values[0] = label(view.screen, "--", &energy_digits_64, 24, 124, GREEN);
    view.today_unit = label(view.screen, "kWh", &clock_cjk_24, 0, 0);
    lv_obj_align_to(view.today_unit, view.values[0], LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -6);
    line(view.screen, 24, 212, 432, 1);

    const char *titles[] = {"本周", "本月", "剩余电量"};
    const int indices[] = {1, 2, 3};
    lv_obj_t *balance = lv_obj_create(view.screen);
    lv_obj_remove_style_all(balance);
    lv_obj_set_pos(balance, 316, 232);
    lv_obj_set_size(balance, 140, 112);
    lv_obj_set_style_bg_color(balance, lv_color_hex(0x171B20), 0);
    lv_obj_set_style_bg_opa(balance, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(balance, 1, 0);
    lv_obj_set_style_border_color(balance, lv_color_hex(0x59616C), 0);
    lv_obj_set_style_radius(balance, 10, 0);
    lv_obj_remove_flag(balance, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(balance, LV_OBJ_FLAG_CLICKABLE);
    for (int col = 0; col < 3; ++col) {
        const int x = 24 + col * 144;
        lv_obj_t *title = label(view.screen, titles[col], &clock_cjk_16, x, 244);
        lv_obj_set_width(title, 144);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *value = label(view.screen, "--", &clock_cjk_32, x, 272);
        lv_obj_set_width(value, 144);
        lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
        view.values[indices[col]] = value;
        lv_obj_t *unit = label(view.screen, "kWh", &clock_cjk_16, x, 315);
        lv_obj_set_width(unit, 144);
        lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);
        if (col == 1) line(view.screen, x, 242, 1, 91);
    }

    view.refresh = lv_button_create(view.screen);
    lv_obj_remove_style_all(view.refresh);
    lv_obj_set_pos(view.refresh, 24, 368);
    lv_obj_set_size(view.refresh, 432, 70);
    lv_obj_set_style_bg_color(view.refresh, lv_color_hex(0x24282C), 0);
    lv_obj_set_style_bg_color(view.refresh, lv_color_hex(0x363C42), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(view.refresh, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(view.refresh, 8, 0);
    lv_obj_remove_flag(view.refresh, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(view.refresh, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(view.refresh, refresh_callback, LV_EVENT_CLICKED, NULL);
    // Draw the icon with a small polyline: no dependency on missing font glyphs.
    static const lv_point_precise_t refresh_points[] = {
        {23, 24}, {19, 27}, {15, 28}, {9, 27}, {5, 23}, {2, 18},
        {2, 12}, {5, 7}, {9, 3}, {15, 2}, {21, 4}, {26, 9},
        {19, 9}, {26, 9}, {26, 2},
    };
    view.refresh_icon = lv_line_create(view.refresh);
    lv_line_set_points(view.refresh_icon, refresh_points,
                       sizeof(refresh_points) / sizeof(refresh_points[0]));
    lv_obj_set_style_line_color(view.refresh_icon, lv_color_white(), 0);
    lv_obj_set_style_line_width(view.refresh_icon, 3, 0);
    lv_obj_set_style_line_rounded(view.refresh_icon, true, 0);
    lv_obj_set_pos(view.refresh_icon, 145, 20);
    view.refresh_label = label(view.refresh, "刷新数据", &clock_cjk_24, 0, 0);
    lv_obj_align(view.refresh_label, LV_ALIGN_CENTER, 23, 0);
    view.updated = label(view.screen, "尚未更新", &clock_cjk_16, 0, 0);
    lv_obj_align(view.updated, LV_ALIGN_BOTTOM_MID, 0, -12);
    return view;
}

static void set_value(lv_obj_t *obj, bool loaded, float value,
                       const lv_font_t *large, const lv_font_t *small, int max_width)
{
    char text[32] = "--";
    if (loaded && isfinite(value)) snprintf(text, sizeof(text), "%.2f", value);
    lv_point_t size;
    lv_text_get_size(&size, text, large, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *font = size.x > max_width ? small : large;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    // Don't wrap enormous or malformed readings into adjacent metrics.
    if (size.x > max_width) snprintf(text, sizeof(text), "--");
    lv_obj_set_style_text_font(obj, font, 0);
    lv_label_set_text(obj, text);
}

void energy_view_update(energy_view_t *view, const energy_view_data_t *data)
{
    set_value(view->values[0], data->loaded, data->today,
              &energy_digits_64, &clock_cjk_32, 332);
    lv_obj_align_to(view->today_unit, view->values[0], LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -6);
    set_value(view->values[1], data->loaded, data->week, &clock_cjk_32, &clock_cjk_24, 136);
    set_value(view->values[2], data->month_loaded, data->month, &clock_cjk_32, &clock_cjk_24, 136);
    set_value(view->values[3], data->loaded, data->remaining, &clock_cjk_32, &clock_cjk_24, 136);

    const char *status = data->refreshing ? "● 更新中" : data->failed ? "● 更新失败"
                         : data->partial ? "● 部分更新" : data->loaded ? "● 已更新" : "● 等待更新";
    lv_label_set_text(view->status, status);
    lv_label_set_text(view->refresh_label, data->refreshing ? "正在刷新…" : "刷新数据");
    if (data->refreshing) {
        lv_obj_add_state(view->refresh, LV_STATE_DISABLED);
        lv_obj_add_flag(view->refresh_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(view->refresh_label);
    } else {
        lv_obj_remove_state(view->refresh, LV_STATE_DISABLED);
        lv_obj_remove_flag(view->refresh_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(view->refresh_label, LV_ALIGN_CENTER, 23, 0);
    }
    if (data->updated_at > 0) {
        struct tm local = {};
        localtime_r(&data->updated_at, &local);
        char text[48];
        snprintf(text, sizeof(text), "更新于 %02d:%02d", local.tm_hour, local.tm_min);
        lv_label_set_text(view->updated, text);
    } else {
        lv_label_set_text(view->updated, "尚未更新");
    }
}
