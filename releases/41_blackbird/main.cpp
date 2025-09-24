#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// Lua includes
extern "C" {
#include "lua/src/lua.h"
#include "lua/src/lualib.h"
#include "lua/src/lauxlib.h"
}

// Crow ASL includes
extern "C" {
#include "lib/ashapes.h"
#include "lib/slopes.h"
#include "lib/casl.h"
#include "lib/detect.h"
#include "lib/events.h"
#include "lib/l_crowlib.h"
#include "lib/ll_timers.h"
#include "lib/metro.h"
}

// Generated Lua bytecode headers
#include "test_enhanced_multicore_safety.h"
#include "test_lockfree_performance.h"
#include "test_random_voltage.h"
#include "asl.h"
#include "asllib.h"
#include "output.h"
#include "input.h"
#include "metro.h"
#include "First.h"

// Lock-free output state storage for maximum performance
extern "C" {
#include "lib/lockfree.h"
}

static lockfree_output_state_t g_output_state;

// Initialize lock-free output state system
static void init_output_state_protection() {
    lockfree_output_init(&g_output_state);
    printf("Lock-free output state system initialized\n");
}

// Set output state using lock-free algorithm
static void set_output_state_atomic(int channel, int32_t value) {
    lockfree_output_set(&g_output_state, channel, value);
}

// Get single channel output state using lock-free algorithm
static int32_t get_output_state_atomic(int channel) {
    return lockfree_output_get(&g_output_state, channel);
}

// Get all output states atomically (consistent snapshot)
static bool get_all_output_states_atomic(int32_t values[4]) {
    return lockfree_output_get_all(&g_output_state, values);
}

// Forward declaration
class BlackbirdCrow;
static volatile BlackbirdCrow* g_blackbird_instance = nullptr;

// Global slopes mutex for thread safety
#ifdef PICO_BUILD
static mutex_t slopes_mutex;
static bool slopes_mutex_initialized = false;
#endif

// Forward declaration of C interface function (implemented after BlackbirdCrow class)
extern "C" void hardware_output_set_voltage(int channel, float voltage);

// Forward declaration of safe event handler
extern "C" void L_handle_change_safe(event_t* e);

/*

Blackbird Crow Emulator - Basic Communication Protocol

This implements the basic crow command protocol using stdio USB:
- ^^v - Version request
- ^^i - Identity request  
- ^^p - Print script request

Commands use crow-style responses with \n\r line endings.

To test, connect USB and use a serial terminal at 115200 baud.
Send commands like ^^v and ^^i to test the protocol.
*/

// Command types (from crow's caw.h)
typedef enum {
    C_none = 0,
    C_repl,
    C_boot,
    C_startupload,
    C_endupload,
    C_flashupload,
    C_restart,
    C_print,
    C_version,
    C_identity,
    C_killlua,
    C_flashclear,
    C_loadFirst
} C_cmd_t;

// Output userdata structure for metamethods
typedef struct {
    int channel;
} OutputUserData;

// Simple Lua Manager for crow emulation
class LuaManager {
public:
    lua_State* L;  // Made public for direct access in main.cpp
private:
    static LuaManager* instance;
    
#ifdef PICO_BUILD
    mutex_t lua_mutex;
    bool lua_mutex_initialized;
#endif
    
    // Lua print function - sends output to serial
    static int lua_print(lua_State* L) {
        int n = lua_gettop(L);  // number of arguments
        lua_getglobal(L, "tostring");
        for (int i = 1; i <= n; i++) {
            lua_pushvalue(L, -1);  // function to be called
            lua_pushvalue(L, i);   // value to print
            lua_call(L, 1, 1);
            const char* s = lua_tostring(L, -1);  // get result
            if (s != NULL) {
                if (i > 1) printf("\t");
                printf("%s", s);
            }
            lua_pop(L, 1);  // pop result
        }
        printf("\n\r");
        fflush(stdout);
        return 0;
    }
    
    // Lua time function - returns milliseconds since boot
    static int lua_time(lua_State* L) {
        uint32_t time_ms = to_ms_since_boot(get_absolute_time());
        lua_pushnumber(L, time_ms / 1000.0); // crow returns seconds
        return 1;
    }
    
    // Forward declaration - implementation after BlackbirdCrow class
    static int lua_unique_card_id(lua_State* L);
    
    // Lua function to run enhanced multicore safety test
    static int lua_test_enhanced_multicore_safety(lua_State* L) {
        printf("Running enhanced multicore safety test...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_enhanced_multicore_safety, test_enhanced_multicore_safety_len, "test_enhanced_multicore_safety.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running enhanced multicore safety test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Enhanced multicore safety test completed successfully!\n\r");
        }
        return 0;
    }
    
    // Lua function to run lock-free performance test
    static int lua_test_lockfree_performance(lua_State* L) {
        printf("Running lock-free performance test...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_lockfree_performance, test_lockfree_performance_len, "test_lockfree_performance.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running lock-free performance test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Lock-free performance test completed successfully!\n\r");
        }
        return 0;
    }
    
    // Lua function to run random voltage test
    static int lua_test_random_voltage(lua_State* L) {
        printf("Running random voltage test...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_random_voltage, test_random_voltage_len, "test_random_voltage.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running random voltage test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Random voltage test loaded successfully!\n\r");
        }
        return 0;
    }
    
    // Lua tab.print function - pretty print tables
    static int lua_tab_print(lua_State* L) {
        if (lua_gettop(L) != 1) {
            lua_pushstring(L, "tab.print expects exactly one argument");
            lua_error(L);
        }
        
        print_table_recursive(L, 1, 0);
        printf("\n\r");
        fflush(stdout);
        return 0;
    }
    
    // Helper function to recursively print table contents
    static void print_table_recursive(lua_State* L, int index, int depth) {
        if (!lua_istable(L, index)) {
            // Not a table, just print the value
            lua_getglobal(L, "tostring");
            lua_pushvalue(L, index);
            lua_call(L, 1, 1);
            const char* s = lua_tostring(L, -1);
            if (s) printf("%s", s);
            lua_pop(L, 1);
            return;
        }
        
        printf("{\n");
        
        // Iterate through table
        lua_pushnil(L);  // first key
        while (lua_next(L, index) != 0) {
            // Print indentation
            for (int i = 0; i < depth + 1; i++) printf("  ");
            
            // Print key
            if (lua_type(L, -2) == LUA_TSTRING) {
                printf("%s = ", lua_tostring(L, -2));
            } else if (lua_type(L, -2) == LUA_TNUMBER) {
                printf("[%.0f] = ", lua_tonumber(L, -2));
            } else {
                printf("[?] = ");
            }
            
            // Print value
            if (lua_istable(L, -1) && depth < 3) {  // Limit recursion depth
                print_table_recursive(L, lua_gettop(L), depth + 1);
            } else {
                lua_getglobal(L, "tostring");
                lua_pushvalue(L, -2);  // the value
                lua_call(L, 1, 1);
                const char* s = lua_tostring(L, -1);
                if (s) printf("%s", s);
                lua_pop(L, 1);
            }
            
            printf(",\n");
            lua_pop(L, 1);  // remove value, keep key for next iteration
        }
        
        // Print closing brace with proper indentation
        for (int i = 0; i < depth; i++) printf("  ");
        printf("}");
    }

public:
    LuaManager() : L(nullptr) {
        instance = this;
        
#ifdef PICO_BUILD
        mutex_init(&lua_mutex);
        lua_mutex_initialized = true;
        printf("Lua mutex initialized\n");
#endif
        
        init();
    }
    
