add_library(serial_data_parser
    src/serial_data_parser.c)

target_include_directories(serial_data_parser PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(serial_data_parser PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(serial_data_parser PRIVATE
    pico_stdlib # for core functionality
    pico_cyw43_arch_none
)
