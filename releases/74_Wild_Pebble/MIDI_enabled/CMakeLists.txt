cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/Users/adrianvos/pico-sdk")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(WildPebbleMIDI C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(WildPebbleMIDI
    WildPebbleMIDI.cpp
    usb_descriptors.c
)

target_include_directories(WildPebbleMIDI PRIVATE
    .
    ..
)

target_compile_options(WildPebbleMIDI PRIVATE
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_compile_definitions(WildPebbleMIDI PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_link_options(WildPebbleMIDI PRIVATE
    -Wl,--print-memory-usage
)

target_link_libraries(WildPebbleMIDI
    pico_stdlib
    pico_unique_id
    pico_multicore

    tinyusb_device
    tinyusb_board

    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_interp
    hardware_spi
    hardware_pio
    hardware_clocks
    hardware_flash
    hardware_i2c
)

pico_enable_stdio_usb(WildPebbleMIDI 0)
pico_enable_stdio_uart(WildPebbleMIDI 0)
pico_set_binary_type(WildPebbleMIDI copy_to_ram)
pico_add_extra_outputs(WildPebbleMIDI)
