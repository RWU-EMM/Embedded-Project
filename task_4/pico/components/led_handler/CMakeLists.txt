add_library(led_handler
            src/led_handler.c
            )

# Public headers (visible to other components)
target_include_directories(led_handler PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Private includes (only for this component)
target_include_directories(led_handler PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(led_handler PRIVATE
    pico_stdlib   # for core functionality
    pico_cyw43_arch_none # we need Wifi to access the GPIO, but we don't need anything else
)