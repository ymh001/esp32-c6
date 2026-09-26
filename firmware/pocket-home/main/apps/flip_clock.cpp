#include "../core/services.h"
#include "../ui/widgets.h"
#include "apps.h"
#include <cstdio>
#include <cstring>
extern "C" {
extern const lv_image_dsc_t pocket_flip_tiles[11][2];
}
namespace pocket {
namespace {
struct Half {
  lv_obj_t *root, *background, *digits[2];
};
struct Panel {
  Half top, bottom, moving_top, moving_bottom;
  char value[3];
};
Panel panels[2];
lv_obj_t *date, *status;
int previous_minute = -1;
void set_digits(Half &h, const char *value, bool bottom) {
  for (int i = 0; i < 2; i++) {
    int digit = value[i] >= '0' && value[i] <= '9' ? value[i] - '0' : 10;
    lv_image_set_src(h.digits[i], &pocket_flip_tiles[digit][bottom ? 1 : 0]);
  }
}
void frame(void *v, int32_t phase) {
  auto &p = *(Panel *)v;
  bool bottom = phase >= 256;
  auto &h = bottom ? p.moving_bottom : p.moving_top;
  lv_obj_set_hidden(p.moving_top.root, bottom);
  lv_obj_set_hidden(p.moving_bottom.root, !bottom);
  int scale = bottom ? phase - 256 : 256 - phase;
  int height = 90 * scale / 256;
  lv_obj_set_height(h.background, height);
  lv_obj_set_y(h.background, bottom ? 0 : 90 - height);
  for (auto image : h.digits)
    lv_image_set_scale_y(image, scale > 0 ? scale : 1);
}
void finished(lv_anim_t *a) {
  auto &p = *(Panel *)a->var;
  set_digits(p.bottom, p.value, true);
  lv_obj_set_hidden(p.moving_top.root, true);
  lv_obj_set_hidden(p.moving_bottom.root, true);
}
Half half(lv_obj_t *card, bool bottom) {
  Half h{};
  h.root = box(card, 0, bottom ? 94 : 0, 194, 90, 0, 0);
  lv_obj_set_style_bg_opa(h.root, LV_OPA_TRANSP, 0);
  h.background = box(h.root, 0, 0, 194, 90, bottom ? 0x282C35 : 0x222630, 10);
  for (int i = 0; i < 2; i++) {
    h.digits[i] = lv_image_create(h.root);
    lv_image_set_src(h.digits[i], &pocket_flip_tiles[10][bottom ? 1 : 0]);
    lv_obj_set_pos(h.digits[i], 21 + i * 76, 0);
    lv_image_set_pivot(h.digits[i], 0, bottom ? 0 : 90);
    lv_obj_set_clickable(h.digits[i], false);
  }
  return h;
}
void set_panel(Panel &p, const char *value, bool animate, uint32_t delay = 0) {
  if (strcmp(p.value, value) == 0)
    return;
  lv_anim_delete(&p, frame);
  set_digits(p.moving_top, p.value, false);
  set_digits(p.moving_bottom, value, true);
  set_digits(p.top, value, false);
  snprintf(p.value, sizeof(p.value), "%s", value);
  if (!animate) {
    set_digits(p.bottom, value, true);
    lv_obj_set_hidden(p.moving_top.root, true);
    lv_obj_set_hidden(p.moving_bottom.root, true);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &p);
  lv_anim_set_values(&a, 0, 512);
  lv_anim_set_duration(&a, 420);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_exec_cb(&a, frame);
  lv_anim_set_completed_cb(&a, finished);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}
} // namespace
void flip_create(lv_obj_t *root) {
  lv_obj_set_style_bg_color(root, lv_color_hex(0x050608), 0);
  auto title = text(root, "翻页时钟", 0, 47, &pocket_text_24, MUTED);
  center_text(title, 480);
  panels[0] = {};
  panels[1] = {};
  previous_minute = -1;
  for (int i = 0; i < 2; i++) {
    auto card = box(root, 28 + i * 230, 130, 194, 184, 0x050608, 12);
    auto &p = panels[i];
    strcpy(p.value, "--");
    p.top = half(card, false);
    p.bottom = half(card, true);
    p.moving_top = half(card, false);
    p.moving_bottom = half(card, true);
    lv_obj_set_hidden(p.moving_top.root, true);
    lv_obj_set_hidden(p.moving_bottom.root, true);
  }
  box(root, 236, 192, 8, 8, 0x9097A3, 4);
  box(root, 236, 240, 8, 8, 0x9097A3, 4);
  date = text(root, "", 0, 348);
  center_text(date, 480);
  status = text(root, "", 0, 391, &clock_cjk_16, MUTED);
  center_text(status, 480);
  flip_tick();
}
void flip_tick() {
  tm t{};
  clock_local(&t);
  bool valid = clock_valid();
  int minute = valid ? t.tm_hour * 60 + t.tm_min : -2;
  if (minute != previous_minute) {
    char hh[3] = "--", mm[3] = "--";
    if (valid) {
      snprintf(hh, 3, "%02d", t.tm_hour);
      snprintf(mm, 3, "%02d", t.tm_min);
    }
    bool animate = previous_minute >= 0 && valid;
    bool hour_changed = strcmp(panels[0].value, hh) != 0;
    set_panel(panels[0], hh, animate);
    set_panel(panels[1], mm, animate, animate && hour_changed ? 440 : 0);
    previous_minute = minute;
  }
  static const char *week[] = {"周日", "周一", "周二", "周三",
                               "周四", "周五", "周六"};
  char s[80];
  if (valid)
    snprintf(s, sizeof(s), "%d月%d日   %s", t.tm_mon + 1, t.tm_mday,
             week[t.tm_wday]);
  else
    snprintf(s, sizeof(s), "连接 Wi-Fi 后自动校时");
  lv_label_set_text(date, s);
  lv_label_set_text(status, clock_synced() ? "北京时间 · 已同步"
                            : valid        ? "北京时间 · 本地时钟"
                                           : "等待校时");
}
void flip_close() {
  for (auto &p : panels)
    lv_anim_delete(&p, frame);
}
} // namespace pocket
