# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
# Define a variable for the card name
set(CARD_NAME corvus)
# type matching project name here
project(corvus C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

macro (add_card _name)
    add_executable(${ARGV})
    if (TARGET ${_name})
      target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
      target_link_libraries(${_name} pico_unique_id pico_stdlib pico_multicore hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi tinyusb_device)
      pico_add_extra_outputs(${_name})
      target_sources(${_name} PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_emulator.cpp
        ${CMAKE_CURRENT_LIST_DIR}/computer_card_impl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c)
      target_compile_definitions(${_name} PRIVATE 
        PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
      # Use only UART for stdio to avoid USB descriptor conflicts
      pico_enable_stdio_uart(${_name} 1)
      pico_enable_stdio_usb(${_name} 0)
    endif()
  endmacro()

# Create card
add_card(${CARD_NAME})

# Copy UF2 files to UF2 directory with descriptive names
add_custom_command(
    TARGET ${CARD_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2
            ${CMAKE_CURRENT_SOURCE_DIR}/UF2/${CARD_NAME}.uf2
)