    ~LuaManager() {
        if (L) {
            lua_close(L);
        }
        instance = nullptr;
        
#ifdef PICO_BUILD
        if (lua_mutex_initialized) {
            // Note: RP2040 SDK doesn't have mutex_destroy, mutex is automatically cleaned up
            lua_mutex_initialized = false;
        }
#endif
    }
    
    void init() {
        if (L) {
            lua_close(L);
        }
        
        L = luaL_newstate();
        if (!L) {
            printf("Error: Could not create Lua state\n\r");
            return;
        }
        
        // Load basic Lua libraries
        luaL_openlibs(L);
        
        // Override print function
        lua_register(L, "print", lua_print);
        
    // Add time function
    lua_register(L, "time", lua_time);
    
    // Add unique_card_id function for Workshop Computer compatibility
    lua_register(L, "unique_card_id", lua_unique_card_id);
    
    // Register test functions
    lua_register(L, "test_enhanced_multicore_safety", lua_test_enhanced_multicore_safety);
    lua_register(L, "test_lockfree_performance", lua_test_lockfree_performance);
    lua_register(L, "test_random_voltage", lua_test_random_voltage);
    
    // Essential crow library functions will be added after we implement them
    // lua_register(L, "nop_fn", lua_nop_fn);
    // lua_register(L, "get_out", lua_get_out);
    // lua_register(L, "get_cv", lua_get_cv);
    // lua_register(L, "math_random_enhanced", lua_math_random_enhanced);
    
    // Create tab table and add print function
    lua_newtable(L);
    lua_pushcfunction(L, lua_tab_print);
    lua_setfield(L, -2, "print");
    lua_setglobal(L, "tab");
    
    // Register CASL functions
    lua_register(L, "casl_describe", lua_casl_describe);
    lua_register(L, "casl_action", lua_casl_action);
    lua_register(L, "casl_defdynamic", lua_casl_defdynamic);
    lua_register(L, "casl_cleardynamics", lua_casl_cleardynamics);
    lua_register(L, "casl_setdynamic", lua_casl_setdynamic);
    lua_register(L, "casl_getdynamic", lua_casl_getdynamic);
    
    // Register crow backend functions for Output.lua compatibility
    lua_register(L, "LL_get_state", lua_LL_get_state);
    lua_register(L, "set_output_scale", lua_set_output_scale);
    
    // Register crow backend functions for Input.lua compatibility
    lua_register(L, "io_get_input", lua_io_get_input);
    lua_register(L, "set_input_stream", lua_set_input_stream);
    lua_register(L, "set_input_change", lua_set_input_change);
    lua_register(L, "set_input_window", lua_set_input_window);
    lua_register(L, "set_input_scale", lua_set_input_scale);
    lua_register(L, "set_input_volume", lua_set_input_volume);
    lua_register(L, "set_input_peak", lua_set_input_peak);
    lua_register(L, "set_input_freq", lua_set_input_freq);
    lua_register(L, "set_input_clock", lua_set_input_clock);
    lua_register(L, "set_input_none", lua_set_input_none);
    
    // Register metro system functions for full crow compatibility
    lua_register(L, "metro_start", lua_metro_start);
    lua_register(L, "metro_stop", lua_metro_stop);
    lua_register(L, "metro_set_time", lua_metro_set_time);
    lua_register(L, "metro_set_count", lua_metro_set_count);
    
    // Create _c table for _c.tell function
    lua_newtable(L);
    lua_pushcfunction(L, lua_c_tell);
    lua_setfield(L, -2, "tell");
    lua_setglobal(L, "_c");
    
    // Initialize CASL instances for all 4 outputs
    for (int i = 0; i < 4; i++) {
        casl_init(i);
    }
    
    // Initialize crow output functionality (will be replaced with Output.lua)
    // init_crow_bindings();
        
        // Load and execute embedded ASL libraries
        load_embedded_asl();
    }
    
