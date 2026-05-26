cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)

project(task_3 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# -------------------------------
# SDK check
# -------------------------------
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.2.0")
    message(FATAL_ERROR "Pico SDK >= 2.2.0 required")
endif()

# -------------------------------
# Initialize SDK
# -------------------------------
pico_sdk_init()


# -------------------------------
# Global Project Config (SHARED)
# -------------------------------


# -------------------------------
# External libraries
# -------------------------------
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_TARGET_EXPORT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(libs/cjson)

# -------------------------------
# Components : make sure add also in target_link_libraries(${PROJECT_NAME}
# -------------------------------
add_subdirectory(components/led_handler)
add_subdirectory(components/rgb_led)
add_subdirectory(components/json_parser)
add_subdirectory(components/pot_driver)

# -------------------------------
# Main executable
# -------------------------------
add_executable(${PROJECT_NAME}
    main/main.c
)

# Include main headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/main/include
    ${CMAKE_SOURCE_DIR}/libs/cJSON
)

# -------------------------------
# Link dependencies
# -------------------------------
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_mbedtls
    pico_lwip_mbedtls
    cjson
    led_handler
    rgb_led
    json_parser
    auth
    pot_driver
)


# -------------------------------
# Output files
# -------------------------------
pico_add_extra_outputs(${PROJECT_NAME})
# to uplaod via picotool
# add_custom_target(flash
#     COMMAND picotool load -f ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.uf2
#     DEPENDS ${PROJECT_NAME}
# )

# -------------------------------
# Copy build outputs to Wokwi folder
# -------------------------------
set(WOKWI_DIR ${CMAKE_SOURCE_DIR}/wokwi)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WOKWI_DIR}

    # Copy UF2
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.uf2
        ${WOKWI_DIR}/${PROJECT_NAME}.uf2

    # Copy ELF (optional)
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.elf
        ${WOKWI_DIR}/${PROJECT_NAME}.elf

    # Copy BIN (optional)
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
        ${WOKWI_DIR}/${PROJECT_NAME}.bin

    COMMENT "Copying build outputs to Wokwi folder"
)

# to uupload with copy paste:
if(NOT DEFINED PICO_DRIVE)
    message(FATAL_ERROR "PICO_DRIVE not set. Define it in CMakePresets.json (e.g. RPI-RP2)")
endif()

add_custom_target(upload
    COMMAND ${CMAKE_COMMAND} -E echo "Uploading UF2 to Pico..."
    COMMAND powershell -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/upload.ps1"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.uf2"
            "${PICO_DRIVE}"
    DEPENDS ${PROJECT_NAME}
)
# -------------------------------


set_source_files_properties(
    ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-result"
)
