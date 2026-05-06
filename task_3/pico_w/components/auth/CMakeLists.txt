add_library(auth
            src/auth.c)

# Public headers (visible to other components)
target_include_directories(auth PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Private includes (only for this component)
target_include_directories(auth PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(auth PRIVATE
    pico_stdlib   # for core functionality
    pico_cyw43_arch_none # we need Wifi to access the GPIO, but we don't need anything else
)