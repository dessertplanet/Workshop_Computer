cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(computer_probe C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(computer_probe
    main.cpp
    usb_descriptors.c
    src/payload.cpp
    src/flasher.cpp
    src/ghostfat.cpp
    src/picoboot_transport.cpp
)

target_include_directories(computer_probe PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(computer_probe
    pico_stdlib
    pico_unique_id
    pico_multicore
    tinyusb_device
    tinyusb_host
    tinyusb_board
    boot_picoboot_headers
    hardware_dma
    hardware_flash
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
)

target_compile_definitions(computer_probe PRIVATE
    # Give oscillator more time to start - some boards won't run if this isn't included
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_compile_options(computer_probe PRIVATE -Wall -Wextra -Wdouble-promotion -Wfloat-conversion)
target_link_options(computer_probe PRIVATE -Wl,--print-memory-usage)

# Runs from RAM, so core 1 keeps running while core 0 writes the payload to flash
pico_set_binary_type(computer_probe copy_to_ram)

pico_enable_stdio_usb(computer_probe 0)
pico_enable_stdio_uart(computer_probe 0)
pico_add_extra_outputs(computer_probe)
