#include "../core/energy.h"
#include "../core/services.h"
#include "../ui/widgets.h"
#include "apps.h"
#include <cstdio>
namespace pocket {
namespace {
lv_obj_t *usage, *cost, *balance, *status, *hint;
}
void energy_tick() {
  if (!usage)
    return;
  auto e = energy_snapshot();
  bool online = network_snapshot().state == NetState::Connected;
  time_t now = time(nullptr);
  bool stale = !e.updated || now - e.updated > 600 || now < e.updated - 60;
  text_update(status, !online      ? "未联网"
                            : e.failed   ? "更新失败"
                            : !e.fetched ? "加载中"
                            : stale      ? "数据较旧"
                                         : "已更新");
  char s[96];
  // Do not label yesterday's cached total as today's consumption after
  // midnight.
  tm source{}, current{};
  localtime_r(&e.fetched, &source);
  localtime_r(&now, &current);
  bool today = e.fetched && source.tm_year == current.tm_year &&
               source.tm_yday == current.tm_yday;
  if ((e.valid & 1) && today)
    snprintf(s, sizeof(s), "%.2f", e.kwh);
  else
    snprintf(s, sizeof(s), "--");
  text_update(usage, s);
  if ((e.valid & 2) && today)
    snprintf(s, sizeof(s), "%.2f 元", e.cost);
  else
    snprintf(s, sizeof(s), "-- 元");
  text_update(cost, s);
  if (e.valid & 4)
    snprintf(s, sizeof(s), "%.2f 元", e.balance);
  else
    snprintf(s, sizeof(s), "-- 元");
  text_update(balance, s);
  char stamp[32] = "--";
  if (e.updated) {
    tm t{};
    localtime_r(&e.updated, &t);
    strftime(stamp, sizeof(stamp), "%m-%d %H:%M", &t);
  }
  snprintf(s, sizeof(s), "电价 1.10 元/度 · 同步 %s", stamp);
  text_update(hint, s);
}
void energy_close() { usage = cost = balance = status = hint = nullptr; }
void energy_create(lv_obj_t *r) {
  text(r, "家庭耗电", 28, 48);
  auto badge = box(r, 342, 48, 112, 32, 0x25362F, 12);
  status = text(badge, "加载中", 0, 5, &clock_cjk_16, GREEN);
  center_text(status, 112);
  auto hero = box(r, 24, 108, 432, 166, 0x132C28, 24);
  text(hero, "今日耗电", 22, 18, &pocket_text_24, GREEN);
  usage = text(hero, "--", 22, 56, &energy_digits_64, GREEN);
  lv_obj_set_width(usage, 310);
  lv_label_set_long_mode(usage, LV_LABEL_LONG_DOT);
  text(hero, "kWh", 340, 108, &pocket_text_24, 0xB6D2C6);
  const char *titles[] = {"今日电费", "剩余电费"};
  for (int i = 0; i < 2; i++) {
    auto card = box(r, 24 + i * 224, 288, 208, 103, CARD, 20);
    text(card, titles[i], 18, 15, &clock_cjk_16, MUTED);
    auto value = text(card, "-- 元", 18, 48);
    lv_obj_set_width(value, 178);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    if (i == 0)
      cost = value;
    else
      balance = value;
  }
  hint = text(r, "", 0, 408, &clock_cjk_16, MUTED);
  center_text(hint, 480);
  energy_tick();
}
} // namespace pocket
