#include "core/services.h"
#include "core/file_transfer.h"
#include "core/energy.h"
#include "ui/shell.h"
#include "ui/widgets.h"
#include "ui_codepoints.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
alignas(64) static uint16_t framebuffer[480 * 480];
alignas(64) static uint16_t buffer[480 * 32];
static lv_point_t pointer{0, 0};
static bool pressed;
static int light = 75;
static int hour = 12, minute = 34;
static unsigned connections, scans;
namespace pocket {
static Preferences prefs;
static TransferSnapshot transfer;
bool transfer_enabled(){return transfer.enabled;}
bool transfer_busy(){return transfer.busy;}
TransferSnapshot transfer_snapshot(){return transfer;}
bool transfer_enable(bool enabled){transfer.enabled=transfer.running=enabled;strcpy(transfer.code,"12345678");return true;}

BatterySnapshot battery_snapshot() { return {true, 85, 4050}; }
static EnergySnapshot energy;
EnergySnapshot energy_snapshot() { return energy; }
static NetworkSnapshot net;
Preferences preferences() { return prefs; }
void preferences_set(Preferences p) { prefs = p; }
void display_brightness(uint8_t n) { light = n; }
NetworkSnapshot network_snapshot() { return net; }
bool network_enable(bool v) {
  net.enabled = v;
  net.state = v ? NetState::Idle : NetState::Off;
  net.revision++;
  return true;
}
bool network_scan() {
  scans++;
  return true;
}
bool network_forget() {
  net.saved_ssid[0] = 0;
  return true;
}
bool network_connect(const char *, const char *p) {
  if (strlen(p) < 8)
    return false;
  connections++;
  net.state = NetState::Connecting;
  net.revision++;
  return true;
}
void clock_local(tm *t) {
  *t = {};
  t->tm_year = 126;
  t->tm_mon = 8;
  t->tm_mday = 26;
  t->tm_wday = 6;
  t->tm_hour = hour;
  t->tm_min = minute;
}
bool clock_valid() { return true; }
bool clock_synced() { return true; }
} // namespace pocket
void step(int ms = 100) {
  for (int t = 0; t < ms; t += 5) {
    lv_tick_inc(5);
    lv_timer_handler();
  }
}
void snap(const char *name) {
  lv_refr_now(nullptr);
  char path[256];
  snprintf(path, sizeof(path), "screens/%s.ppm", name);
  FILE *f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n480 480\n255\n");
  for (auto c : framebuffer) {
    unsigned char pixel[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                              (unsigned char)(((c >> 5) & 63) * 255 / 63),
                              (unsigned char)((c & 31) * 255 / 31)};
    fwrite(pixel, 1, 3, f);
  }
  fclose(f);
}
void tap(int x, int y) {
  pointer = {x, y};
  pressed = true;
  step(70);
  pressed = false;
  step(150);
}
void swipe(int x, int y, int dx, int dy) {
  pointer = {x, y};
  pressed = true;
  step(50);
  for (int k = 1; k <= 8; k++) {
    pointer = {x + dx * k / 8, y + dy * k / 8};
    step(35);
  }
  pressed = false;
  step(150);
}
lv_obj_t *find(lv_obj_t *root, const lv_obj_class_t *cls) {
  if (lv_obj_check_type(root, cls))
    return root;
  for (unsigned i = 0; i < lv_obj_get_child_count(root); i++)
    if (auto found = find(lv_obj_get_child(root, i), cls))
      return found;
  return nullptr;
}
bool has_text(lv_obj_t *root, const char *value) {
  if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), value)) return true;
  for (unsigned i=0; i<lv_obj_get_child_count(root); ++i)
    if (has_text(lv_obj_get_child(root,i), value)) return true;
  return false;
}
void page(const char *s) {
  if (strcmp(pocket::ui_page(), s)) {
    fprintf(stderr, "Expected %s, got %s\n", s, pocket::ui_page());
    abort();
  }
}
int main() {
  std::filesystem::create_directories("screens");
  for (auto code : ui_codepoints) {
    for (auto font : {&clock_cjk_16, &pocket_text_24}) {
      lv_font_glyph_dsc_t glyph{};
      if (!font->get_glyph_dsc(font, &glyph, code, 0)) {
        fprintf(stderr, "Missing UI glyph U+%04X in font height %u\n", code,
                font->line_height);
        abort();
      }
    }
  }
  lv_init();
  auto d = lv_display_create(480, 480);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d, buffer, nullptr, sizeof(buffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(
      d, [](lv_display_t *d, const lv_area_t *a, uint8_t *p) {
        int w = lv_area_get_width(a);
        for (int y = a->y1; y <= a->y2; y++)
          memcpy(&framebuffer[y * 480 + a->x1], p + (y - a->y1) * w * 2, w * 2);
        lv_display_flush_ready(d);
      });
  auto in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, [](lv_indev_t *, lv_indev_data_t *v) {
    v->point = pointer;
    v->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  });
  lv_indev_set_gesture_min_distance(in, 42);
  lv_indev_set_gesture_min_velocity(in, 4);
  using namespace pocket;
  net.enabled = true;
  net.state = NetState::Connected;
  strcpy(net.ssid, "Home Wi-Fi");
  strcpy(net.ip, "192.168.3.129");
  strcpy(net.saved_ssid, "Home Wi-Fi");
  strcpy(net.message, "已连接");
  net.count = 2;
  strcpy(net.aps[0].ssid, "Home Wi-Fi");
  net.aps[0].secure = true;
  net.aps[0].supported = true;
  net.aps[0].rssi = -42;
  strcpy(net.aps[1].ssid, "客厅网络");
  net.aps[1].secure = true;
  net.aps[1].supported = true;
  net.aps[1].rssi = -65;
  ui_start();
  step();
  page("home");
  snap("home");
  tap(123, 230);
  page("clock");
  snap("clock");
  minute = 35;
  step(1000);
  snap("flip");
  step(500);
  snap("clock-after-flip");
  swipe(220, 235, -150, 0);
  page("clock");
  swipe(220, 60, 0, 140);
  page("control");
  snap("control");
  tap(402,394);step(1100);assert(transfer.enabled);
  assert(has_text(lv_screen_active(),"http://192.168.3.129 · 码 12345678"));
  snap("control-transfer");
  tap(110,393);assert(transfer.enabled); // Label must not toggle the switch.
  tap(402,394);step(1100);assert(!transfer.enabled);

  tap(345, 322);
  assert(preferences().rotation_locked);
  assert(has_text(lv_screen_active(), "已锁定"));
  snap("control-locked");
  tap(345, 322);
  assert(!preferences().rotation_locked);
  swipe(12, 390, 0, -170);
  page("clock");
  swipe(240, 455, 0, -95);
  page("home");
  tap(340, 230);
  page("energy");
  assert(has_text(lv_screen_active(), "加载中"));
  energy.kwh = 12.34; energy.cost = 13.57; energy.balance = 84.22;
  energy.valid = 7; energy.updated = energy.fetched = time(nullptr);
  step(1100);
  assert(has_text(lv_screen_active(), "12.34"));
  assert(has_text(lv_screen_active(), "13.57 元"));
  assert(has_text(lv_screen_active(), "84.22 元"));
  snap("energy");
  energy.failed = true; step(1100);
  assert(has_text(lv_screen_active(), "更新失败"));
  assert(has_text(lv_screen_active(), "12.34"));
  energy.failed = false; energy.updated -= 3600; step(1100);
  assert(has_text(lv_screen_active(), "数据较旧"));
  energy.updated = time(nullptr);
  energy.fetched -= 86400; step(1100);
  assert(!has_text(lv_screen_active(), "12.34"));
  energy.fetched = time(nullptr);
  ui_control();
  step();
  tap(373, 144);
  page("wifi");
  step(1000);
  snap("wifi");
  tap(140, 249);
  page("password");
  snap("password");
  auto ta = find(lv_screen_active(), &lv_textarea_class);
  assert(ta);
  tap(70, 300);
  assert(strlen(lv_textarea_get_text(ta)) > 0);
  lv_textarea_set_text(ta, "short");
  tap(200, 236);
  page("password");
  assert(connections == 0);
  lv_textarea_set_text(ta, "test-password");
  unsigned before = scans;
  tap(200, 236);
  page("wifi");
  assert(connections == 1 && scans == before);
  ui_control();
  step();
  tap(180, 135);
  assert(!net.enabled);
  tap(180, 135);
  assert(net.enabled);
  tap(200, 322);
  page("timeout");
  tap(200, 113);
  page("control");
  assert(preferences().timeout_seconds == 30);
  ui_home();
  step();
  step(31000);
  assert(light == 0);
  tap(123, 230);
  page("home");
  assert(light == 75);
  tap(123, 230);
  page("clock");
  auto p = preferences();
  p.timeout_seconds = 0;
  preferences_set(p);
  hour = 23;
  minute = 59;
  step(1100);
  hour = 0;
  minute = 0;
  step(1900);
  assert(lv_mem_test() == LV_RESULT_OK);
  for (int k = 0; k < 80; k++) {
    ui_open_app(0);
    step(50);
    minute = k % 60;
    step(1100);
    ui_control();
    step(50);
    ui_wifi();
    step(50);
    ui_home();
    step(50);
    assert(lv_mem_test() == LV_RESULT_OK);
  }
  // Physical touch positions must still select the same icon at all orientations.
  for (unsigned r = 0; r < 4; ++r) {
    ui_home(); step();
    lv_display_set_rotation(d, (lv_display_rotation_t)r); step();
    lv_area_t point{123,230,123,230};
    lv_display_rotate_area(d, &point);
    tap(point.x1, point.y1); page("clock");
    ui_control(); step();
    point = {345,322,345,322}; lv_display_rotate_area(d, &point);
    tap(point.x1, point.y1);
    assert(preferences().rotation_locked);
    assert(preferences().locked_rotation == r);
    tap(point.x1, point.y1);
    assert(!preferences().rotation_locked);
  }
  lv_display_set_rotation(d, LV_DISPLAY_ROTATION_0);
  ui_home(); step();
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  assert(m.free_size > 28 * 1024);
  printf("PASS: navigation, gestures, flip, Wi-Fi keyboard, on/off, timeout "
         "wake, 80 lifecycle cycles. LVGL peak %u bytes; free %zu bytes\n",
         (unsigned)m.max_used, m.free_size);
}
