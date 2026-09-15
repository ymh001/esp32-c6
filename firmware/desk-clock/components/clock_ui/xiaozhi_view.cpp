#include "xiaozhi_view.h"
#include <stdint.h>

LV_FONT_DECLARE(clock_cjk_16);
LV_FONT_DECLARE(clock_cjk_24);
LV_FONT_DECLARE(clock_cjk_32);

static lv_obj_t *text(lv_obj_t *p, const char *s, const lv_font_t *font,
                      uint32_t color, int x, int y)
{
    lv_obj_t *o = lv_label_create(p);
    lv_label_set_text(o, s);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_pos(o, x, y);
    return o;
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *heading_label, *hint_label, *speak_button, *speak_label, *status_label;

void xiaozhi_view_update(int state, const char *message)
{
    static const char *titles[] = {"今天想做什么？", "正在连接", "我在听", "正在处理", "正在回答", "请再试一次"};
    if (state < 0 || state > 5 || !heading_label) return;
    lv_label_set_text(heading_label, titles[state]);
    lv_label_set_text(hint_label, message);
    lv_label_set_text(speak_label, state == 2 ? "结束说话" : state == 1 || state == 3 || state == 4 ? "请稍候…" : "点击说话");
    lv_label_set_text(status_label, state == 5 ? "连接异常" : "本地语音");
    if (state == 1 || state == 3 || state == 4) lv_obj_add_state(speak_button, LV_STATE_DISABLED);
    else lv_obj_remove_state(speak_button, LV_STATE_DISABLED);
}

lv_obj_t *xiaozhi_view_create(lv_event_cb_t navigation, lv_event_cb_t speak)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    text(screen, "小智", &clock_cjk_24, 0xFFFFFF, 24, 24);
    status_label = text(screen, "本地语音", &clock_cjk_16, 0xA4A6FF, 382, 28);

    lv_obj_t *face = box(screen, 152, 96, 176, 130, 0x0C101C, 60);
    lv_obj_set_style_border_width(face, 3, 0);
    lv_obj_set_style_border_color(face, lv_color_hex(0x969AFF), 0);
    box(screen, 196, 138, 16, 38, 0x969AFF, 8);
    box(screen, 268, 138, 16, 38, 0x969AFF, 8);
    static const lv_point_precise_t smile[] = {{0,0},{6,5},{14,7},{22,5},{28,0}};
    lv_obj_t *line = lv_line_create(screen);
    lv_line_set_points(line, smile, 5);
    lv_obj_set_pos(line, 226, 189);
    lv_obj_set_style_line_width(line, 4, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(0x969AFF), 0);
    const int heights[] = {16,30,48,30,16};
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < 5; ++i)
            box(screen, (side ? 352 : 76) + i * 10, 160 - heights[i]/2,
                7, heights[i], 0x8589FA, 3);
    lv_obj_t *heading = heading_label = text(screen, "今天想做什么？", &clock_cjk_32, 0xFFFFFF, 0, 236);
    lv_obj_set_width(heading, 480);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *hint = hint_label = text(screen, "点击说话，控制家中设备", &clock_cjk_16, 0xA6A9B5, 32, 283);
    lv_obj_set_size(hint, 416, 56);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *button = speak_button = box(screen, 24, 348, 432, 70, 0x5157C7, 16);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    if (speak) lv_obj_add_event_cb(button, speak, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x303448), LV_STATE_DISABLED);
    lv_obj_t *label = speak_label = text(button, "点击说话", &clock_cjk_24, 0xFFFFFF, 0, 0);
    lv_obj_center(label);
    const char *names[] = {"时钟", "小智", "设备", "耗电"};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *tab = box(screen, i * 120, 428, 120, 52, 0, 0);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        if (navigation) lv_obj_add_event_cb(tab, navigation, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = text(tab, names[i], &clock_cjk_16, i == 1 ? 0xA4A6FF : 0xD8DAE2, 0, 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 10);
    }
    box(screen, 161, 467, 38, 3, 0xA4A6FF, 1);
    return screen;
}
