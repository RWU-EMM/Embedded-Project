add_library(rgb_led
    src/rgb_led.c
)

target_include_directories(rgb_led PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(rgb_led PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Generate PIO header (FIXED)
pico_generate_pio_header(rgb_led
    ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio
    OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/include
)

# Ensure correct build order
add_dependencies(rgb_led rgb_led_ws2812_pio_h)

target_link_libraries(rgb_led PRIVATE
    pico_stdlib
    hardware_pio
    pico_cyw43_arch_none
)