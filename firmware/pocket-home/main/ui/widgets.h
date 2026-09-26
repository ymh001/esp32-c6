#pragma once
#include "lvgl.h"
LV_FONT_DECLARE(clock_cjk_16);
LV_FONT_DECLARE(pocket_text_24);
LV_FONT_DECLARE(energy_digits_64);
namespace pocket {
constexpr uint32_t BG = 0x090D18, CARD = 0x1C2434, MUTED = 0x939FB5,
                   BLUE = 0x3688FF, GREEN = 0x64E5B3;
lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t color,
              int radius = 0);
lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y,
               const lv_font_t *font = &pocket_text_24,
               uint32_t color = 0xF4F7FF);
lv_obj_t *button(lv_obj_t *p, const char *s, int x, int y, int w, int h,
                 lv_event_cb_t cb, void *data = nullptr, uint32_t color = CARD);
void text_update(lv_obj_t *obj, const char *value);
void center_text(lv_obj_t *obj, int width);
void icon(lv_obj_t *parent, bool clock, int size);
} // namespace pocket
