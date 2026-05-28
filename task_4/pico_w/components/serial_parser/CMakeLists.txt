add_library(serial_parser
    src/serial_parser.c)

target_include_directories(serial_parser PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(serial_parser PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(serial_parser PRIVATE
    pico_stdlib # for core functionality
    pico_cyw43_arch_none
)
