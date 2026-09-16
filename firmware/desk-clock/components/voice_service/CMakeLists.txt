idf_component_register(SRCS "voice_service.cpp" "voice_text.cpp" INCLUDE_DIRS "include"
    REQUIRES board wifi_manager network_gate
    PRIV_REQUIRES mbedtls espressif__esp_websocket_client esp_http_client json esp_timer)

if(CONFIG_DESK_CLOCK_VOICE_SELF_TEST)
    target_add_binary_data(${COMPONENT_LIB} "../../host_tests/data/ha_query.pcm" BINARY
        RENAME_TO voice_test_pcm)
    if(CONFIG_DESK_CLOCK_VOICE_LAMP_TEST)
        target_add_binary_data(${COMPONENT_LIB} "../../host_tests/data/ha_lamp_on.pcm" BINARY RENAME_TO voice_lamp_on)
        target_add_binary_data(${COMPONENT_LIB} "../../host_tests/data/ha_lamp_off.pcm" BINARY RENAME_TO voice_lamp_off)
    endif()
endif()
