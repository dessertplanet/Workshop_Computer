cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(usb_audio_midi_interface C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()



add_executable(${PROJECT_NAME}
    src/main.cpp
    src/usb_descriptors.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${PICO_SDK_PATH}/lib/tinyusb/src/
    ${PICO_SDK_PATH}/lib/tinyusb/hw/
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_pwm
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_timer
    hardware_flash
    tinyusb_device
    tinyusb_board
)

# Overclock to 200 MHz for better performance under heavy MIDI load
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PICO_DEFAULT_SYS_CLK_KHZ=200000
)

# Disable stdio via USB (we're using USB for audio)
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Create UF2, HEX output files
pico_add_extra_outputs(${PROJECT_NAME})