    // Load embedded ASL libraries using luaL_dobuffer - made public for manual debug triggers
    void load_embedded_asl() {
        if (!L) return;
        
        // Load ASL library first
        printf("Loading embedded ASL library...\n\r");
        if (luaL_loadbuffer(L, (const char*)asl, asl_len, "asl.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading ASL library: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
            return;
        }
        
        // ASL library returns the Asl table - capture it
        lua_setglobal(L, "Asl");
        
        // Also set up lowercase 'asl' for compatibility
        lua_getglobal(L, "Asl");
        lua_setglobal(L, "asl");
        
        // Load ASLLIB library
        printf("Loading embedded ASLLIB library...\n\r");
        if (luaL_loadbuffer(L, (const char*)asllib, asllib_len, "asllib.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading ASLLIB library: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
            return;
        }
        
        // The ASLLIB functions are now available globally
        // Make sure basic ASL functions are available globally
        const char* setup_globals = R"(
            -- Make ASL library functions globally available
            for name, func in pairs(Asllib or {}) do
                _G[name] = func
            end
        )";
        
        if (luaL_dostring(L, setup_globals) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error setting up ASL globals: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        // Load Output.lua class from embedded bytecode
        printf("Loading embedded Output.lua class...\n\r");
        if (luaL_loadbuffer(L, (const char*)output, output_len, "output.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading Output.lua: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            // Output.lua returns the Output table - capture it
            lua_setglobal(L, "Output");
            
            // Create output[1] through output[4] instances using Output.new()
            if (luaL_dostring(L, R"(
                output = {}
                for i = 1, 4 do
                    output[i] = Output.new(i)
                end
                print("Output objects created successfully!")
            )") != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                printf("Error creating output objects: %s\n\r", error ? error : "unknown error");
                lua_pop(L, 1);
            } else {
                printf("Output.lua loaded successfully!\n\r");
            }
        }
        
        // Load Input.lua class from embedded bytecode
        printf("Loading embedded Input.lua class...\n\r");
        if (luaL_loadbuffer(L, (const char*)input, input_len, "input.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading Input.lua: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            // Input.lua returns the Input table - capture it
            lua_setglobal(L, "Input");
            
            // Create input[1] and input[2] instances using Input.new() (crow-style)
            if (luaL_dostring(L, R"(
                input = {}
                for i = 1, 2 do
                    input[i] = Input.new(i)
                end
            )") != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                printf("Error creating input objects: %s\n\r", error ? error : "unknown error");
                lua_pop(L, 1);
            } else {
                printf("Input.lua loaded and objects created successfully!\n\r");
            }
        }
        
        // Load Metro.lua class from embedded bytecode (CRITICAL for First.lua)
        printf("Loading embedded Metro.lua class...\n\r");
        if (luaL_loadbuffer(L, (const char*)metro, metro_len, "metro.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading Metro.lua: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            // Metro.lua returns the metro table - capture it as global "metro"
            lua_setglobal(L, "metro");
            printf("Metro.lua loaded as global 'metro' object!\n\r");
        }
        
        // Set up crow-style global handlers for event dispatching
        if (luaL_dostring(L, R"(
            -- Global change_handler function like real crow
            function change_handler(channel, state)
                if input and input[channel] and input[channel].change then
                    input[channel].change(state)
                else
                    print("change: ch" .. channel .. "=" .. tostring(state))
                end
            end
            
            -- Global stream_handler function like real crow  
            function stream_handler(channel, value)
                if input and input[channel] and input[channel].stream then
                    input[channel].stream(value)
                else
                    print("stream: ch" .. channel .. "=" .. tostring(value))
                end
            end
            
            print("Global event handlers set up successfully!")
        )") != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error setting up global handlers: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        printf("ASL libraries loaded successfully!\n\r");
    }
    
    
    // Initialize crow-compatible Lua bindings with userdata metamethods
    void init_crow_bindings() {
        if (!L) return;
        
        // Create the output metatable
        luaL_newmetatable(L, "Output");
        
        // Set __index metamethod
        lua_pushstring(L, "__index");
        lua_pushcfunction(L, output_index);
        lua_settable(L, -3);
        
        // Set __newindex metamethod
        lua_pushstring(L, "__newindex");
        lua_pushcfunction(L, output_newindex);
        lua_settable(L, -3);
        
        // Pop the metatable
        lua_pop(L, 1);
        
        // Create output table
        lua_newtable(L);
        
        // Create output[1] through output[4] userdata objects
        for (int i = 1; i <= 4; i++) {
            // Create userdata for this output
            OutputUserData* output_data = (OutputUserData*)lua_newuserdata(L, sizeof(OutputUserData));
            output_data->channel = i;
            
            // Set the metatable
            luaL_getmetatable(L, "Output");
            lua_setmetatable(L, -2);
            
            // Set output[i] = this userdata
            lua_seti(L, -2, i);
        }
        
        lua_setglobal(L, "output");
    }
    
    // Metamethod functions for output objects
    static int output_index(lua_State* L);
    static int output_newindex(lua_State* L);
    
    // CASL bridge functions
    static int lua_casl_describe(lua_State* L);
    static int lua_casl_action(lua_State* L);
    static int lua_casl_defdynamic(lua_State* L);
    static int lua_casl_cleardynamics(lua_State* L);
    static int lua_casl_setdynamic(lua_State* L);
    static int lua_casl_getdynamic(lua_State* L);
    
    // Crow backend functions for Output.lua compatibility
    static int lua_LL_get_state(lua_State* L);
    static int lua_set_output_scale(lua_State* L);
    static int lua_c_tell(lua_State* L);
    
    // Crow backend functions for Input.lua compatibility
    static int lua_io_get_input(lua_State* L);
    static int lua_set_input_stream(lua_State* L);
    static int lua_set_input_change(lua_State* L);
    static int lua_set_input_window(lua_State* L);
    static int lua_set_input_scale(lua_State* L);
    static int lua_set_input_volume(lua_State* L);
    static int lua_set_input_peak(lua_State* L);
    static int lua_set_input_freq(lua_State* L);
    static int lua_set_input_clock(lua_State* L);
    static int lua_set_input_none(lua_State* L);
    
    // Essential crow library functions that scripts expect
    static int lua_nop_fn(lua_State* L);          // function() end
    static int lua_get_out(lua_State* L);         // get_out(channel) -> voltage
    static int lua_get_cv(lua_State* L);          // get_cv(channel) -> voltage  
    static int lua_math_random_enhanced(lua_State* L); // Enhanced random with true randomness
    
    // Metro system Lua bindings
    static int lua_metro_start(lua_State* L);
    static int lua_metro_stop(lua_State* L);
    static int lua_metro_set_time(lua_State* L);
    static int lua_metro_set_count(lua_State* L);
    
    // Forward declaration - implementation will be after BlackbirdCrow class
    static int lua_output_volts(lua_State* L);
    
    // Evaluate Lua code and return result
    bool evaluate(const char* code) {
        if (!L) return false;
        
        int result = luaL_dostring(L, code);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("lua error: %s\n\r", error ? error : "unknown error");
            fflush(stdout);
            lua_pop(L, 1);  // remove error from stack
            return false;
        }
        
        return true;
    }
    
    // Safe evaluation with error handling - prevents crashes from user code
    bool evaluate_safe(const char* code) {
        if (!L) return false;
        
        // Use protected call to prevent crashes
        int result = luaL_loadstring(L, code);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("lua load error: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
            return false;
        }
        
        // Call with error handler
        result = lua_pcall(L, 0, 0, 0);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("lua runtime error: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
            return false;
        }
        
        return true;
    }
    
    // Thread-safe wrapper for evaluate() - protects Lua state with mutex
    bool evaluate_thread_safe(const char* code) {
        if (!L) return false;
        
#ifdef PICO_BUILD
        if (lua_mutex_initialized) {
            mutex_enter_blocking(&lua_mutex);
            bool result = evaluate(code);
            mutex_exit(&lua_mutex);
            return result;
        }
#endif
        // Fall back to non-thread-safe version if mutex not available
        return evaluate(code);
    }
    
    // Thread-safe wrapper for evaluate_safe() - protects Lua state with mutex
    bool evaluate_safe_thread_safe(const char* code) {
        if (!L) return false;
        
#ifdef PICO_BUILD
        if (lua_mutex_initialized) {
            mutex_enter_blocking(&lua_mutex);
            bool result = evaluate_safe(code);
            mutex_exit(&lua_mutex);
            return result;
        }
#endif
        // Fall back to non-thread-safe version if mutex not available
        return evaluate_safe(code);
    }
    
    // Non-blocking wrapper for evaluate_safe() - for use in event handlers
    // Returns false if mutex is busy (don't wait)
    bool evaluate_safe_non_blocking(const char* code) {
        if (!L) return false;
        
#ifdef PICO_BUILD
        if (lua_mutex_initialized) {
            // Try to enter mutex without blocking
            if (!mutex_try_enter(&lua_mutex, nullptr)) {
                // Mutex is busy - skip this call to prevent deadlock
                printf("Lua mutex busy - skipping event handler call\n\r");
                return false;
            }
            
            bool result = evaluate_safe(code);
            mutex_exit(&lua_mutex);
            return result;
        }
#endif
        // Fall back to non-thread-safe version if mutex not available
        return evaluate_safe(code);
    }
    
    static LuaManager* getInstance() {
        return instance;
    }
};

// Static instance pointer
LuaManager* LuaManager::instance = nullptr;

class BlackbirdCrow : public ComputerCard
{
    // Variables for communication between cores
    volatile uint32_t v1, v2;
    
    // USB communication buffer
    static const int USB_RX_BUFFER_SIZE = 256;
    char rx_buffer[USB_RX_BUFFER_SIZE];
    int rx_buffer_pos;
    
    // Lua manager for REPL
    LuaManager* lua_manager;
    
public:
    // Cached unique ID for Lua access (since UniqueCardID() is protected)
    uint64_t cached_unique_id;
    // Hardware abstraction functions for output
    void hardware_set_output(int channel, float volts) {
        if (channel < 1 || channel > 4) return;
        
        // Clamp voltage to ±6V range
        if (volts > 6.0f) volts = 6.0f;
        if (volts < -6.0f) volts = -6.0f;
        
        // Convert to DAC range: -6V to +6V maps to -2048 to +2047
        // Use integer math for RP2040 efficiency
        int32_t volts_mV = (int32_t)(volts * 1000.0f);
        int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
        
        // Store state for lua queries (in millivolts) - thread-safe
        set_output_state_atomic(channel - 1, volts_mV);
        
        // Route to correct hardware output
        switch (channel) {
            case 1: // Output 1 → AudioOut1
                AudioOut1(dac_value);
                break;
            case 2: // Output 2 → AudioOut2  
                AudioOut2(dac_value);
                break;
            case 3: // Output 3 → CVOut1
                CVOut1(dac_value);
                break;
            case 4: // Output 4 → CVOut2
                CVOut2(dac_value);
                break;
        }
    }
    
    float hardware_get_output(int channel) {
        if (channel < 1 || channel > 4) return 0.0f;
        return (float)get_output_state_atomic(channel - 1) / 1000.0f;
    }
    
    // Hardware abstraction functions for input
    float hardware_get_input(int channel) {
        if (channel < 1 || channel > 2) return 0.0f;
        
        int16_t raw_value = 0;
        if (channel == 1) {
            raw_value = AudioIn1();
        } else if (channel == 2) {
            raw_value = AudioIn2();
        }
        
        // Convert from ComputerCard range (-2048 to 2047) to crow voltage range (±6V)
        return (float)raw_value * 6.0f / 2048.0f;
    }
    
    // Public LED control functions for debugging
    void debug_led_on(int index) {
        if (index >= 0 && index <= 5) {
            LedOn(index, true);
        }
    }
    
    void debug_led_off(int index) {
        if (index >= 0 && index <= 5) {
            LedOn(index, false);
        }
    }
    
    BlackbirdCrow()
    {
        rx_buffer_pos = 0;
        memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        // Cache the unique ID for Lua access
        cached_unique_id = UniqueCardID();
        
        // Set global instance for Lua bindings
        g_blackbird_instance = this;
        
        // Initialize slopes system for crow-style output processing
        S_init(4); // Initialize 4 output channels
        
        // Initialize slopes mutex for thread safety
#ifdef PICO_BUILD
        mutex_init(&slopes_mutex);
        slopes_mutex_initialized = true;
        printf("Slopes mutex initialized\n");
#endif
        
        // Initialize detection system for 2 input channels
        Detect_init(2);
        
        // Initialize thread-safe output state protection
        init_output_state_protection();
        
        // Initialize event system - CRITICAL for processing input events
        events_init();
        
        // Initialize timer system for metro support (8 timers for full crow compatibility)
        Timer_Init(8);
        
        // Initialize metro system (depends on timer system)
        Metro_Init(8);
        
        // Initialize Lua manager
        lua_manager = new LuaManager();
        
        // Start the second core for USB processing
        multicore_launch_core1(core1);
    }
    
    ~BlackbirdCrow() {
        if (lua_manager) {
            delete lua_manager;
        }
    }

    // Boilerplate to call member function as second core
    static void core1()
    {
        ((BlackbirdCrow *)ThisPtr())->USBProcessingCore();
    }

    // Parse command from buffer
    C_cmd_t parse_command(const char* buffer, int length)
    {
        for (int i = 0; i < length - 2; i++) {
            if (buffer[i] == '^' && buffer[i + 1] == '^') {
                switch (buffer[i + 2]) {
                    case 'v': return C_version;
                    case 'i': return C_identity;
                    case 'p': return C_print;
                    case 'r': return C_restart;
                    case 'b': return C_boot;
                    case 's': return C_startupload;
                    case 'e': return C_endupload;
                    case 'w': return C_flashupload;
                    case 'c': return C_flashclear;
                    case 'k': return C_killlua;
                    case 'f':
                    case 'F': return C_loadFirst;
                }
            }
        }
        return C_none;
    }
    
    // Send string with crow-style line ending (\n\r)
    void send_crow_response(const char* text)
    {
        printf("%s", text);
        // Send raw bytes for proper crow line ending
        putchar_raw('\n');
        putchar_raw('\r');
        fflush(stdout);
    }
    
    // Handle crow commands
    void handle_command(C_cmd_t cmd)
    {
        switch (cmd) {
            case C_version:
                send_crow_response("^^version('blackbird-0.1')");
                break;
                
            case C_identity: {
                uint64_t unique_id = UniqueCardID();
                char response[64];
                snprintf(response, sizeof(response), "^^identity('0x%016llx')", unique_id);
                send_crow_response(response);
                break;
            }
            
            case C_print:
                send_crow_response("-- no script loaded --");
                break;
                
            case C_restart:
                send_crow_response("restarting...");
                // Could implement actual restart here
                break;
                
            case C_killlua:
                send_crow_response("lua killed");
                break;
                
            case C_boot:
                send_crow_response("entering bootloader mode");
                break;
                
            case C_startupload:
                send_crow_response("script upload started");
                break;
                
            case C_endupload:
                send_crow_response("script uploaded");
                break;
                
            case C_flashupload:
                send_crow_response("script saved to flash");
                break;
                
            case C_flashclear:
                send_crow_response("flash cleared");
                break;
                
            case C_loadFirst:
                send_crow_response("loading first.lua");
                printf("[first] handler invoked, attempting bytecode load\n\r");
                // Actually load First.lua using the compiled bytecode
                if (lua_manager) {
                    printf("Loading First.lua from embedded bytecode...\n\r");
                    if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") != LUA_OK || lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
                        const char* error = lua_tostring(lua_manager->L, -1);
                        printf("Error loading First.lua: %s\n\r", error ? error : "unknown error");
                        lua_pop(lua_manager->L, 1);
                        send_crow_response("error loading first.lua");
                    } else {
                        printf("First.lua loaded and executed successfully!\n\r");

                        // Model real crow: reset runtime so newly loaded script boots
                        if (!lua_manager->evaluate_safe_thread_safe("if crow and crow.reset then crow.reset() end")) {
                            printf("Warning: crow.reset() failed after First.lua load\n\r");
                        }
                        if (!lua_manager->evaluate_safe_thread_safe("local ok, err = pcall(function() if init then init() end end); if not ok then print('init() error', err) end")) {
                            printf("Warning: init() invocation failed after First.lua load\n\r");
                        }

                        float input1_volts = hardware_get_input(1);
                        float input2_volts = hardware_get_input(2);
                        printf("[diag] input volts after load: in1=%.3fV in2=%.3fV\n\r", input1_volts, input2_volts);

                        send_crow_response("first.lua loaded");
                    }
                } else {
                    send_crow_response("error: lua manager not available");
                }
                break;
                
            default:
                // For unimplemented commands, send a simple acknowledgment
                send_crow_response("ok");
                break;
        }
    }
    
    // Check if we have a complete packet (ends with newline or carriage return)
    bool is_packet_complete(const char* buffer, int length)
    {
        if (length == 0) return false;
        char last_char = buffer[length - 1];
        return (last_char == '\n' || last_char == '\r' || last_char == '\0');
    }

    // Core 1: USB Processing (blocking)
    void USBProcessingCore()
    {
        printf("Blackbird Crow Emulator v0.1\n");
        printf("Send ^^v for version, ^^i for identity\n");
        
        while (1) {
            // Read available characters
            int c = getchar_timeout_us(1000); // 1ms timeout
            
            if (c != PICO_ERROR_TIMEOUT) {
                // Add character to buffer
                if (rx_buffer_pos < USB_RX_BUFFER_SIZE - 1) {
                    rx_buffer[rx_buffer_pos++] = (char)c;
                    rx_buffer[rx_buffer_pos] = '\0';
                    
                    // Check if we have a complete packet
                    if (is_packet_complete(rx_buffer, rx_buffer_pos)) {
                        // Strip trailing whitespace
                        int clean_length = rx_buffer_pos;
                        while (clean_length > 0 && 
                               (rx_buffer[clean_length-1] == '\n' || 
                                rx_buffer[clean_length-1] == '\r' || 
                                rx_buffer[clean_length-1] == ' ' || 
                                rx_buffer[clean_length-1] == '\t')) {
                            clean_length--;
                        }
                        rx_buffer[clean_length] = '\0';
                        
                        // Skip empty commands
                        if (clean_length == 0) {
                            rx_buffer_pos = 0;
                            memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
                            continue;
                        }
                        
                        // Parse and handle command
                        C_cmd_t cmd = parse_command(rx_buffer, clean_length);
                        if (cmd != C_none) {
                            handle_command(cmd);
                        } else if (strcmp(rx_buffer, "test_enhanced_multicore_safety") == 0) {
                            // Comprehensive multicore safety test
                            if (lua_manager) {
                                lua_manager->evaluate_thread_safe("test_enhanced_multicore_safety()");
                            }
                        } else if (strcmp(rx_buffer, "test_lockfree_performance") == 0) {
                            // Lock-free performance benchmark test
                            if (lua_manager) {
                                lua_manager->evaluate_thread_safe("test_lockfree_performance()");
                            }
                        } else if (strcmp(rx_buffer, "test_random_voltage") == 0) {
                            // Random voltage on rising edge test
                            if (lua_manager) {
                                lua_manager->evaluate_thread_safe("test_random_voltage()");
                            }
                        } else if (strcmp(rx_buffer, "debug_input_loading") == 0) {
                            // Manual debug of Input.lua loading - can be triggered after connection
                            if (lua_manager) {
                                printf("=== MANUAL INPUT DEBUG TRIGGERED ===\n\r");
                                lua_manager->load_embedded_asl();  // This will re-run the Input.lua loading with debug output
                                printf("=== INPUT DEBUG COMPLETED ===\n\r");
                            }
                        } else if (strcmp(rx_buffer, "check_input_state") == 0) {
                            // Check current state of input objects
                            if (lua_manager) {
                                printf("=== CHECKING INPUT STATE ===\n\r");
                                lua_manager->evaluate_thread_safe("print('Input class:', Input); print('input array:', input); if input then for i=1,2 do print('input[' .. i .. ']:', input[i]) end else print('input is nil!') end");
                                printf("=== INPUT STATE CHECK DONE ===\n\r");
                            }
                        } else {
                            // Not a ^^ command, treat as Lua code
                            if (lua_manager) {
                                lua_manager->evaluate_thread_safe(rx_buffer);
                            }
                        }
                        
                        // Clear buffer
                        rx_buffer_pos = 0;
                        memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
                    }
                } else {
                    // Buffer overflow - clear it
                    rx_buffer_pos = 0;
                    memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
                    send_crow_response("!buffer overflow!");
                }
            }
            
            // // Optional: periodically show CV values for debugging
            // static uint32_t last_debug = 0;
            // uint32_t now = to_ms_since_boot(get_absolute_time());
            // if (now - last_debug > 5000) { // every 5 seconds
            //     last_debug = now;
            //     char debug[64];
            //     snprintf(debug, sizeof(debug), "-- CV1: %ld, CV2: %ld --", (long)v1, (long)v2);
            //     send_crow_response(debug);
            // }
        }
    }

    // 48kHz audio processing function
    virtual void ProcessSample()
    {
        // CRITICAL: Process timers first - this drives the metro system at exact 48kHz
        Timer_Process();
        
        // Hardware verification LEDs - test basic functionality
        static uint32_t heartbeat_counter = 0;
        static uint32_t input_test_counter = 0;
        
        // LED 5: Heartbeat - blinks every second to prove ProcessSample is running
        if (++heartbeat_counter >= 48000) { // 1 second at 48kHz
            heartbeat_counter = 0;
            static bool heartbeat_state = false;
            heartbeat_state = !heartbeat_state;
            debug_led_on(5); // LED 5 for heartbeat
            if (!heartbeat_state) {
                debug_led_off(5);
            }
        }
        
        // LED 4: Input activity - lights when any input signal detected
        if (++input_test_counter >= 1000) { // Check every ~20ms
            input_test_counter = 0;
            
            // Get raw input values directly from hardware
            int16_t raw1 = AudioIn1();
            int16_t raw2 = AudioIn2();
            
            // Convert to voltages for display
            float input1 = hardware_get_input(1);
            float input2 = hardware_get_input(2);
            
            // LED 4: Any significant input detected (above 0.5V threshold)
            if (fabs(input1) > 0.5f || fabs(input2) > 0.5f) {
                debug_led_on(4);
            } else {
                debug_led_off(4);
            }
            
            // // Debug print every 500ms (24000 samples)
            // static uint32_t print_counter = 0;
            // if (++print_counter >= 500) {
            //     print_counter = 0;
            //     printf("HW: raw1=%d raw2=%d, volt1=%.3f volt2=%.3f\n\r", 
            //            raw1, raw2, input1, input2);
            // }
        }
        
        // Run detection on every sample to capture fast DC-coupled edges reliably
        const bool input1_connected = Connected(ComputerCard::Input::Audio1);
        const bool input2_connected = Connected(ComputerCard::Input::Audio2);

    static float filtered_inputs[2] = {0.0f, 0.0f};
    constexpr float kDetectAlpha = 0.02f; // ~10ms time constant at 48kHz

        float raw_input1 = hardware_get_input(1);
        float raw_input2 = hardware_get_input(2);

        if (!input1_connected) {
            filtered_inputs[0] = 0.0f;
        } else {
            filtered_inputs[0] += kDetectAlpha * (raw_input1 - filtered_inputs[0]);
        }

        if (!input2_connected) {
            filtered_inputs[1] = 0.0f;
        } else {
            filtered_inputs[1] += kDetectAlpha * (raw_input2 - filtered_inputs[1]);
        }

        float input1 = input1_connected ? filtered_inputs[0] : 0.0f;
        float input2 = input2_connected ? filtered_inputs[1] : 0.0f;

        Detect_process_sample(0, input1); // Channel 0 (input[1])
        Detect_process_sample(1, input2); // Channel 1 (input[2])

        // Process queued change/stream events at a reduced rate to limit contention
        static int event_counter = 0;
        if (++event_counter >= 32) {
            event_counter = 0;

            // Optional: pulse LED 3 to show event service activity
            static bool event_led_state = false;
            event_led_state = !event_led_state;
            if (event_led_state) {
                debug_led_on(3);
            } else {
                debug_led_off(3);
            }

            event_next();
        }
        
        // Process slopes system for all output channels (crow-style)
        // Using safe timer-based approach to avoid USB enumeration issues
        static int slope_sample_accum = 0;
        static float slope_buffer[48];

        // Advance envelopes in 48-sample blocks (≈1 kHz) to stay aligned with SAMPLES_PER_MS
        if (++slope_sample_accum >= 48) {
            slope_sample_accum = 0;

#ifdef PICO_BUILD
            if (slopes_mutex_initialized) {
                mutex_enter_blocking(&slopes_mutex);
            }
#endif

            for (int i = 0; i < 4; i++) {
                S_step_v(i, slope_buffer, 48);  // advance envelope over one millisecond
                hardware_set_output(i + 1, slope_buffer[47]);
            }

#ifdef PICO_BUILD
            if (slopes_mutex_initialized) {
                mutex_exit(&slopes_mutex);
            }
#endif
        }
        
        // Basic audio passthrough for now
        // AudioOut1(AudioIn1());
        // AudioOut2(AudioIn2());
        
        // Could add basic crow-like functionality here later
        // For now just pass audio through
    }
};

// Implementation of metamethod functions (now that BlackbirdCrow is fully defined)

// __index metamethod: handles property reading (output[1].volts)
int LuaManager::output_index(lua_State* L) {
    // Get the userdata (output object)
    OutputUserData* output_data = (OutputUserData*)luaL_checkudata(L, 1, "Output");
    const char* key = luaL_checkstring(L, 2);
    
    if (strcmp(key, "volts") == 0) {
        // Return current voltage from slopes system
        float volts = S_get_state(output_data->channel - 1); // Convert to 0-based indexing
        lua_pushnumber(L, volts);
        return 1;
    }
    
    // Property not found, return nil
    lua_pushnil(L);
    return 1;
}

// __newindex metamethod: handles property assignment (output[1].volts = 3.5)
int LuaManager::output_newindex(lua_State* L) {
    // Get the userdata (output object)
    OutputUserData* output_data = (OutputUserData*)luaL_checkudata(L, 1, "Output");
    const char* key = luaL_checkstring(L, 2);
    
    if (strcmp(key, "volts") == 0) {
        // Set voltage - use slopes system for smooth transitions
        float volts = (float)luaL_checknumber(L, 3);
        
        // Thread-safe slopes system access - NON-BLOCKING for Lua calls
#ifdef PICO_BUILD
        if (slopes_mutex_initialized) {
            // Try to enter mutex without blocking
            if (!mutex_try_enter(&slopes_mutex, nullptr)) {
                // Mutex is busy - skip this call to prevent deadlock
                printf("Slopes mutex busy - skipping output voltage set to %.3fV\n\r", volts);
                return 0;  // Graceful degradation instead of deadlock
            }
        }
#endif
        
        // Use slopes system to handle the voltage change
        // This enables slew and other crow features
        S_toward(output_data->channel - 1, // Convert to 0-based indexing
                volts,                    // Destination voltage
                0.0,                     // Immediate (no slew for now)
                SHAPE_Linear,            // Linear transition
                nullptr);                // No callback
        
#ifdef PICO_BUILD
        if (slopes_mutex_initialized) {
            mutex_exit(&slopes_mutex);
        }
#endif
        
        return 0;
    }
    
    // Unknown property - could either silently ignore or error
    // For crow compatibility, we'll silently ignore unknown properties
    return 0;
}

// CASL bridge functions
int LuaManager::lua_casl_describe(lua_State* L) {
    casl_describe(luaL_checkinteger(L, 1) - 1, L); // C is zero-based
    lua_pop(L, 2);
    return 0;
}

int LuaManager::lua_casl_action(lua_State* L) {
    casl_action(luaL_checkinteger(L, 1) - 1, luaL_checkinteger(L, 2)); // C is zero-based
    lua_pop(L, 2);
    return 0;
}

int LuaManager::lua_casl_defdynamic(lua_State* L) {
    int c_ix = luaL_checkinteger(L, 1) - 1; // lua is 1-based
    lua_pop(L, 1);
    lua_pushinteger(L, casl_defdynamic(c_ix));
    return 1;
}

int LuaManager::lua_casl_cleardynamics(lua_State* L) {
    casl_cleardynamics(luaL_checkinteger(L, 1) - 1); // lua is 1-based
    lua_pop(L, 1);
    return 0;
}

int LuaManager::lua_casl_setdynamic(lua_State* L) {
    casl_setdynamic(luaL_checkinteger(L, 1) - 1, // lua is 1-based
                    luaL_checkinteger(L, 2),
                    luaL_checknumber(L, 3));
    lua_pop(L, 3);
    return 0;
}

int LuaManager::lua_casl_getdynamic(lua_State* L) {
    float d = casl_getdynamic(luaL_checkinteger(L, 1) - 1, // lua is 1-based
                             luaL_checkinteger(L, 2));
    lua_pop(L, 2);
    lua_pushnumber(L, d);
    return 1;
}

// Crow backend functions for Output.lua compatibility

// LL_get_state(channel) - Get current voltage state from slopes system
int LuaManager::lua_LL_get_state(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float volts = S_get_state(channel - 1);  // Convert to 0-based for C
    lua_pushnumber(L, volts);
    return 1;
}

// set_output_scale(channel, ...) - Set voltage scaling (stub for now)
int LuaManager::lua_set_output_scale(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    // For now, just consume the arguments - real crow has complex scaling
    // TODO: Implement proper voltage scaling if needed
    printf("set_output_scale called for channel %d (not implemented)\n\r", channel);
    return 0;
}

// _c.tell('output', channel, value) - Send output to hardware
int LuaManager::lua_c_tell(lua_State* L) {
    // Add safety check for argument count
    int argc = lua_gettop(L);
    if (argc < 3) {
        printf("_c.tell: insufficient arguments (%d)\n\r", argc);
        return 0;
    }
    
    const char* module = luaL_checkstring(L, 1);
    int channel = luaL_checkinteger(L, 2);
    
        if (strcmp(module, "output") == 0) {
            static int output_tell_debug_count = 0;
            float value = (float)luaL_checknumber(L, 3);
            if (output_tell_debug_count < 32) {
                printf("[tell] output ch%d value=%.3f\n\r", channel, value);
                output_tell_debug_count++;
            }
            if (g_blackbird_instance) {
                ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_output(channel, value);
            }
    } else if (strcmp(module, "change") == 0) {
        // Handle default change callback from Input.lua - just ignore for now
        // This prevents crashes when detection triggers before user defines custom callback
        int state = luaL_checkinteger(L, 3);
        printf("Default change callback: ch%d=%d (ignored)\n\r", channel, state);
    } else if (strcmp(module, "stream") == 0) {
        // Handle stream callback - ignore for now
        float value = (float)luaL_checknumber(L, 3);
        printf("Stream callback: ch%d=%.3f (ignored)\n\r", channel, value);
    } else {
        // Handle unknown modules gracefully
        printf("_c.tell: unsupported module '%s' (ch=%d)\n\r", module, channel);
    }
    
    return 0;
}

// Crow backend functions for Input.lua compatibility

// io_get_input(channel) - Read input voltage using AudioIn1/AudioIn2
int LuaManager::lua_io_get_input(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float volts = 0.0f;
    
    if (g_blackbird_instance) {
        volts = ((BlackbirdCrow*)g_blackbird_instance)->hardware_get_input(channel);
    }
    
    lua_pushnumber(L, volts);
    return 1;
}

// Event-based detection callback - queues events like real crow
static constexpr bool kDetectionDebug = false;

static void detection_callback(int channel, float value) {
    // Reduce debug output to prevent buffer overflow
    static uint32_t callback_count = 0;
    callback_count++;
    
    // For change detection, 'value' is actually the state (0.0 or 1.0), not voltage
    bool state = (value > 0.5f);
    if (kDetectionDebug) {
        printf("CALLBACK #%lu: ch%d state=%s\n\r", callback_count, channel + 1, state ? "HIGH" : "LOW");
    }
    
    // Queue a change event like real crow does (using event system for safety)
    // This prevents direct Lua calls that can corrupt the stack
    event_t e = { 
        .handler = L_handle_change_safe,
        .index = { .i = channel },
        .data = { .f = value } // Pass the state value directly
    };
    
    if (!event_post(&e)) {
        if (kDetectionDebug) {
            printf("Failed to post change event for channel %d\n\r", channel + 1);
        }
    }
}

// Core-safe change event handler - NO BLOCKING CALLS, NO SLEEP!
extern "C" void L_handle_change_safe(event_t* e) {
    // CRITICAL: This function can be called from either core
    // Must be thread-safe and never block!
    
    static volatile uint32_t callback_counter = 0;
    callback_counter++;
    
    // LED 0: Event handler called (proves event system works)
    // Use non-blocking LED control only
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(0);
    }
    
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (!lua_mgr) {
        if (g_blackbird_instance) {
            ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(0);
        }
        return;
    }
    
    int channel = e->index.i + 1; // Convert to 1-based
    bool state = (e->data.f > 0.5f);
    
    // Debug output with callback counter to track progress
    if (kDetectionDebug) {
        printf("SAFE CALLBACK #%lu: ch%d state=%s\n\r",
               callback_counter, channel, state ? "HIGH" : "LOW");
    }
    
    // Use crow-style global change_handler dispatching for error isolation
    char lua_call[128];
    snprintf(lua_call, sizeof(lua_call), 
        "if change_handler then change_handler(%d, %s) end", 
        channel, state ? "true" : "false");
    
    // LED 1: About to call Lua (proves we reach Lua execution attempt)
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(1);
    }
    
    // Call with non-blocking error protection - if mutex is busy, skip to prevent deadlock
    if (!lua_mgr->evaluate_safe_non_blocking(lua_call)) {
        if (kDetectionDebug) {
            printf("Skipped change_handler for channel %d (mutex busy or error)\n\r", channel);
        }
        // Turn off LEDs on error/skip (non-blocking)
        if (g_blackbird_instance) {
            ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(0);
            ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(1);
        }
        return;
    }
    
    // LED 2: Lua callback completed successfully (proves no crash/hang)
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(2);
        // Turn off other LEDs now that we're done (non-blocking)
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(0);
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(1);
    }
    
    if (kDetectionDebug) {
        printf("SAFE CALLBACK #%lu: Completed successfully\n\r", callback_counter);
    }
}

// Input mode functions - connect to detection system
int LuaManager::lua_set_input_stream(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_stream(detector, detection_callback, time);
        printf("Input %d: stream mode, interval %.3fs\n\r", channel, time);
    }
    return 0;
}

