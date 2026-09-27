idf_component_register(
 SRCS "board.cpp" "sd_card.cpp" "sd_files.cpp" "display.cpp" "power.cpp" "rtc.cpp" "imu.cpp" "orientation_math.cpp" "battery_math.cpp"
 INCLUDE_DIRS "include"
 REQUIRES esp_lcd waveshare__esp_lcd_touch_cst9217
 PRIV_REQUIRES fatfs sdmmc esp_driver_sdspi esp_driver_gpio esp_driver_i2c esp_driver_spi esp_timer espressif__esp_lcd_sh8601)
