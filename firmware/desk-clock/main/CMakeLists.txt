idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        board
        esp_pm
        network_gate
        ha_devices
        clock_ui
        energy_service
        settings
        time_service
        wifi_manager
)
