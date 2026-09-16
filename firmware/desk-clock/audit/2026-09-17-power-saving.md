# Power-saving firmware, 2026-09-17

Branch: `feature/power-saving`.

## Behavior

- CPU PM enabled, min 40 / max 160 MHz, automatic light sleep disabled.
- Clock always uses 24-hour format, including migration of saved 12-hour settings.
- Voice page, service, codec support and voice fixtures removed. HA credentials
  moved to a separate ignored header; no credentials enter version control.
- ALDO1/2 (microphone/speaker) disabled. Panel/touch power retained.
- Shared global HA/Grafana schedule: 0 (always online), 1, 5, 15, 30, 60 minutes;
  default 15. Always-online uses a one-minute schedule.
- Recursive network leases keep WiFi alive during HTTP batches/commands and
  confirmation reads. Connection wait bounded to 30 seconds. WiFi stops after
  30 seconds without an owner. Commands/refreshes can wake it immediately.
- Last successful SSID stored in NVS on change; looked up by name at reconnect.
- Screen timeout 5/15/30/60 seconds or never; default 30. External-power exemption
  enabled by default, uses VBUS presence. First touch is consumed until release.
- Background refresh does not count as input. Off-screen clock/device repaint
  and orientation polling pause. Last confirmed device state survives WiFi sleep.

## Verification

- ESP-IDF 5.5.2 full build and incremental final build passed.
- Binary 0x55ff80 bytes, app partition 0x600000, 10% free.
- Host lunar, orientation, battery tests passed.
- Real network coordinator tested with fake clock/WiFi/FreeRTOS: every interval,
  nested leases, a different task attempting idle shutdown during a batch,
  manual wake, always-online mode, bounded connection failure and recovery.
- Real LVGL energy and device tests passed, including switch drag suppression.
- App-only flash to `/dev/cu.usbmodem101`, hash verified; existing NVS preserved.
- Board PM log: CPU_MAX 160, APB_MAX 80, APB_MIN 40, light sleep disabled.
- Board settings log: 24-hour clock; 15-minute sync; 30-second screen-off;
  plugged-awake enabled.
- Board obtained IP, synchronized NTP, read all six HA devices successfully.
- Grafana: today 0.31 kWh / 0.34 yuan, remaining 127.59 yuan, stale=false.
- Last request finished at 14.919 s; WiFi stopped at 45.008 s.
- LVGL: 39% used, 57,372 bytes free; no panic/watchdog in captured boot log.
- Later UI activity changed brightness to 95%; two main-loop LVGL lock attempts
  timed out and returned. Serial USB disconnected near 77 seconds, so the
  planned 100-second capture did not complete. Cause pending user confirmation.
- Board was USB powered; auto screen-off on battery and physical first-touch
  behavior were not exercised. Power draw/long-term endurance remain unmeasured.

Logs in the local session: `/tmp/power-release-build.log`, `/tmp/power-flash.log`,
`/tmp/power-board.log`. No NAS containers or home appliance states were modified.
