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

cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(usb_serial C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

macro (add_example _name)
  add_executable(${ARGV})
  if (TARGET ${_name})

	# Add warnings, in particular about float/double conversion, as it's very easy to end up
	# with slower double-precision calculations when we only mean to use single-precision float.
	target_compile_options(${_name} PRIVATE -Wdouble-promotion -Wfloat-conversion -Wall -Wextra)
	target_link_options(${_name} PRIVATE -Wl,--print-memory-usage)
	
	# Give oscillator more time to start - some boards won't run if this isn't included
	target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
	
	target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
	target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/${_name}/)
    target_link_libraries(${_name} pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)
	pico_add_extra_outputs(${_name})
	target_sources(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/examples/${_name}/main.cpp)	  
	pico_enable_stdio_usb(${_name} 0)
  endif()
endmacro()

add_example(usb_serial)
target_link_libraries(usb_serial pico_multicore)
pico_enable_stdio_usb(usb_serial 1)
