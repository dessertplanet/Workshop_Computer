# CMakeLists.txt for Lua library - RP2040 compatible build
# Creates a static Lua library for embedded systems

add_library(lua "")

# Add all required Lua source files
target_sources(lua
    PUBLIC
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    # ldblib.c              # REMOVED: debug library not used
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit_crow.c            # CUSTOM: minimal library set for crow
    # liolib.c              # REMOVED: io library not used
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    # loslib.c              # REMOVED: os library not used
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    # lutf8lib.c            # REMOVED: utf8 library not used
    lvm.c
    lzio.c
)

# Include directories
target_include_directories(lua
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# Compiler definitions for RP2040 compatibility
target_compile_definitions(lua
    PUBLIC
    LUA_32BITS=1                    # Enable 32-bit mode for RP2040
    PRIVATE
    LUA_USE_C89=1                   # Use C89 for compatibility
    LUA_COMPAT_5_3=1                # Enable compatibility features
)

# Additional compiler flags for embedded systems
target_compile_options(lua
    PRIVATE
    -Os                             # Optimize for size
    -ffunction-sections             # Enable function section removal
    -fdata-sections                 # Enable data section removal
)

# Link with math library
target_link_libraries(lua
    PUBLIC
    m                               # Math library
)
