#include "diagnostics.h"
#include "../ui/shell.h"
#include "board.h"
#include "board_sd.h"
#include "file_transfer.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "energy.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "services.h"
#include <cstring>
namespace pocket {
namespace {
char command[384];
bool overflow;
unsigned length;
int test_step = -1;
uint32_t next_step;
Preferences original;
void status() {
  board_sd_status();
  auto n = network_snapshot();
  auto b = battery_snapshot();
  ESP_LOGI("diagnostics", "battery_valid=%d percent=%u voltage_mv=%u", b.valid, b.percent, b.voltage_mv);
  auto e = energy_snapshot();
  ESP_LOGI("diagnostics", "minimum_heap=%u", (unsigned)esp_get_minimum_free_heap_size());
  ESP_LOGI("diagnostics",
           "energy_valid=%u failed=%d kwh=%.2f cost=%.2f balance=%.2f "
           "rotation=%d locked=%d",
           e.valid, e.failed, e.kwh, e.cost, e.balance,
           (int)lv_display_get_rotation(lv_display_get_default()) * 90,
           preferences().rotation_locked);
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  ESP_LOGI("diagnostics",
           "page=%s touch=%s wifi=%d scanning=%d aps=%u ip=%s time=%s heap=%u "
           "lvgl_peak=%u "
           "lvgl_free=%u",
           ui_page(), board_display()->touch ? "ready" : "missing",
           (int)n.state, n.scanning, n.count, n.ip,
           clock_synced() ? "synced" : "local",
           (unsigned)esp_get_free_heap_size(), (unsigned)m.max_used,
           (unsigned)m.free_size);
}
void execute() {
  if(!strcmp(command,"transfer-on") || !strcmp(command,"transfer-off") || !strcmp(command,"transfer-status")) {
    if(!strcmp(command,"transfer-on"))transfer_enable(true);
    if(!strcmp(command,"transfer-off"))transfer_enable(false);
    auto t=transfer_snapshot();auto n=network_snapshot();
    ESP_LOGI("transfer","enabled=%d running=%d busy=%d url=http://%s/ code=%s",t.enabled,t.running,t.busy,n.ip,t.enabled?t.code:"--------");
    return;
  }

  if(!strncmp(command,"sd-",3)) {
    if(!strcmp(command,"sd-status")) board_sd_status();
    else if(!strcmp(command,"sd-ls")) board_sd_list();
    else if(!strcmp(command,"sd-test")) board_sd_self_test();
    else if(!strcmp(command,"sd-mount")) board_sd_mount();
    else if(!strcmp(command,"sd-unmount")) board_sd_unmount();
    else if(!strncmp(command,"sd-read ",8)) {
      unsigned char data[512];size_t size=0;
      int err=board_sd_read(command+8,data,sizeof(data),&size);
      if(err)ESP_LOGW("sdcard","Read failed: %s",strerror(err));
      else {
        ESP_LOGI("sdcard","Read %u bytes (maximum 512)",(unsigned)size);
        for(size_t offset=0;offset<size;offset+=16){
          char hex[49]={},ascii[17]={};
          size_t count=size-offset<16?size-offset:16;
          for(size_t i=0;i<count;++i){snprintf(hex+i*3,4,"%02X ",data[offset+i]);ascii[i]=data[offset+i]>=32 && data[offset+i]<127?data[offset+i]:'.';}
          ESP_LOGI("sdcard","%04x  %-48s %s",(unsigned)offset,hex,ascii);
        }
      }
    } else if(!strncmp(command,"sd-write ",9)) {
      char *text=strchr(command+9,' ');
      if(!text)ESP_LOGW("sdcard","Usage: sd-write NEW_FILENAME TEXT");
      else {
        *text++=0;int err=board_sd_write_new(command+9,text,strlen(text));
        ESP_LOGI("sdcard","Write new file: %s",err?strerror(err):"OK (synced)");
      }
    } else ESP_LOGW("sdcard","Commands: sd-status sd-ls sd-test sd-mount sd-unmount sd-read PATH sd-write NEW_PATH TEXT");
    return;
  }
  const char *known[] = {"home",     "clock",   "energy", "control", "wifi",
                         "wifi-off", "wifi-on", "status", "test"};
  bool recognized = false;
  for (const char *name : known)
    if (!strcmp(name, command))
      recognized = true;
  if (!recognized)
    return;
  ESP_LOGI("diagnostics", "Command: %s", command);
  if (!strcmp(command, "home"))
    ui_home();
  else if (!strcmp(command, "clock"))
    ui_open_app(0);
  else if (!strcmp(command, "energy"))
    ui_open_app(1);
  else if (!strcmp(command, "wifi-off"))
    network_enable(false);
  else if (!strcmp(command, "wifi-on"))
    network_enable(true);
  else if (!strcmp(command, "control"))
    ui_control();
  else if (!strcmp(command, "wifi")) {
    network_scan();
    ui_wifi();
  } else if (!strcmp(command, "status"))
    status();
  else if (!strcmp(command, "test") && test_step < 0) {
    original = preferences();
    auto p = original;
    p.timeout_seconds = 0;
    preferences_set(p);
    test_step = 0;
    next_step = lv_tick_get();
    ESP_LOGI("diagnostics", "SELFTEST START");
  }
}
} // namespace
void diagnostics_init() {
  usb_serial_jtag_driver_config_t cfg{.tx_buffer_size = 2048,
                                      .rx_buffer_size = 256};
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
  usb_serial_jtag_vfs_use_driver();
}
void diagnostics_process() {
  char c;
  while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
    if (c == '\n' || c == '\r') {
      command[length] = 0;
      if (overflow) ESP_LOGW("diagnostics","Command too long; discarded");
      else if (length) execute();
      length = 0;overflow=false;
    } else if ((unsigned char)c >= 32 && c != 127) {
      if(length < sizeof(command)-1)command[length++]=c;
      else overflow=true;
    }
  }
  if (test_step < 0 || (int32_t)(lv_tick_get() - next_step) < 0)
    return;
  assert(lv_mem_test() == LV_RESULT_OK);
  assert(heap_caps_check_integrity_all(true));
  if (test_step == 30) {
    preferences_set(original);
    lv_display_set_rotation(lv_display_get_default(), (lv_display_rotation_t)original.locked_rotation);
    ui_home();
    test_step = -1;
    status();
    ESP_LOGI("diagnostics",
             "SELFTEST PASS: 30 screen changes, four rotations and brightness writes");
    return;
  }
  switch (test_step % 5) {
  case 0:
    ui_home();
    break;
  case 1:
    ui_open_app(0);
    break;
  case 2:
    ui_open_app(1);
    break;
  case 3:
    ui_control();
    break;
  case 4:
    ui_wifi();
    break;
  }
  auto p = preferences();
  p.brightness = test_step % 2 ? 70 : 40;
  p.rotation_locked = true;
  lv_display_set_rotation(lv_display_get_default(), (lv_display_rotation_t)(test_step % 4));
  preferences_set(p);
  ++test_step;
  next_step = lv_tick_get() + 350;
}
} // namespace pocket
