cmake_minimum_required(VERSION 3.13)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

set(CARD_NAME "ssi263voice")
project(${CARD_NAME} C CXX ASM)

# C++20 is required: the vendored Casso engine uses std::numbers.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

pico_sdk_init()

add_executable(${CARD_NAME}
  main.cpp
  SsiVoice.cpp
  usb_descriptors.c
  usb_midi_host.c
  usb_midi_host_app_driver.c)

target_compile_options(${CARD_NAME} PRIVATE
  -Wdouble-promotion -Wfloat-conversion -Wall -Wextra -O2)

target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)

# REQUIRED for the Workshop Computer's crystal: without it the card fails to
# boot on a cold power-up. Do not remove.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
  pico_stdlib
  pico_multicore
  hardware_adc
  hardware_clocks
  hardware_dma
  hardware_flash
  hardware_gpio
  hardware_i2c
  hardware_pwm
  hardware_spi
  tinyusb_host
  tinyusb_device
  tinyusb_board)

# Run all code from RAM: the audio ISR on core 0 and USB transfers on core 1
# would otherwise contend for the XIP flash cache.
pico_set_binary_type(${CARD_NAME} copy_to_ram)

pico_add_extra_outputs(${CARD_NAME})

# The front USB jack is used for MIDI host/device duty, so no USB stdio.
pico_enable_stdio_usb(${CARD_NAME} 0)
pico_enable_stdio_uart(${CARD_NAME} 0)
