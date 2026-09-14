idf_component_register(
    SRCS "energy_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        esp-tls
        json
)
