#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "lib/debug.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>

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
#include "lib/mailbox.h"
#include "lib/clock.h"
#include "lib/events_lockfree.h"
}

// Generated Lua bytecode headers - Core libraries (always included)
#include "asl.h"
#include "asllib.h"
#include "output.h"
#include "input.h"
#include "metro.h"
#include "First.h"

// Conditionally included test script headers
#ifdef EMBED_ALL_TESTS
#include "test_enhanced_multicore_safety.h"
#include "test_lockfree_performance.h"
#include "test_random_voltage.h"
#include "test_phase2_performance.h"
#include "test_simple_output.h"
#elif defined(EMBED_TEST_ENHANCED_MULTICORE_SAFETY)
#include "test_enhanced_multicore_safety.h"
#elif defined(EMBED_TEST_LOCKFREE_PERFORMANCE)
#include "test_lockfree_performance.h"
#elif defined(EMBED_TEST_RANDOM_VOLTAGE)
#include "test_random_voltage.h"
#elif defined(EMBED_TEST_PHASE2_PERFORMANCE)
#include "test_phase2_performance.h"
#elif defined(EMBED_TEST_SIMPLE_OUTPUT)
#include "test_simple_output.h"
#endif

// Simplified output state storage - no lock-free complexity needed
static volatile int32_t g_output_state_mv[4] = {0, 0, 0, 0};

//Simplified input state storage
static volatile int32_t g_input_state_q12[2] = {0, 0};

// Simple output state access - direct variable access is sufficient
static void set_output_state_simple(int channel, int32_t value_mv) {
    if (channel >= 0 && channel < 4) {
        g_output_state_mv[channel] = value_mv;
    }
}

float get_input_state_simple(int channel) {
    if (channel >= 0 && channel < 2) {
        return (float) g_input_state_q12[channel] * (6.0f / 2047.0f);
    }
    return 0.0f;
}

// Simple input state access - direct variable access is sufficient
static void set_input_state_simple(int channel, int16_t rawValue) {
    if (channel >= 0 && channel < 2) {
        g_input_state_q12[channel] = rawValue;
    }
}

static int32_t get_output_state_simple(int channel) {
    if (channel >= 0 && channel < 4) {
        return g_output_state_mv[channel];
    }
    return 0;
}

 // Forward declaration
class BlackbirdCrow;
static volatile BlackbirdCrow* g_blackbird_instance = nullptr;
static BlackbirdCrow* g_crow_core1 = nullptr;
static void core1_entry(); // defined after BlackbirdCrow


// Forward declaration of C interface function (implemented after BlackbirdCrow class)
extern "C" void hardware_output_set_voltage(int channel, float voltage);

// Forward declaration of safe event handlers
extern "C" void L_handle_change_safe(event_t* e);
extern "C" void L_handle_stream_safe(event_t* e);
extern "C" void L_handle_asl_done_safe(event_t* e);
extern "C" void L_handle_input_lockfree(input_event_lockfree_t* event);
extern "C" void L_handle_metro_lockfree(metro_event_lockfree_t* event);


class LuaManager;

// Message queue system for audio-safe printf replacement
#define MESSAGE_QUEUE_SIZE 32
#define MESSAGE_MAX_LENGTH 240

typedef struct {
    char message[MESSAGE_MAX_LENGTH];
    uint32_t timestamp;
    bool is_debug;
} queued_message_t;

static volatile queued_message_t g_message_queue[MESSAGE_QUEUE_SIZE];
static volatile uint32_t g_message_write_idx = 0;
static volatile uint32_t g_message_read_idx = 0;

// Audio-safe message queuing - replaces printf calls
static bool queue_message(bool is_debug, const char* fmt, ...) {
    // Get next write position
    uint32_t next_write = (g_message_write_idx + 1) % MESSAGE_QUEUE_SIZE;
    
    // Check for queue full
    if (next_write == g_message_read_idx) {
        return false; // Queue full, drop message
    }
    
    // Format message
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf((char*)g_message_queue[g_message_write_idx].message, 
                           MESSAGE_MAX_LENGTH, fmt, args);
    va_end(args);
    
    if (written < 0) {
        return false;
    }
    
    // Store metadata
    g_message_queue[g_message_write_idx].timestamp = to_ms_since_boot(get_absolute_time());
    g_message_queue[g_message_write_idx].is_debug = is_debug;
    
    // Update write index (atomic on single-core writes)
    g_message_write_idx = next_write;
    
    return true;
}

// Process queued messages on Core0
static void process_queued_messages() {
    while (g_message_read_idx != g_message_write_idx) {
        // Cast away volatile for local use
        const queued_message_t* msg = (const queued_message_t*)&g_message_queue[g_message_read_idx];
        
        // Output message
        printf("%s", msg->message);
        if (!strstr(msg->message, "\n") && !strstr(msg->message, "\r")) {
            printf("\r\n"); // Add line ending if not present
        }
        fflush(stdout);
        
        // Update read index
        g_message_read_idx = (g_message_read_idx + 1) % MESSAGE_QUEUE_SIZE;
    }
}

// Convenience macros for different message types
#define queue_user_message(fmt, ...) queue_message(false, fmt, ##__VA_ARGS__)
#define queue_debug_message(fmt, ...) queue_message(true, fmt, ##__VA_ARGS__)

static bool usb_log_printf(const char* fmt, ...) {
    char buffer[240];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0) {
        return false;
    }
    if (!mailbox_send_response(buffer)) {
        queue_user_message("%s", buffer);
        return false;
    }
    return true;
}

