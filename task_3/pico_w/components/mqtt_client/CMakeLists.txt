add_library(mqtt_client
            src/mqtt_client.c)

# Public headers (visible to other components)
target_include_directories(mqtt_client PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Private includes (only for this component)
target_include_directories(mqtt_client PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(mqtt_client PRIVATE
    pico_stdlib   # for core functionality
    pico_cyw43_arch_lwip_threadsafe_background
    wifi_driver    # for WiFi connectivity
    pico_lwip_mqtt
    pico_mbedtls
    pico_lwip_mbedtls
    project_config   

)

set_source_files_properties(
    ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-result"
)