int LuaManager::lua_set_input_change(lua_State* L) {
    printf("DEBUG: lua_set_input_change called!\n\r");
    int channel = luaL_checkinteger(L, 1);
    float threshold = (float)luaL_checknumber(L, 2);
    float hysteresis = (float)luaL_checknumber(L, 3);
    const char* direction = luaL_checkstring(L, 4);
    
    printf("DEBUG: args: ch=%d, thresh=%.3f, hyst=%.3f, dir='%s'\n\r", 
           channel, threshold, hysteresis, direction);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        int8_t dir = Detect_str_to_dir(direction);
        printf("DEBUG: Direction '%s' converted to %d\n\r", direction, dir);
        Detect_change(detector, detection_callback, threshold, hysteresis, dir);
        printf("Input %d: change mode, thresh %.3f, hyst %.3f, dir %s\n\r", 
               channel, threshold, hysteresis, direction);
    } else {
        printf("Input %d: Error - detector not found\n\r", channel);
    }
    return 0;
}

int LuaManager::lua_set_input_window(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    
    // Extract windows array from Lua table
    if (!lua_istable(L, 2)) {
        printf("set_input_window: windows must be a table\n\r");
        return 0;
    }
    
    float hysteresis = (float)luaL_checknumber(L, 3);
    
    // Get table length
    int wLen = lua_rawlen(L, 2);
    if (wLen > WINDOW_MAX_COUNT) wLen = WINDOW_MAX_COUNT;
    
    float windows[WINDOW_MAX_COUNT];
    for (int i = 1; i <= wLen; i++) {
        lua_rawgeti(L, 2, i);
        windows[i-1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_window(detector, detection_callback, windows, wLen, hysteresis);
        printf("Input %d: window mode, %d windows, hyst %.3f\n\r", channel, wLen, hysteresis);
    }
    return 0;
}

int LuaManager::lua_set_input_scale(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    
    // Extract scale array from Lua table
    float scale[SCALE_MAX_COUNT];
    int sLen = 0;
    
    if (lua_istable(L, 2)) {
        sLen = lua_rawlen(L, 2);
        if (sLen > SCALE_MAX_COUNT) sLen = SCALE_MAX_COUNT;
        
        for (int i = 1; i <= sLen; i++) {
            lua_rawgeti(L, 2, i);
            scale[i-1] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    
    float temp = (float)luaL_checknumber(L, 3);    // Temperament (divisions)
    float scaling = (float)luaL_checknumber(L, 4); // Voltage scaling
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_scale(detector, detection_callback, scale, sLen, temp, scaling);
        printf("Input %d: scale mode, %d notes, temp %.1f, scaling %.3f\n\r", 
               channel, sLen, temp, scaling);
    }
    return 0;
}

int LuaManager::lua_set_input_volume(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_volume(detector, detection_callback, time);
        printf("Input %d: volume mode, interval %.3fs\n\r", channel, time);
    }
    return 0;
}

int LuaManager::lua_set_input_peak(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float threshold = (float)luaL_checknumber(L, 2);
    float hysteresis = (float)luaL_checknumber(L, 3);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_peak(detector, detection_callback, threshold, hysteresis);
        printf("Input %d: peak mode, thresh %.3f, hyst %.3f\n\r", 
               channel, threshold, hysteresis);
    }
    return 0;
}

