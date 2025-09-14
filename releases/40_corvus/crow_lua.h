#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/critical_section.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

class CrowLua {
private:
    lua_State* L;
    critical_section_t lua_critical_section;
    
    // Cross-core communication
    volatile bool script_update_pending;
    volatile bool lua_initialized;
    
    // Memory management
    uint32_t last_gc_time;
    static const uint32_t GC_INTERVAL_MS = 20; // 20ms between GC cycles
    
    // Helper functions
    void setup_crow_globals();
    
public:
    CrowLua();
    ~CrowLua();
    
    // Initialization
    bool init();
    void deinit();
    
    // Core lua operations (simplified to match crow's single environment)
    bool eval_script(const char* script, size_t script_len, const char* chunkname);
    bool load_user_script(const char* code);
    
    // Hardware abstraction (crow-style - single global state)
    bool get_output_volts_and_trigger(int channel, float* volts, bool* volts_new, bool* trigger);
    void set_input_volts(int channel, float volts);
    
    // Event callbacks (crow-style)
    bool call_init();
    bool call_step();
    bool call_metro_handler(int id, int stage);
    
    // REPL integration
    bool is_lua_command(const char* command);
    bool execute_repl_command(const char* command, size_t length);
    
    // Memory and performance
    void garbage_collect();
    void process_periodic_tasks(uint32_t current_time_ms);
    uint32_t get_memory_usage();
    
    // Cross-core synchronization (simplified)
    void schedule_script_update(const char* script);
    bool process_pending_updates();
    
    // Error handling
    const char* get_last_error();
    
    // State queries
    bool is_initialized() const { return lua_initialized; }
};

// Global instance (singleton pattern for cross-core access)
extern CrowLua* g_crow_lua;

// C-style interface for core 1 access
extern "C" {
    bool crow_lua_init();
    void crow_lua_deinit();
    bool crow_lua_eval_repl(const char* command, size_t length);
    bool crow_lua_update_script(int index, const char* script);
    void crow_lua_process_events();
    void crow_lua_garbage_collect();
    
    // Slopes and ASL lua bindings
    void crow_lua_register_slopes_functions(lua_State* L);
    
    // CASL lua binding functions
    int l_casl_describe(lua_State* L);
    int l_casl_action(lua_State* L);
    int l_casl_defdynamic(lua_State* L);
    int l_casl_cleardynamics(lua_State* L);
    int l_casl_setdynamic(lua_State* L);
    int l_casl_getdynamic(lua_State* L);
}