static bool parse_output_volts_command(const char* command, int* channel, float* value) {
    if (!command || !channel || !value) {
        return false;
    }
    int ch = 0;
    float val = 0.0f;
    if (sscanf(command, "output[%d].volts = %f", &ch, &val) == 2 ||
        sscanf(command, "output[%d].volts=%f", &ch, &val) == 2) {
        *channel = ch;
        *value = val;
        return true;
    }
    return false;
}

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
    printf("\r\n");
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
    
    // Conditional test function implementations - only compiled when tests are embedded
#ifdef EMBED_ALL_TESTS
    // Lua function to run enhanced multicore safety test
    static int lua_test_enhanced_multicore_safety(lua_State* L) {
        printf("Running enhanced multicore safety test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_enhanced_multicore_safety, test_enhanced_multicore_safety_len, "test_enhanced_multicore_safety.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running enhanced multicore safety test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Enhanced multicore safety test completed successfully!\r\n");
        }
        return 0;
    }
    
    // Lua function to run lock-free performance test
    static int lua_test_lockfree_performance(lua_State* L) {
        printf("Running lock-free performance test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_lockfree_performance, test_lockfree_performance_len, "test_lockfree_performance.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running lock-free performance test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Lock-free performance test completed successfully!\r\n");
        }
        return 0;
    }
    
    // Lua function to run random voltage test
    static int lua_test_random_voltage(lua_State* L) {
        printf("Running random voltage test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_random_voltage, test_random_voltage_len, "test_random_voltage.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running random voltage test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Random voltage test loaded successfully!\r\n");
        }
        return 0;
    }
    
    // Lua function to run Phase 2 performance test
    static int lua_test_phase2_performance(lua_State* L) {
        printf("Running Phase 2 block processing performance test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_phase2_performance, test_phase2_performance_len, "test_phase2_performance.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running Phase 2 performance test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Phase 2 performance test completed successfully!\r\n");
        }
        return 0;
    }
    
    // Lua function to run simple output test
    static int lua_test_simple_output(lua_State* L) {
        printf("Running simple output hardware test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_simple_output, test_simple_output_len, "test_simple_output.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running simple output test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Simple output test completed successfully!\r\n");
        }
        return 0;
    }
#elif defined(EMBED_TEST_ENHANCED_MULTICORE_SAFETY)
    // Single test: Enhanced multicore safety test
    static int lua_test_enhanced_multicore_safety(lua_State* L) {
        printf("Running enhanced multicore safety test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_enhanced_multicore_safety, test_enhanced_multicore_safety_len, "test_enhanced_multicore_safety.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running enhanced multicore safety test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Enhanced multicore safety test completed successfully!\r\n");
        }
        return 0;
    }
#elif defined(EMBED_TEST_LOCKFREE_PERFORMANCE)
    // Single test: Lock-free performance test
    static int lua_test_lockfree_performance(lua_State* L) {
        printf("Running lock-free performance test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_lockfree_performance, test_lockfree_performance_len, "test_lockfree_performance.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running lock-free performance test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Lock-free performance test completed successfully!\r\n");
        }
        return 0;
    }
#elif defined(EMBED_TEST_RANDOM_VOLTAGE)
    // Single test: Random voltage test
    static int lua_test_random_voltage(lua_State* L) {
        printf("Running random voltage test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_random_voltage, test_random_voltage_len, "test_random_voltage.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running random voltage test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Random voltage test loaded successfully!\r\n");
        }
        return 0;
    }
#elif defined(EMBED_TEST_PHASE2_PERFORMANCE)
    // Single test: Phase 2 performance test
    static int lua_test_phase2_performance(lua_State* L) {
        printf("Running Phase 2 block processing performance test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_phase2_performance, test_phase2_performance_len, "test_phase2_performance.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running Phase 2 performance test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Phase 2 performance test completed successfully!\r\n");
        }
        return 0;
    }
#elif defined(EMBED_TEST_SIMPLE_OUTPUT)
    // Single test: Simple output test
    static int lua_test_simple_output(lua_State* L) {
        printf("Running simple output hardware test...\r\n");
        if (luaL_loadbuffer(L, (const char*)test_simple_output, test_simple_output_len, "test_simple_output.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running simple output test: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Simple output test completed successfully!\r\n");
        }
        return 0;
    }
#else
    // Production build - no test functions
