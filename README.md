# ESP32-C6 Touch AMOLED 2.16

This directory is the isolated workspace for the Waveshare
`ESP32-C6-Touch-AMOLED-2.16` board.

The first firmware project is `firmware/desk-clock/`. It contains the working
ESP-IDF clock, touch UI, local settings, hardcoded Wi-Fi connection, lunar
calendar, NTP time synchronization, electricity usage monitoring, and
brightness controls.

## Layout

| Path | Purpose |
| --- | --- |
| `hardware/` | Board facts, pin map, source links, and known hardware constraints |
| `firmware/` | Candidate catalog and one self-contained directory per firmware |

## Firmware Rules

- Do not place a project directly in `firmware/`; create one directory per
  independent firmware under it.
- Keep each firmware project's source, configuration, notes, and build
  instructions together.
- Do not share generated `build/`, `sdkconfig`, dependency downloads, or logs
  across firmware projects.
- Keep vendor examples named explicitly, for example
  `firmware/waveshare-idf-lvgl-v9/`.
- Keep original and recovered flash images under the existing top-level
  `../firmware_backup/` tree, grouped by board and firmware.
- Keep Wi-Fi credentials in the dedicated credentials header, never in logs or
  documentation.

## Start Here

Read
[`hardware/WAVESHARE_ESP32_C6_TOUCH_AMOLED_2_16_HARDWARE.md`](hardware/WAVESHARE_ESP32_C6_TOUCH_AMOLED_2_16_HARDWARE.md)
before selecting or porting firmware. See
[`firmware/CANDIDATE_PROJECTS.md`](firmware/CANDIDATE_PROJECTS.md) for the
current survey of official and community projects.
