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
    }
    
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
    BlackbirdCrow()
    {
        rx_buffer_pos = 0;
        memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
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
        v1 = CVIn1();
        v2 = CVIn2();
        
        // Basic audio passthrough for now
        AudioOut1(AudioIn1());
        AudioOut2(AudioIn2());
        
        // Could add basic crow-like functionality here later
        // For now just pass audio through
    }
};

int main()
{
    stdio_init_all();

    BlackbirdCrow crow;
    crow.Run();
}
