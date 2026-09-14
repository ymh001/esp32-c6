idf_component_register(
    SRCS "net_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        settings
        time_service
        esp_wifi
        esp_netif
        esp_http_server
        nvs_flash
    PRIV_REQUIRES
        esp_timer
        lwip
)
