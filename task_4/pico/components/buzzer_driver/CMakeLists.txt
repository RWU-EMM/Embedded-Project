add_library(buzzer_driver src/buzzer_driver.c)

target_include_directories(buzzer_driver PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(buzzer_driver PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)


target_link_libraries(buzzer_driver PRIVATE
    pico_stdlib # for core functionality
    pico_cyw43_arch_none
    hardware_pio
    gpio_driver
)
