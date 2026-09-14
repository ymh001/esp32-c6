idf_component_register(
    SRCS "wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_netif
    PRIV_REQUIRES
        esp_timer
)
