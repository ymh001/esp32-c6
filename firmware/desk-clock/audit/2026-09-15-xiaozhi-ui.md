# Xiaozhi UI and energy layout

Replaced the full calendar page with a Xiaozhi page. Clock week strip remains.
Navigation order: clock, Xiaozhi, energy. Swipe and brightness drawer preserved.
Xiaozhi is currently a UI shell: the speak button explicitly reports that voice
service is not connected; no recording or HA action is simulated.

Energy metrics now appear as week, month, remaining balance. The remaining
balance uses a bordered dark card; text remains white and today's usage green.

Validation: ESP-IDF build and flash to /dev/cu.usbmodem101 succeeded; energy
state tests passed; both views rendered with the real LVGL implementation and
were visually inspected. Images: docs/xiaozhi-view.png and docs/energy-view.png.
