#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

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
#include "lib/lualink.h"
#include "lib/casl.h"
}

// Generated Lua bytecode headers
#include "test_asl.h"
#include "asl.h"
#include "asllib.h"
#include "output.h"
#include "input.h"

// Output state storage - use int32 for RP2040 efficiency
static volatile int32_t output_states_mV[4] = {0, 0, 0, 0}; // Store in millivolts

// Forward declaration
class BlackbirdCrow;
static volatile BlackbirdCrow* g_blackbird_instance = nullptr;

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
private:
    lua_State* L;
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
            printf("Error: Could not create Lua state\n\r");
            return;
        }
        
        // Load basic Lua libraries
        luaL_openlibs(L);
        
        // Override print function
        lua_register(L, "print", lua_print);
        
    // Add time function
    lua_register(L, "time", lua_time);
    
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
    
    // Load embedded ASL libraries using luaL_dobuffer
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
            
            // Create input[1] and input[2] instances using Input.new()
            if (luaL_dostring(L, R"(
                input = {}
                for i = 1, 2 do
                    input[i] = Input.new(i)
                end
                print("Input objects created successfully!")
            )") != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                printf("Error creating input objects: %s\n\r", error ? error : "unknown error");
                lua_pop(L, 1);
            } else {
                printf("Input.lua loaded successfully!\n\r");
            }
        }
        
        printf("ASL libraries loaded successfully!\n\r");
    }
    
    // Execute the embedded test suite
    void run_embedded_test() {
        if (!L) return;
        
        printf("Running embedded ASL test suite...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_asl, test_asl_len, "test_asl.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running ASL test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("ASL test suite completed!\n\r");
        }
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
        
        // Store state for lua queries (in millivolts)
        output_states_mV[channel - 1] = volts_mV;
        
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
        return (float)output_states_mV[channel - 1] / 1000.0f;
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
    
    BlackbirdCrow()
    {
        rx_buffer_pos = 0;
        memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        // Set global instance for Lua bindings
        g_blackbird_instance = this;
        
        // Initialize slopes system for crow-style output processing
        S_init(4); // Initialize 4 output channels
        
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
                        } else if (strcmp(rx_buffer, "test_asl") == 0) {
                            // Special command to run embedded ASL test
                            if (lua_manager) {
                                lua_manager->run_embedded_test();
                            }
                        } else if (strcmp(rx_buffer, "test_casl") == 0) {
                            // Special command to run CASL integration test
                            if (lua_manager) {
                                lua_manager->evaluate("dofile('test_casl_integration.lua')");
                            }
                        } else {
                            // Not a ^^ command, treat as Lua code
                            if (lua_manager) {
                                lua_manager->evaluate(rx_buffer);
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
        // Copy CV inputs for Core 1 to access
        // v1 = CVIn1();
        // v2 = CVIn2();
        
        // Process slopes system for all output channels (crow-style)
        // Using safe timer-based approach to avoid USB enumeration issues
        static uint32_t last_slopes_update = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        // Run slopes at 1kHz instead of 48kHz for safety
        if (now != last_slopes_update) {
            last_slopes_update = now;
            static float sample_buffer[1]; // Single sample buffer
            
            for(int i = 0; i < 4; i++) {
                S_step_v(i, sample_buffer, 1);  // Generate one sample from slopes
                hardware_set_output(i+1, sample_buffer[0]);  // Send to hardware
            }
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
        
        // Use slopes system to handle the voltage change
        // This enables slew and other crow features
        S_toward(output_data->channel - 1, // Convert to 0-based indexing
                volts,                    // Destination voltage
                0.0,                     // Immediate (no slew for now)
                SHAPE_Linear,            // Linear transition
                nullptr);                // No callback
        
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
    const char* module = luaL_checkstring(L, 1);
    int channel = luaL_checkinteger(L, 2);
    float value = (float)luaL_checknumber(L, 3);
    
    if (strcmp(module, "output") == 0) {
        if (g_blackbird_instance) {
            ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_output(channel, value);
        }
    } else {
        printf("_c.tell called with unknown module: %s\n\r", module);
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

// Input mode functions (basic stubs for now - can be enhanced later)
int LuaManager::lua_set_input_stream(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    printf("set_input_stream: channel %d, time %.3f (basic stub)\n\r", channel, time);
    return 0;
}

int LuaManager::lua_set_input_change(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float threshold = (float)luaL_checknumber(L, 2);
    float hysteresis = (float)luaL_checknumber(L, 3);
    const char* direction = luaL_checkstring(L, 4);
    printf("set_input_change: channel %d, thresh %.3f, hyst %.3f, dir %s (basic stub)\n\r", 
           channel, threshold, hysteresis, direction);
    return 0;
}

int LuaManager::lua_set_input_window(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    // windows table and hysteresis
    printf("set_input_window: channel %d (basic stub)\n\r", channel);
    return 0;
}

int LuaManager::lua_set_input_scale(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    // notes table, temp, scaling
    printf("set_input_scale: channel %d (basic stub)\n\r", channel);
    return 0;
}

int LuaManager::lua_set_input_volume(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    printf("set_input_volume: channel %d, time %.3f (basic stub)\n\r", channel, time);
    return 0;
}

int LuaManager::lua_set_input_peak(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float threshold = (float)luaL_checknumber(L, 2);
    float hysteresis = (float)luaL_checknumber(L, 3);
    printf("set_input_peak: channel %d, thresh %.3f, hyst %.3f (basic stub)\n\r", 
           channel, threshold, hysteresis);
    return 0;
}

int LuaManager::lua_set_input_freq(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    printf("set_input_freq: channel %d, time %.3f (basic stub)\n\r", channel, time);
    return 0;
}

int LuaManager::lua_set_input_clock(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float div = (float)luaL_checknumber(L, 2);
    float threshold = (float)luaL_checknumber(L, 3);
    float hysteresis = (float)luaL_checknumber(L, 4);
    printf("set_input_clock: channel %d, div %.3f, thresh %.3f, hyst %.3f (basic stub)\n\r", 
           channel, div, threshold, hysteresis);
    return 0;
}

int LuaManager::lua_set_input_none(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    printf("set_input_none: channel %d (basic stub)\n\r", channel);
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

int main()
{
    stdio_init_all();

    BlackbirdCrow crow;
    crow.Run();
}
