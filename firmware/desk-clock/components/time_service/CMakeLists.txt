idf_component_register(
    SRCS "time_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        wifi_manager
        board
        settings
        esp_netif
)
