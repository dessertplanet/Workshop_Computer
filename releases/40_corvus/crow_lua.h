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
    
    // Environment management (adapted from miditocv)
    static const int NUM_ENVIRONMENTS = 4; // 4 outputs for crow
    
    // Helper functions
    bool with_lua_env(int index);
    void setup_crow_globals();
    
public:
    CrowLua();
    ~CrowLua();
    
    // Initialization
    bool init();
    void deinit();
    
    // Core lua operations
    bool eval_simple(const char* script, size_t script_len, const char* chunkname);
    bool update_environment(int index, const char* code);
    
    // Hardware abstraction (crow-specific)
    bool get_output_volts_and_trigger(int channel, float* volts, bool* volts_new, bool* trigger);
    void set_input_volts(int channel, float volts);
    
    // Event callbacks (adapted from miditocv pattern)
    bool run_on_init(int index);
    bool run_on_step(int index);  
    float get_bpm(int index);
    
    // REPL integration
    bool is_lua_command(const char* command);
    bool execute_repl_command(const char* command, size_t length);
    
    // Memory and performance
    void garbage_collect();
    void process_periodic_tasks(uint32_t current_time_ms);
    uint32_t get_memory_usage();
    
    // Cross-core synchronization
    void schedule_script_update(int index, const char* script);
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
}
