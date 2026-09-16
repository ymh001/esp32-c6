# Desk Clock

ESP-IDF firmware for the Waveshare ESP32-C6-Touch-AMOLED-2.16 board.

## Current Features

- 480 × 480 AMOLED clock, fixed 24-hour display, Gregorian and lunar week strip.
- Three pages: clock, manual HA devices, Grafana electricity. Voice is removed.
- Six manual switches: living-room light, bedroom light, screen lamp, fan, air
  conditioner and printer. Scroll the list; only the separate switches send commands.
- Grafana supplies today's kWh, today's cost and remaining credit.
- ESP32-C6 dynamic CPU frequency: 40–160 MHz. Automatic light sleep is disabled.
- One global sync interval: always online, 1 / 5 / 15 / 30 / 60 minutes. Default
  15 minutes. Always-online mode refreshes once per minute.
- Scheduled HA and energy refreshes share Wi-Fi. It stops 30 seconds after the
  last request; manual refresh/control wakes Wi-Fi with a bounded connection wait.
- NVS remembers the last successful Wi-Fi SSID and the control-center settings.
- Automatic screen-off: 5 / 15 / 30 / 60 seconds or never; default 30 seconds.
  Stay awake on external power is enabled by default, using VBUS presence.
- First touch or KEY wakes the screen without triggering a switch or page change.
- Four-way screen rotation, day/night brightness, RTC and SNTP synchronization.

## Controls

- Use the bottom navigation for clock, devices and electricity.
- Swipe horizontally on clock/energy to change pages. The device list scrolls
  vertically; tapping a device name does not toggle it.
- Swipe down on clock/energy to open controls: brightness, sync interval,
  screen-off timeout and stay-awake-on-power. Tap Return or swipe up to close.
- Manual device operations and the energy refresh button wake Wi-Fi as needed.
  Device switches show the last confirmed HA state while Wi-Fi is asleep.
- Press KEY (GPIO10) to cycle pages, or wake an off screen.

## Wi-Fi Configuration

Wi-Fi is hardcoded at build time. To change the primary and backup networks,
edit these values in `components/wifi_manager/include/wifi_credentials.h`:

```c
#define DESK_CLOCK_WIFI_PRIMARY_SSID "your-primary-ssid"
#define DESK_CLOCK_WIFI_PRIMARY_PASSWORD "your-primary-password"
#define DESK_CLOCK_WIFI_BACKUP_SSID "your-backup-ssid"
#define DESK_CLOCK_WIFI_BACKUP_PASSWORD "your-backup-password"
```

The device tries the last successful SSID first (primary on first boot). After three failed connection
attempts it switches to the backup network, alternating between the two on
subsequent failures. Then rebuild and flash. The device no longer starts a
setup hotspot or serves a configuration page.

## Build

The project is tested with the repository-local ESP-IDF 5.5.2 installation.

```bash
export IDF_TOOLS_PATH="$HOME/projects/esp32/.espressif"
source "$HOME/projects/esp32/esp-idf-v5.5.2/export.sh"
idf.py build
```

Existing `sdkconfig` files must set `CONFIG_LV_MEM_SIZE_KILOBYTES=96`;
`sdkconfig.defaults` does not override saved values. The UI now rejects a
build with a smaller pool. Use `idf.py menuconfig` to update an old config.

Host-side lunar tests:

```bash
bash host_tests/run-tests.sh
```

## Flash

Back up the original full flash before the first write. Then:

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

If the ROM loader does not reset automatically, power off, hold BOOT, power on,
and retry.

## Source Layout

| Path | Purpose |
| --- | --- |
| `components/board/` | Display, touch, PMIC, RTC, I2C, and buttons |
| `components/settings/` | NVS-backed configuration |
| `components/time_service/` | Time zone, RTC restore, and SNTP |
| `components/lunar/` | Lunar conversion, solar terms, and festivals |
| `components/wifi_manager/` | Primary/backup Wi-Fi credentials, failover, and station reconnection |
| `components/energy_service/` | Grafana read-only query client and cached energy/cost data |
| `components/clock_ui/` | LVGL clock and calendar UI |
| `host_tests/` | Host-side calendar tests |