int LuaManager::lua_set_input_freq(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_freq(detector, detection_callback, time);
        printf("Input %d: freq mode, interval %.3fs (not fully implemented)\n\r", channel, time);
    }
    return 0;
}

int LuaManager::lua_set_input_clock(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float div = (float)luaL_checknumber(L, 2);
    float threshold = (float)luaL_checknumber(L, 3);
    float hysteresis = (float)luaL_checknumber(L, 4);
    
    // Clock mode is a specialized change detection
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        // Use change detection as base for clock
        Detect_change(detector, detection_callback, threshold, hysteresis, 1); // Rising edge
        printf("Input %d: clock mode, div %.3f, thresh %.3f, hyst %.3f\n\r", 
               channel, div, threshold, hysteresis);
    }
    return 0;
}

int LuaManager::lua_set_input_none(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_none(detector);
        printf("Input %d: none mode (detection disabled)\n\r", channel);
    }
    return 0;
}

// Metro system Lua bindings implementation

// metro_start(id, time) - Start a metro with specified interval
int LuaManager::lua_metro_start(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    // Set time first, then start (crow-style)
    Metro_set_time(id, time);
    Metro_start(id);
    printf("Metro %d started with interval %.3fs\n\r", id, time);
    return 0;
}

// metro_stop(id) - Stop a metro
int LuaManager::lua_metro_stop(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    
    // Call C metro backend
    Metro_stop(id);
    printf("Metro %d stopped\n\r", id);
    return 0;
}

