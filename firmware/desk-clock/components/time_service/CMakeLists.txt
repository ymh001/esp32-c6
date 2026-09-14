idf_component_register(
    SRCS "time_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        settings
        esp_netif
)
