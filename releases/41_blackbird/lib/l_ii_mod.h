#pragma once

#include <lua.h>

// Stub i2c module loader for RP2040 Workshop Computer
// Replaces the original crow i2c module functionality

// Function to preload i2c modules into Lua environment
void l_ii_mod_preload(lua_State* L);
