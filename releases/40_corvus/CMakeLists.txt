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
      target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/submodules/miditocv/lua-5.4.6/src)
      target_link_libraries(${_name} pico_unique_id pico_stdlib pico_multicore hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_flash hardware_sync tinyusb_device lua_core wrDsp wrLib)
      target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/submodules/wrDsp)
      target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/submodules/wrLib)
      pico_add_extra_outputs(${_name})
      target_sources(${_name} PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_emulator.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_multicore.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_lua.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_metro.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_slopes.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_slopes_optimized.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_slopes_integration.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_asl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_casl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_detect.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_events.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_error.cpp
        ${CMAKE_CURRENT_LIST_DIR}/crow_flash.cpp
        ${CMAKE_CURRENT_LIST_DIR}/computer_card_impl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c)
      target_compile_definitions(${_name} PRIVATE 
        PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
      # Use only UART for stdio to avoid USB descriptor conflicts
      pico_enable_stdio_uart(${_name} 1)
      pico_enable_stdio_usb(${_name} 0)
    endif()
  endmacro()

# Add lua library sources
add_library(lua_core STATIC
    submodules/miditocv/lua-5.4.6/src/lapi.c
    submodules/miditocv/lua-5.4.6/src/lcode.c
    submodules/miditocv/lua-5.4.6/src/lctype.c
    submodules/miditocv/lua-5.4.6/src/ldebug.c
    submodules/miditocv/lua-5.4.6/src/ldo.c
    submodules/miditocv/lua-5.4.6/src/ldump.c
    submodules/miditocv/lua-5.4.6/src/lfunc.c
    submodules/miditocv/lua-5.4.6/src/lgc.c
    submodules/miditocv/lua-5.4.6/src/llex.c
    submodules/miditocv/lua-5.4.6/src/lmem.c
    submodules/miditocv/lua-5.4.6/src/lobject.c
    submodules/miditocv/lua-5.4.6/src/lopcodes.c
    submodules/miditocv/lua-5.4.6/src/lparser.c
    submodules/miditocv/lua-5.4.6/src/lstate.c
    submodules/miditocv/lua-5.4.6/src/lstring.c
    submodules/miditocv/lua-5.4.6/src/ltable.c
    submodules/miditocv/lua-5.4.6/src/ltm.c
    submodules/miditocv/lua-5.4.6/src/lundump.c
    submodules/miditocv/lua-5.4.6/src/lvm.c
    submodules/miditocv/lua-5.4.6/src/lzio.c
    submodules/miditocv/lua-5.4.6/src/lauxlib.c
    submodules/miditocv/lua-5.4.6/src/lbaselib.c
    submodules/miditocv/lua-5.4.6/src/lcorolib.c
    submodules/miditocv/lua-5.4.6/src/ldblib.c
    submodules/miditocv/lua-5.4.6/src/liolib.c
    submodules/miditocv/lua-5.4.6/src/lmathlib.c
    submodules/miditocv/lua-5.4.6/src/loadlib.c
    submodules/miditocv/lua-5.4.6/src/loslib.c
    submodules/miditocv/lua-5.4.6/src/lstrlib.c
    submodules/miditocv/lua-5.4.6/src/ltablib.c
    submodules/miditocv/lua-5.4.6/src/lutf8lib.c
    submodules/miditocv/lua-5.4.6/src/linit.c
)

target_include_directories(lua_core PUBLIC submodules/miditocv/lua-5.4.6/src)
target_compile_definitions(lua_core PRIVATE LUA_32BITS)

# Add wrDsp library sources for vector processing
add_library(wrDsp STATIC
    submodules/wrDsp/wrBlocks.c
    submodules/wrDsp/wrClip.c
    submodules/wrDsp/wrDelay.c
    submodules/wrDsp/wrFilter.c
    submodules/wrDsp/wrFuncGen.c
    submodules/wrDsp/wrHead.c
    submodules/wrDsp/wrHistory.c
    submodules/wrDsp/wrInterpolate.c
    submodules/wrDsp/wrLpGate.c
    submodules/wrDsp/wrMix.c
    submodules/wrDsp/wrOscSine.c
    submodules/wrDsp/wrResamp.c
    submodules/wrDsp/wrShaper.c
    submodules/wrDsp/wrTransport.c
    submodules/wrDsp/wrVtl.c
    # wrWaveGuide.c temporarily excluded due to syntax errors
)

target_include_directories(wrDsp PUBLIC submodules/wrDsp)
target_include_directories(wrDsp PUBLIC submodules/wrLib)

# Add wrLib library sources for utilities
add_library(wrLib STATIC
    submodules/wrLib/str_buffer.c
    submodules/wrLib/wrConvert.c
    submodules/wrLib/wrEvent.c
    submodules/wrLib/wrMath.c
    submodules/wrLib/wrMeters.c
    submodules/wrLib/wrPoly.c
    submodules/wrLib/wrQueue.c
    submodules/wrLib/wrTrig.c
    submodules/wrLib/wrVactrol.c
    submodules/wrLib/wrWav.c
)

target_include_directories(wrLib PUBLIC submodules/wrLib)
target_include_directories(wrLib PUBLIC submodules/wrDsp)

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
