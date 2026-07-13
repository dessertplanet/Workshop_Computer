#pragma once

#include "lua.h" // Fixed path for our Lua build
#include "lauxlib.h"
#include "lualib.h"

void l_bootstrap_init(lua_State* L);
int l_bootstrap_dofile(lua_State* L);
int l_bootstrap_c_tell(lua_State* L);
