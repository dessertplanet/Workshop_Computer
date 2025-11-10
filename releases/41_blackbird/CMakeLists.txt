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
set(CARD_NAME blackbird)
# type matching project name here
project(blackbird C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

macro (add_card _name)
    add_executable(${ARGV})
    if (TARGET ${_name})
      target_include_directories(${_name} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
      target_link_libraries(${_name} pico_unique_id pico_stdlib pico_multicore hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi hardware_flash lua tinyusb_device tinyusb_board)
      pico_add_extra_outputs(${_name})
      target_sources(${_name} PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/casl.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/slopes.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ashapes.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/detect.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/caw.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ii.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/metro.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/clock.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/clock_ll.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/events.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/events_lockfree.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/usb_lockfree.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/l_bootstrap.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/l_crowlib.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/l_ii_mod.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/ll_timers.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/random.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/wrblocks.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/mailbox.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/flash_storage.cpp
      )
      target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
      # Disable stdio USB - we're using TinyUSB directly with custom descriptors for druid compatibility
      pico_enable_stdio_usb(${_name} 0)
      pico_enable_stdio_uart(${_name} 0)
    endif()
  endmacro()

# Add Lua library
add_subdirectory(lua/src)

# Build host luac before compiling Lua files to bytecode
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/host_luac/luac
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/util/build_host_luac.sh
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/luac.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lapi.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lauxlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lbaselib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lcode.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lcorolib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lctype.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldblib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldebug.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldo.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ldump.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lfunc.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lgc.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/linit.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/liolib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/llex.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lmathlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lmem.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/loadlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lobject.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lopcodes.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/loslib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lparser.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lstate.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lstring.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lstrlib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ltable.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ltablib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/ltm.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lundump.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lutf8lib.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lvm.c
            ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lzio.c
            ${CMAKE_CURRENT_SOURCE_DIR}/util/build_host_luac.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building host luac for Lua bytecode compilation"
)

add_custom_target(build_host_luac DEPENDS ${CMAKE_BINARY_DIR}/host_luac/luac)

# Function to add Lua-to-header conversion
function(add_lua_header LUA_FILE HEADER_VAR)
    get_filename_component(LUA_NAME ${LUA_FILE} NAME_WE)
    set(HEADER_FILE ${CMAKE_BINARY_DIR}/${LUA_NAME}.h)
    
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/util/lua2header.py 
                ${CMAKE_CURRENT_SOURCE_DIR}/${LUA_FILE} 
                ${HEADER_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${LUA_FILE} build_host_luac
        COMMENT "Converting ${LUA_FILE} to C header (bytecode or source fallback)"
    )
    
    set(${HEADER_VAR} ${HEADER_FILE} PARENT_SCOPE)
endfunction()

# Generate headers for core Lua libraries (always included)
add_lua_header(lib/asl.lua ASL_HEADER)
add_lua_header(lib/asllib.lua ASLLIB_HEADER)
add_lua_header(lib/crowlib.lua CROWLIB_HEADER)
add_lua_header(lib/clock.lua CLOCK_HEADER)
add_lua_header(lib/metro.lua METRO_HEADER)
add_lua_header(lib/public.lua PUBLIC_HEADER)
add_lua_header(lib/input.lua INPUT_HEADER)
add_lua_header(lib/output.lua OUTPUT_HEADER)
add_lua_header(lib/ii.lua II_HEADER)
add_lua_header(lib/calibrate.lua CALIBRATE_HEADER)
add_lua_header(lib/sequins.lua SEQUINS_HEADER)
add_lua_header(lib/quote.lua QUOTE_HEADER)
add_lua_header(lib/timeline.lua TIMELINE_HEADER)
add_lua_header(lib/hotswap.lua HOTSWAP_HEADER)
add_lua_header(lib/First.lua FIRST_HEADER)


message(STATUS "Blackbird: Production build - no test scripts embedded")
set(TEST_DEFINE "PRODUCTION_BUILD")


# Create card
add_card(${CARD_NAME})

# Add generated headers as dependencies
target_sources(${CARD_NAME} PRIVATE 
  ${ASL_HEADER}
  ${ASLLIB_HEADER}
  ${CROWLIB_HEADER}
  ${CLOCK_HEADER}
  ${METRO_HEADER}
  ${PUBLIC_HEADER}
  ${INPUT_HEADER}
  ${OUTPUT_HEADER}
  ${II_HEADER}
  ${CALIBRATE_HEADER}
  ${SEQUINS_HEADER}
  ${QUOTE_HEADER}
  ${TIMELINE_HEADER}
  ${HOTSWAP_HEADER}
  ${FIRST_HEADER}
)

# Add conditional compilation defines
target_compile_definitions(${CARD_NAME} PRIVATE ${TEST_DEFINE})
target_include_directories(${CARD_NAME} PRIVATE ${CMAKE_BINARY_DIR})

# Performance optimizations for main code
target_compile_options(${CARD_NAME} PRIVATE
    -O3                    # Maximum speed optimization
    -ffast-math           # Faster floating-point (safe for audio DSP)
    # Note: -fno-exceptions and -fno-rtti already set by pico-sdk
    # Note: -flto disabled due to naming conflict with clock.h/clock()
)

# Copy UF2 files to UF2 directory with descriptive names
add_custom_command(
    TARGET ${CARD_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2
            ${CMAKE_CURRENT_SOURCE_DIR}/UF2/${CARD_NAME}.uf2
)
