# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(CARD_NAME warps)
project(warps C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

# Warps DSP port. main.cpp wires the ComputerCard I/O to the (vendored) Mutable
# Instruments "parasites" Warps Modulator. Only the DSP subset the Modulator
# depends on is vendored here (see warps/ and stmlib/); the STM32/USB/hardware
# tree from the original firmware is intentionally not included.
add_executable(${CARD_NAME}
    main.cpp
    warps/dsp/modulator.cc
    warps/dsp/oscillator.cc
    warps/dsp/vocoder.cc
    warps/dsp/filter_bank.cc
    warps/resources.cc
    stmlib/utils/random.cc
)

# Include root so `#include "warps/..."` and `#include "stmlib/..."` resolve.
target_include_directories(${CARD_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${CARD_NAME}
    pico_unique_id
    pico_stdlib
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_adc
    hardware_spi
    pico_multicore
)

pico_add_extra_outputs(${CARD_NAME})

# Give the oscillator more time to start - some boards won't run without this.
target_compile_definitions(${CARD_NAME} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
pico_enable_stdio_usb(${CARD_NAME} 0)

# Match the other cards' warning set. Warps' DSP is float-heavy; keep the
# double-promotion / float-conversion warnings on to catch accidental doubles
# (expensive without an FPU on RP2040).
target_compile_options(${CARD_NAME} PRIVATE -Wall -Wextra -O3)
target_link_options(${CARD_NAME} PRIVATE -Wl,--print-memory-usage)
