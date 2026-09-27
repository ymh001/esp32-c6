idf_component_register(
 SRCS "main.cpp" "core/diagnostics.cpp" "core/services.cpp" "core/energy.cpp" "core/energy_parse.cpp" "core/network.cpp" "ui/port.cpp" "ui/shell.cpp" "ui/widgets.cpp"
      "apps/apps.cpp" "apps/flip_clock.cpp" "apps/energy.cpp"
      "fonts/clock_cjk_16.c" "fonts/pocket_text_24.c" "fonts/flip_tiles.c" "fonts/energy_digits_64.c"
 INCLUDE_DIRS "."
 REQUIRES esp_pm esp_http_client mbedtls json board lvgl__lvgl esp_wifi esp_event esp_netif esp_timer nvs_flash esp_driver_usb_serial_jtag)
