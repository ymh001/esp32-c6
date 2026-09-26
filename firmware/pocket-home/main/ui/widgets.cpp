#include "widgets.h"
#include <cstring>
namespace pocket {
lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t color,
              int radius) {
  auto o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_clickable(o, false);
  return o;
}
lv_obj_t *text(lv_obj_t *p, const char *s, int x, int y, const lv_font_t *f,
               uint32_t color) {
  auto o = lv_label_create(p);
  lv_label_set_text(o, s);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_style_text_font(o, f, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_obj_set_clickable(o, false);
  return o;
}
lv_obj_t *button(lv_obj_t *p, const char *s, int x, int y, int w, int h,
                 lv_event_cb_t cb, void *data, uint32_t color) {
  auto o = box(p, x, y, w, h, color, 18);
  lv_obj_set_clickable(o, true);
  lv_obj_set_style_bg_color(o, lv_color_hex(color + 0x080808),
                            LV_STATE_PRESSED);
  if (s && s[0]) {
    auto l = text(o, s, 0, 0);
    lv_obj_center(l);
  }
  if (cb)
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, data);
  return o;
}
void text_update(lv_obj_t *obj, const char *value) {
  if (strcmp(lv_label_get_text(obj), value)) lv_label_set_text(obj, value);
}
void center_text(lv_obj_t *o, int w) {
  lv_obj_set_width(o, w);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
}
void icon(lv_obj_t *p, bool clock, int size) {
  if (clock) {
    auto face =
        box(p, 12, 12, size - 24, size - 24, 0xF4F7FF, LV_RADIUS_CIRCLE);
    auto h = box(face, (size - 24) / 2 - 2, 12, 4, (size - 24) / 2 - 10,
                 0x162236, 2);
    (void)h;
    box(face, (size - 24) / 2, (size - 24) / 2 - 2, 19, 4, 0x162236, 2);
    box(face, (size - 24) / 2 - 4, (size - 24) / 2 - 4, 8, 8, BLUE,
        LV_RADIUS_CIRCLE);
  } else {
    static const lv_point_precise_t bolt[] = {
        {46, 15}, {26, 43}, {43, 43}, {32, 73}, {63, 36}, {46, 36}, {46, 15}};
    auto l = lv_line_create(p);
    lv_line_set_points(l, bolt, 7);
    lv_obj_set_style_line_width(l, 5, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(0x0B4236), 0);
  }
}
} // namespace pocket
