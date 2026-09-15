# Local voice implementation

## Home Assistant

- Added Wyoming entries for 192.168.3.2:10300 (faster-whisper) and :10200 (piper).
- Created and selected `小智本地语音`, ID `01m2js6vbkvvtc2ef42e3xfpp2`.
- Conversation Home Assistant/zh; STT faster_whisper/zh; TTS piper/zh_CN,
  zh_CN-huayan-medium. Existing English pipeline retained.
- Added aliases to existing exposed primary entities for 客厅灯、卧室灯、屏幕挂灯、
  空调/卧室空调、窗帘/卧室窗帘、风扇/卧室风扇、空气净化器. Existing aliases retained.
- Whisper Compose command now includes simplified Chinese/domain vocabulary prompt.
  Without it a synthetic query transcribed into traditional Chinese and HA did not
  match the intent. With it, `客厅灯开着吗?` returned a query_answer with the light entity.

## Firmware

- ESP32 connects directly to HA WebSocket Assist, streaming 16kHz/16-bit mono PCM;
  no extra NAS container or cloud ASR.
- Click to begin, click again to finish, at most eight seconds. HA VAD can end early.
- Recognition/intent via Assist; TTS requested as 16kHz mono PCM WAV through HA.
- Fetches TTS by relative path from the configured LAN HA host, ignoring an absolute
  URL that currently advertises the NAS proxy interface (198.18.0.1).
- Handles RIFF chunk scanning and streaming WAV with unknown data length; bounds
  audio to 30 seconds. Network operations have finite timeouts.
- Codec pin/slot configuration follows vendor BoxAudioCodec for this exact board:
  ES7210 capture channel 0, ES8311 output, shared existing I2C bus, I2S pins 19-23.
- UI changes are applied by an LVGL timer; networking/codec work runs outside the UI
  lock. States: idle, connecting, listening, thinking, speaking, error/retry.
- CJK 16px font includes U+4E00–U+9FFF for recognized and response text. Large font
  metadata enabled. Firmware remains inside the 6 MiB application partition.
- Device token in gitignored `components/voice_service/include/voice_credentials.h`;
  sample header documents configuration. No account password embedded.

## Validation

- Firmware build succeeded, host energy tests and Xiaozhi render succeeded.
- Board diagnostic: mic=1, 3200 samples, mean amplitude 7 in ambient silence;
  downloaded readiness WAV and playback write succeeded (playback=1).
- LVGL pool reported 28% used, 68184 bytes free; no watchdog/panic during observation.
- HA synthetic audio pipeline recognized the query and returned actual state without
  changing a light. This does not measure real room speech/noise accuracy.
- Production configuration disables the boot audio self-test.

## HA connection timeout fix

- Reproduced three consecutive on-board greeting timeouts: TCP/WebSocket connected,
  but AUTH_REQUEST was never delivered before the 10-second deadline.
- ESP-IDF 5.5.2 stores bytes coalesced with HTTP 101 in `ws->buffer`, but both
  `ws_poll_read` and `ws_read_header` poll the parent socket without considering
  that buffer. HA waits for authentication while the client waits for more TCP
  bytes. Fixing the outer poll alone still reproduces the timeout at header read.
- `cmake/fix_ws_buffered_read.cmake` compiles a project-local patched source copy:
  report readiness for buffered data and bypass header socket polling when buffered
  bytes exist. The shared IDF tree is untouched. Unexpected source changes fail
  configuration so an IDF upgrade cannot silently drop the fix.
- Regression harness compiles the actual polling and header functions from that
  generated source. Unpatched IDF fails the readiness case; the incomplete fix
  fails header parsing; the complete fix passes buffered and empty-socket cases.
- Failed requests now retain their first error and publish the retryable ERROR state
  only after socket/codec cleanup and releasing `busy`. Previously the button looked
  enabled while cleanup was still running and ignored clicks. Timeouts distinguish
  greeting, authentication, STT startup and processing.
- With both fixes, on-board synthetic audio completed all three STT/intent/TTS
  cycles: final event bits=31 each time (all completion bits, no FAILED=32),
  with 25,821 PCM samples sent each time. Microphone self-test captured 3,200 samples;
  readiness playback succeeded. The fixture is a state question, not an actuator command.
- Host UI tests and transport regression passed. Automatic audio/query diagnostics
  and their PCM fixture are excluded from the normal production build.
- Real button input also exercised error/retry: a one-second recording returned
  `stt-no-text-recognized`, cleanup completed, and the next click started a new STT
  session successfully. A later microphone turn sent 51,520 samples, received
  STT/intent/run completion, played the reply and returned idle (stack high-water
  mark 2,132 bytes). This validates the device path, not transcription accuracy
  for every phrase or room condition.
