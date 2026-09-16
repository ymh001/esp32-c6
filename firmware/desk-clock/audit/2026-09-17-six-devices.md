# Six-device list and ten-minute screen polling

Added current HA climate.cuco_cp6_56e3_air_conditioner and
switch.cuco_v3_6c00_switch to the shared catalog. Verified they are the AC climate
entity (turn_on/off supported) and printer power outlet, not indicator/protection
switches. Existing first four controls are retained; two additional rows scroll.

Updated existing UI tests to use catalog count; six controls, pointer drag rejection,
row noninteraction and fixed navigation tests passed. Firmware compiled and flashed.
Board boot completed HA device refresh and Grafana energy query.

Screen automatic energy read interval is now 600,000ms. Startup fetch and manual
refresh remain immediate. This changes screen polling, not NAS website collection.
At the first boot after midnight, NAS returned null for today's fields; the screen
correctly shows missing values instead of reusing yesterday. Remaining cost loaded.
