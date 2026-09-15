# Desk Clock

ESP-IDF firmware for the Waveshare ESP32-C6-Touch-AMOLED-2.16 board.

## Current Features

- 480 x 480 AMOLED clock with a unified Source Han Sans display, Gregorian
  date, weekday, and lunar date.
- Local Chinese lunar conversion and 24 solar terms for 1902 through 2099.
- Traditional lunar festivals and common Gregorian holidays.
- Full Gregorian month view with lunar day, solar term, or festival in each
  cell.
- Shared I2C support for AXP2101, PCF85063, and CST9220.
- PCF85063 startup time and SNTP synchronization.
- Compile-time primary/backup Wi-Fi credentials with automatic failover,
  reconnect, and SNTP startup after a network is available.
- Electricity page with today, current week, current month, and remaining kWh.
- Automatic electricity refresh every hour plus a manual refresh.
- NVS persistence for local clock preferences.
- Automatic four-way screen rotation based on the onboard QMI8658
  accelerometer, including touch-coordinate rotation.
- Automatic day and night brightness.
- Two-pixel QSPI redraw alignment to prevent sheared or slanted partial updates.
- A rounded-screen safe layout for the calendar controls and grid.
- A dense clock layout with a 96 px time display and a full-width seven-day
  Gregorian and lunar strip.

## Controls

- Swipe left or right anywhere on the screen to cycle through the clock,
  calendar, and electricity pages.
- Use `<` and `>` to change months.
- Swipe down from any main page to open the control screen, or use the gear
  button on the electricity page.
- The control panel opens immediately. Tap or drag anywhere on the pill-shaped
  brightness track, then swipe up to return. Device battery percentage is shown
  below.
- Press KEY (GPIO10) as an optional hardware shortcut to toggle between the
  main pages.
- Rotate the device in 90-degree steps; the display and touch input follow the
  current upright orientation after a short debounce.

## Wi-Fi Configuration

Wi-Fi is hardcoded at build time. To change the primary and backup networks,
edit these values in `components/wifi_manager/include/wifi_credentials.h`:

```c
#define DESK_CLOCK_WIFI_PRIMARY_SSID "your-primary-ssid"
#define DESK_CLOCK_WIFI_PRIMARY_PASSWORD "your-primary-password"
#define DESK_CLOCK_WIFI_BACKUP_SSID "your-backup-ssid"
#define DESK_CLOCK_WIFI_BACKUP_PASSWORD "your-backup-password"
```

The device tries the primary network first. After three failed connection
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
| `components/energy_service/` | Electricity API client and cached usage data |
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
directions in all four rotations (728 value changes), changes months 96 times,
and refreshes every page on the real panel. It checks LVGL/system heap
integrity and requires completed panel transfers. Allow about 90 seconds and
look for `UI SELF TEST PASS`, with no watchdog or DMA timeout errors.

The test restores the loaded settings. Disable the diagnostic option and
build/flash again for normal use. Physical touch gestures and extended uptime
still need device testing. See `audit/2026-09-15-review.md` for the review.

## Electricity page

The electricity page highlights today's consumption in green, with remaining,
weekly and monthly kWh below. A blue status label distinguishes refreshing,
success, partial success and failure; the footer shows the last successful
summary update time. Failed requests retain the previous readings. The bottom
refresh button has a 432×70 pixel touch target and ignores repeated requests
while an update is pending. Swipe down to reach brightness controls.

`components/clock_ui/energy_view.cpp` owns the layout. Its headless test uses the
same LVGL version and fonts as the firmware:

```bash
bash host_tests/run-energy-view-tests.sh
```

The test checks empty readings, success, busy/disabled, failed and partial
states, numeric bounds, callback dispatch, and heap integrity. It also writes a
480×480 PPM of the actual layout. A PNG reference is in `docs/energy-view.png`.

## 本地语音助手

小智页面直接连接本地 Home Assistant Assist。点击说话开始录音，再点一次结束，
最长 8 秒；识别结果和回复显示在页面中，Piper 语音由板载扬声器播放。
麦克风与扬声器采用轮流工作的方式，播放回复时不录音；暂不支持唤醒词和语音打断。

构建前将 `components/voice_service/include/voice_credentials.example.h` 复制为同目录的
`voice_credentials.h`，填入 HA 地址、中文 Assist pipeline ID 和设备专用访问令牌。
该凭据文件被 Git 忽略。HA 必须已配置 Whisper、Piper，且设备已向 Assist 暴露。
部署与测试记录见 `audit/2026-09-15-local-voice.md`。

ESP-IDF 5.5.2 的 WebSocket 接收有缓存轮询缺陷：HA 授权消息与 HTTP 101
一同到达时可能被搁置，导致连接超时。构建通过
`cmake/fix_ws_buffered_read.cmake` 修正轮询和帧头读取两处逻辑，只编译项目内
生成的副本，不改共享 IDF。升级 IDF 后若补丁匹配失败，需要重新审查这两处。

构建后运行 `python3 host_tests/test_ws_buffered_read.py` 验证实际编译的读取逻辑。
开启 `CONFIG_DESK_CLOCK_VOICE_SELF_TEST` 可测试麦克风、播报，以及三次预录的
“客厅灯开着吗”查询；日常固件应保持关闭。

## 手动设备页

左右滑动到“设备”（小智与耗电之间），点击客厅灯、卧室灯、屏幕挂灯或空调卡片切换电源。
绿色表示已开启，灰色表示已关闭；操作后回读 HA 确认状态。右上角可刷新，停留时每 15 秒自动同步。
空调目前仅控制电源，详细模式与温度仍在 HA 设置。

### 本地凭据

首次编译前，将 WiFi、耗电服务和语音服务各自 `include/` 目录下的
`*_credentials.example.h` 复制为 `*_credentials.h` 并填写本地配置。
实际凭据文件已加入忽略规则，不随代码提交；现有本地配置保持可用。
