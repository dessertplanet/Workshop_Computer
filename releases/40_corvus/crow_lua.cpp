#include "crow_lua.h"
#include "crow_metro.h"
#include "crow_slopes.h"
#include "crow_detect.h"
#include "crow_emulator.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "pico/time.h"

/* Added detection event drain integration */
#include "crow_detect.h"

// Global instance
CrowLua* g_crow_lua = nullptr;

lua_State* crow_lua_get_state() {
    if (g_crow_lua && g_crow_lua->is_initialized()) {
        return g_crow_lua->raw_state();
    }
    return nullptr;
}

// C functions accessible to lua (metro system)
static int crow_lua_metro_start(lua_State *L) {
    int nargs = lua_gettop(L);
    if (nargs < 1) {
        return luaL_error(L, "metro_start requires at least 1 argument (id)");
    }
    
    int id = (int)luaL_checkinteger(L, 1) - 1;  // Convert to 0-based
    float time = (nargs > 1 && lua_isnumber(L, 2)) ? (float)lua_tonumber(L, 2) : -1.0f;
    int count = (nargs > 2 && lua_isnumber(L, 3)) ? (int)lua_tointeger(L, 3) : -1;
    int stage = (nargs > 3 && lua_isnumber(L, 4)) ? (int)lua_tointeger(L, 4) - 1 : 0;  // Convert to 0-based
    
    // Set parameters if provided
    if (time >= 0.0f) {
        metro_set_time(id, time);
    }
    if (count >= 0) {
        metro_set_count(id, count);
    }
    metro_set_stage(id, stage);
    
    // Start the metro
    metro_start(id);
    
    lua_pop(L, nargs);
    return 0;
}

static int crow_lua_metro_stop(lua_State *L) {
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "metro_stop requires 1 argument (id)");
    }
    
    int id = (int)luaL_checkinteger(L, 1) - 1;  // Convert to 0-based
    metro_stop(id);
    
    lua_pop(L, 1);
    return 0;
}

static int crow_lua_metro_set_time(lua_State *L) {
    if (lua_gettop(L) != 2) {
        return luaL_error(L, "metro_set_time requires 2 arguments (id, time)");
    }
    
    int id = (int)luaL_checkinteger(L, 1) - 1;      // Convert to 0-based
    float time = (float)luaL_checknumber(L, 2);
    
    metro_set_time(id, time);
    
    lua_pop(L, 2);
    return 0;
}

static int crow_lua_computer_card_unique_id(lua_State *L) {
    // Get unique ID from ComputerCard instance
    extern CrowEmulator* g_crow_emulator;
    if (g_crow_emulator) {
        uint64_t unique_id = g_crow_emulator->get_unique_card_id();
        lua_pushinteger(L, (lua_Integer)unique_id);
        return 1;
    }
    lua_pushinteger(L, 0);  // Fallback value
    return 1;
}

static int crow_lua_reset(lua_State *L) {
    // Reinitialize Lua environment and emit ready packet
    if (g_crow_lua) {
        g_crow_lua->deinit();
        if (g_crow_lua->init()) {
            if (CrowEmulator::instance) {
                CrowEmulator::instance->send_usb_string("^^ready()");
            }
        }
    }
    return 0;
}

// Crow lua globals - simplified single environment matching real crow
static int crow_lua_set_output_scale(lua_State *L) {
    int nargs = lua_gettop(L);
    if (nargs < 2) {
        return luaL_error(L, "set_output_scale(channel, table|'none' [, mod [, scaling]])");
    }
    int channel = (int)luaL_checkinteger(L, 1); // 1-based from Lua
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "channel out of range");
    }
    // Handle 'none'
    if (lua_isstring(L, 2)) {
        const char *s = lua_tostring(L, 2);
        if (strcmp(s, "none") == 0) {
            if (g_crow_emulator) g_crow_emulator->disable_output_scale(channel - 1);
            lua_settop(L, 0);
            return 0;
        }
        return luaL_error(L, "unknown string argument (expected 'none')");
    }
    if (!lua_istable(L, 2)) {
        return luaL_error(L, "second argument must be table or 'none'");
    }
    int tlen = (int)lua_rawlen(L, 2);
    if (tlen <= 0) {
        if (g_crow_emulator) g_crow_emulator->disable_output_scale(channel - 1);
        lua_settop(L, 0);
        return 0;
    }
    if (tlen > 16) tlen = 16;
    float degrees[16];
    for (int i = 0; i < tlen; i++) {
        lua_rawgeti(L, 2, i + 1);
        degrees[i] = (float)luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    int mod = 12;
    float scaling = 1.0f;
    if (nargs >= 3 && lua_isnumber(L, 3)) mod = (int)lua_tointeger(L, 3);
    if (nargs >= 4 && lua_isnumber(L, 4)) scaling = (float)lua_tonumber(L, 4);
    if (g_crow_emulator) g_crow_emulator->set_output_scale(channel - 1, degrees, tlen, mod, scaling);
    lua_settop(L, 0);
    return 0;
}

