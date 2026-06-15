cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/Users/adrianvos/pico-sdk")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(C1ZZL3_v04_stable_experimental C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(C1ZZL3_v04_stable_experimental
    C1ZZL3.cpp
    C1ZZL3_LUT.cpp
)

target_include_directories(C1ZZL3_v04_stable_experimental PRIVATE
    .
)

target_link_libraries(C1ZZL3_v04_stable_experimental
    pico_stdlib
    pico_unique_id

    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_spi
    hardware_clocks
    hardware_flash
    hardware_i2c
)

target_compile_definitions(C1ZZL3_v04_stable_experimental PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

pico_enable_stdio_usb(C1ZZL3_v04_stable_experimental 0)
pico_set_binary_type(C1ZZL3_v04_stable_experimental copy_to_ram)

target_compile_options(C1ZZL3_v04_stable_experimental PRIVATE
    -O3
    -ffast-math
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(C1ZZL3_v04_stable_experimental PRIVATE
    -Wl,--print-memory-usage
)

pico_add_extra_outputs(C1ZZL3_v04_stable_experimental)
