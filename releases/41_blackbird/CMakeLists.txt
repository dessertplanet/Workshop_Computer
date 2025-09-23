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
      target_link_libraries(${_name} pico_unique_id pico_stdlib pico_multicore hardware_dma hardware_i2c hardware_pwm hardware_adc hardware_spi lua)
      pico_add_extra_outputs(${_name})
      target_sources(${_name} PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/lib/casl.c
      )
      target_compile_definitions(${_name} PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
      pico_enable_stdio_usb(${_name} 1)
    endif()
  endmacro()

# Add Lua library
add_subdirectory(lua/src)

# Function to add Lua-to-header conversion
function(add_lua_header LUA_FILE HEADER_VAR)
    get_filename_component(LUA_NAME ${LUA_FILE} NAME_WE)
    set(HEADER_FILE ${CMAKE_BINARY_DIR}/${LUA_NAME}.h)
    
    add_custom_command(
        OUTPUT ${HEADER_FILE}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/util/lua2header.py 
                ${CMAKE_CURRENT_SOURCE_DIR}/${LUA_FILE} 
                ${HEADER_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${LUA_FILE}
        COMMENT "Converting ${LUA_FILE} to C header (will use source fallback)"
    )
    
    set(${HEADER_VAR} ${HEADER_FILE} PARENT_SCOPE)
endfunction()

# Generate headers for Lua files
add_lua_header(test_asl.lua TEST_ASL_HEADER)
add_lua_header(lib/asl.lua ASL_HEADER)
add_lua_header(lib/asllib.lua ASLLIB_HEADER)

# Create card
add_card(${CARD_NAME})

# Add generated headers as dependencies
target_sources(${CARD_NAME} PRIVATE ${TEST_ASL_HEADER} ${ASL_HEADER} ${ASLLIB_HEADER})
target_include_directories(${CARD_NAME} PRIVATE ${CMAKE_BINARY_DIR})

# Copy UF2 files to UF2 directory with descriptive names
add_custom_command(
    TARGET ${CARD_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/UF2
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/${CARD_NAME}.uf2
            ${CMAKE_CURRENT_SOURCE_DIR}/UF2/${CARD_NAME}.uf2
)