// Clock mode C bindings
static int crow_lua_set_output_clock(lua_State* L) {
    int nargs = lua_gettop(L);
    if (nargs < 2) {
        return luaL_error(L, "set_output_clock(channel, period [, width])");
    }
    int channel = (int)luaL_checkinteger(L, 1);
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "channel out of range");
    }
    float period = (float)luaL_checknumber(L, 2);
    float width = 0.01f;
    if (nargs >= 3 && lua_isnumber(L, 3)) {
        width = (float)lua_tonumber(L, 3);
    }
    if (g_crow_emulator) {
        g_crow_emulator->set_output_clock(channel - 1, period, width);
    }
    lua_settop(L, 0);
    return 0;
}

static int crow_lua_clear_output_clock(lua_State* L) {
    int nargs = lua_gettop(L);
    if (nargs < 1) {
        return luaL_error(L, "clear_output_clock(channel)");
    }
    int channel = (int)luaL_checkinteger(L, 1);
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "channel out of range");
    }
    if (g_crow_emulator) {
        g_crow_emulator->clear_output_clock(channel - 1);
    }
    lua_settop(L, 0);
    return 0;
}

static const char* crow_globals_lua = R"(
-- Crow globals initialization (single environment like real crow)
print("Crow Lua initializing...")

-- Create global output and input tables (matches crow architecture)
output = {}
input = {}

-- crow table with reset
crow = {}
function crow.reset()
    crow_reset()
end

