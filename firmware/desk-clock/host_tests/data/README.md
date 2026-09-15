# Voice regression fixture

`ha_query.pcm` is Piper-generated speech saying “客厅灯开着吗” (is the living-room
light on?), 16 kHz, signed little-endian 16-bit mono, without a WAV header.
It asks for state only and contains no recorded personal speech.

`CONFIG_DESK_CLOCK_VOICE_SELF_TEST=y` embeds this fixture for three on-board
Assist/Whisper/intent/Piper round trips at boot, after testing microphone capture.
The default production build excludes both the fixture and automatic queries.

`ha_lamp_on.pcm` / `ha_lamp_off.pcm` use the same format and say “打开屏幕挂灯” /
“关闭屏幕挂灯”. These change the real lamp and require explicit owner authorization
plus `CONFIG_DESK_CLOCK_VOICE_LAMP_TEST=y`. Use an even number of turns and verify
the lamp's final state. The owner authorized these commands on 2026-09-16.

`CONFIG_DESK_CLOCK_VOICE_SELF_TEST_RUNS` sets the number of turns (default 3).
The self-test also shows the Xiaozhi page to exercise actual display rendering.
