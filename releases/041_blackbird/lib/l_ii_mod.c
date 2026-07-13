#include "l_ii_mod.h"

#include <stdio.h>

// Stub i2c module loader for RP2040 Workshop Computer
// Replaces the original crow i2c module functionality

void l_ii_mod_preload(lua_State* L) {
    // Stub implementation - in the original crow this would load i2c device modules
    // For now, this is just a no-op placeholder for Workshop Computer
    printf("l_ii_mod_preload: stub implementation (no i2c modules loaded)\n");
    
    // In a real implementation, this would:
    // 1. Register various i2c device types (like Just Friends, W/, etc.)
    // 2. Load device-specific Lua modules
    // 3. Set up i2c communication handlers
    
    // For the Workshop Computer emulator, we don't need actual i2c functionality
}
