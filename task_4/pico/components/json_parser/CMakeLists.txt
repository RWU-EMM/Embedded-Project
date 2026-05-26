add_library(json_parser
            src/json_parser.c)

# Public headers (visible to other components)
target_include_directories(json_parser PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Private includes (only for this component)
target_include_directories(json_parser PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs/cJSON
)

target_link_libraries(json_parser PRIVATE
    pico_stdlib   # for core functionality
    mqtt_client   # for MQTT-related functionality
    cjson
    pico_cyw43_arch_none # we need Wifi to access the GPIO, but we don't need anything else
)