cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk" CACHE PATH "Local Pico SDK path")

include(pico_sdk_import.cmake)

project(breaky C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(breaky
    src/main.cpp
    src/BreakyAudioBank.cpp
    src/BreakySampleManager.cpp
)

target_include_directories(breaky PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_options(breaky PRIVATE
    -Wall
    -Wextra
    -Wdouble-promotion
    -Wfloat-conversion
)

target_link_options(breaky PRIVATE
    -Wl,--print-memory-usage
)

target_compile_definitions(breaky PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    BREAKY_FIRMWARE_RESERVE=163840u
)

target_link_libraries(breaky
    pico_unique_id
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    hardware_clocks
    hardware_flash
)

pico_set_binary_type(breaky copy_to_ram)
pico_enable_stdio_usb(breaky 1)
pico_enable_stdio_uart(breaky 0)
pico_add_extra_outputs(breaky)

add_custom_command(
    TARGET breaky
    POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DBIN_FILE="$<TARGET_FILE_DIR:breaky>/breaky.bin"
        -DFIRMWARE_RESERVE=163840
        -P "${CMAKE_CURRENT_LIST_DIR}/cmake/check_firmware_reserve.cmake"
)