// metro_set_time(id, time) - Change metro interval
int LuaManager::lua_metro_set_time(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    // Call C metro backend
    Metro_set_time(id, time);
    printf("Metro %d time set to %.3fs\n\r", id, time);
    return 0;
}

// metro_set_count(id, count) - Set metro repeat count
int LuaManager::lua_metro_set_count(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int count = luaL_checkinteger(L, 2);
    
    // Call C metro backend
    Metro_set_count(id, count);
    printf("Metro %d count set to %d\n\r", id, count);
    return 0;
}

// Legacy function-based API (kept for backward compatibility during transition)
int LuaManager::lua_output_volts(lua_State* L) {
    // Get channel from upvalue
    int channel = (int)lua_tointeger(L, lua_upvalueindex(1));
    
    if (lua_gettop(L) == 0) {
        // Get current voltage
        if (g_blackbird_instance) {
            float volts = ((BlackbirdCrow*)g_blackbird_instance)->hardware_get_output(channel);
            lua_pushnumber(L, volts);
            return 1;
        }
        lua_pushnumber(L, 0.0);
        return 1;
    } else {
        // Set voltage
        float volts = (float)luaL_checknumber(L, 1);
        if (g_blackbird_instance) {
            ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_output(channel, volts);
        }
        return 0;
    }
}

