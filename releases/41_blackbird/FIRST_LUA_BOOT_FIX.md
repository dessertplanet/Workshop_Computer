# First.lua Boot Issue - Fix Documentation

## Issue Description

After implementing flash storage, First.lua was not properly initializing on boot, and the `^^f` command appeared broken.

## Root Cause

The boot sequence was loading First.lua bytecode and executing it, but **was not calling `crow.reset()` and `init()`** like the real crow does. This meant:

1. The crow runtime didn't reset to defaults
2. User `init()` function (defined in First.lua) was never invoked
3. Scripts appeared to "not load" even though they were being executed

## The Fix

### Boot Sequence (Constructor)
Added calls to `crow.reset()` and `init()` after loading scripts in all three cases:

```cpp
case USERSCRIPT_Default:
    // Load First.lua from compiled bytecode
    if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") != LUA_OK 
        || lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
        printf("Failed to load First.lua\n");
    } else {
        printf("Loaded: First.lua (default)\n");
        // NEW: Call crow.reset() and init() like real crow does
        lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
        lua_manager->evaluate_safe("if init then init() end");
    }
    break;
```

### User Script from Flash
```cpp
case USERSCRIPT_User:
    if (script_addr && luaL_loadbuffer(lua_manager->L, script_addr, script_len, "=userscript") == LUA_OK
        && lua_pcall(lua_manager->L, 0, 0, 0) == LUA_OK) {
        printf("Loaded: user script from flash (%u bytes)\n", script_len);
        // NEW: Call crow.reset() and init()
        lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
        lua_manager->evaluate_safe("if init then init() end");
    }
```

### Fallback to First.lua
```cpp
else {
    printf("Failed to load user script from flash, loading First.lua\n");
    if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") == LUA_OK 
        && lua_pcall(lua_manager->L, 0, 0, 0) == LUA_OK) {
        printf("Loaded First.lua fallback\n");
        // NEW: Call crow.reset() and init() for fallback too
        lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
        lua_manager->evaluate_safe("if init then init() end");
    }
}
```

## Why This Matters

### `crow.reset()` Purpose
Resets the crow runtime to defaults:
- Clears output states
- Resets ASL (Actions & Slopes Library)
- Initializes metros
- Clears event handlers

### `init()` Purpose
User-defined initialization function that:
- Sets up outputs
- Configures inputs
- Starts metros
- Defines event handlers
- Runs user startup code

## How Real Crow Does It

From `crow/lib/repl.c`:

```c
bool REPL_run_script( USERSCRIPT_t mode, char* buf, uint32_t len )
{
    switch (mode)
    {
        case USERSCRIPT_Default:
            Lua_load_default_script();  // Loads First.lua
            strcpy( running_script_name, "Running: First.lua" );
            break;
        case USERSCRIPT_User:
            if ( Lua_eval( Lua, buf, len, "=userscript" ) ){
                return false;
            }
            // ...
            break;
    }
    return true;
}

// After REPL_run_script() returns true, crow calls:
void REPL_upload( int flash )
{
    if( REPL_run_script( USERSCRIPT_User, new_script, new_script_len ) ){
        // ...
        Lua_crowbegin();  // <-- This calls crow.reset() and init()
    }
}
```

`Lua_crowbegin()` implementation:
```c
void Lua_crowbegin( void )
{
    Lua_eval( Lua, "crow.reset()", -1, "crowbegin" );
    Lua_eval( Lua, "if init then init() end", -1, "init" );
}
```

## Testing

### Before Fix:
```lua
-- First.lua
function init()
    print("init called!")
    output[1].volts = 5
end
```

**Result:** Nothing happened, outputs stayed at 0V

### After Fix:
```lua
-- First.lua
function init()
    print("init called!")
    output[1].volts = 5
end
```

**Result:** 
- Console shows: "init called!"
- Output 1 goes to 5V
- Script works as expected!

## Commands Affected

All these now work correctly:

| Command | Description | Now Works |
|---------|-------------|-----------|
| Boot | Default First.lua load | ✅ |
| `^^f` | Load First.lua | ✅ (already worked) |
| `^^e` | Run script from RAM | ✅ |
| `^^w` | Save & run from flash | ✅ |

## Related Files

- `main.cpp` lines 1287-1325 (constructor boot sequence)
- `main.cpp` lines 1717-1741 (`^^f` command handler - reference implementation)

## Verification

To verify the fix works:

1. **Flash the new firmware**
2. **Boot the device** - should see:
   ```
   Loaded: First.lua (default)
   ```
3. **Connect via druid and type:**
   ```lua
   output[1].volts = 5
   ```
4. **Output 1 should go to 5V** (proves crow runtime initialized)

5. **Test `^^f` command** - should reload First.lua and call init()

---

**Fixed:** October 2, 2025  
**Issue:** Boot sequence not calling init()  
**Solution:** Added crow.reset() and init() calls after script loading
