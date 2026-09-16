# Scrollable device list and HA entity migration

Device catalog is now shared by the backend and UI and derives its count from the
entries. The four configured targets are living-room light, bedroom light, screen
lamp and fan, verified against the current HA entity registry. Domains are explicit
per target, so the fourth entry now uses fan.turn_on/off rather than climate.

Each row displays name and status; only the separate switch is clickable. Switches
are drawn as buttons with a thumb, preserving actual HA state until command readback.
Pending/unavailable switches are disabled. Pointer movement beyond eight pixels in
either axis cancels activation, and active parent scrolling also prevents commands.
The device page has a vertically scrolling list with fixed header/footer. Global
page/settings gestures are ignored on this page; use bottom navigation to leave.

Host tests use real LVGL pointer input: tapping a row does nothing, horizontal and
vertical drags over a switch do nothing, a stationary tap calls the correct device.
Extra offscreen content scrolls while the header and main screen remain fixed.
Busy/offline/error, navigation integration and existing energy/Xiaozhi renders pass.
Device names use the full CJK font so new catalog names remain renderable.

HA's new entities lacked aliases. Added the four familiar Chinese names and 挂灯
for the screen lamp; all four read-only Chinese conversation queries returned
query_answer. Existing Chinese Whisper/Piper pipeline remains valid, so no speech
protocol changes were needed and no appliance toggles were used for these checks.
