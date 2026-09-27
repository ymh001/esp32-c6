#include "shell.h"
#include "../apps/apps.h"
#include "../core/services.h"
#include "../core/file_transfer.h"
#include "widgets.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace pocket {
namespace {
enum class Overlay { None, Control, Wifi, Password, Timeout };
enum class Action {
  None,
  Home,
  OpenApp,
  Control,
  Wifi,
  Password,
  Timeout,
  Close
};
lv_obj_t *screen, *scene, *overlay, *shield, *status_time, *status_net,
    *wifi_title, *wifi_detail, *wifi_message, *wifi_list, *wifi_switch,
    *brightness_value, *password_input, *password_message, *keyboard;
lv_obj_t *transfer_switch, *transfer_title, *transfer_detail;
lv_obj_t *battery_value, *battery_icon, *overlay_battery_value, *overlay_battery_icon;
const App *active_app = nullptr;
Overlay mode = Overlay::None;
Action pending = Action::None;
unsigned requested_app = 0;
bool asleep = false, waking = false;
char selected_ssid[33]{};
bool selected_secure = true;
uint32_t last_network_revision = UINT32_MAX, last_second = UINT32_MAX;
int last_light = -1;
void request(Action a) { pending = a; }
void click(lv_event_t *e) {
  auto a = (Action)(intptr_t)lv_event_get_user_data(e);
  if (a == Action::Wifi && mode == Overlay::Control)
    network_scan();
  request(a);
}
lv_obj_t *nav_button(lv_obj_t *p, const char *s, int x, int y, int w, int h,
                     Action a, uint32_t c = CARD) {
  return button(p, s, x, y, w, h, click, (void *)(intptr_t)a, c);
}
void apply_light() {
  int level = asleep ? 0 : preferences().brightness;
  if (level != last_light) {
    display_brightness(level);
    last_light = level;
  }
}
void dismiss_overlay() {
  if (overlay) {
    lv_obj_delete(overlay);
    overlay = nullptr;
  }
  overlay_battery_value = overlay_battery_icon = nullptr;
  transfer_switch=transfer_title=transfer_detail=nullptr;
  mode = Overlay::None;
  wifi_title = wifi_detail = wifi_message = wifi_list = wifi_switch =
      password_input = password_message = keyboard = brightness_value = nullptr;
}
void close_scene() {
  if (active_app && active_app->close)
    active_app->close();
  active_app = nullptr;
  if (scene)
    lv_obj_delete(scene);
  scene = box(screen, 0, 0, 480, 480, BG);
}
void gesture(lv_event_t *) {
  auto i = lv_indev_active();
  if (!i || asleep || waking)
    return;
  auto d = lv_indev_get_gesture_dir(i);
  lv_point_t p;
  lv_indev_get_point(i, &p);
  if (d == LV_DIR_BOTTOM && mode == Overlay::None)
    request(Action::Control);
  else if (d == LV_DIR_TOP && mode == Overlay::Control)
    request(Action::Close);
  else if (d == LV_DIR_TOP && mode == Overlay::None && p.y > 315)
    request(Action::Home);
  lv_indev_wait_release(i);
}
void update_battery(lv_obj_t *value, lv_obj_t *symbol) {
  if (!value || !symbol) return;
  auto b = battery_snapshot();
  char s[12];
  if (b.valid) snprintf(s, sizeof(s), "%u%%", b.percent);
  else snprintf(s, sizeof(s), "--%%");
  text_update(value, s);
  const char *icon = !b.valid ? LV_SYMBOL_BATTERY_EMPTY :
    b.percent > 80 ? LV_SYMBOL_BATTERY_FULL : b.percent > 55 ? LV_SYMBOL_BATTERY_3 :
    b.percent > 30 ? LV_SYMBOL_BATTERY_2 : b.percent > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  text_update(symbol, icon);
  lv_obj_set_style_text_color(symbol, lv_color_hex(!b.valid ? MUTED : b.percent <= 20 ? 0xFF7373 : GREEN), 0);
}
void battery_chrome(lv_obj_t *parent, int y, lv_obj_t **value, lv_obj_t **symbol) {
  *value = text(parent, "", 344, y+2, &clock_cjk_16, MUTED);
  lv_obj_set_width(*value, 54);
  lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_RIGHT, 0);
  *symbol = text(parent, "", 409, y, &lv_font_montserrat_24, MUTED);
  update_battery(*value, *symbol);
}
void chrome() {
  status_time = text(scene, "--:--", 28, 16, &clock_cjk_16, MUTED);
  status_net =
      text(scene, LV_SYMBOL_WIFI, 300, 14, &lv_font_montserrat_24, MUTED);
  battery_chrome(scene, 14, &battery_value, &battery_icon);
  auto home = nav_button(scene, "", 166, 439, 148, 36, Action::Home, BG);
  lv_obj_set_style_bg_opa(home, LV_OPA_TRANSP, 0);
  box(home, 24, 14, 100, 5, 0xBAC4D7, 3);
}
void build_home() {
  dismiss_overlay();
  close_scene();
  lv_obj_set_style_bg_color(scene, lv_color_hex(0x101C31), 0);
  lv_obj_set_style_bg_grad_color(scene, lv_color_hex(0x070C16), 0);
  lv_obj_set_style_bg_grad_dir(scene, LV_GRAD_DIR_VER, 0);
  text(scene, "我的桌面", 32, 71);
  text(scene, "让时间与生活，一目了然", 32, 111, &clock_cjk_16, MUTED);
  unsigned n;
  const App *apps = app_catalog(&n);
  for (unsigned i = 0; i < n; i++) {
    int x = 76 + (i % 2) * 232, y = 184 + (i / 2) * 146;
    auto b = button(
        scene, "", x, y, 96, 96,
        [](lv_event_t *e) {
          requested_app = (uintptr_t)lv_event_get_user_data(e);
          request(Action::OpenApp);
        },
        (void *)(uintptr_t)i, apps[i].clock_icon ? 0xDDE8FC : GREEN);
    lv_obj_set_style_radius(b, 26, 0);
    icon(b, apps[i].clock_icon, 96);
    auto name = text(scene, apps[i].name, x - 20, y + 113);
    center_text(name, 136);
  }
  auto hint = text(scene, "下滑打开控制中心", 0, 397, &clock_cjk_16, MUTED);
  center_text(hint, 480);
  chrome();
}
void build_app(unsigned index) {
  unsigned n;
  auto apps = app_catalog(&n);
  if (index >= n)
    return;
  dismiss_overlay();
  close_scene();
  active_app = &apps[index];
  active_app->create(scene);
  chrome();
}
void overlay_base(Overlay kind, const char *title, Action back) {
  dismiss_overlay();
  mode = kind;
  overlay = box(screen, 0, 0, 480, 480, 0x101622);
  nav_button(overlay, "‹", 20, 24, 48, 46, back);
  text(overlay, title, 88, 32);
  battery_chrome(overlay, 34, &overlay_battery_value, &overlay_battery_icon);
}
void toggle_wifi(lv_event_t *) { network_enable(!network_snapshot().enabled); }
void build_control() {
  overlay_base(Overlay::Control, "控制中心", Action::Close);
  auto tile = button(overlay, "", 24, 91, 278, 101, toggle_wifi, nullptr, CARD);
  auto symbol = text(tile, LV_SYMBOL_WIFI, 20, 10, &lv_font_montserrat_32);
  (void)symbol;
  wifi_title = text(tile, "Wi-Fi", 20, 46);
  lv_obj_set_width(wifi_title, 234);
  lv_label_set_long_mode(wifi_title, LV_LABEL_LONG_DOT);
  wifi_detail = text(tile, "", 20, 76, &clock_cjk_16);
  lv_obj_set_width(wifi_detail, 234);
  lv_label_set_long_mode(wifi_detail, LV_LABEL_LONG_DOT);
  wifi_switch = tile;
  auto settings =
      nav_button(overlay, "", 316, 91, 140, 101, Action::Wifi, CARD);
  auto a = text(settings, LV_SYMBOL_SETTINGS, 0, 10, &lv_font_montserrat_32);
  center_text(a, 140);
  a = text(settings, "选网络", 0, 58);
  center_text(a, 140);
  auto bright = box(overlay, 24, 204, 432, 77, CARD, 24);
  text(bright, "亮度", 20, 12);
  brightness_value = text(bright, "", 340, 16, &clock_cjk_16, MUTED);
  auto slider = lv_slider_create(bright);
  lv_obj_set_pos(slider, 24, 48);
  lv_obj_set_size(slider, 384, 16);
  lv_slider_set_range(slider, 5, 100);
  lv_slider_set_value(slider, preferences().brightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x39445A), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xF4F7FF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xF4F7FF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
  lv_obj_set_gesture_bubble(slider, false);
  lv_obj_add_event_cb(
      slider,
      [](lv_event_t *e) {
        auto p = preferences();
        p.brightness = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
        preferences_set(p);
        char s[16];
        snprintf(s, sizeof(s), "%u%%", p.brightness);
        text_update(brightness_value, s);
        apply_light();
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
  char s[16];
  snprintf(s, sizeof(s), "%u%%", preferences().brightness);
  text_update(brightness_value, s);
  auto timeout = nav_button(overlay, "", 24, 293, 208, 62, Action::Timeout);
  text(timeout, "亮屏时长", 18, 10, &clock_cjk_16);
  uint16_t seconds = preferences().timeout_seconds;
  if (seconds)
    snprintf(s, sizeof(s), "%u 秒  >", seconds);
  else
    snprintf(s, sizeof(s), "常亮  >");
  text(timeout, s, 18, 35, &clock_cjk_16, MUTED);
  auto rotate = button(
      overlay, "", 248, 293, 208, 62,
      [](lv_event_t *) {
        auto p = preferences();
        p.rotation_locked = !p.rotation_locked;
        p.locked_rotation =
            (uint8_t)lv_display_get_rotation(lv_display_get_default());
        preferences_set(p);
        request(Action::Control);
      },
      nullptr, preferences().rotation_locked ? 0x325AAF : CARD);
  text(rotate, "旋转锁定", 18, 10, &clock_cjk_16);
  text(rotate, preferences().rotation_locked ? "已锁定" : "自动旋转", 18, 35,
       &clock_cjk_16, preferences().rotation_locked ? 0xD7E7FF : MUTED);
  auto transfer=box(overlay,24,367,432,73,CARD,18);
  transfer_title=text(transfer,"文件传输",18,14,&clock_cjk_16);
  transfer_detail=text(transfer,"",18,45,&clock_cjk_16,MUTED);
  lv_obj_set_width(transfer_detail,396);
  lv_label_set_long_mode(transfer_detail,LV_LABEL_LONG_DOT);
  transfer_switch=lv_switch_create(transfer);
  lv_obj_set_pos(transfer_switch,346,10);lv_obj_set_size(transfer_switch,66,32);
  lv_obj_set_style_bg_color(transfer_switch,lv_color_hex(GREEN),LV_PART_INDICATOR|LV_STATE_CHECKED);
  lv_obj_set_gesture_bubble(transfer_switch,false);
  if(transfer_enabled())lv_obj_add_state(transfer_switch,LV_STATE_CHECKED);
  lv_obj_add_event_cb(transfer_switch,[](lv_event_t *e){
    auto target=(lv_obj_t *)lv_event_get_target(e);
    bool on=lv_obj_has_state(target,LV_STATE_CHECKED);
    if(!transfer_enable(on))lv_obj_remove_state(target,LV_STATE_CHECKED);
    last_second=UINT32_MAX;
  },LV_EVENT_VALUE_CHANGED,nullptr);
  auto hint = text(overlay, "上滑收起", 0, 445, &clock_cjk_16, MUTED);
  center_text(hint, 480);
  last_network_revision = UINT32_MAX;
}
void build_timeout() {
  overlay_base(Overlay::Timeout, "亮屏时长", Action::Control);
  const uint16_t options[] = {30, 60, 120, 300, 0};
  for (unsigned i = 0; i < 5; i++) {
    char s[48];
    snprintf(s, sizeof(s), options[i] ? "%u 秒" : "常亮", options[i]);
    if (preferences().timeout_seconds == options[i])
      strcat(s, "   · 已选");
    button(
        overlay, s, 24, 88 + i * 69, 432, 57,
        [](lv_event_t *e) {
          auto p = preferences();
          p.timeout_seconds = (uintptr_t)lv_event_get_user_data(e);
          preferences_set(p);
          request(Action::Control);
        },
        (void *)(uintptr_t)options[i]);
  }
}
void choose_network(lv_event_t *e) {
  unsigned i = (uintptr_t)lv_event_get_user_data(e);
  auto net = network_snapshot();
  if (i >= net.count)
    return;
  // Rows carry a stable copy in their label; don't select a different AP after
  // a new scan.
  auto row = (lv_obj_t *)lv_event_get_target(e);
  auto label = lv_obj_get_child(row, 0);
  const char *name = lv_label_get_text(label);
  const AccessPoint *ap = nullptr;
  for (unsigned j = 0; j < net.count; j++)
    if (strcmp(net.aps[j].ssid, name) == 0)
      ap = &net.aps[j];
  if (!ap)
    return;
  if (!ap->supported) {
    text_update(wifi_message, "暂不支持企业认证网络");
    return;
  }
  snprintf(selected_ssid, sizeof(selected_ssid), "%s", ap->ssid);
  selected_secure = ap->secure;
  request(Action::Password);
}
void render_wifi(const NetworkSnapshot &net) {
  if (!wifi_list)
    return;
  lv_obj_clean(wifi_list);
  for (unsigned i = 0; i < net.count; i++) {
    auto row = button(wifi_list, "", 0, i * 70, 432, 62, choose_network,
                      (void *)(uintptr_t)i);
    auto name = text(row, net.aps[i].ssid, 16, 9, &clock_cjk_16);
    lv_obj_set_width(name, 320);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    char info[72];
    snprintf(info, sizeof(info), "%s · %s",
             net.aps[i].rssi > -60   ? "信号强"
             : net.aps[i].rssi > -75 ? "信号中"
                                     : "信号弱",
             net.aps[i].supported
                 ? (net.aps[i].secure ? "需要密码" : "开放网络")
                 : "企业认证");
    text(row, info, 16, 35, &clock_cjk_16, MUTED);
    text(row, LV_SYMBOL_WIFI, 381, 18, &lv_font_montserrat_24, MUTED);
  }
}
void build_wifi() {
  overlay_base(Overlay::Wifi, "Wi-Fi", Action::Control);
  wifi_message = text(overlay, "", 24, 87, &clock_cjk_16, MUTED);
  lv_obj_set_width(wifi_message, 432);
  wifi_title = text(overlay, "", 24, 119, &clock_cjk_16);
  lv_obj_set_width(wifi_title, 432);
  lv_label_set_long_mode(wifi_title, LV_LABEL_LONG_DOT);
  auto refresh =
      button(overlay, "搜索网络", 24, 157, 208, 48, [](lv_event_t *) {
        if (network_snapshot().enabled)
          network_scan();
        else
          network_enable(true);
      });
  (void)refresh;
  button(overlay, "忘记网络", 248, 157, 208, 48,
         [](lv_event_t *) { network_forget(); });
  wifi_list = box(overlay, 24, 220, 432, 233, 0x101622);
  lv_obj_set_scrollable(wifi_list, true);
  lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
  lv_obj_set_gesture_bubble(wifi_list, false);
  lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_AUTO);
  last_network_revision = UINT32_MAX;
}
void submit_password() {
  const char *pwd = selected_secure ? lv_textarea_get_text(password_input) : "";
  if (!network_connect(selected_ssid, pwd)) {
    text_update(password_message, "密码需为 8-63 字符，请检查后重试");
    return;
  }
  if (password_input)
    lv_textarea_set_text(password_input, "");
  request(Action::Wifi);
}
void build_password() {
  overlay_base(Overlay::Password, "加入网络", Action::Wifi);
  auto name = text(overlay, selected_ssid, 24, 87, &clock_cjk_16);
  lv_obj_set_width(name, 432);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  password_message =
      text(overlay, selected_secure ? "请输入密码" : "此网络无需密码", 24, 123,
           &clock_cjk_16, MUTED);
  if (selected_secure) {
    password_input = lv_textarea_create(overlay);
    lv_obj_set_pos(password_input, 24, 152);
    lv_obj_set_size(password_input, 352, 51);
    lv_textarea_set_one_line(password_input, true);
    lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_max_length(password_input, 63);
    lv_obj_set_style_text_font(password_input, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(password_input, lv_color_hex(CARD), 0);
    lv_obj_set_style_text_color(password_input, lv_color_white(), 0);
    lv_obj_set_style_border_width(password_input, 1, 0);
    lv_obj_set_style_border_color(password_input, lv_color_hex(BLUE), 0);
    button(overlay, "显示", 384, 152, 72, 51, [](lv_event_t *) {
      lv_textarea_set_password_mode(
          password_input, !lv_textarea_get_password_mode(password_input));
    });
    keyboard = lv_keyboard_create(overlay);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 8, 266);
    lv_obj_set_size(keyboard, 464, 206);
    lv_keyboard_set_textarea(keyboard, password_input);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x101622), 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x303B50), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard, 4, 0);
    lv_obj_set_style_pad_column(keyboard, 5, 0);
    lv_obj_set_style_pad_row(keyboard, 7, 0);
    lv_obj_set_gesture_bubble(keyboard, false);
    lv_obj_add_event_cb(
        keyboard,
        [](lv_event_t *e) {
          if (lv_event_get_code(e) == LV_EVENT_READY)
            submit_password();
          else if (lv_event_get_code(e) == LV_EVENT_CANCEL)
            request(Action::Wifi);
        },
        LV_EVENT_ALL, nullptr);
  }
  button(
      overlay, "连接", 24, 214, 432, 44,
      [](lv_event_t *) { submit_password(); }, nullptr, BLUE);
}
void update_transfer() {
  if(mode!=Overlay::Control||!transfer_switch)return;
  auto t=transfer_snapshot();auto n=network_snapshot();
  if(t.enabled)lv_obj_add_state(transfer_switch,LV_STATE_CHECKED);
  else lv_obj_remove_state(transfer_switch,LV_STATE_CHECKED);
  if(t.stopping)lv_obj_add_state(transfer_switch,LV_STATE_DISABLED);
  else lv_obj_remove_state(transfer_switch,LV_STATE_DISABLED);
  char title[64],detail[128];
  if(t.busy)snprintf(title,sizeof(title),"文件传输 %u%%",t.progress);
  else snprintf(title,sizeof(title),"文件传输");
  if(t.running&&n.state==NetState::Connected)snprintf(detail,sizeof(detail),"http://%s · 码 %s",n.ip,t.code);
  else snprintf(detail,sizeof(detail),"%s",t.message);
  text_update(transfer_title,title);text_update(transfer_detail,detail);
  lv_obj_set_style_text_color(transfer_detail,lv_color_hex(t.running?GREEN:MUTED),0);
}
void update_network() {
  auto n = network_snapshot();
  lv_obj_set_style_text_color(
      status_net, lv_color_hex(n.state == NetState::Connected ? GREEN : MUTED),
      0);
  if (n.revision == last_network_revision)
    return;
  last_network_revision = n.revision;
  if (mode == Overlay::Control) {
    lv_obj_set_style_bg_color(wifi_switch,
                              lv_color_hex(n.enabled ? BLUE : CARD), 0);
    lv_obj_set_style_text_font(
        wifi_title,
        n.state == NetState::Connected ? &clock_cjk_16 : &pocket_text_24, 0);
    text_update(wifi_title, n.state == NetState::Connected ? n.ssid
                                  : n.enabled ? "Wi-Fi 已开启"
                                              : "Wi-Fi 已关闭");
    char detail[96];
    const bool has_ip = n.state == NetState::Connected && n.ip[0];
    lv_obj_set_y(wifi_detail, has_ip ? 70 : 76);
    lv_obj_set_style_text_font(wifi_detail,
                              has_ip ? &pocket_text_24 : &clock_cjk_16, 0);
    if (n.state == NetState::Connected && n.ip[0]) {
      snprintf(detail, sizeof(detail), "%s", n.ip);
    } else {
      snprintf(detail, sizeof(detail), "%s",
               n.enabled && n.message[0] ? n.message : "未连接");
    }
    text_update(wifi_detail, detail);
    lv_obj_set_style_text_color(wifi_detail,
                                lv_color_hex(n.enabled ? 0xD7E7FF : MUTED), 0);
  } else if (mode == Overlay::Wifi) {
    text_update(wifi_message, n.message);
    char s[96];
    snprintf(s, sizeof(s), "%s%s",
             n.state == NetState::Connected    ? "已连接："
             : n.state == NetState::Connecting ? "正在连接："
                                               : "已保存：",
             n.state == NetState::Connected || n.state == NetState::Connecting
                 ? n.ssid
                 : n.saved_ssid);
    text_update(wifi_title, s);
    render_wifi(n);
  }
}
void timer(lv_timer_t *) {
  if (pending != Action::None) {
    Action a = pending;
    pending = Action::None;
    switch (a) {
    case Action::Home:
      build_home();
      break;
    case Action::OpenApp:
      build_app(requested_app);
      break;
    case Action::Control:
      build_control();
      break;
    case Action::Wifi:
      build_wifi();
      break;
    case Action::Password:
      build_password();
      break;
    case Action::Timeout:
      build_timeout();
      break;
    case Action::Close:
      dismiss_overlay();
      break;
    default:
      break;
    }
    lv_obj_move_foreground(shield);
    last_second = UINT32_MAX;
  }
  uint32_t second = lv_tick_get() / 1000;
  if (second != last_second) {
    last_second = second;
    tm t{};
    clock_local(&t);
    char s[16] = "--:--";
    if (clock_valid())
      snprintf(s, sizeof(s), "%02d:%02d", t.tm_hour, t.tm_min);
    text_update(status_time, s);
    update_battery(battery_value, battery_icon);
    update_battery(overlay_battery_value, overlay_battery_icon);
    if (active_app && active_app->tick && mode == Overlay::None && !asleep)
      active_app->tick();
    update_network();
    update_transfer();
  }
  auto p = preferences();
  if (!asleep && p.timeout_seconds &&
      lv_display_get_inactive_time(lv_display_get_default()) >
          p.timeout_seconds * 1000U) {
    asleep = true;
    lv_obj_set_hidden(shield, false);
    lv_obj_move_foreground(shield);
  }
  apply_light();
}
} // namespace
void ui_start() {
  screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_scrollable(screen, false);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
  lv_screen_load(screen);
  lv_obj_add_event_cb(screen, gesture, LV_EVENT_GESTURE, nullptr);
  build_home();
  shield = box(screen, 0, 0, 480, 480, 0x000000);
  lv_obj_set_clickable(shield, true);
  lv_obj_set_hidden(shield, true);
  lv_obj_set_gesture_bubble(shield, false);
  lv_obj_add_event_cb(
      shield,
      [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
          asleep = false;
          waking = true;
          lv_obj_set_style_bg_opa(shield, LV_OPA_TRANSP, 0);
          apply_light();
        } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
          waking = false;
          lv_obj_set_hidden(shield, true);
          lv_obj_set_style_bg_opa(shield, LV_OPA_COVER, 0);
          lv_display_trigger_activity(lv_display_get_default());
        }
      },
      LV_EVENT_ALL, nullptr);
  lv_timer_create(timer, 25, nullptr);
  timer(nullptr);
}
void ui_home() {
  asleep = false;
  waking = false;
  lv_obj_set_hidden(shield, true);
  lv_display_trigger_activity(lv_display_get_default());
  apply_light();
  request(Action::Home);
}
void ui_open_app(unsigned i) {
  requested_app = i;
  request(Action::OpenApp);
}
void ui_control() { request(Action::Control); }
void ui_wifi() { request(Action::Wifi); }
const char *ui_page() {
  if (mode == Overlay::Control)
    return "control";
  if (mode == Overlay::Wifi)
    return "wifi";
  if (mode == Overlay::Password)
    return "password";
  if (mode == Overlay::Timeout)
    return "timeout";
  return active_app ? active_app->id : "home";
}
} // namespace pocket
