# Desk Clock

ESP-IDF firmware for the Waveshare ESP32-C6-Touch-AMOLED-2.16 board.

## Current Features

- 480 x 480 AMOLED clock with a Dot Matrix time font, Gregorian date, weekday,
  and lunar date.
- Local Chinese lunar conversion and 24 solar terms for 1902 through 2099.
- Traditional lunar festivals and common Gregorian holidays.
- Full Gregorian month view with lunar day, solar term, or festival in each
  cell.
- Shared I2C support for AXP2101, PCF85063, and CST9220.
- PCF85063 startup time and SNTP synchronization.
- SoftAP setup portal at `192.168.4.1`.
- Captive-portal DNS and redirect endpoints that open the setup page
  automatically on phones and desktop systems.
- Visible Wi-Fi scan results, manual SSID fallback, time zone, 24-hour format,
  brightness, and manual time setup.
- NVS persistence with no hardcoded Wi-Fi credentials.
- Automatic day and night brightness.
- Two-pixel QSPI redraw alignment to prevent sheared or slanted partial updates.
- A rounded-screen safe layout for the calendar controls and grid.
- A dense clock layout with a 96 px Dot Matrix time display and a full-width
  seven-day Gregorian and lunar strip.

## Controls

- Tap the clock to open the calendar.
- Tap the `时钟` button in the calendar to return.
- Use `<` and `>` to change months.
- Press KEY (GPIO10) to toggle between clock and calendar.
- Hold BOOT (GPIO9) for three seconds to open the configuration hotspot.

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
| `components/net_config/` | Wi-Fi connection and web setup portal |
| `components/clock_ui/` | LVGL clock and calendar UI |
| `host_tests/` | Host-side calendar tests |

## Pending Hardware Verification

- Display orientation and panel offsets.
- Touch coordinate mapping.
- PCF85063 read/write behavior.
- Wi-Fi scan and connection on the actual board.
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

The desk-clock firmware is flashed and enters the `DeskClock-XXXX`
configuration hotspot.

The generated DOT Matrix and Chinese subset fonts are stored under
`components/clock_ui/fonts/` with their OFL license files.
