cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(usb_host_check C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(usb_host_check main.cpp)

target_include_directories(usb_host_check PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../..   # ComputerCard.h
)

target_link_libraries(usb_host_check
    pico_stdlib
    pico_unique_id
    pico_multicore
    tinyusb_host
    tinyusb_board
    boot_picoboot_headers
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
)

target_compile_definitions(usb_host_check PRIVATE
    # Give oscillator more time to start - some boards won't run if this isn't included
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_options(usb_host_check PRIVATE -Wall -Wextra -Wdouble-promotion -Wfloat-conversion)
target_link_options(usb_host_check PRIVATE -Wl,--print-memory-usage)

pico_enable_stdio_usb(usb_host_check 0)
pico_enable_stdio_uart(usb_host_check 0)
pico_add_extra_outputs(usb_host_check)