#endif
    
    // Lua tab.print function - pretty print tables
    static int lua_tab_print(lua_State* L) {
        if (lua_gettop(L) != 1) {
            lua_pushstring(L, "tab.print expects exactly one argument");
            lua_error(L);
        }
        
        print_table_recursive(L, 1, 0);
    printf("\r\n");
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
        init();
    }
    
    ~LuaManager() {
        if (L) {
            lua_close(L);
        }
        instance = nullptr;
    }
    
    void init() {
        if (L) {
            lua_close(L);
        }
        
        L = luaL_newstate();
        if (!L) {
            printf("Error: Could not create Lua state\r\n");
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
    
    // Register test functions - conditional compilation
#ifdef EMBED_ALL_TESTS
    lua_register(L, "test_enhanced_multicore_safety", lua_test_enhanced_multicore_safety);
    lua_register(L, "test_lockfree_performance", lua_test_lockfree_performance);
    lua_register(L, "test_random_voltage", lua_test_random_voltage);
    lua_register(L, "test_phase2_performance", lua_test_phase2_performance);
    lua_register(L, "test_simple_output", lua_test_simple_output);
#elif defined(EMBED_TEST_ENHANCED_MULTICORE_SAFETY)
    lua_register(L, "test_enhanced_multicore_safety", lua_test_enhanced_multicore_safety);
#elif defined(EMBED_TEST_LOCKFREE_PERFORMANCE)
    lua_register(L, "test_lockfree_performance", lua_test_lockfree_performance);
#elif defined(EMBED_TEST_RANDOM_VOLTAGE)
    lua_register(L, "test_random_voltage", lua_test_random_voltage);
#elif defined(EMBED_TEST_PHASE2_PERFORMANCE)
    lua_register(L, "test_phase2_performance", lua_test_phase2_performance);
#elif defined(EMBED_TEST_SIMPLE_OUTPUT)
    lua_register(L, "test_simple_output", lua_test_simple_output);
#endif
    // Production builds have no test functions registered
    
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
    lua_register(L, "soutput_handler", lua_soutput_handler);
    
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
    printf("Loading embedded ASL library...\r\n");
        if (luaL_loadbuffer(L, (const char*)asl, asl_len, "asl.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading ASL library: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
            return;
        }
        
        // ASL library returns the Asl table - capture it
        lua_setglobal(L, "Asl");
        
        // Also set up lowercase 'asl' for compatibility
        lua_getglobal(L, "Asl");
        lua_setglobal(L, "asl");
        
        // Load ASLLIB library
    printf("Loading embedded ASLLIB library...\r\n");
        if (luaL_loadbuffer(L, (const char*)asllib, asllib_len, "asllib.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading ASLLIB library: %s\r\n", error ? error : "unknown error");
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
            printf("Error setting up ASL globals: %s\r\n", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        // Load Output.lua class from embedded bytecode
    printf("Loading embedded Output.lua class...\r\n");
        if (luaL_loadbuffer(L, (const char*)output, output_len, "output.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading Output.lua: %s\r\n", error ? error : "unknown error");
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
                printf("Error creating output objects: %s\r\n", error ? error : "unknown error");
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
        // Index translation handled directly in asl.lua (Option B); runtime patch removed.
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
    static int lua_soutput_handler(lua_State* L);
    
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
    
    
    static LuaManager* getInstance() {
        return instance;
    }
};

// Static instance pointer
LuaManager* LuaManager::instance = nullptr;

// Global USB buffer to ensure proper initialization across cores
static const int USB_RX_BUFFER_SIZE = 256;
static char g_rx_buffer[USB_RX_BUFFER_SIZE] = {0};
static volatile int g_rx_buffer_pos = 0;

// Hardware timer-based PulseOut2 performance monitoring (outside class to avoid C++ issues)
static volatile bool g_pulse2_state = false;
static volatile uint32_t g_pulse2_counter = 0;
static struct repeating_timer g_pulse2_timer;

// Timer callback for consistent 250Hz PulseOut2 pulse (independent of audio processing load)
static bool __not_in_flash_func(pulse2_timer_callback)(struct repeating_timer *t) {
    g_pulse2_state = !g_pulse2_state;
    // Use direct GPIO access since PulseOut2 is protected
    gpio_put(PULSE_2_RAW_OUT, !g_pulse2_state); // Note: raw output is inverted
    g_pulse2_counter++;
    return true; // Continue repeating
}

class BlackbirdCrow : public ComputerCard
{
    // Variables for communication between cores
    volatile uint32_t v1, v2;
    
    // Lua manager for REPL
    LuaManager* lua_manager;
    
public:
    // Cached unique ID for Lua access (since UniqueCardID() is protected)
    uint64_t cached_unique_id;

    uint16_t inputs[4];
    // Hardware abstraction functions for output
    void hardware_set_output(int channel, float volts) {
        if (channel < 1 || channel > 4) return;
        
        static int debug_prints_remaining = 32;
        
        // Clamp voltage to ±6V range
        if (volts > 6.0f) volts = 6.0f;
        if (volts < -6.0f) volts = -6.0f;
        
        // Convert to DAC range: -6V to +6V maps to -2048 to +2047
        // Use integer math for RP2040 efficiency
        int32_t volts_mV = (int32_t)(volts * 1000.0f);
        int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
        
        // Store state for lua queries (in millivolts) - simplified
        set_output_state_simple(channel - 1, volts_mV);
        
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
        
        if (debug_prints_remaining > 0) {
           // printf("[hardware] set_output ch%d volts=%.3f dac=%d\n\r", channel, volts, dac_value);
            debug_prints_remaining--;
        }
    }
    
    float hardware_get_output(int channel) {
        if (channel < 1 || channel > 4) return 0.0f;
        // Use AShaper_get_state to match crow's behavior exactly
        return AShaper_get_state(channel - 1);
    }
    
    // Hardware abstraction functions for input
    void hardware_get_input(int channel) {
        
        int16_t raw_value = 0;
        if (channel == 1) {
            raw_value = CVIn1();
        } else if (channel == 2) {
            raw_value = CVIn2();
        }

        set_input_state_simple(channel - 1, raw_value);

        return;
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
        // Initialize global USB buffer
        g_rx_buffer_pos = 0;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        // Cache the unique ID for Lua access
        cached_unique_id = UniqueCardID();
        
        // Set global instance for Lua bindings
        g_blackbird_instance = this;
        
        // Initialize slopes system for crow-style output processing
        S_init(4); // Initialize 4 output channels
        
        // Initialize AShaper system for output quantization (pass-through mode)
        AShaper_init(4); // Initialize 4 output channels
        printf("AShaper system initialized (pass-through mode)\n");
        
        
        // Initialize detection system for 2 input channels
        Detect_init(2);
        
        // Initialize simple output state storage
        // (No special initialization needed for simple volatile array)
        
        // Initialize event system - CRITICAL for processing input events
        events_init();
        
        // Initialize lock-free event queues for timing-critical events
        events_lockfree_init();
        
        // Initialize timer system for metro support (8 timers for full crow compatibility)
        Timer_Init(8);
        
        // Initialize metro system (depends on timer system)
        Metro_Init(8);
        
        // Initialize Lua manager
        lua_manager = new LuaManager();
        
        // Note: New ComputerCard.h API uses sample-by-sample processing only
        printf("Sample-by-sample processing (48kHz)\n");
        
        // Initialize hardware timer for consistent 250Hz PulseOut2 performance monitoring
        if (!add_repeating_timer_us(-4000, pulse2_timer_callback, NULL, &g_pulse2_timer)) {
            printf("Failed to start PulseOut2 timer\n");
        } else {
            printf("PulseOut2 timer started: 250Hz consistent pulse for performance monitoring\n");
        }
        
        // Start slopes processing timer (runs at 1.5kHz via Timer_Process_Block)
        // This processes all slope channels and outputs the results
        printf("Slopes processing will run via Timer_Process_Block at 1.5kHz\n");
        
        // Dual-core architecture: Core0=USB/Control, Core1=Audio
        printf("Dual-core architecture initialized\n");
    }
    
    // Core0 main control loop - handles USB, events, Lua AND timer processing
    void MainControlLoop()
    {
        printf("Blackbird Crow Emulator v0.3 (Dual-Core Architecture)\n");
        printf("Send ^^v for version, ^^i for identity\n");
        
        // Initialize USB buffer
        g_rx_buffer_pos = 0;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        // Timer processing state - moved from ISR!
        static uint32_t last_timer_process_us = 0;
        // Process every 667us = 1.5kHz (matches TIMER_BLOCK_SIZE=96 @ 48kHz)
        // Calculation: 96 samples / 48000 Hz = 0.002s = 2000us
        // But we check more frequently to reduce jitter
        const uint32_t timer_interval_us = 667;
        
        printf("Timer processing will run at ~1.5kHz in MainControlLoop (NOT in ISR)\n");
        printf("This prevents input corruption from output processing overruns\n");
        
        while (1) {
            // Handle USB input directly - no mailbox needed
            handle_usb_input();
            
            // Process queued messages from audio thread
            process_queued_messages();
            
            // *** CRITICAL: Process timer/slopes updates at ~1.5kHz (OUTSIDE ISR!) ***
            uint32_t now_us = time_us_32();
            if (now_us - last_timer_process_us >= timer_interval_us) {
                Timer_Process();  // Safe here - not in ISR context!
                last_timer_process_us = now_us;
            }
            
            // Process lock-free metro events first (highest priority)
            metro_event_lockfree_t metro_event;
            while (metro_lockfree_get(&metro_event)) {
                L_handle_metro_lockfree(&metro_event);
            }
            
            // Process lock-free input detection events (high priority)
            input_event_lockfree_t input_event;
            while (input_lockfree_get(&input_event)) {
                L_handle_input_lockfree(&input_event);
            }
            
            // Process regular events (lower priority - system events, etc.)
            event_next();
            
            // Reduced sleep for tighter timer loop - 100us allows 10kHz loop rate
            sleep_us(100);  // Was sleep_ms(1) - now much more responsive
        }
    }
    
    // Handle USB input directly - no mailbox complexity
    void handle_usb_input() {
        // Read available characters with short timeout
        int c = getchar_timeout_us(1000); // 1ms timeout
        
        if (c != PICO_ERROR_TIMEOUT) {
            // Safety check - ensure buffer position is sane
            if (g_rx_buffer_pos < 0 || g_rx_buffer_pos >= USB_RX_BUFFER_SIZE) {
                printf("ERROR: Buffer corruption detected! Resetting...\r\n");
                g_rx_buffer_pos = 0;
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
            }
            
            // Check for buffer overflow BEFORE adding character
            if (g_rx_buffer_pos >= USB_RX_BUFFER_SIZE - 1) {
                // Buffer full - clear and start over
                g_rx_buffer_pos = 0;
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
            }
            
            // Add character to buffer
            g_rx_buffer[g_rx_buffer_pos] = (char)c;
            g_rx_buffer_pos++;
            g_rx_buffer[g_rx_buffer_pos] = '\0';
            
            // Check if we have a complete packet
            if (is_packet_complete(g_rx_buffer, g_rx_buffer_pos)) {
                // Strip trailing whitespace
                int clean_length = g_rx_buffer_pos;
                while (clean_length > 0 && 
                       (g_rx_buffer[clean_length-1] == '\n' || 
                        g_rx_buffer[clean_length-1] == '\r' || 
                        g_rx_buffer[clean_length-1] == ' ' || 
                        g_rx_buffer[clean_length-1] == '\t')) {
                    clean_length--;
                }
                g_rx_buffer[clean_length] = '\0';
                
                // Skip empty commands
                if (clean_length == 0) {
                    g_rx_buffer_pos = 0;
                    memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                    return;
                }
                
                // Execute command directly - no mailbox needed
                handle_usb_command(g_rx_buffer);
                
                // Clear buffer after processing
                g_rx_buffer_pos = 0;
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
            }
        }
    }
    
    // Handle USB commands received from Core1 via mailbox
    void handle_usb_command(const char* command)
    {
        //printf("[core0] received cmd: \"%s\"\n\r", command);
        // Parse and handle command
        C_cmd_t cmd = parse_command(command, strlen(command));
        if (cmd != C_none) {
            handle_command_with_response(cmd);
        } else {
            // Not a ^^ command, treat as Lua code
            int parsed_channel = 0;
            float parsed_volts = 0.0f;
            // if (parse_output_volts_command(command, &parsed_channel, &parsed_volts)) {
            //     if (parsed_channel >= 1 && parsed_channel <= 4) {
            //         printf("[core0] request output[%d].volts -> %.3f (queued)\n\r", parsed_channel, parsed_volts);
            //     }
            // }
            if (lua_manager) {
                lua_manager->evaluate_safe(command);
            }
        }
    }
    
    // Handle commands and send responses directly (single-core)
    void handle_command_with_response(C_cmd_t cmd)
    {
        switch (cmd) {
            case C_version: {
                // Embed build date/time and debug format so a late serial connection can verify firmware
                printf("^^version('blackbird-0.3 %s %s dual-core')\r\n", __DATE__, __TIME__);
                break; }
                
            case C_identity: {
                uint64_t unique_id = UniqueCardID();
                printf("^^identity('0x%016llx')\r\n", unique_id);
                break;
            }
            
            case C_print:
                printf("-- no script loaded --\r\n");
                break;
                
            case C_restart:
                printf("restarting...\r\n");
                // Could implement actual restart here
                break;
                
            case C_killlua:
                printf("lua killed\r\n");
                break;
                
            case C_boot:
                printf("entering bootloader mode\r\n");
                break;
                
            case C_startupload:
                printf("script upload started\r\n");
                break;
                
            case C_endupload:
                printf("script uploaded\r\n");
                break;
                
            case C_flashupload:
                printf("script saved to flash\r\n");
                break;
                
            case C_flashclear:
                printf("flash cleared\r\n");
                break;
                
            case C_loadFirst:
                printf("loading first.lua\r\n");
                printf("[first] handler invoked, attempting bytecode load\n\r");
                // Actually load First.lua using the compiled bytecode
                if (lua_manager) {
                    printf("Loading First.lua from embedded bytecode...\n\r");
                    if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") != LUA_OK || lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
                        const char* error = lua_tostring(lua_manager->L, -1);
                        printf("Error loading First.lua: %s\n\r", error ? error : "unknown error");
                        lua_pop(lua_manager->L, 1);
                        printf("error loading first.lua\r\n");
                    } else {
                        printf("First.lua loaded and executed successfully!\n\r");

                        // Model real crow: reset runtime so newly loaded script boots
                        if (!lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end")) {
                            printf("Warning: crow.reset() failed after First.lua load\n\r");
                        }
                        if (!lua_manager->evaluate_safe("local ok, err = pcall(function() if init then init() end end); if not ok then print('init() error', err) end")) {
                            printf("Warning: init() invocation failed after First.lua load\n\r");
                        }

                        //convert 12 bit signed raw input to volts
                        // 0 = 0V, 2047 = +6V, -2048 = -6V
                        float input1_volts = get_input_state_simple(0);
                        float input2_volts = get_input_state_simple(1);
                        printf("[diag] input volts after load: in1=%.3fV in2=%.3fV\n\r", input1_volts, input2_volts);

                        printf("first.lua loaded\r\n");
                    }
                } else {
                    printf("error: lua manager not available\r\n");
                }
                break;
                
            default:
                // For unimplemented commands, send a simple acknowledgment
                printf("ok\r\n");
                break;
        }
        fflush(stdout);
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
            case C_version: {
                char response[96];
                // Embed build date/time and debug format so a late serial connection can verify firmware
                snprintf(response, sizeof(response), "^^version('blackbird-0.1 %s %s dbg=v2')", __DATE__, __TIME__);
                send_crow_response(response);
                break; }
                
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
                        if (!lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end")) {
                            printf("Warning: crow.reset() failed after First.lua load\n\r");
                        }
                        if (!lua_manager->evaluate_safe("local ok, err = pcall(function() if init then init() end end); if not ok then print('init() error', err) end")) {
                            printf("Warning: init() invocation failed after First.lua load\n\r");
                        }

                        float input1_volts = get_input_state_simple(0);
                        float input2_volts = get_input_state_simple(1);
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

    // Core 1: Simplified USB-only processing
    void USBProcessingCore()
    {
        printf("Blackbird Crow Emulator v0.2 (Simplified Dual-Core)\n");
        printf("Send ^^v for version, ^^i for identity\n");
        
        // Initialize mailbox communication
        mailbox_init();
        
        // CRITICAL: Ensure buffer is properly initialized
        g_rx_buffer_pos = 0;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        while (1) {
            // Read available characters
            int c = getchar_timeout_us(1000); // 1ms timeout
            
            if (c != PICO_ERROR_TIMEOUT) {
                // Safety check - ensure buffer position is sane
                if (g_rx_buffer_pos < 0 || g_rx_buffer_pos >= USB_RX_BUFFER_SIZE) {
                    printf("ERROR: Buffer corruption detected! Resetting...\r\n");
                    g_rx_buffer_pos = 0;
                    memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                }
                
                // Check for buffer overflow BEFORE adding character
                if (g_rx_buffer_pos >= USB_RX_BUFFER_SIZE - 1) {
                    // Buffer full - clear and start over
                    g_rx_buffer_pos = 0;
                    memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                }
                
                // Add character to buffer
                g_rx_buffer[g_rx_buffer_pos] = (char)c;
                g_rx_buffer_pos++;
                g_rx_buffer[g_rx_buffer_pos] = '\0';
                
                // Check if we have a complete packet
                if (is_packet_complete(g_rx_buffer, g_rx_buffer_pos)) {
                    // Strip trailing whitespace
                    int clean_length = g_rx_buffer_pos;
                    while (clean_length > 0 && 
                           (g_rx_buffer[clean_length-1] == '\n' || 
                            g_rx_buffer[clean_length-1] == '\r' || 
                            g_rx_buffer[clean_length-1] == ' ' || 
                            g_rx_buffer[clean_length-1] == '\t')) {
                        clean_length--;
                    }
                    g_rx_buffer[clean_length] = '\0';
                    
                    // Skip empty commands
                    if (clean_length == 0) {
                        g_rx_buffer_pos = 0;
                        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                        continue;
                    }
                    
                    // Send command to Core0 via mailbox
                    if (!mailbox_send_command(g_rx_buffer)) {
                        // Mailbox full - inform user (rate limited)
                        static uint32_t last_full_msg = 0;
                        uint32_t now = to_ms_since_boot(get_absolute_time());
                        if (now - last_full_msg > 1000) {
                            printf("Command queue full, try again\r\n");
                            last_full_msg = now;
                        }
                    }
                    
                    // Clear buffer after processing
                    g_rx_buffer_pos = 0;
                    memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                }
            }
            
            // Check for responses from Core0 and send them
            char response[256];
            if (mailbox_get_response(response, sizeof(response))) {
                printf("%s", response);
                // Check for crow-style line endings
                if (strstr(response, "\n\r") == NULL && strstr(response, "\r\n") == NULL) {
                    printf("\r\n"); // Add line ending if not present
                }
                fflush(stdout);
                mailbox_mark_response_sent();
            }
            
            // Minimal CPU usage when idle
            tight_loop_contents();
        }
    }
    
    // ULTRA-LIGHTWEIGHT audio callback - ONLY READ INPUTS!
    // NO output processing in ISR - prevents multiplexer misalignment
    virtual void ProcessSample() override
    {
        // Increment sample counter for timer system (lightweight)
        extern volatile uint64_t global_sample_counter;
        global_sample_counter++;
        
        // Keep clock system synchronized (lightweight)
        clock_increment_sample_counter();
        
        // Read CV inputs directly - clean and simple
        int16_t cv1 = CVIn1();
        int16_t cv2 = CVIn2();
        
        // Process detection sample-by-sample for edge accuracy
        Detect_process_sample(0, cv1);
        Detect_process_sample(1, cv2);
        
        // That's it! Output processing moved to MainControlLoop()
        // ISR time: ~5us vs. previous 500+us
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

        // Actually execute the voltage change (was missing!)
        printf("[lua] output[%d].volts=%.3f -> executing\n\r", output_data->channel, volts);
        hardware_output_set_voltage(output_data->channel, volts);
        
        return 0;
    }
    
    // Unknown property - could either silently ignore or error
    // For crow compatibility, we'll silently ignore unknown properties
    return 0;
}

// CASL bridge functions
int LuaManager::lua_casl_describe(lua_State* L) {
    int raw = luaL_checkinteger(L, 1);
    int internal = raw - 1;
    //printf("[DBG] lua_casl_describe raw=%d internal=%d\n\r", raw, internal);
    casl_describe(internal, L); // C is zero-based
    lua_pop(L, 2);
    return 0;
}

int LuaManager::lua_casl_action(lua_State* L) {
    int raw = luaL_checkinteger(L, 1);
    int act = luaL_checkinteger(L, 2);
    int internal = raw - 1;
   // printf("[DBG] lua_casl_action raw=%d internal=%d action=%d\n\r", raw, internal, act);
    casl_action(internal, act); // C is zero-based
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

// _c.tell('output', channel, value) - Send output to slopes system (proper crow behavior)
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
            printf("[core0] _c.tell output[%d] %.3f\n\r", channel, value);
            if (output_tell_debug_count < 32) {
                usb_log_printf("log: output[%d].volts -> %.3f", channel, value);
                output_tell_debug_count++;
            }
            hardware_output_set_voltage(channel, value);
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

// soutput_handler(channel, voltage) - Bridge from C to Lua output callbacks
int LuaManager::lua_soutput_handler(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from C
    float voltage = (float)luaL_checknumber(L, 2);
    
    // Call the Lua soutput_handler function which will invoke output[channel].receive()
    lua_getglobal(L, "soutput_handler");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, channel);
        lua_pushnumber(L, voltage);
        lua_call(L, 2, 0);
    } else {
        // soutput_handler not defined, just log it
        printf("soutput_handler: ch%d=%.3f (no handler)\n\r", channel, voltage);
        lua_pop(L, 1);
    }
    
    return 0;
}

// Crow backend functions for Input.lua compatibility

// io_get_input(channel) - Read input voltage using AudioIn1/AudioIn2
int LuaManager::lua_io_get_input(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float volts = 0.0f;
    
    if (g_blackbird_instance) {
        volts = get_input_state_simple(channel - 1); // Convert to 0-based
    }
    
    lua_pushnumber(L, volts);
    return 1;
}

// Mode-specific detection callbacks - OPTIMIZED for direct execution
static constexpr bool kDetectionDebug = false;

// Lock-free stream callback with time-based batching - posts to queue without blocking ISR
static void stream_callback(int channel, float value) {
    // OPTIMIZATION: Time-based batching to reduce queue overhead
    // Only post if: 1) Value changed significantly, OR 2) Timeout expired
    static float last_value[8] = {0};
    static uint32_t last_post_time[8] = {0};
    
    uint32_t now = time_us_32();
    float delta = fabsf(value - last_value[channel]);
    uint32_t time_since_post = now - last_post_time[channel];
    
    // Post if significant change (>10mV) OR timeout (10ms for stream mode)
    bool significant_change = (delta > 0.01f);  // 10mV threshold
    bool timeout = (time_since_post > 10000);    // 10ms timeout
    
    if (significant_change || timeout) {
        if (input_lockfree_post(channel, value, 1)) {  // type=1 for stream
            last_value[channel] = value;
            last_post_time[channel] = now;
        } else {
            static uint32_t drop_count = 0;
            if (++drop_count % 100 == 0) {
                queue_debug_message("Stream lock-free queue full, dropped %lu events", drop_count);
            }
        }
    }
}

// Shared state for change callback duplicate suppression - MUST BE SHARED!
static int8_t g_change_last_reported_state[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

// Reset function for change callback state - called when input modes change
static void reset_change_callback_state(int channel) {
    if (channel >= 0 && channel < 8) {
        g_change_last_reported_state[channel] = -1;
    }
}

// Lock-free change callback - posts to queue without blocking ISR
static void change_callback(int channel, float value) {
    // For change detection, 'value' is actually the state (0.0 or 1.0), not voltage
    bool state = (value > 0.5f);
    
    // CRITICAL FIX: Don't suppress duplicates here!
    // The detect.c layer already handles proper edge detection and only calls us on transitions.
    // The old duplicate suppression was breaking 'rising'/'falling' modes because:
    // - rising mode: rise(1) -> fall(silent) -> rise(1) <- second rise was blocked as "duplicate 1"
    // - falling mode: fall(0) -> rise(silent) -> fall(0) <- second fall was blocked as "duplicate 0"
    // 
    // For 'both' mode, detect.c alternates between 0 and 1, so no duplicates occur anyway.
    // Therefore, this duplicate check was incorrect - remove it entirely.
    
    // Update tracking state (for debugging/diagnostics only)
    if (channel >= 0 && channel < 8) {
        g_change_last_reported_state[channel] = (int8_t)state;
    }
    
    // Post to lock-free input queue - NEVER BLOCKS!
    if (!input_lockfree_post(channel, value, 0)) {  // type=0 for change
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Change lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Generic callback for other modes (volume, peak, etc.) with time-based batching
static void generic_callback(int channel, float value) {
    // OPTIMIZATION: Time-based batching for volume/peak modes
    static float last_value[8] = {0};
    static uint32_t last_post_time[8] = {0};
    
    uint32_t now = time_us_32();
    float delta = fabsf(value - last_value[channel]);
    uint32_t time_since_post = now - last_post_time[channel];
    
    // Post if significant change (>5mV) OR timeout (5ms for volume/peak)
    bool significant_change = (delta > 0.005f);  // 5mV threshold
    bool timeout = (time_since_post > 5000);     // 5ms timeout
    
    if (significant_change || timeout) {
        if (input_lockfree_post(channel, value, 2)) {  // type=2 for generic
            last_value[channel] = value;
            last_post_time[channel] = now;
        } else {
            static uint32_t drop_count = 0;
            if (++drop_count % 100 == 0) {
                queue_debug_message("Generic lock-free queue full, dropped %lu events", drop_count);
            }
        }
    }
}

// Lock-free input event handler - processes detection events from lock-free queue
extern "C" void L_handle_input_lockfree(input_event_lockfree_t* event) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (!lua_mgr) return;
    
    int channel = event->channel + 1;  // Convert to 1-based for Lua
    float value = event->value;
    int detection_type = event->detection_type;
    
    // LED indicator for lock-free input processing
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(0);
    }
    
    // Call appropriate Lua handler based on detection type
    char lua_call[128];
    if (detection_type == 1) {
        // Stream mode
        snprintf(lua_call, sizeof(lua_call),
            "if stream_handler then stream_handler(%d, %.6f) end",
            channel, value);
    } else {
        // Change mode (and other modes)
        bool state = (value > 0.5f);
        snprintf(lua_call, sizeof(lua_call),
            "if change_handler then change_handler(%d, %d) end",
            channel, state ? 1 : 0);
    }
    
    if (kDetectionDebug) {
        printf("LOCKFREE INPUT: ch%d type=%d value=%.3f\n\r",
               channel, detection_type, value);
    }
    
    // Execute Lua callback (safe on Core 0)
    lua_mgr->evaluate_safe(lua_call);
    
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(0);
    }
}

// Core-safe stream event handler - NO BLOCKING CALLS, NO SLEEP!
extern "C" void L_handle_stream_safe(event_t* e) {
    // CRITICAL: This function can be called from either core
    // Must be thread-safe and never block!
    
    static volatile uint32_t callback_counter = 0;
    callback_counter++;
    
    // LED 3: Stream event handler called
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(3);
    }
    
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (!lua_mgr) {
        if (g_blackbird_instance) {
            ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(3);
        }
        return;
    }
    
    int channel = e->index.i + 1; // Convert to 1-based
    float value = e->data.f;
    
    if (kDetectionDebug) {
        printf("STREAM SAFE CALLBACK #%lu: ch%d value=%.3f\n\r",
               callback_counter, channel, value);
    }
    
    // Use crow-style global stream_handler dispatching
    char lua_call[128];
    snprintf(lua_call, sizeof(lua_call),
        "if stream_handler then stream_handler(%d, %.6f) end",
        channel, value);
    
    // Safe to use blocking safe evaluation (runs on control core)
    lua_mgr->evaluate_safe(lua_call);
    
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(3);
    }
    
    if (kDetectionDebug) {
        printf("STREAM SAFE CALLBACK #%lu: Completed successfully\n\r", callback_counter);
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
    // Pass numeric 0/1 like original crow firmware (Input.lua expects number, not boolean)
    snprintf(lua_call, sizeof(lua_call),
        "if change_handler then change_handler(%d, %d) end",
        channel, state ? 1 : 0);
    
    // LED 1: About to call Lua (proves we reach Lua execution attempt)
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(1);
    }
    
    // Now safe to use blocking safe evaluation (runs on control core, outside real-time audio path)
    lua_mgr->evaluate_safe(lua_call);
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

// Core-safe ASL done event handler - triggers Lua "done" callbacks
extern "C" void L_handle_asl_done_safe(event_t* e) {
    // CRITICAL: This function can be called from either core
    // Must be thread-safe and never block!
    
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (!lua_mgr) {
        return;
    }
    
    int channel = e->index.i + 1; // Convert to 1-based
    
    // printf("ASL sequence completed on output[%d] - triggering 'done' callback\n\r", channel);
    
    // Use crow-style ASL completion callback dispatching
    char lua_call[128];
    snprintf(lua_call, sizeof(lua_call),
        "if output and output[%d] and output[%d].done then output[%d].done() end",
        channel, channel, channel);
    
    // Safe to use blocking safe evaluation (runs on control core)
    lua_mgr->evaluate_safe(lua_call);
}

// L_queue_asl_done function - queues ASL completion event
extern "C" void L_queue_asl_done(int channel) {
    event_t e = { 
        .handler = L_handle_asl_done_safe,
        .index = { .i = channel }, // 0-based channel index
        .data = { .f = 0.0f },
        .type = EVENT_TYPE_CHANGE, // Reuse change event type
        .timestamp = to_ms_since_boot(get_absolute_time())
    };
    
    if (!event_post(&e)) {
        printf("Failed to post ASL done event for channel %d\n\r", channel + 1);
    }
}

// Input mode functions - connect to detection system with mode-specific callbacks
int LuaManager::lua_set_input_stream(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_stream(detector, stream_callback, time);
        printf("Input %d: stream mode, interval %.3fs\n\r", channel, time);
    }
    return 0;
}

int LuaManager::lua_set_input_change(lua_State* L) {
    DEBUG_AUDIO_PRINT("DEBUG: lua_set_input_change called!\n\r");
    int channel = luaL_checkinteger(L, 1);
    float threshold = (float)luaL_checknumber(L, 2);
    float hysteresis = (float)luaL_checknumber(L, 3);
    const char* direction = luaL_checkstring(L, 4);
    
    DEBUG_AUDIO_PRINT("DEBUG: args: ch=%d, thresh=%.3f, hyst=%.3f, dir='%s'\n\r", 
                      channel, threshold, hysteresis, direction);
    
    // CRITICAL FIX: Reset callback state when mode changes to allow new callbacks to fire
    reset_change_callback_state(channel - 1);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        int8_t dir = Detect_str_to_dir(direction);
        DEBUG_AUDIO_PRINT("DEBUG: Direction '%s' converted to %d\n\r", direction, dir);
        Detect_change(detector, change_callback, threshold, hysteresis, dir);
        DEBUG_DETECT_PRINT("Input %d: change mode, thresh %.3f, hyst %.3f, dir %s\n\r", 
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
        Detect_window(detector, generic_callback, windows, wLen, hysteresis);
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
        Detect_scale(detector, generic_callback, scale, sLen, temp, scaling);
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
        Detect_volume(detector, generic_callback, time);
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
        Detect_peak(detector, generic_callback, threshold, hysteresis);
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
        Detect_freq(detector, generic_callback, time);
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
        Detect_change(detector, change_callback, threshold, hysteresis, 1); // Rising edge
        printf("Input %d: clock mode, div %.3f, thresh %.3f, hyst %.3f\n\r", 
               channel, div, threshold, hysteresis);
    }
    return 0;
}

int LuaManager::lua_set_input_none(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        // ATOMIC MODE SWITCHING: Set flag to prevent callback corruption
        detector->mode_switching = true;
        
        // Clear detection mode safely
        Detect_none(detector);
        
        // Clear flag after mode change is complete
        detector->mode_switching = false;
        
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

// Bridge function called from slopes.c to trigger Lua soutput_handler
extern "C" void trigger_soutput_handler(int channel, float voltage) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (lua_mgr && lua_mgr->L) {
        // Call the C binding for soutput_handler which will call the Lua function
        lua_pushcfunction(lua_mgr->L, LuaManager::lua_soutput_handler);
        lua_pushinteger(lua_mgr->L, channel + 1); // Convert to 1-based
        lua_pushnumber(lua_mgr->L, voltage);
        if (lua_pcall(lua_mgr->L, 2, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(lua_mgr->L, -1);
            printf("soutput_handler error: %s\n\r", error ? error : "unknown error");
            lua_pop(lua_mgr->L, 1);
        }
    }
}

// Provide Lua state access for l_crowlib metro handler
extern "C" lua_State* get_lua_state(void) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    return lua_mgr ? lua_mgr->L : nullptr;
}

BlackbirdCrow crow;

static void core1_entry() {
        printf("[boot] core1 audio engine starting\n\r");
        //Normalisation probe was causing issues so disabling.
        //crow.EnableNormalisationProbe();
        crow.Run(); 
}

int main()
{
    set_sys_clock_khz(200000, true);

    stdio_init_all();
    // Disable stdio buffering to ensure immediate visibility of debug prints
    setvbuf(stdout, NULL, _IONBF, 0);
    // Wait briefly (up to 1500ms) for a USB serial host to connect so the boot banner is visible
    {
        absolute_time_t until = make_timeout_time_ms(1500);
        while (!stdio_usb_connected() && absolute_time_diff_us(get_absolute_time(), until) > 0) {
            tight_loop_contents();
        }
    }

    multicore_launch_core1(core1_entry);
    
    // Allow core1 to start before entering control loop
    sleep_ms(500);
    
    // Start Core0 main control loop (handles commands and events)
    crow.MainControlLoop();
}
