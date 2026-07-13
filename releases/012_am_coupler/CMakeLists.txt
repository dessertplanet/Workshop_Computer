cmake_minimum_required (VERSION 3.13)
include(pico_sdk_import.cmake)
project(am_coupler C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(PICO_COPY_TO_RAM,1)
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
    target_link_libraries(${_name} pico_unique_id pico_stdlib hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi)
	pico_add_extra_outputs(${_name})
	target_sources(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/main.cpp)
	
	pico_enable_stdio_usb(${_name} 0)
	pico_enable_stdio_uart(${_name} 0)
  endif()
endmacro()
  
  
add_example(am_coupler)
target_link_libraries(am_coupler pico_multicore )

