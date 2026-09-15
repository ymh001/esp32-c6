# Device page and voice reliability — 2026-09-16

## Device page

Added between Xiaozhi and Energy. Four large cards control living-room light,
bedroom light, screen lamp, and AC power. Green means on; grey means off.
Busy cards cannot be tapped again, unavailable cards require refresh. GET state
runs on entry, explicit refresh and every 15 seconds while visible. Loss of WiFi
marks previous states unavailable; reconnection forces a fresh read.

A queued worker calls explicit light/climate turn_on or turn_off, then reads back
actual state (up to five attempts). It does not optimistically flip the display.
The AC retains HA's own turn_on behavior; no temperature or mode changes are sent.
All requests use the existing ignored HA credentials and 192.168.3.2:8123.

## Voice fixes

Whisper returned a trailing U+FFFD in the synthetic screen-lamp on command.
A bounded normalizer removes only trailing replacement characters and whitespace.
STT and intent now run separately on the same authenticated WebSocket so HA receives
the normalized text. Internal corruption and oversized commands are not guessed.
The UI enters speaking as soon as the intent reply is ready, before TTS download.

The C6 has no PSRAM. Concurrent energy TLS requests reduced available heap enough
to break audio WebSocket writes. A recursive FreeRTOS mutex now serializes energy,
voice and manual HA network operations. Energy uses per-request leases and an
8-second HTTP timeout; foreground tasks have priority 5. Voice holds its lease
through audio playback. Requests remain asynchronous to the LVGL thread.

## Validation

- Host device-view test: all four callback indices, refresh callback, busy/unknown/
  offline/error states, light and climate power parsing, LVGL memory integrity.
- Real LVGL 480x480 preview rendered and visually reviewed.
- Existing lunar/orientation/battery/voice-normalization, energy/Xiaozhi UI and
  generated WebSocket buffered-read regression tests passed.
- Diagnostic board: initial four-device refresh succeeded. Manual screen-lamp on
  and off both accepted=1, confirmed=1; device worker stack reserve 4,332 bytes.
- Ten alternating prerecorded screen-lamp voice commands all completed with bits=95
  (both pipeline runs complete, no FAILED bit), correct on/off replies and roughly
  64–66 KB free heap after each turn. Energy refresh and manual UI activity were
  also active. No spontaneous reboot or transport send failure in this run.
- HA subsequently reported all three lights off, AC still cool.
- Synthetic audio validates protocol and control, not arbitrary room-speech accuracy.
  The earlier reported spontaneous reset was not reproduced; checkpoints remain
  available for diagnosing a future occurrence.
- Production disables automatic voice/lamp diagnostics and their embedded fixtures.

## Production deployment

Production image 0x577380 bytes (9% application partition free) flashed successfully.
Boot reached Desk clock started and completed four-device state refresh. Automatic
UI/voice/lamp diagnostics are disabled. WiFi, energy and HA credentials remain local;
only example configuration headers are tracked.
