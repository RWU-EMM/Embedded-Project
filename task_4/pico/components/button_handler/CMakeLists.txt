add_library(button_handler src/button_handler.c)

target_include_directories(button_handler PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(button_handler PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)


target_link_libraries(button_handler PRIVATE
    pico_stdlib # for core functionality
    pico_cyw43_arch_none
    hardware_pio
    gpio_driver
)