// Implementation of lua_unique_card_id function (after BlackbirdCrow class is fully defined)
int LuaManager::lua_unique_card_id(lua_State* L) {
    // Get the cached ID from the global instance
    if (g_blackbird_instance) {
        lua_pushinteger(L, (lua_Integer)((BlackbirdCrow*)g_blackbird_instance)->cached_unique_id);
        return 1;
    }
    
    lua_pushinteger(L, 0);
    return 1;
}

// Implementation of C interface function (after BlackbirdCrow class is fully defined)
extern "C" void hardware_output_set_voltage(int channel, float voltage) {
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_output(channel, voltage);
    }
}

// Provide Lua state access for l_crowlib metro handler
extern "C" lua_State* get_lua_state(void) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    return lua_mgr ? lua_mgr->L : nullptr;
}

// Stub implementation of IO_GetADC for crow compatibility
// This is hardware-specific to real crow, so we provide Workshop Computer version
extern "C" float IO_GetADC(uint8_t channel) {
    if (g_blackbird_instance) {
        // Convert from crow's 0-based indexing to Workshop Computer's 1-based indexing
        return ((BlackbirdCrow*)g_blackbird_instance)->hardware_get_input(channel + 1);
    }
    return 0.0f;
}

int main()
{
    if (!set_sys_clock_khz(200000, false)) {
        if (!set_sys_clock_khz(150000, false)) {
            set_sys_clock_khz(133000, true);
        }
    }
    stdio_init_all();

    BlackbirdCrow crow;
    crow.EnableNormalisationProbe();
    crow.Run();
}
