# Changelog

## 2026-09-15

### Changed

- Added an electricity page with today, week, month, and remaining kWh.
- Added hourly background refresh and a manual refresh button.
- Added a control page with a brightness slider, opened by a downward swipe or
  the electricity page's gear button.
- Reworked the brightness control as a thick pill track that jumps to the
  touched position and accepts horizontal dragging from anywhere on the track.
- Added a slide/fade transition for the control panel and device battery
  percentage from the AXP2101.
- Replaced the calendar/clock tap targets with left/right swipe navigation and
  cyclic switching across the three main pages.
- Replaced the setup portal and editable Wi-Fi settings with a station-only
  Wi-Fi manager using compile-time credentials.
- Centralized primary/backup SSIDs and passwords in `wifi_credentials.h`.
- Added automatic failover between the primary and backup networks after
  repeated connection failures.
- Added automatic reconnect with capped backoff.
- Start SNTP only after the device obtains a Wi-Fi address.

### Removed

- SoftAP setup hotspot, HTTP configuration server, captive-portal DNS, Wi-Fi
  scanning, NVS Wi-Fi storage, and the BOOT-button configuration entry point.

### Fixed

- Removed full-screen slide animations that overloaded the LVGL render task
  and triggered the task watchdog on rapid swipes.
- Rebuilt calendar cells without retaining stale objects from the previous
  month.
- Replaced the partial CJK font subset with source-generated fonts covering all
  current UI and lunar text, eliminating missing-glyph boxes.

## 2026-09-14

### Added

- Initial ESP-IDF 5.5.2 desk clock firmware.
- Gregorian, Chinese lunar, solar-term, and festival calculations.
- PCF85063 startup time and SNTP synchronization.
- SoftAP setup portal with Wi-Fi scanning and persistent settings.
- Captive-portal DNS and common OS probe redirects.
- Dot Matrix time font and exact Chinese subset fonts.

### Fixed

- Added two-pixel QSPI redraw alignment after the first hardware test showed
  partial clock updates becoming slanted.
- Replaced the incomplete generic CJK font that rendered some characters as
  square glyphs.
- Moved calendar controls and cells inside the rounded AMOLED safe area.
- Replaced the sparse clock composition with a full-screen clock and weekly
  Gregorian/lunar strip.

### Verified

- ESP-IDF build succeeds with the repository-local 5.5.2 toolchain.
- Host-side lunar tests pass.
- Board detects SCL/SDA at idle high and initializes AXP2101, CO5300, and
  CST9220 on hardware.
