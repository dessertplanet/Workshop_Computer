cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(decanter C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(decanter
    main.cpp
    usb_descriptors.c
    src/payload.cpp
    src/flasher.cpp
    src/ghostfat.cpp
    src/picoboot_transport.cpp
    src/uart_transport.cpp
)

pico_generate_pio_header(decanter ${CMAKE_CURRENT_LIST_DIR}/src/uart_link.pio)

target_include_directories(decanter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(decanter
    pico_stdlib
    pico_unique_id
    pico_multicore
    tinyusb_device
    tinyusb_host
    tinyusb_board
    boot_picoboot_headers
    hardware_dma
    hardware_flash
    hardware_pio
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
)

target_compile_definitions(decanter PRIVATE
    # Give oscillator more time to start - some boards won't run if this isn't included
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    # Leave GPIO 0/1 (the UART pads) to the UART link, not ComputerCard's debug outputs
    ENABLE_UART_DEBUGGING
)

target_compile_options(decanter PRIVATE -Wall -Wextra -Wdouble-promotion -Wfloat-conversion)
target_link_options(decanter PRIVATE -Wl,--print-memory-usage)

# Runs from RAM, so core 1 keeps running while core 0 writes the payload to flash
pico_set_binary_type(decanter copy_to_ram)

pico_enable_stdio_usb(decanter 0)
pico_enable_stdio_uart(decanter 0)
pico_add_extra_outputs(decanter)