-- Initialize output tables with crow-style interface
for i = 1, 4 do
    output[i] = {
        volts = 0,
        _volts_changed = false,
        _trigger = false,
        action = function(self, func)
            if func then self._action = func end
        end,
        slew = function(self, time, shape) end,
        dyn = function(self, ...) end
    }

    local ch = i

    setmetatable(output[i], {
        __newindex = function(t, k, v)
            if k == "volts" then
                rawset(t, k, v)
                rawset(t, "_volts_changed", true)
                return
            elseif k == "action" and type(v) == "function" then
                rawset(t, "_action", v)
                return
            elseif k == "scale" then
                if v == nil or (type(v) == 'string' and v == 'none') or
                   (type(v) == 'table' and v.degrees == nil and #v == 0) then
                    set_output_scale(ch, 'none')
                    rawset(t, k, 'none')
                    return
                elseif type(v) == 'table' then
                    local degrees_tbl = v.degrees or v
                    local mod = v.mod or v.divs or 12
                    local scaling = v.scaling or v.vpo or 1.0
                    set_output_scale(ch, degrees_tbl, mod, scaling)
                    rawset(t, k, v)
                    return
                end
                return
            end
            rawset(t, k, v)
        end,
        __call = function(t, ...)
            local args = {...}
            if #args > 0 then
                t.volts = args[1]
            end
            return t.volts
        end
    })

    -- Backwards-compatible function form for scale
    output[i].scale = function(arg, mod, scaling)
        if type(arg) == 'string' and arg == 'none' then
            set_output_scale(ch, 'none')
            return
        elseif type(arg) == 'table' then
            set_output_scale(ch, arg, mod or 12, scaling or 1.0)
            return
        end
    end

    -- Clock helpers
    output[i].clock = function(self, period, width)
        if type(period) == 'string' and period == 'stop' then
            clear_output_clock(ch)
            return
        end
        set_output_clock(ch, period, width or 0.01)
    end
    output[i].unclock = function(self)
        clear_output_clock(ch)
    end
end

-- Initialize input tables
for i = 1, 2 do  -- Only inputs 1 and 2 for audio inputs
    input[i] = {
        volts = 0,
        _last_volts = 0,
        
        -- Input event handlers
        -- Assign: input[i].clock = function(bpm, period) ... end  (set_input_clock configures detection)
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

-- Metro system (crow-style)
metro = {}
for i = 1, 8 do  -- 8 metros like crow
    metro[i] = {
        start = function(time, count, stage)
            metro_start(i, time or -1, count or -1, stage or 0)
        end,
        stop = function()
            metro_stop(i)
        end,
        time = function(time)
            if time then
                metro_set_time(i, time)
            end
        end
    }
end

-- Global metro functions (crow-style)
function metro_start(id, time, count, stage)
    crow_metro_start(id, time or -1, count or -1, stage or 0)
end

function metro_stop(id)
    crow_metro_stop(id)
end

function metro_set_time(id, time)
    crow_metro_set_time(id, time)
end

-- Metro handler (called from C code)
function metro_handler(id, stage)
    -- Default empty metro handler - user can override
    -- This matches crow's behavior
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
    
    // Register C functions for lua to call
    lua_register(L, "crow_metro_start", crow_lua_metro_start);
    lua_register(L, "crow_metro_stop", crow_lua_metro_stop);
    lua_register(L, "crow_metro_set_time", crow_lua_metro_set_time);
    lua_register(L, "computer_card_unique_id", crow_lua_computer_card_unique_id);
    lua_register(L, "set_output_scale", crow_lua_set_output_scale);
    lua_register(L, "crow_reset", crow_lua_reset);
    lua_register(L, "set_output_clock", crow_lua_set_output_clock);
    lua_register(L, "clear_output_clock", crow_lua_clear_output_clock);
    
    // Register CASL functions
    lua_register(L, "casl_describe", l_casl_describe);
    lua_register(L, "casl_action", l_casl_action);
    lua_register(L, "casl_defdynamic", l_casl_defdynamic);
    lua_register(L, "casl_cleardynamics", l_casl_cleardynamics);
    lua_register(L, "casl_setdynamic", l_casl_setdynamic);
    lua_register(L, "casl_getdynamic", l_casl_getdynamic);
    
    // Register detection system functions
    lua_register(L, "set_input_none", set_input_none);
    lua_register(L, "set_input_stream", set_input_stream);
    lua_register(L, "set_input_change", set_input_change);
    lua_register(L, "set_input_window", set_input_window);
    lua_register(L, "set_input_scale", set_input_scale);
    lua_register(L, "set_input_volume", set_input_volume);
    lua_register(L, "set_input_peak", set_input_peak);
    lua_register(L, "set_input_freq", set_input_freq);
    lua_register(L, "set_input_clock", set_input_clock);
    lua_register(L, "io_get_input", io_get_input);
    
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

bool CrowLua::call_metro_handler(int id, int stage) {
    if (!lua_initialized || !L) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    lua_getglobal(L, "metro_handler");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false; // No metro handler defined - this is OK
    }
    
    lua_pushinteger(L, id);     // metro id (1-indexed)
    lua_pushinteger(L, stage);  // stage (1-indexed)
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        printf("Error running metro_handler(%d, %d): %s\n", id, stage, lua_tostring(L, -1));
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

    // Drain any pending detection events (queued on Core 0)
    lua_State* L = g_crow_lua->raw_state();
    if (L) {
        crow_detect_drain_events(L);
    }
}
    
    void crow_lua_garbage_collect() {
        if (g_crow_lua) {
            g_crow_lua->garbage_collect();
        }
    }
    
    // Slopes lua bindings
    void crow_lua_register_slopes_functions(lua_State* L) {
        if (!L) return;
        
        // Register slopes_toward function
        lua_register(L, "slopes_toward", [](lua_State* L) -> int {
            int channel = luaL_checkinteger(L, 1) - 1;  // Convert to 0-based
            float destination = luaL_checknumber(L, 2);
            float time_s = luaL_checknumber(L, 3);
            const char* shape_str = luaL_optstring(L, 4, "linear");
            
            float time_ms = time_s * 1000.0f;
            crow_shape_t shape = crow_str_to_shape(shape_str);
            
            crow_slopes_toward(channel, destination, time_ms, shape, nullptr);
            return 0;
        });
        
        printf("Slopes lua functions registered\n");
    }
}
