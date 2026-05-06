add_library(pot_driver
            src/pot_driver.c)

# Public headers (visible to other components)
target_include_directories(pot_driver PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Private includes (only for this component)
target_include_directories(pot_driver PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(pot_driver PRIVATE
    pico_stdlib   # for core functionality
    hardware_adc
    pico_cyw43_arch_none # we need Wifi to access the GPIO, but we don't need anything else
)