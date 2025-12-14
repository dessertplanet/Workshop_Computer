#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Install fastmath module into Lua and optionally patch global math table.
// Returns 0 on success, non-zero on error (leaves Lua stack balanced).
struct lua_State;
int fastmath_lua_install(struct lua_State *L, int patch_math_table);

#ifdef __cplusplus
}
#endif
