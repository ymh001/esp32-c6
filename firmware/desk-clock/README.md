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
- The control panel slides and fades in. Tap or drag anywhere on the pill-shaped
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