The clock UI uses generated CJK font subsets. After adding any new Chinese
text, run:

```bash
bash tools/generate_clock_fonts.sh
```

The script scans the firmware sources and regenerates all three font files, so
missing-glyph boxes cannot reappear after a UI text change.

## Pending Hardware Verification

- Display orientation and panel offsets.
- Touch coordinate mapping.
- PCF85063 read/write behavior.
- Wi-Fi connection to the configured network on the actual board.
- AXP2101 rail timing and AMOLED brightness.

## Hardware Status 2026-09-14

The first boot after flashing left both I2C lines low. The same problem was
reproduced with the original firmware. After a full PMU power cycle, the bus
came up normally:

```text
I2C idle levels: SCL=1 SDA=1 (ready)
CST9217: Chip Type: 0x9220
```

Display, PMIC, touch, and RTC initialization now complete normally.

The complete 16 MB pre-flash image is stored outside Git at:

```text
firmware_backup/pre-desk-clock-full-20260914.bin
SHA-256 7a806086fa8eebb0fe885a381048b16b60aaa9d05906ecb8904beeb91ce2b9c2
```

The desk-clock firmware connects directly to the primary/backup SSIDs
configured in `components/wifi_manager/include/wifi_credentials.h`.

The generated DOT Matrix and Chinese subset fonts are stored under
`components/clock_ui/fonts/` with their OFL license files.

## UI regression test

In `idf.py menuconfig`, enable **Desk clock diagnostics → Run UI stress test
at boot**, then build and flash. The test sweeps 10–100% brightness in both
directions in all four rotations (728 value changes), changes pages 96 times,
and refreshes every page on the real panel. It checks LVGL/system heap
integrity and requires completed panel transfers. Allow about 90 seconds and
look for `UI SELF TEST PASS`, with no watchdog or DMA timeout errors.

The test restores the loaded settings. Disable the diagnostic option and
build/flash again for normal use. Physical touch gestures and extended uptime
still need device testing. See `audit/2026-09-15-review.md` for the review.

## 手动设备与凭据

设备页保留客厅灯、卧室灯、屏幕挂灯、风扇、空调、打印机，使用独立开关，
指令发送后回读 HA 确认状态。进入设备页和点击刷新会立即更新；周期更新由全局同步间隔控制。
主动关闭 WiFi 时保留上次状态。卡片与滑动操作不会触发开关。

设备目录：`components/ha_devices/include/device_catalog.h`，追加条目后列表自动滚动扩展。
将 `components/ha_devices/include/ha_credentials.h.example` 复制为 `ha_credentials.h`，
填写 HA HTTPS 地址和访问令牌。实际凭据文件被 Git 忽略。
原有本地语音配置已迁移至该私有文件，固件不再依赖 Assist、Whisper 或 Piper。

## 耗电页（Grafana）

只展示今日耗电、今日电费、剩余电费，使用 Grafana HTTPS 只读查询 NAS 数据库，
不携带电量网站凭据，不自行计算金额。跟随全局同步间隔，刷新按钮立即唤醒联网读取。
失败保留旧数据；超过两小时的源数据标记为较旧，跨日未更新时今日项为空。
模板为 `components/energy_service/include/grafana_credentials.example.h`，部署契约见
仓库 `deploy/grafana-screen/`。HA 与 Grafana HTTPS 均校验服务器证书。

运行 `bash host_tests/run-energy-view-tests.sh` 验证实际 LVGL 能源与设备布局、
状态显示、刷新回调和设备开关滑动防误触。`bash host_tests/run-tests.sh` 还验证网络
同步档位、并发使用互斥、延迟关闭、临时唤醒及连接超时后的重试。

实机验证记录见 `audit/2026-09-17-power-saving.md`。
