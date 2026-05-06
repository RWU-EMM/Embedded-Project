# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(controller)


# -------------------------------
# Copy build outputs to Wokwi
# -------------------------------
set(WOKWI_DIR ${CMAKE_SOURCE_DIR}/wokwi)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WOKWI_DIR}

    # Copy ELF
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.elf
        ${WOKWI_DIR}/${PROJECT_NAME}.elf

    COMMENT "Copying ESP-IDF outputs to Wokwi folder"
)