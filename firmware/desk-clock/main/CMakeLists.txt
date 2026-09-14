idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        board
        clock_ui
        settings
        time_service
        wifi_manager
)
