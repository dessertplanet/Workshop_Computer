#include "crow_lua.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "pico/time.h"

// Global instance
CrowLua* g_crow_lua = nullptr;

// Basic crow lua globals (minimal set for now)
static const char* crow_globals_lua = R"(
-- Crow globals initialization
print("Crow Lua environment initializing...")

-- Create output and input tables
output = {}
input = {}

-- Basic utility functions
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

-- Environment management (adapted from miditocv)
envs = {}

function new_env(code)
    local env = {
        -- Crow-specific environment setup
        output = {},
        input = {},
        volts = 0,
        trigger = false,
        volts_new = false,
        -- Utility functions available in environment
        linlin = linlin,
        linexp = linexp,
        math = math,
        print = print
    }
    
    -- Load code into environment if provided
    if code and code ~= "" then
        local chunk, err = load(code, "user_script", "t", env)
        if chunk then
            local ok, result = pcall(chunk)
            if not ok then
                print("Error in user script: " .. tostring(result))
            end
        else
            print("Compile error: " .. tostring(err))
        end
    end
    
    return env
end

function update_env(index, code)
    envs[index] = new_env(code)
end

-- Output state management
function get_output_state(channel)
    if envs[channel] then
        local volts = envs[channel].volts or 0
        local trigger = envs[channel].trigger or false
        -- Reset trigger after reading
        envs[channel].trigger = false
        return volts, envs[channel].volts_new or false, trigger
    end
    return 0, false, false
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
    
    printf("Initializing Crow Lua VM...\n");
    
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
    
    // Initialize environments for each output
    for (int i = 1; i <= NUM_ENVIRONMENTS; i++) {
        if (!update_environment(i, "")) {
            printf("Failed to initialize environment %d\n", i);
        }
    }
    
    lua_initialized = true;
    printf("Crow Lua VM initialized successfully\n");
    return true;
}

void CrowLua::deinit() {
    if (L) {
        lua_close(L);
        L = nullptr;
    }
    lua_initialized = false;
}

bool CrowLua::eval_simple(const char* script, size_t script_len, const char* chunkname) {
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

bool CrowLua::update_environment(int index, const char* code) {
    if (!lua_initialized || !L || index < 1 || index > NUM_ENVIRONMENTS) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    lua_getglobal(L, "update_env");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    lua_pushinteger(L, index);
    lua_pushstring(L, code ? code : "");
    
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        printf("Error updating environment %d: %s\n", index, lua_tostring(L, -1));
        lua_pop(L, 1);
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    critical_section_exit(&lua_critical_section);
    return true;
}

bool CrowLua::with_lua_env(int index) {
    if (!lua_initialized || !L || index < 1 || index > NUM_ENVIRONMENTS) {
        return false;
    }
    
    lua_getglobal(L, "envs");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    
    lua_pushinteger(L, index);
    lua_gettable(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    
    return true;
}

bool CrowLua::get_output_volts_and_trigger(int channel, float* volts, bool* volts_new, bool* trigger) {
    if (!lua_initialized || !L || channel < 1 || channel > NUM_ENVIRONMENTS) {
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
    if (!lua_initialized || !L || channel < 1 || channel > 4) {
        return;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    // Set input[channel].volts = volts
    lua_getglobal(L, "input");
    if (lua_istable(L, -1)) {
        lua_pushinteger(L, channel);
        lua_newtable(L);  // Create input[channel] table if needed
        lua_pushstring(L, "volts");
        lua_pushnumber(L, volts);
        lua_settable(L, -3);  // input[channel].volts = volts
        lua_settable(L, -3);  // input[channel] = table
    }
    lua_pop(L, 1);
    
    critical_section_exit(&lua_critical_section);
}

float CrowLua::get_bpm(int index) {
    if (!lua_initialized || !L) {
        return -1;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    if (!with_lua_env(index)) {
        critical_section_exit(&lua_critical_section);
        return -1;
    }
    
    lua_getfield(L, -1, "bpm");
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 3);  // Pop bpm, envs[index], envs
        critical_section_exit(&lua_critical_section);
        return -1;
    }
    
    float bpm = (float)lua_tonumber(L, -1);
    lua_pop(L, 3);  // Pop bpm, envs[index], envs
    critical_section_exit(&lua_critical_section);
    return bpm;
}

bool CrowLua::run_on_init(int index) {
    if (!lua_initialized || !L) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    if (!with_lua_env(index)) {
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    lua_getfield(L, -1, "init");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 3);  // Pop init, envs[index], envs
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("Error running init for environment %d: %s\n", index, lua_tostring(L, -1));
        lua_pop(L, 3);  // Pop error, envs[index], envs
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    lua_pop(L, 2);  // Pop envs[index], envs
    critical_section_exit(&lua_critical_section);
    return true;
}

bool CrowLua::run_on_step(int index) {
    if (!lua_initialized || !L) {
        return false;
    }
    
    critical_section_enter_blocking(&lua_critical_section);
    
    if (!with_lua_env(index)) {
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    lua_getfield(L, -1, "step");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 3);  // Pop step, envs[index], envs
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("Error running step for environment %d: %s\n", index, lua_tostring(L, -1));
        lua_pop(L, 3);  // Pop error, envs[index], envs
        critical_section_exit(&lua_critical_section);
        return false;
    }
    
    lua_pop(L, 2);  // Pop envs[index], envs
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

bool CrowLua::is_lua_command(const char* command) {
    if (!command) return false;
    
    // Simple heuristic: if it doesn't start with ^^, it's likely lua
    if (command[0] == '^' && command[1] == '^') {
        return false;  // Crow system command
    }
    
    return true;  // Assume it's lua
}

bool CrowLua::execute_repl_command(const char* command, size_t length) {
    return eval_simple(command, length, "repl");
}

// C-style interface implementation
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
        if (!g_crow_lua) return false;
        return g_crow_lua->update_environment(index, script);
    }
    
    void crow_lua_process_events() {
        if (!g_crow_lua) return;
        
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        g_crow_lua->process_periodic_tasks(current_time);
        
        // Process events for each environment
        for (int i = 1; i <= 4; i++) {
            g_crow_lua->run_on_step(i);
        }
    }
    
    void crow_lua_garbage_collect() {
        if (g_crow_lua) {
            g_crow_lua->garbage_collect();
        }
    }
}
