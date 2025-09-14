#include "crow_lua.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "pico/time.h"

// Global instance
CrowLua* g_crow_lua = nullptr;

// Crow lua globals - simplified single environment matching real crow
static const char* crow_globals_lua = R"(
-- Crow globals initialization (single environment like real crow)
print("Crow Lua initializing...")

-- Create global output and input tables (matches crow architecture)
output = {}
input = {}

-- Initialize output tables with crow-style interface
for i = 1, 4 do
    output[i] = {
        volts = 0,
        _volts_changed = false,
        _trigger = false,
        
        -- Crow output functions
        action = function(self, func) 
            if func then self._action = func end
        end,
        slew = function(self, time, shape)
            -- TODO: Implement slew rate limiting
        end,
        dyn = function(self, ...)
            -- TODO: Implement dynamics
        end
    }
    
    -- Set up metamethods for output[i].volts assignment
    setmetatable(output[i], {
        __newindex = function(t, k, v)
            if k == "volts" then
                rawset(t, k, v)
                rawset(t, "_volts_changed", true)
            elseif k == "action" and type(v) == "function" then
                rawset(t, "_action", v)
            else
                rawset(t, k, v)
            end
        end,
        __call = function(t, ...)
            -- Allow output[n]() function calls
            local args = {...}
            if #args > 0 then
                t.volts = args[1]
            end
            return t.volts
        end
    })
end

-- Initialize input tables
for i = 1, 2 do  -- Only inputs 1 and 2 for audio inputs
    input[i] = {
        volts = 0,
        _last_volts = 0,
        
        -- Input event handlers
        change = function(self, func, threshold)
            if func then 
                self._change_handler = func 
                self._change_threshold = threshold or 0.1
            end
        end,
        stream = function(self, func)
            if func then self._stream_handler = func end
        end
    }
    
    setmetatable(input[i], {
        __call = function(t, ...)
            -- Allow input[n]() to return current volts
            return t.volts
        end
    })
end

-- Enhanced crow utility functions (Phase 2.3)
function linlin(x, xmin, xmax, ymin, ymax)
    if x <= xmin then return ymin end
    if x >= xmax then return ymax end
    return ymin + (x - xmin) * (ymax - ymin) / (xmax - xmin)
end

function linexp(x, xmin, xmax, ymin, ymax)
    if x <= xmin then return ymin end
    if x >= xmax then return ymax end
    local ratio = (x - xmin) / (xmax - xmin)
    return ymin * math.pow(ymax / ymin, ratio)
end

-- Additional crow utility functions
function explin(x, xmin, xmax, ymin, ymax)
    if x <= xmin then return ymin end
    if x >= xmax then return ymax end
    local normalized = math.log(x / xmin) / math.log(xmax / xmin)
    return ymin + normalized * (ymax - ymin)
end

function expexp(x, xmin, xmax, ymin, ymax)
    if x <= xmin then return ymin end
    if x >= xmax then return ymax end
    local norm_x = math.log(x / xmin) / math.log(xmax / xmin)
    return ymin * math.pow(ymax / ymin, norm_x)
end

-- Math utilities
function clamp(x, min, max)
    if x < min then return min end
    if x > max then return max end
    return x
end

function wrap(x, min, max)
    local range = max - min
    if range <= 0 then return min end
    while x >= max do x = x - range end
    while x < min do x = x + range end
    return x
end

function fold(x, min, max)
    local range = max - min
    if range <= 0 then return min end
    x = x - min
    local cycles = math.floor(x / range)
    local folded = x - cycles * range
    if cycles % 2 == 1 then
        folded = range - folded
    end
    return folded + min
end

-- Voltage scaling helpers
function v_to_hz(volts)
    -- 1V/octave scaling, A4 = 440Hz at 0V (C4)
    return 440 * math.pow(2, volts + (3/12))  -- C4 to A4 offset
end

function hz_to_v(hz)
    -- Convert frequency to 1V/octave
    return math.log(hz / 440) / math.log(2) - (3/12)
end

-- Time and clock functions
function time()
    -- TODO: Connect to actual system clock
    return 0
end

function clock(tempo)
    -- TODO: Implement tempo setting
    if tempo then
        -- Set tempo
    end
    -- Return current tempo
    return 120
end

-- Output state management (simplified for single global environment)
function get_output_state(channel)
    if output[channel] then
        local volts = output[channel].volts or 0
        local volts_new = output[channel]._volts_changed or false
        local trigger = output[channel]._trigger or false
        
        -- Reset change flags after reading
        output[channel]._volts_changed = false
        output[channel]._trigger = false
        
        return volts, volts_new, trigger
    end
    return 0, false, false
end

-- Helper function to set input volts from C code
function set_input_volts(channel, volts)
    if input[channel] then
        input[channel].volts = volts
    end
end

-- User script placeholder functions
function init()
    -- Default empty init function
end

function step()
    -- Default empty step function
end

print("Crow Lua globals loaded")
)";

CrowLua::CrowLua() : 
    L(nullptr),
    script_update_pending(false), 
    lua_initialized(false),
    last_gc_time(0)
{
    critical_section_init(&lua_critical_section);
}

CrowLua::~CrowLua() {
    deinit();
}

