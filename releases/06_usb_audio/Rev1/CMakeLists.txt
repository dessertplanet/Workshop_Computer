cmake_minimum_required(VERSION 3.12)

# Pull in PICO SDK (must be before project)
set(PICO_BOARD mtm_computer_16mb CACHE STRING "Board type")
include(pico_sdk_import.cmake)


PROJECT(usb_audio_interface)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()
file(GLOB sources *.c *.h)
add_executable(usb_audio_interface ${sources})
target_link_libraries(usb_audio_interface pico_stdlib hardware_adc pico_multicore pico_fix_rp2040_usb_device_enumeration pico_stdlib hardware_pwm hardware_adc hardware_spi hardware_timer)
pico_add_extra_outputs(usb_audio_interface)
