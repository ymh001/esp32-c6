idf_component_register(SRCS "ha_devices.cpp" "device_power.cpp" INCLUDE_DIRS "include"
    REQUIRES network_gate wifi_manager esp_common
    PRIV_REQUIRES mbedtls esp_http_client json esp_timer)
