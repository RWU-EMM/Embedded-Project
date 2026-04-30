cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)

project(task_3 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.2.0")
    message(FATAL_ERROR "Pico SDK >= 2.2.0 required (found ${PICO_SDK_VERSION_STRING})")
endif()

# USB debug wait
if (NOT DEFINED PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS)
    set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 3000)
endif()


# Environment variable handling (FIXED)
if (DEFINED ENV{MQTT_SERVER} AND NOT DEFINED MQTT_SERVER)
    set(MQTT_SERVER $ENV{MQTT_SERVER})
    message("Using MQTT_SERVER from environment: ${MQTT_SERVER}")
endif()

if (DEFINED ENV{MQTT_USERNAME} AND NOT DEFINED MQTT_USERNAME)
    set(MQTT_USERNAME $ENV{MQTT_USERNAME})
    message("Using MQTT_USERNAME from environment")
endif()

if (DEFINED ENV{MQTT_PASSWORD} AND NOT DEFINED MQTT_PASSWORD)
    set(MQTT_PASSWORD $ENV{MQTT_PASSWORD})
    message("Using MQTT_PASSWORD from environment")
endif()

# Mandatory check
if (NOT MQTT_SERVER)
    message(FATAL_ERROR "MQTT_SERVER is not defined!")
endif()

# Cache variables
set(MQTT_SERVER "${MQTT_SERVER}" CACHE INTERNAL "")
set(MQTT_USERNAME "${MQTT_USERNAME}" CACHE INTERNAL "")
set(MQTT_PASSWORD "${MQTT_PASSWORD}" CACHE INTERNAL "")


# Certificates 
set(MQTT_CERT_PATH ${CMAKE_CURRENT_LIST_DIR}/certs)
set(MQTT_CERT_INC mqtt_client.inc)


# Init SDK
pico_sdk_init()

add_executable(${PROJECT_NAME}
    ${PROJECT_NAME}.c
)

target_link_libraries(${PROJECT_NAME} 
    pico_stdlib
    hardware_adc
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_mbedtls
    pico_lwip_mbedtls
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
)

# Compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    MQTT_SERVER=\"${MQTT_SERVER}\"
)

# TLS (STRICT MODE)
if (EXISTS "${MQTT_CERT_PATH}/${MQTT_CERT_INC}")
    message("Using MQTT TLS certificates")

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        MQTT_CERT_INC=\"${MQTT_CERT_INC}\"
        ALTCP_MBEDTLS_AUTHMODE=MBEDTLS_SSL_VERIFY_REQUIRED
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${MQTT_CERT_PATH}
    )
else()
    message(WARNING "MQTT certificate not found → TLS DISABLED")
endif()

# Credentials
if (MQTT_USERNAME AND MQTT_PASSWORD)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        MQTT_USERNAME=\"${MQTT_USERNAME}\"
        MQTT_PASSWORD=\"${MQTT_PASSWORD}\"
    )
endif()

if ("${WIFI_PASSWORD}" STREQUAL "")
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIFI_AUTH=CYW43_AUTH_OPEN)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIFI_AUTH=CYW43_AUTH_WPA2_AES_PSK)
endif()

pico_add_extra_outputs(${PROJECT_NAME})

set_source_files_properties(
    ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-result"
)