bool CrowLua::init() {
    if (lua_initialized) {
        return true;
    }
    
    printf("Initializing Crow Lua VM (simplified single environment)...\n");
    
    // Create lua state
    L = luaL_newstate();
    if (!L) {
        printf("Failed to create Lua state\n");
        return false;
    }
    
    // Open standard libraries
    luaL_openlibs(L);
    
    // Load crow globals
    if (luaL_loadbuffer(L, crow_globals_lua, strlen(crow_globals_lua), "crow_globals") != LUA_OK) {
        printf("Error loading crow globals: %s\n", lua_tostring(L, -1));
        lua_close(L);
        L = nullptr;
        return false;
    }
    
    // Execute crow globals
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("Error executing crow globals: %s\n", lua_tostring(L, -1));
        lua_close(L);
        L = nullptr;
        return false;
    }
    
    lua_initialized = true;
    printf("Crow Lua VM initialized successfully (crow-style single environment)\n");
    return true;
}

void CrowLua::deinit() {
    if (L) {
        lua_close(L);
        L = nullptr;
    }
    lua_initialized = false;
}

bool CrowLua::eval_script(const char* script, size_t script_len, const char* chunkname) {
    if (!lua_initialized || !L) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    if (luaL_loadbuffer(L, script, script_len, chunkname) != LUA_OK) {
        printf("Error loading script: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("Error executing script: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    critical_section_exit(&lua_critical_section);
    return true;
}

bool CrowLua::load_user_script(const char* code) {
    if (!lua_initialized || !L || !code) {
        return false;
    }
    
    return eval_script(code, strlen(code), "user_script");
}

bool CrowLua::get_output_volts_and_trigger(int channel, float* volts, bool* volts_new, bool* trigger) {
    if (!lua_initialized || !L || channel < 1 || channel > 4) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    lua_getglobal(L, "get_output_state");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    lua_pushinteger(L, channel);
    if (lua_pcall(L, 1, 3, 0) != LUA_OK) {
        printf("Error getting output state for channel %d: %s\n", channel, lua_tostring(L, -1));
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    *volts = (float)lua_tonumber(L, -3);
    *volts_new = lua_toboolean(L, -2);
    *trigger = lua_toboolean(L, -1);
    lua_pop(L, 3);
    
    critical_section_exit(&lua_critical_section);
    return true;
}

void CrowLua::set_input_volts(int channel, float volts) {
    if (!lua_initialized || !L || channel < 1 || channel > 2) {
        return;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    lua_getglobal(L, "set_input_volts");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, channel);
        lua_pushnumber(L, volts);
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            printf("Error setting input volts for channel %d: %s\n", channel, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    
    critical_section_exit(&lua_critical_section);
}

bool CrowLua::call_init() {
    if (!lua_initialized || !L) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    lua_getglobal(L, "init");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("Error running init: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    critical_section_exit(&lua_critical_section);
    return true;
}

bool CrowLua::call_step() {
    if (!lua_initialized || !L) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    lua_getglobal(L, "step");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("Error running step: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    critical_section_exit(&lua_critical_section);
    return true;
}

void CrowLua::garbage_collect() {
    if (!lua_initialized || !L) {
        return;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    lua_gc(L, LUA_GCCOLLECT, 0);
    critical_section_exit(&lua_critical_section);
}

void CrowLua::process_periodic_tasks(uint32_t current_time_ms) {
    // Garbage collection
    if (current_time_ms - last_gc_time > GC_INTERVAL_MS) {
        garbage_collect();
        last_gc_time = current_time_ms;
    }
}

uint32_t CrowLua::get_memory_usage() {
    if (!lua_initialized || !L) {
        return 0;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    lua_gc(L, LUA_GCCOUNT, 0);
    int kb = lua_gc(L, LUA_GCCOUNT, 0);
    critical_section_exit(&lua_critical_section);
    
    return (uint32_t)kb * 1024;
}

bool CrowLua::is_lua_command(const char* command) {
    if (!command) return false;
    
    // Simple heuristic: if it doesn't start with ^^, it's likely lua
    if (command[0] == '^' && command[1] == '^') {
        return false;  // Crow system command
    }
    
    return true;  // Assume it's lua
}

bool CrowLua::execute_repl_command(const char* command, size_t length) {
    return eval_script(command, length, "repl");
}

void CrowLua::schedule_script_update(const char* script) {
    // For simplicity, just load the script immediately
    // In a more complex implementation, this could be queued
    load_user_script(script);
}

bool CrowLua::process_pending_updates() {
    // No pending updates in this simplified implementation
    return true;
}

const char* CrowLua::get_last_error() {
    // TODO: Implement error tracking
    return "No error information available";
}

// C-style interface implementation (updated for simplified API)
extern "C" {
    bool crow_lua_init() {
        if (!g_crow_lua) {
            g_crow_lua = new CrowLua();
        }
        return g_crow_lua->init();
    }
    
    void crow_lua_deinit() {
        if (g_crow_lua) {
            g_crow_lua->deinit();
            delete g_crow_lua;
            g_crow_lua = nullptr;
        }
    }
    
    bool crow_lua_eval_repl(const char* command, size_t length) {
        if (!g_crow_lua) return false;
        return g_crow_lua->execute_repl_command(command, length);
    }
    
    bool crow_lua_update_script(int index, const char* script) {
        // Updated to use simplified API (no more per-environment scripts)
        if (!g_crow_lua) return false;
        return g_crow_lua->load_user_script(script);
    }
    
    void crow_lua_process_events() {
        if (!g_crow_lua) return;
        
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        g_crow_lua->process_periodic_tasks(current_time);
        
        // Call step function (simplified - no per-environment processing)
        g_crow_lua->call_step();
    }
    
    void crow_lua_garbage_collect() {
        if (g_crow_lua) {
            g_crow_lua->garbage_collect();
        }
    }
}
