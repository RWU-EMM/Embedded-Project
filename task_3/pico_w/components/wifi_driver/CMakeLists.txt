add_library(wifi_driver
            src/wifi_driver.c)

# Public headers (visible to other components)
target_include_directories(wifi_driver PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Private includes (only for this component)
target_include_directories(wifi_driver PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(wifi_driver PRIVATE
    pico_stdlib   # for core functionality
    pico_cyw43_arch_lwip_threadsafe_background
    project_config   
)

set_source_files_properties(
    ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-result"
)