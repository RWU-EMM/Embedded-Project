add_library(gpio_driver src/gpio_driver.c)

target_include_directories(gpio_driver PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(gpio_driver PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)


target_link_libraries(gpio_driver PRIVATE
    pico_stdlib # for core functionality
    pico_cyw43_arch_none
    hardware_irq
)
