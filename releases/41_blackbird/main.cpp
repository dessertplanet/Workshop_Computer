#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "class/cdc/cdc_device.h"
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
#include "lib/l_bootstrap.h"
#include "lib/ll_timers.h"
#include "lib/metro.h"
#include "lib/mailbox.h"
#include "lib/clock.h"
#include "lib/events_lockfree.h"
#include "lib/flash_storage.h"
#include "lib/caw.h"
}

// Generated Lua bytecode headers - Core libraries (always included)
#include "asl.h"
#include "asllib.h"
#include "output.h"
#include "input.h"
#include "metro.h"
#include "First.h"

// Crow ecosystem library headers
#include "calibrate.h"
#include "sequins.h"
#define public public_lua
#include "public.h"
#undef public
extern "C" {
extern const unsigned char clock[];
extern const unsigned int clock_len;
}
#include "quote.h"
#include "timeline.h"
#include "hotswap.h"

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

static volatile int32_t g_output_state_mv[4] = {0, 0, 0, 0};
static volatile int32_t g_input_state_q12[2] = {0, 0};
static volatile int16_t g_audioin_raw[2] = {0, 0};

// Stream-equivalent values: Always maintained for .volts queries (matches crow behavior)
static float g_input_stream_volts[2] = {0.0f, 0.0f};
static uint32_t g_input_stream_last_update[2] = {0, 0};
static float g_audioin_stream_volts[2] = {0.0f, 0.0f};
static uint32_t g_audioin_stream_last_update[2] = {0, 0};

// Pulse output state tracking (set from Lua layer)
static volatile bool g_pulse_out_state[2] = {false, false};

// Pulse input detection mode: 0='none', 1='change', 2='clock'
static volatile int8_t g_pulsein_mode[2] = {0, 0};

// Pulse input direction filter: 0='both', 1='rising', -1='falling'
static volatile int8_t g_pulsein_direction[2] = {0, 0};

// Pulse input clock division (for clock mode)
static volatile float g_pulsein_clock_div[2] = {1.0f, 1.0f};

// LED pulse stretching counters (to make 10ms pulses visible at 100Hz update rate)
static volatile uint16_t g_pulse_led_stretch[2] = {0, 0};

// Pulse input state tracking (read from hardware, cached for Lua access)
static volatile bool g_pulsein_state[2] = {false, false};

// Pulse input edge detection flags (set at 48kHz, cleared in main loop)
static volatile bool g_pulsein_edge_detected[2] = {false, false};
static volatile bool g_pulsein_edge_state[2] = {false, false};

// Clock edge pending flags (deferred from ISR to Core 0 to avoid FP math in ISR)
static volatile bool g_pulsein_clock_edge_pending[2] = {false, false};

// LED update coordination between cores
static volatile bool g_led_update_pending = false;
static volatile int32_t g_led_output_snapshot[4] = {0, 0, 0, 0};
static volatile bool g_led_pulse_snapshot[2] = {false, false};

// ========================================================================
// AUDIO-RATE NOISE GENERATOR (48kHz) - INTEGER MATH ONLY
// ========================================================================
// Noise generator state for audio-rate output
static volatile bool g_noise_active[4] = {false, false, false, false};
static volatile uint8_t g_noise_active_mask = 0;  // Bitmask for fast checking if ANY noise is active
static volatile int32_t g_noise_gain[4] = {0, 0, 0, 0};  // Gain in fixed-point (0-6000 mV)
static volatile uint32_t g_noise_lock_counter[4] = {0, 0, 0, 0};  // Prevent clearing noise for a few calls

// Fast xorshift32 PRNG state for audio-rate noise
static uint32_t g_noise_state = 0xDEADBEEF;

// ========================================================================
// PERFORMANCE MONITORING
// ========================================================================
static volatile bool g_performance_warning = false;
static volatile uint32_t g_worst_case_us = 0;
static volatile uint32_t g_overrun_count = 0;

// Fast audio-rate noise generation in ISR - NO FLOATING POINT
// Returns noise value in millivolts (-6000 to +6000)
static inline int32_t generate_audio_noise_mv(int32_t gain_mv) {
    // xorshift32 algorithm - very fast PRNG (only shifts and XORs)
    g_noise_state ^= g_noise_state << 13;
    g_noise_state ^= g_noise_state >> 17;
    g_noise_state ^= g_noise_state << 5;
    
    // Extract 16-bit signed value from upper bits
    int16_t noise_16 = (int16_t)(g_noise_state >> 16);
    
    // Scale by gain using integer math: (noise_16 * gain_mv) / 32768
    // noise_16 is in range [-32768, 32767]
    // gain_mv is in range [0, 6000]
    // Result is in range [-6000, +6000] mV
    return ((int32_t)noise_16 * gain_mv) >> 15;  // Divide by 32768 using shift
}

static void set_output_state_simple(int channel, int32_t value_mv) {
    if (channel >= 0 && channel < 4) {
        g_output_state_mv[channel] = value_mv;
    }
}

extern "C" float get_input_state_simple(int channel) {
    if (channel >= 0 && channel < 2) {
        // Return stream-equivalent value (always maintained, matches crow .volts behavior)
        return g_input_stream_volts[channel];
    }
    return 0.0f;
}

static void set_input_state_simple(int channel, int16_t rawValue) {
    if (channel >= 0 && channel < 2) {
        g_input_state_q12[channel] = rawValue;
    }
}

// Update stream-equivalent values (called from main loop, not ISR)
// Uses same denoising logic as stream callback: 10mV threshold + 5ms timeout
static void update_input_stream_values() {
    uint32_t now = time_us_32();
    
    // Update CV inputs
    for (int ch = 0; ch < 2; ch++) {
        // Get current raw value
        float current_volts = (float) g_input_state_q12[ch] * (6.0f / 2047.0f);
        
        // Apply same denoising as stream callback
        float delta = fabsf(current_volts - g_input_stream_volts[ch]);
        uint32_t time_since_update = now - g_input_stream_last_update[ch];
        
        // Update if significant change (>10mV) OR timeout (10ms) OR first run
        bool significant_change = (delta > 0.01f);  // 10mV threshold
        bool timeout = (time_since_update > 5000); // 5ms timeout
        
        if (significant_change || timeout || g_input_stream_last_update[ch] == 0) {
            g_input_stream_volts[ch] = current_volts;
            g_input_stream_last_update[ch] = now;
        }
    }
    
    // Update audio inputs
    for (int ch = 0; ch < 2; ch++) {
        // Get current raw value
        float current_volts = (float) g_audioin_raw[ch] * (6.0f / 2047.0f);
        
        // Apply same denoising as stream callback
        float delta = fabsf(current_volts - g_audioin_stream_volts[ch]);
        uint32_t time_since_update = now - g_audioin_stream_last_update[ch];
        
        // Update if significant change (>10mV) OR timeout (5ms) OR first run
        bool significant_change = (delta > 0.01f);  // 10mV threshold
        bool timeout = (time_since_update > 5000); // 5ms timeout
        
        if (significant_change || timeout || g_audioin_stream_last_update[ch] == 0) {
            g_audioin_stream_volts[ch] = current_volts;
            g_audioin_stream_last_update[ch] = now;
        }
    }
}

// Store raw ADC value in ISR (no floating point)
static void set_audioin_raw(int channel, int16_t rawValue) {
    if (channel >= 0 && channel < 2) {
        g_audioin_raw[channel] = rawValue;
    }
}

// Convert to volts when accessed from Lua (outside ISR)
extern "C" float get_audioin_volts(int channel) {
    if (channel >= 0 && channel < 2) {
        // Return stream-equivalent value (always maintained, matches crow .volts behavior)
        return g_audioin_stream_volts[channel];
    }
    return 0.0f;
}

static int32_t get_output_state_simple(int channel) {
    if (channel >= 0 && channel < 4) {
        return g_output_state_mv[channel];
    }
    return 0;
}

// Forward declarations
static void public_update();

// Helper function to check if we have a complete packet (ends with \n or \r)
static bool is_packet_complete(const char* buffer, int length) {
    if (length == 0) return false;
    char last_char = buffer[length - 1];
    return (last_char == '\n' || last_char == '\r');
}

// Helper function to detect triple backticks (multi-line delimiter)
static inline bool check_for_backticks(const char* buffer, int pos) {
    // Need at least 3 chars in buffer
    if (pos < 3) return false;
    
    // Check if last 3 characters are backticks
    return (buffer[pos-3] == '`' && 
            buffer[pos-2] == '`' && 
            buffer[pos-1] == '`');
}

class BlackbirdCrow;
static volatile BlackbirdCrow* g_blackbird_instance = nullptr;
static BlackbirdCrow* g_crow_core1 = nullptr;
void core1_entry(); // defined after BlackbirdCrow - non-static so flash_storage can call it


// Forward declaration of C interface function (implemented after BlackbirdCrow class)
extern "C" void hardware_output_set_voltage(int channel, float voltage);
extern "C" void hardware_pulse_output_set(int channel, bool state);
extern "C" bool pulseout_has_custom_action(int channel);
extern "C" void pulseout_execute_action(int channel);

// Forward declaration of safe event handlers
extern "C" void L_handle_change_safe(event_t* e);
extern "C" void L_handle_stream_safe(event_t* e);
extern "C" void L_handle_asl_done_safe(event_t* e);
extern "C" void L_handle_input_lockfree(input_event_lockfree_t* event);
extern "C" void L_handle_metro_lockfree(metro_event_lockfree_t* event);

// Forward declaration of output batching functions
extern "C" void output_batch_begin();
extern "C" void output_batch_flush();
static void output_batch_queue(int channel, float volts);
static bool output_is_batching();


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
            printf("\n\r"); // Add line ending if not present
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

// Command types (from crow's caw.h) - now included via lib/caw.h

// Audio input Lua C functions - similar to pulsein but simpler (read-only volts)
static int audioin_index(lua_State* L) {
    // Get the index from the table itself
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = lua_tostring(L, 2);
    
    // Only support .volts property
    if (strcmp(key, "volts") == 0) {
        // Floating point conversion happens here, not in ISR
        float volts = get_audioin_volts(idx);
        lua_pushnumber(L, volts);
        return 1;
    } else if (strcmp(key, "_idx") == 0) {
        lua_pushinteger(L, idx);
        return 1;
    }
    
    lua_pushnil(L);
    return 1;
}

// Pulse input Lua C functions (must be at file scope, not in class)
// NO UPVALUES - get index from table's _idx field to avoid closure memory issues
static int pulsein_index(lua_State* L) {
    // Get the index from the table itself
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = lua_tostring(L, 2);
    
    // First check real fields (but skip _idx)
    if (strcmp(key, "_idx") != 0) {
        lua_pushvalue(L, 2);
        lua_rawget(L, 1);
        if (!lua_isnil(L, -1)) {
            return 1;
        }
        lua_pop(L, 1);
    }
    
    // Synthesize dynamic properties
    if (strcmp(key, "state") == 0) {
        lua_pushboolean(L, g_pulsein_state[idx]);
        return 1;
    } else if (strcmp(key, "direction") == 0) {
        const char* dir = (g_pulsein_direction[idx] == 1) ? "rising" :
                          (g_pulsein_direction[idx] == -1) ? "falling" : "both";
        lua_pushstring(L, dir);
        return 1;
    } else if (strcmp(key, "change") == 0) {
        char callback_name[32];
        snprintf(callback_name, sizeof(callback_name), "_pulsein%d_change_callback", idx + 1);
        lua_getglobal(L, callback_name);
        return 1;
    } else if (strcmp(key, "mode") == 0) {
        const char* mode_str = (g_pulsein_mode[idx] == 2) ? "clock" :
                               (g_pulsein_mode[idx] == 1) ? "change" : "none";
        lua_pushstring(L, mode_str);
        return 1;
    } else if (strcmp(key, "division") == 0) {
        lua_pushnumber(L, g_pulsein_clock_div[idx]);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int pulsein_newindex(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = lua_tostring(L, 2);
    
    if (strcmp(key, "change") == 0) {
        if (lua_isfunction(L, 3) || lua_isnil(L, 3)) {
            char callback_name[32];
            snprintf(callback_name, sizeof(callback_name), "_pulsein%d_change_callback", idx + 1);
            lua_pushvalue(L, 3);
            lua_setglobal(L, callback_name);
        } else {
            return luaL_error(L, "pulsein.change must be a function or nil");
        }
        return 0;
    } else if (strcmp(key, "mode") == 0) {
        const char* mode = lua_tostring(L, 3);
        if (!mode) return luaL_error(L, "pulsein.mode must be a string");
        if (strcmp(mode, "none") == 0) {
            g_pulsein_mode[idx] = 0;
        } else if (strcmp(mode, "change") == 0) {
            g_pulsein_mode[idx] = 1;
        } else if (strcmp(mode, "clock") == 0) {
            g_pulsein_mode[idx] = 2;
            // Clock mode only detects rising edges
            g_pulsein_direction[idx] = 1;
            // Set clock source to external (crow input)
            clock_set_source(CLOCK_SOURCE_CROW);
            clock_crow_in_div(g_pulsein_clock_div[idx]);
        } else {
            return luaL_error(L, "pulsein.mode must be 'none', 'change', or 'clock'");
        }
        return 0;
    } else if (strcmp(key, "division") == 0) {
        float div = (float)lua_tonumber(L, 3);
        if (div <= 0) return luaL_error(L, "pulsein.division must be positive");
        g_pulsein_clock_div[idx] = div;
        // Update if currently in clock mode
        if (g_pulsein_mode[idx] == 2) {
            clock_crow_in_div(div);
        }
        return 0;
    } else if (strcmp(key, "direction") == 0) {
        const char* dir = lua_tostring(L, 3);
        if (!dir) return luaL_error(L, "pulsein.direction must be a string");
        if (strcmp(dir, "both") == 0) {
            g_pulsein_direction[idx] = 0;
        } else if (strcmp(dir, "rising") == 0) {
            g_pulsein_direction[idx] = 1;
        } else if (strcmp(dir, "falling") == 0) {
            g_pulsein_direction[idx] = -1;
        } else {
            return luaL_error(L, "pulsein.direction must be 'both', 'rising', or 'falling'");
        }
        return 0;
    } else if (strcmp(key, "state") == 0) {
        return luaL_error(L, "pulsein.state is read-only");
    }
    return 0;
}

// __call metamethod for bb.pulsein[n]{mode='change', direction='rising'}
static int pulsein_call(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    // Argument should be a table
    if (!lua_istable(L, 2)) {
        return luaL_error(L, "pulsein() expects a table argument");
    }
    
    // Process mode field
    lua_getfield(L, 2, "mode");
    if (lua_isstring(L, -1)) {
        const char* mode = lua_tostring(L, -1);
        if (strcmp(mode, "none") == 0) {
            g_pulsein_mode[idx] = 0;
        } else if (strcmp(mode, "change") == 0) {
            g_pulsein_mode[idx] = 1;
        } else if (strcmp(mode, "clock") == 0) {
            g_pulsein_mode[idx] = 2;
            g_pulsein_direction[idx] = 1;  // Clock always uses rising edges
            clock_set_source(CLOCK_SOURCE_CROW);
            clock_crow_in_div(g_pulsein_clock_div[idx]);
        } else {
            return luaL_error(L, "pulsein mode must be 'none', 'change', or 'clock'");
        }
    }
    lua_pop(L, 1);
    
    // Process division field (for clock mode)
    lua_getfield(L, 2, "division");
    if (lua_isnumber(L, -1)) {
        float div = (float)lua_tonumber(L, -1);
        if (div > 0) {
            g_pulsein_clock_div[idx] = div;
            if (g_pulsein_mode[idx] == 2) {
                clock_crow_in_div(div);
            }
        }
    }
    lua_pop(L, 1);
    
    // Process direction field
    lua_getfield(L, 2, "direction");
    if (lua_isstring(L, -1)) {
        const char* dir = lua_tostring(L, -1);
        if (strcmp(dir, "both") == 0) {
            g_pulsein_direction[idx] = 0;
        } else if (strcmp(dir, "rising") == 0) {
            g_pulsein_direction[idx] = 1;
        } else if (strcmp(dir, "falling") == 0) {
            g_pulsein_direction[idx] = -1;
        } else {
            return luaL_error(L, "pulsein direction must be 'both', 'rising', or 'falling'");
        }
    }
    lua_pop(L, 1);
    
    return 0;
}

// ========================================================================
// PULSE OUTPUT LUA C FUNCTIONS (bb.pulseout[1] and bb.pulseout[2])
// ========================================================================

// __index metamethod for bb.pulseout[n].action, bb.pulseout[n].state, bb.pulseout[n].clock
static int pulseout_index(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = luaL_checkstring(L, 2);
    
    if (strcmp(key, "action") == 0) {
        // Return the stored action
        lua_getfield(L, 1, "_action");
        return 1;
    } else if (strcmp(key, "state") == 0) {
        // Return current pulse output state
        lua_pushboolean(L, g_pulse_out_state[idx]);
        return 1;
    } else if (strcmp(key, "_idx") == 0) {
        lua_pushinteger(L, idx);
        return 1;
    }
    
    // For other keys (like "clock"), check if there's a method in the metatable
    if (lua_getmetatable(L, 1)) {  // Get metatable
        lua_pushvalue(L, 2);  // Push the key
        lua_rawget(L, -2);  // Get metatable[key]
        if (!lua_isnil(L, -1)) {
            lua_remove(L, -2);  // Remove metatable, keep value
            return 1;
        }
        lua_pop(L, 2);  // Pop nil and metatable
    }
    
    lua_pushnil(L);
    return 1;
}

// __newindex metamethod for bb.pulseout[n].action = ...
static int pulseout_newindex(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = luaL_checkstring(L, 2);
    
    if (strcmp(key, "action") == 0) {
        // Check if user is clearing the action with 'none'
        if (lua_isstring(L, 3)) {
            const char* str = lua_tostring(L, 3);
            if (strcmp(str, "none") == 0) {
                // Clear the action
                lua_pushstring(L, "_action");
                lua_pushnil(L);
                lua_rawset(L, 1);  // Use rawset to bypass metamethod
                // Also ensure output is low
                g_pulse_out_state[idx] = false;
                hardware_pulse_output_set(idx + 1, false);  // Use 1-based channel numbering
                return 0;
            }
        }
        
        // User is setting a custom action
        // Store the action in the table using rawset to bypass metamethod
        lua_pushstring(L, "_action");
        lua_pushvalue(L, 3);
        lua_rawset(L, 1);
        return 0;
    } else if (strcmp(key, "clock_div") == 0 || strcmp(key, "ckcoro") == 0) {
        // Allow setting clock-related fields (used by :clock() method)
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 3);
        lua_rawset(L, 1);
        return 0;
    }
    
    return luaL_error(L, "pulseout: cannot set field '%s'", key);
}

// __call metamethod for bb.pulseout[n]() or bb.pulseout[n]('none') or bb.pulseout[n](action)
static int pulseout_call(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    // If an argument is provided, set it as the action (like bb.pulseout[1]('none'))
    if (lua_gettop(L) >= 2) {
        // Check if it's 'none' string
        if (lua_isstring(L, 2)) {
            const char* str = lua_tostring(L, 2);
            if (strcmp(str, "none") == 0) {
                // Clear the action
                lua_pushstring(L, "_action");
                lua_pushnil(L);
                lua_rawset(L, 1);
                // Set output low
                g_pulse_out_state[idx] = false;
                hardware_pulse_output_set(idx + 1, false);
                return 0;
            }
        }
        
        // Otherwise, set the provided value as the action AND execute it immediately
        lua_pushstring(L, "_action");
        lua_pushvalue(L, 2);
        lua_rawset(L, 1);
        
        // Now execute the action we just stored
        // Execute via _c.tell to route through proper pulse output handling
        lua_getglobal(L, "_c");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "tell");
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, "output");
                lua_pushinteger(L, idx + 3);  // Pulse outputs are logical channels 3 & 4
                lua_pushvalue(L, 2);  // Push the action argument
                
                if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
                    const char* error = lua_tostring(L, -1);
                    if (tud_cdc_connected() && error) {
                        tud_cdc_write_str("pulseout error: ");
                        tud_cdc_write_str(error);
                        tud_cdc_write_str("\n\r");
                        tud_cdc_write_flush();
                    }
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);  // pop non-function
            }
            lua_pop(L, 1);  // pop _c
        } else {
            lua_pop(L, 1);  // pop non-table
        }
        
        return 0;
    }
    
    // No argument - execute the current action
    lua_getfield(L, 1, "_action");
    if (lua_istable(L, -1)) {
        // It's an ASL action table - execute via _c.tell
        lua_getglobal(L, "_c");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "tell");
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, "output");
                lua_pushinteger(L, idx + 3);  // Pulse outputs are logical channels 3 & 4
                lua_pushvalue(L, -5);  // Push the action table
                if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
                    const char* error = lua_tostring(L, -1);
                    if (tud_cdc_connected() && error) {
                        tud_cdc_write_str("pulseout error: ");
                        tud_cdc_write_str(error);
                        tud_cdc_write_str("\n\r");
                        tud_cdc_write_flush();
                    }
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);  // pop non-function
            }
            lua_pop(L, 1);  // pop _c
        } else {
            lua_pop(L, 1);  // pop non-table
        }
        lua_pop(L, 1);  // pop _action
        return 0;
    } else if (lua_isfunction(L, -1)) {
        // It's a function - call it directly
        lua_call(L, 0, 0);
        return 0;
    }
    
    lua_pop(L, 1);
    return 0;
}

// External C function from l_crowlib.c
extern "C" {
    int l_crowlib_crow_reset(lua_State* L);
}

// Simple Lua Manager for crow emulation
class LuaManager {
public:
    lua_State* L;  // Made public for direct access in main.cpp
private:
    static LuaManager* instance;
    
    
    // Lua print function - sends output to serial
    static int lua_print(lua_State* L) {
        if (!tud_cdc_connected()) return 0;
        
        int n = lua_gettop(L);  // number of arguments
        lua_getglobal(L, "tostring");
        for (int i = 1; i <= n; i++) {
            lua_pushvalue(L, -1);  // function to be called
            lua_pushvalue(L, i);   // value to print
            lua_call(L, 1, 1);
            const char* s = lua_tostring(L, -1);  // get result
            if (s != NULL) {
                if (i > 1) tud_cdc_write_char('\t');
                tud_cdc_write_str(s);
            }
            lua_pop(L, 1);  // pop result
        }
        tud_cdc_write("\n\r",2);
        // REMOVED: tud_cdc_write_flush(); - batched in main loop
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
    static int lua_unique_id(lua_State* L);
    static int lua_memstats(lua_State* L);
    static int lua_perf_stats(lua_State* L);
    static int lua_pub_view_in(lua_State* L);
    static int lua_pub_view_out(lua_State* L);
    static int lua_tell(lua_State* L);
    static int lua_hardware_pulse(lua_State* L);
    
    // Conditional test function implementations - only compiled when tests are embedded
#ifdef EMBED_ALL_TESTS
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
    
    // Lua function to run Phase 2 performance test
    static int lua_test_phase2_performance(lua_State* L) {
        printf("Running Phase 2 block processing performance test...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_phase2_performance, test_phase2_performance_len, "test_phase2_performance.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running Phase 2 performance test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Phase 2 performance test completed successfully!\n\r");
        }
        return 0;
    }
    
    // Lua function to run simple output test
    static int lua_test_simple_output(lua_State* L) {
        printf("Running simple output hardware test...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_simple_output, test_simple_output_len, "test_simple_output.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running simple output test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Simple output test completed successfully!\n\r");
        }
        return 0;
    }
#elif defined(EMBED_TEST_ENHANCED_MULTICORE_SAFETY)
    // Single test: Enhanced multicore safety test
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
#elif defined(EMBED_TEST_LOCKFREE_PERFORMANCE)
    // Single test: Lock-free performance test
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
#elif defined(EMBED_TEST_RANDOM_VOLTAGE)
    // Single test: Random voltage test
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
#elif defined(EMBED_TEST_PHASE2_PERFORMANCE)
    // Single test: Phase 2 performance test
    static int lua_test_phase2_performance(lua_State* L) {
        printf("Running Phase 2 block processing performance test...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_phase2_performance, test_phase2_performance_len, "test_phase2_performance.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running Phase 2 performance test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Phase 2 performance test completed successfully!\n\r");
        }
        return 0;
    }
#elif defined(EMBED_TEST_SIMPLE_OUTPUT)
    // Single test: Simple output test
    static int lua_test_simple_output(lua_State* L) {
        printf("Running simple output hardware test...\n\r");
        if (luaL_loadbuffer(L, (const char*)test_simple_output, test_simple_output_len, "test_simple_output.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error running simple output test: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            printf("Simple output test completed successfully!\n\r");
        }
        return 0;
    }
#else
    // Production build - no test functions
#endif
    
    // Lua tab.print function - pretty print tables
    static int lua_tab_print(lua_State* L) {
        if (!tud_cdc_connected()) return 0;  // Add CDC connection check
        
        if (lua_gettop(L) != 1) {
            lua_pushstring(L, "tab.print expects exactly one argument");
            lua_error(L);
        }
        
        print_table_recursive(L, 1, 0);
        tud_cdc_write_str("\n\r");  // Use CDC write instead of printf
        tud_cdc_write_flush();       // Flush the CDC buffer
        return 0;
    }
    
// Helper to flush CDC buffer if it's getting full
// Flush when less than 64 bytes available (keep a safety margin)
static inline void flush_if_needed() {
    if (tud_cdc_write_available() < 64) {
        tud_cdc_write_flush();
        // Give USB stack time to transmit - wait until buffer has space
        uint32_t timeout = 0;
        while (tud_cdc_write_available() < 128 && timeout < 10000) {
            sleep_us(10);
            tud_task();  // Process USB tasks to transmit data
            timeout++;
        }
    }
}

// Helper function to recursively print table contents
static void print_table_recursive(lua_State* L, int index, int depth) {
    if (!lua_istable(L, index)) {
        // Not a table, just print the value
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, index);
        lua_call(L, 1, 1);
        const char* s = lua_tostring(L, -1);
        if (s) tud_cdc_write_str(s);  // Use CDC write instead of printf
        lua_pop(L, 1);
        flush_if_needed();  // Check buffer after writing value
        return;
    }
    
    tud_cdc_write_str("{\n\r");  // Use CDC write and proper line endings
    flush_if_needed();  // Check buffer after opening brace
    
    // Iterate through table
    lua_pushnil(L);  // first key
    while (lua_next(L, index) != 0) {
        // Print indentation
        for (int i = 0; i < depth + 1; i++) tud_cdc_write_str("  ");
        
        // Print key
        if (lua_type(L, -2) == LUA_TSTRING) {
            tud_cdc_write_str(lua_tostring(L, -2));
            tud_cdc_write_str(" = ");
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "[%.0f] = ", lua_tonumber(L, -2));
            tud_cdc_write_str(buffer);
        } else {
            tud_cdc_write_str("[?] = ");
        }
        
        // Print value
        if (lua_istable(L, -1) && depth < 3) {  // Limit recursion depth
            print_table_recursive(L, lua_gettop(L), depth + 1);
        } else {
            lua_getglobal(L, "tostring");
            lua_pushvalue(L, -2);  // the value (fix: was -2, should be -2 to get value below tostring)
            lua_call(L, 1, 1);
            const char* s = lua_tostring(L, -1);
            if (s) tud_cdc_write_str(s);
            lua_pop(L, 1);
        }
        
        tud_cdc_write_str(",\n\r");  // Use proper line endings
        flush_if_needed();  // Check buffer after each entry
        lua_pop(L, 1);  // remove value, keep key for next iteration
    }
    
    // Print closing brace with proper indentation
    for (int i = 0; i < depth; i++) tud_cdc_write_str("  ");
    tud_cdc_write_str("}");
    flush_if_needed();  // Check buffer after closing brace
}    // Lua panic handler - called when Lua encounters an unrecoverable error
    static int lua_panic_handler(lua_State* L) {
        const char* msg = lua_tostring(L, -1);
        char buffer[256];
        
        // Use TinyUSB CDC for output
        tud_cdc_write_str("\n\r");
        tud_cdc_write_str("========================================\n\r");
        tud_cdc_write_str("*** LUA PANIC - UNRECOVERABLE ERROR ***\n\r");
        tud_cdc_write_str("========================================\n\r");
        
        snprintf(buffer, sizeof(buffer), "Error: %s\n\r", msg ? msg : "unknown error");
        tud_cdc_write_str(buffer);
        
        // Print memory usage
        int kb_used = lua_gc(L, LUA_GCCOUNT, 0);
        int bytes = lua_gc(L, LUA_GCCOUNTB, 0);
        snprintf(buffer, sizeof(buffer), "Lua memory usage: %d KB + %d bytes (%.2f KB total)\n\r", 
                 kb_used, bytes, kb_used + (bytes / 1024.0f));
        tud_cdc_write_str(buffer);
        
        tud_cdc_write_str("========================================\n\r");
        tud_cdc_write_str("System halted. Please reset the device.\n\r");
        tud_cdc_write_str("========================================\n\r");
        tud_cdc_write_flush();
        
        // Flash LED rapidly to indicate panic state
        while (true) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(100);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            sleep_ms(100);
        }
        
        return 0;  // Never reached
    }
    
    // Custom allocator with memory tracking and diagnostics
    static void* lua_custom_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
        (void)ud;  // unused
        
        // Print detailed allocation info for debugging (can be disabled in production)
        static size_t total_allocated = 0;
        static size_t peak_allocated = 0;
        static size_t allocation_count = 0;
        
        if (nsize == 0) {
            // Free
            if (ptr) {
                total_allocated -= osize;
                free(ptr);
            }
            return NULL;
        } else {
            // Allocate or reallocate
            void* new_ptr = realloc(ptr, nsize);
            
            if (new_ptr == NULL) {
                // Allocation failed! Use TinyUSB CDC for output
                char buffer[256];
                
                tud_cdc_write_str("\n\r");
                tud_cdc_write_str("========================================\n\r");
                tud_cdc_write_str("*** LUA MEMORY ALLOCATION FAILED ***\n\r");
                tud_cdc_write_str("========================================\n\r");
                
                snprintf(buffer, sizeof(buffer), "Requested: %zu bytes\n\r", nsize);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Old size: %zu bytes\n\r", osize);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Total allocated: %zu bytes (%.2f KB)\n\r", 
                         total_allocated, total_allocated / 1024.0f);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Peak allocated: %zu bytes (%.2f KB)\n\r", 
                         peak_allocated, peak_allocated / 1024.0f);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Allocation #%zu\n\r", allocation_count);
                tud_cdc_write_str(buffer);
                
                tud_cdc_write_str("========================================\n\r");
                tud_cdc_write_str("Try: 1) Run collectgarbage()\n\r");
                tud_cdc_write_str("     2) Simplify your script\n\r");
                tud_cdc_write_str("     3) Remove unused libraries\n\r");
                tud_cdc_write_str("========================================\n\r");
                tud_cdc_write_flush();
                
                return NULL;  // Return NULL to let Lua handle it
            }
            
            // Update statistics
            total_allocated = total_allocated - osize + nsize;
            if (total_allocated > peak_allocated) {
                peak_allocated = total_allocated;
            }
            allocation_count++;
            
            return new_ptr;
        }
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
        
        // Create Lua state with custom allocator for memory tracking
        L = lua_newstate(lua_custom_alloc, NULL);
        if (!L) {
            printf("Error: Could not create Lua state\n\r");
            return;
        }
        
        // Set panic handler for unrecoverable errors
        lua_atpanic(L, lua_panic_handler);
        printf("Lua panic handler installed\n\r");
        
        // Load basic Lua libraries
        luaL_openlibs(L);
        
        // CRITICAL: Set aggressive garbage collection like crow does
        // Without this, Lua will run out of memory on embedded systems!
        // setpause = 55 (default 200) - run GC more frequently
        // setstepmul = 260 (default 200) - do more work per GC cycle
        lua_gc(L, LUA_GCSETPAUSE, 55);
        lua_gc(L, LUA_GCSETSTEPMUL, 260);
        printf("Lua GC configured: pause=55, stepmul=260 (aggressive for embedded)\n\r");
        
        // Override print function
        lua_register(L, "print", lua_print);
        
    // Add time function
    lua_register(L, "time", lua_time);
    
    // Add unique_card_id function for Workshop Computer compatibility
    lua_register(L, "unique_card_id", lua_unique_card_id);
    
    // Add unique_id function for crow compatibility (returns 3 integers)
    lua_register(L, "unique_id", lua_unique_id);
    
    // Add memory statistics function for debugging
    lua_register(L, "memstats", lua_memstats);
    
    // Add performance statistics function for monitoring
    lua_register(L, "perf_stats", lua_perf_stats);
    
    // Add public view functions for norns integration
    lua_register(L, "pub_view_in", lua_pub_view_in);
    lua_register(L, "pub_view_out", lua_pub_view_out);
    
    // Add tell function for ^^event messages (critical for input.stream/change)
    lua_register(L, "tell", lua_tell);
    
    // Add hardware pulse output control
    lua_register(L, "hardware_pulse", lua_hardware_pulse);
    
    // Add hardware knob and switch access functions
    lua_register(L, "get_knob_main", lua_get_knob_main);
    lua_register(L, "get_knob_x", lua_get_knob_x);
    lua_register(L, "get_knob_y", lua_get_knob_y);
    lua_register(L, "get_switch_position", lua_get_switch_position);
    
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
    
    // Register audio-rate noise functions
    lua_register(L, "LL_set_noise", lua_LL_set_noise);
    lua_register(L, "LL_clear_noise", lua_LL_clear_noise);
    
    // Register Just Intonation functions
    lua_register(L, "justvolts", lua_justvolts);
    lua_register(L, "just12", lua_just12);
    lua_register(L, "hztovolts", lua_hztovolts);
    
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
    
    // Register clock system functions for full crow compatibility
    lua_register(L, "clock_cancel", lua_clock_cancel);
    lua_register(L, "clock_schedule_sleep", lua_clock_schedule_sleep);
    lua_register(L, "clock_schedule_sync", lua_clock_schedule_sync);
    lua_register(L, "clock_schedule_beat", lua_clock_schedule_beat);
    lua_register(L, "clock_get_time_beats", lua_clock_get_time_beats);
    lua_register(L, "clock_get_tempo", lua_clock_get_tempo);
    lua_register(L, "clock_set_source", lua_clock_set_source);
    lua_register(L, "clock_internal_set_tempo", lua_clock_internal_set_tempo);
    lua_register(L, "clock_internal_start", lua_clock_internal_start);
    lua_register(L, "clock_internal_stop", lua_clock_internal_stop);
    
    // Create _c table with l_bootstrap_c_tell (handles both hardware and crow protocol)
    lua_newtable(L);
    lua_pushcfunction(L, l_bootstrap_c_tell);
    lua_setfield(L, -2, "tell");
    lua_setglobal(L, "_c");
    
    // Create crow table as a SEPARATE table, but with the same tell function
    // This prevents the aliasing bug where crow.tell = tell would overwrite _c.tell
    lua_newtable(L);
    lua_pushcfunction(L, l_bootstrap_c_tell);
    lua_setfield(L, -2, "tell");
    lua_setglobal(L, "crow");
    
    // Set up crow.reset and crow.init functions (from l_crowlib_crow_reset in l_crowlib.c)
    // We do this manually here instead of calling l_bootstrap_init which has STM32-specific code
    lua_getglobal(L, "crow");       // Get crow table
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "reset");   // Set crow.reset
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "init");    // Set crow.init (alias for reset)
    lua_pop(L, 1);                  // Pop crow table
    printf("crow.reset and crow.init functions registered\n\r");
    
    // NOTE: _c and crow tables are created above with l_bootstrap_c_tell
    // That function handles both hardware commands (output, stream, etc) 
    // AND crow protocol messages (pupdate, pub, etc)
    // DO NOT recreate _c here or we'll overwrite the proper implementation
    
    // Old code that created _c with lua_c_tell (which only handles hardware):
    // lua_newtable(L);                // Create _c table
    // lua_pushcfunction(L, lua_c_tell);  // Push the c_tell function
    // lua_setfield(L, -2, "tell");    // Set _c.tell = lua_c_tell
    // lua_setglobal(L, "_c");         // Set _c as global
    
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
        // Index translation handled directly in asl.lua (Option B); runtime patch removed.
        
        // Load crow ecosystem libraries (sequins, public, clock, quote, timeline)
        load_crow_ecosystem();
    }
    
    // Load crow ecosystem libraries that don't conflict with our custom setup
    void load_crow_ecosystem() {
        if (!L) return;
        
        printf("Loading minimal crow ecosystem (sequins, public, clock)...\n\r");
        
        // Helper lambda to load a library from embedded bytecode
        auto load_lib = [this](const char* lib_name, const char* global_name, 
                               const unsigned char* bytecode, size_t len) {
            printf("  Loading %s...\n\r", lib_name);
            if (luaL_loadbuffer(L, (const char*)bytecode, len, lib_name) != LUA_OK) {
                printf("  ERROR loading %s: %s\n\r", lib_name, lua_tostring(L, -1));
                lua_pop(L, 1);
                return;
            }
            if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
                printf("  ERROR executing %s: %s\n\r", lib_name, lua_tostring(L, -1));
                lua_pop(L, 1);
                return;
            }
            lua_setglobal(L, global_name);
            printf("  %s loaded as '%s'\n\r", lib_name, global_name);
        };
        
        // Load only essential libraries (sequins before public!)
        load_lib("sequins.lua", "sequins", sequins, sequins_len);
        load_lib("public.lua", "public", public_lua, public_len);
        load_lib("clock.lua", "clock", clock, clock_len);
        
        // Optional libraries - comment these out to save memory:
        // load_lib("calibrate.lua", "cal", calibrate, calibrate_len);
        load_lib("quote.lua", "quote", quote, quote_len);
        load_lib("timeline.lua", "timeline", timeline, timeline_len);
        load_lib("hotswap.lua", "hotswap", hotswap, hotswap_len);
        
        // Define delay() function (from l_crowlib.c)
        // This creates a closure using clock.run and clock.sleep
        if (luaL_dostring(L, "function delay(action, time, repeats)\n"
                             "local r = repeats or 0\n"
                             "return clock.run(function()\n"
                                     "for i=1,1+r do\n"
                                         "clock.sleep(time)\n"
                                         "action(i)\n"
                                     "end\n"
                                 "end)\n"
                         "end\n") != LUA_OK) {
            printf("  ERROR defining delay() function: %s\n\r", lua_tostring(L, -1));
            lua_pop(L, 1);
        } else {
            printf("  delay() function defined\n\r");
        }
        
        printf("Crow ecosystem loaded (6 libraries: sequins, public, clock, quote, timeline, hotswap)!\n\r");
        
        // Create Workshop Computer namespace table with knob and switch
        create_bb_table(L);
        
        // Print Lua memory usage for diagnostics
        int lua_mem_kb = lua_gc(L, LUA_GCCOUNT, 0);
        printf("Lua memory usage: %d KB\n\r", lua_mem_kb);
    }
    
    // Create knob table with __index metamethod for dynamic reads (returns 0.0-1.0)
    void create_knob_table(lua_State* L) {
        // Create knob table
        lua_newtable(L);  // knob table @1
        
        // Create metatable for dynamic property access
        lua_newtable(L);  // metatable @2
        
        // __index function for property access
        lua_pushcfunction(L, [](lua_State* L) -> int {
            const char* key = lua_tostring(L, 2);
            if (strcmp(key, "main") == 0) {
                return lua_get_knob_main(L);
            } else if (strcmp(key, "x") == 0) {
                return lua_get_knob_x(L);
            } else if (strcmp(key, "y") == 0) {
                return lua_get_knob_y(L);
            }
            lua_pushnil(L);
            return 1;
        });
        lua_setfield(L, -2, "__index");  // metatable.__index = func
        
        // Make table read-only
        lua_pushcfunction(L, [](lua_State* L) -> int {
            luaL_error(L, "knob table is read-only");
            return 0;
        });
        lua_setfield(L, -2, "__newindex");
        
        lua_setmetatable(L, -2);  // setmetatable(knob, metatable)
        
        // Don't set as global - will be set as bb.knob by caller
        // Leave knob table on stack for caller
    }
    // Create switch table with change callback support
    void create_switch_table(lua_State* L) {
        lua_newtable(L);  // switch table
        
        // Create metatable for property access
        lua_newtable(L);  // metatable
        
        lua_pushcfunction(L, [](lua_State* L) -> int {
            const char* key = lua_tostring(L, 2);
            if (strcmp(key, "position") == 0) {
                return lua_get_switch_position(L);
            } else if (strcmp(key, "change") == 0) {
                // Return the stored change callback
                lua_getglobal(L, "_switch_change_callback");
                return 1;
            }
            lua_pushnil(L);
            return 1;
        });
        lua_setfield(L, -2, "__index");
        
        lua_pushcfunction(L, [](lua_State* L) -> int {
            const char* key = lua_tostring(L, 2);
            if (strcmp(key, "change") == 0) {
                // Store the callback
                lua_pushvalue(L, 3);  // Copy callback function
                lua_setglobal(L, "_switch_change_callback");
                return 0;
            } else if (strcmp(key, "position") == 0) {
                luaL_error(L, "switch.position is read-only");
                return 0;
            }
            return 0;
        });
        lua_setfield(L, -2, "__newindex");
        
        lua_setmetatable(L, -2);
        
        // Don't set as global - will be set as bb.switch by caller
        
        // Initialize with nop callback
        lua_pushcfunction(L, [](lua_State* L) -> int { return 0; });
        lua_setglobal(L, "_switch_change_callback");
        
        // Leave switch table on stack for caller
    }
    
    // Create Workshop Computer namespace table
    void create_bb_table(lua_State* L) {
        // Create bb table
        lua_newtable(L);  // bb table @1
        
        // Create and add knob table
        create_knob_table(L);  // knob table @2
        lua_setfield(L, -2, "knob");  // bb.knob = knob table, pops @2
        
        // Create and add switch table
        create_switch_table(L);  // switch table @2
        lua_setfield(L, -2, "switch");  // bb.switch = switch table, pops @2
        
        // Create and add pulsein array table - simplified approach without closures
        lua_newtable(L);  // pulsein array table @2
        
        // Create pulsein[1] with simple metatable (no upvalues)
        lua_pushinteger(L, 1);
        lua_newtable(L);  // pulsein[1] table
        
        // Store the index directly in the table
        lua_pushinteger(L, 0);  // index 0 for pulsein[1]
        lua_setfield(L, -2, "_idx");
        
        // Add simple metatable
        lua_newtable(L);  // metatable
        lua_pushcfunction(L, pulsein_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, pulsein_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, pulsein_call);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);
        
        lua_settable(L, -3);  // pulsein[1] = table
        
        // Create pulsein[2]
        lua_pushinteger(L, 2);
        lua_newtable(L);  // pulsein[2] table
        
        lua_pushinteger(L, 1);  // index 1 for pulsein[2]
        lua_setfield(L, -2, "_idx");
        
        lua_newtable(L);  // metatable
        lua_pushcfunction(L, pulsein_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, pulsein_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, pulsein_call);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);
        
        lua_settable(L, -3);  // pulsein[2] = table
        
        lua_setfield(L, -2, "pulsein");  // bb.pulsein = pulsein array, pops @2
        
        // Create pulseout array [1] and [2] - for controlling pulse outputs
        lua_newtable(L);  // pulseout array @2
        
        // Create shared metatable with methods
        lua_newtable(L);  // metatable @3
        lua_pushcfunction(L, pulseout_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, pulseout_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, pulseout_call);
        lua_setfield(L, -2, "__call");
        // Store metatable in registry for reuse
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "pulseout_metatable");
        
        for (int i = 1; i <= 2; i++) {
            lua_pushinteger(L, i);
            lua_newtable(L);  // pulseout[i] table @4
            
            lua_pushinteger(L, i - 1);  // 0-indexed for C (0 = pulse1, 1 = pulse2)
            lua_setfield(L, -2, "_idx");
            
            // Set the shared metatable
            lua_pushvalue(L, -3);  // push metatable
            lua_setmetatable(L, -2);
            
            lua_settable(L, -4);  // pulseout[i] = table
        }
        
        lua_pop(L, 1);  // pop metatable
        
        lua_setfield(L, -2, "pulseout");  // bb.pulseout = array
        
        // Create audioin array [1] and [2] - for reading audio input voltages
        lua_newtable(L);  // audioin array @2
        
        // Create shared metatable
        lua_newtable(L);  // metatable @3
        lua_pushcfunction(L, audioin_index);
        lua_setfield(L, -2, "__index");
        
        for (int i = 1; i <= 2; i++) {
            lua_pushinteger(L, i);
            lua_newtable(L);  // audioin[i] table @4
            
            lua_pushinteger(L, i - 1);  // 0-indexed for C (0 = audioin1, 1 = audioin2)
            lua_setfield(L, -2, "_idx");
            
            // Set the shared metatable
            lua_pushvalue(L, -3);  // push metatable
            lua_setmetatable(L, -2);
            
            lua_settable(L, -4);  // audioin[i] = table
        }
        
        lua_pop(L, 1);  // pop metatable
        
        lua_setfield(L, -2, "audioin");  // bb.audioin = array
        
        // Set bb as global
        lua_setglobal(L, "bb");  // _G.bb = bb table
        
        // Add noise function to bb table
        const char* add_noise = R"(
            bb.noise = function(gain)
                gain = gain or 1.0
                -- Clamp gain to 0.0-1.0 range
                if gain < 0.0 then gain = 0.0 end
                if gain > 1.0 then gain = 1.0 end
                return { asl._noise(gain) }
            end
        )";
        if (luaL_dostring(L, add_noise) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error adding bb.noise: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        // Set up default pulse output actions
        const char* setup_pulseout_defaults = R"(
            -- Add clock() method to the shared metatable (crow-style API)
            local mt1 = getmetatable(bb.pulseout[1])
            local mt2 = getmetatable(bb.pulseout[2])
            
            if mt1 and mt2 and mt1 == mt2 then
                -- They share the same metatable, set clock on it
                rawset(mt1, 'clock', function(self, div)
                    if type(div) == 'string' and (div == 'none' or div == 'off') then
                        -- Turn off clocked output
                        if self.ckcoro then 
                            clock.cancel(self.ckcoro)
                            self.ckcoro = nil
                        end
                        -- Clear the action
                        rawset(self, '_action', nil)
                        return
                    end
                    
                    -- Set default pulse action
                    self.clock_div = div or 1
                    rawset(self, '_action', pulse(0.010))  -- 10ms default pulse width
                    
                    -- Cancel existing clock coroutine if any
                    if self.ckcoro then 
                        clock.cancel(self.ckcoro)
                        self.ckcoro = nil  -- Clear reference to prevent memory leak
                    end
                    
                    -- Start new clock coroutine
                    self.ckcoro = clock.run(function()
                        while true do
                            clock.sync(self.clock_div)
                            -- Execute the action if still set
                            if self._action then
                                if type(self._action) == 'table' then
                                    _c.tell('output', self._idx + 3, self._action)
                                elseif type(self._action) == 'function' then
                                    self._action()
                                end
                            end
                        end
                    end)
                end)
                
                -- Add high() and low() methods for manual control
                rawset(mt1, 'high', function(self)
                    -- Stop any clock coroutine
                    if self.ckcoro then 
                        clock.cancel(self.ckcoro)
                        self.ckcoro = nil
                    end
                    rawset(self, '_action', nil)
                    -- Set output high indefinitely via C
                    _c.tell('output', self._idx + 3, pulse(999999))
                end)
                
                rawset(mt1, 'low', function(self)
                    -- Stop any clock coroutine
                    if self.ckcoro then 
                        clock.cancel(self.ckcoro)
                        self.ckcoro = nil
                    end
                    rawset(self, '_action', nil)
                    -- Set output low via C
                    _c.tell('output', self._idx + 3, pulse(0))
                end)
            end
            
            -- Set up default: pulseout[1] generates 10ms pulses on beat
            bb.pulseout[1]:clock(1)
            
            -- Hook into clock.cleanup to stop pulseout clocks
            local original_cleanup = clock.cleanup
            clock.cleanup = function()
                -- Stop pulseout clocks by calling :clock('off') on each
                for i = 1, 2 do
                    if bb.pulseout[i].ckcoro then
                        bb.pulseout[i]:clock('off')
                    end
                end
                -- Call original cleanup
                if original_cleanup then
                    original_cleanup()
                end
            end
        )";
        
        if (luaL_dostring(L, setup_pulseout_defaults) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            if (tud_cdc_connected()) {
                tud_cdc_write_str("ERROR setting up pulseout: ");
                tud_cdc_write_str(error ? error : "unknown error");
                tud_cdc_write_str("\n\r");
                tud_cdc_write_flush();
            }
            printf("Error setting up pulseout defaults: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            if (tud_cdc_connected()) {
                tud_cdc_write_str("Pulseout setup completed\n\r");
                tud_cdc_write_flush();
            }
        }
        
        //printf("bb table created (bb.knob.main, bb.knob.x, bb.knob.y, bb.switch.position, bb.switch.change, bb.noise)\n\r");
    }
    

    
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
    
    // Audio-rate noise functions
    static int lua_LL_set_noise(lua_State* L);
    static int lua_LL_clear_noise(lua_State* L);
    
    // Just Intonation functions
    static int lua_justvolts(lua_State* L);
    static int lua_just12(lua_State* L);
    static int lua_hztovolts(lua_State* L);
    
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
    
    // Metro system Lua bindings
    static int lua_metro_start(lua_State* L);
    static int lua_metro_stop(lua_State* L);
    static int lua_metro_set_time(lua_State* L);
    static int lua_metro_set_count(lua_State* L);
    
    // Clock system Lua bindings
    static int lua_clock_cancel(lua_State* L);
    static int lua_clock_schedule_sleep(lua_State* L);
    static int lua_clock_schedule_sync(lua_State* L);
    static int lua_clock_schedule_beat(lua_State* L);
    static int lua_clock_get_time_beats(lua_State* L);
    static int lua_clock_get_tempo(lua_State* L);
    static int lua_clock_set_source(lua_State* L);
    static int lua_clock_internal_set_tempo(lua_State* L);
    static int lua_clock_internal_start(lua_State* L);
    static int lua_clock_internal_stop(lua_State* L);
    
    // Hardware knob and switch access
    static int lua_get_knob_main(lua_State* L);
    static int lua_get_knob_x(lua_State* L);
    static int lua_get_knob_y(lua_State* L);
    static int lua_get_switch_position(lua_State* L);
    
    // Evaluate Lua code and return result
    bool evaluate(const char* code) {
        if (!L) return false;
        
        int result = luaL_dostring(L, code);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            tud_cdc_write_str("lua error: ");
            tud_cdc_write_str(error ? error : "unknown error");
            tud_cdc_write_str("\n\r");
            tud_cdc_write_flush();
            lua_pop(L, 1);  // remove error from stack
            return false;
        }
        
        return true;
    }
    
    // Safe evaluation with error handling - prevents crashes from user code
    bool evaluate_safe(const char* code) {
        if (!L) return false;
        
        // ===============================================
        // OPTIMIZATION 2: Enable batching for script execution
        // ===============================================
        output_batch_begin();
        
        // Use protected call to prevent crashes
        int result = luaL_loadstring(L, code);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            tud_cdc_write_str("lua load error: ");
            tud_cdc_write_str(error ? error : "unknown error");
            tud_cdc_write_str("\n\r");
            tud_cdc_write_flush();
            lua_pop(L, 1);
            output_batch_flush(); // Flush even on error
            return false;
        }
        
        // Call with error handler
        result = lua_pcall(L, 0, 0, 0);
        
        // ===============================================
        // OPTIMIZATION 2: Flush batched outputs after execution
        // ===============================================
        output_batch_flush();
        
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            tud_cdc_write_str("lua runtime error: ");
            tud_cdc_write_str(error ? error : "unknown error");
            tud_cdc_write_str("\n\r");
            tud_cdc_write_flush();
            lua_pop(L, 1);
            return false;
        }
        
        return true;
    }
    
    
    static LuaManager* getInstance() {
        return instance;
    }
};

LuaManager* LuaManager::instance = nullptr;

// Global USB buffer to ensure proper initialization across cores
static const int USB_RX_BUFFER_SIZE = 2048;  // Increased to support multi-line scripts
static char g_rx_buffer[USB_RX_BUFFER_SIZE] = {0};
static volatile int g_rx_buffer_pos = 0;
static volatile bool g_multiline_mode = false;  // Track multi-line mode (triple backticks)

// Global flag to signal core1 to pause for flash operations (not static - accessed from flash_storage.cpp)
volatile bool g_flash_operation_pending = false;

// ========================================================================
// Output Batching System - OPTIMIZATION 2
// Queue output changes during Lua execution, flush all at once to avoid
// redundant calibration calculations and hardware writes
// ========================================================================
typedef struct {
    bool pending;       // Has this output changed?
    float target_volts; // Target voltage to set
} pending_output_t;

static struct {
    pending_output_t outputs[4];
    bool batch_mode_active;
} g_output_batch = {
    {{false, 0.0f}, {false, 0.0f}, {false, 0.0f}, {false, 0.0f}},
    false
};

// Enable batching (call before Lua execution)
// Exported for use from C files
extern "C" void output_batch_begin() {
    g_output_batch.batch_mode_active = true;
    // Don't clear pending flags - allow accumulation across calls
}

// Queue an output change (called from lua_manager)
static void output_batch_queue(int channel, float volts) {
    if (channel < 1 || channel > 4) return;
    g_output_batch.outputs[channel - 1].pending = true;
    g_output_batch.outputs[channel - 1].target_volts = volts;
}

// Execute all queued changes (call after Lua execution)
// Exported for use from C files
extern "C" void output_batch_flush() {
    if (!g_output_batch.batch_mode_active) return;
    
    for (int i = 0; i < 4; i++) {
        if (g_output_batch.outputs[i].pending) {
            // Call actual hardware function once per changed output
            extern void hardware_output_set_voltage(int channel, float voltage);
            hardware_output_set_voltage(i + 1, g_output_batch.outputs[i].target_volts);
            g_output_batch.outputs[i].pending = false;
        }
    }
    
    g_output_batch.batch_mode_active = false;
}

// Check if in batch mode
static inline bool output_is_batching() {
    return g_output_batch.batch_mode_active;
}

// Upload state machine (matches crow's REPL mode)
typedef enum {
    REPL_normal = 0,
    REPL_reception,
    REPL_discard
} repl_mode_t;

static volatile repl_mode_t g_repl_mode = REPL_normal;
static char g_new_script[16 * 1024];  // 16KB script buffer for uploads
static volatile uint32_t g_new_script_len = 0;
static char g_new_script_name[64] = "";  // Store script name from first comment line

//REMOVED PERF MONITOR
// Hardware timer-based PulseOut2 performance monitoring (outside class to avoid C++ issues)
// static volatile bool g_pulse2_state = false;
// static volatile uint32_t g_pulse2_counter = 0;
// static struct repeating_timer g_pulse2_timer;

// static bool __not_in_flash_func(pulse2_timer_callback)(struct repeating_timer *t) {
//     g_pulse2_state = !g_pulse2_state;
//     gpio_put(PULSE_2_RAW_OUT, !g_pulse2_state);
//     g_pulse2_counter++;
//     return true;
// }

// Try to extract script name from first comment line (e.g., "-- Fiirst.lua")
static void extract_script_name(const char* script, uint32_t length) {
    g_new_script_name[0] = '\0';
    if (!script || length < 5) return;
    
    // Look for "-- filename.lua" pattern in first 200 chars
    const char* end = script + (length < 200 ? length : 200);
    const char* p = script;
    
    // Skip whitespace at start
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    
    // Check if it starts with "--"
    if (p + 2 < end && p[0] == '-' && p[1] == '-') {
        p += 2;
        // Skip spaces after "--"
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        
        // Look for .lua extension
        const char* start = p;
        const char* lua_ext = nullptr;
        while (p < end && *p != '\r' && *p != '\n') {
            if (p + 4 < end && strncmp(p, ".lua", 4) == 0) {
                lua_ext = p + 4;
                break;
            }
            p++;
        }
        
        // If we found .lua, extract the filename
        if (lua_ext) {
            // Back up to start of filename (look for last space before extension)
            const char* name_start = start;
            for (const char* s = start; s < lua_ext - 4; s++) {
                if (*s == ' ' || *s == '\t' || *s == '/') {
                    name_start = s + 1;
                }
            }
            
            // Copy filename
            size_t name_len = lua_ext - name_start;
            if (name_len > 0 && name_len < sizeof(g_new_script_name) - 1) {
                memcpy(g_new_script_name, name_start, name_len);
                g_new_script_name[name_len] = '\0';
            }
        }
    }
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
    
    // Public wrappers for hardware knob and switch access (for Lua bindings)
    int32_t GetKnobValue(ComputerCard::Knob ind) { return KnobVal(ind); }
    ComputerCard::Switch GetSwitchValue() { return SwitchVal(); }
    bool GetSwitchChanged() { return SwitchChanged(); }
    
    // Public wrappers for pulse input access (for Lua bindings)
    bool GetPulseIn1() { return PulseIn1(); }
    bool GetPulseIn2() { return PulseIn2(); }
    
    // Hardware abstraction functions for output
    void hardware_set_output(int channel, float volts) {
        if (channel < 1 || channel > 4) return;
        
        // Clamp voltage to ±6V range
        if (volts > 6.0f) volts = 6.0f;
        if (volts < -6.0f) volts = -6.0f;
        
        // Convert to millivolts for calibrated output functions
        int32_t volts_mV = (int32_t)(volts * 1000.0f);
        
        // Store state for lua queries (in millivolts) - simplified
        set_output_state_simple(channel - 1, volts_mV);
        
        // Route to correct hardware output
        switch (channel) {
            case 1: // Output 3 → CVOut1 (use calibrated millivolts function)
                CVOut1Millivolts(volts_mV);
                break;
            case 2: // Output 4 → CVOut2 (use calibrated millivolts function)
                CVOut2Millivolts(volts_mV);
                break;
            case 3: // Output 1 → AudioOut1 (audio outputs use raw 12-bit values)
                {
                    int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
                    AudioOut1(dac_value);
                }
                break;
            case 4: // Output 2 → AudioOut2 (audio outputs use raw 12-bit values)
                {
                    int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
                    AudioOut2(dac_value);
                }
                break;
        }
    }
    
    // Hardware abstraction function for pulse outputs
    void hardware_set_pulse(int channel, bool state) {
        if (channel == 1) {
            PulseOut1(state);
            g_pulse_out_state[0] = state;
        } else if (channel == 2) {
            PulseOut2(state);
            g_pulse_out_state[1] = state;
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
        // if (index >= 0 && index <= 5) {
        //     //noop
        //     //LedOn(index, true);
        // }
    }
    
    void debug_led_off(int index) {
        // if (index >= 0 && index <= 5) {
        //    //noop
        //     // LedOn(index, false);
        // }
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
        
        // Initialize clock system for coroutine scheduling (8 max clock threads)
        clock_init(8);
        
        // Initialize flash storage system
        FlashStorage::init();
        
        lua_manager = new LuaManager();
       
        //REMOVED: timer for perf monitoring
        // if (!add_repeating_timer_us(-4000, pulse2_timer_callback, NULL, &g_pulse2_timer)) {
        //     printf("Failed to start PulseOut2 timer\n");
        // }
    }
    
    // Load boot script from flash (called after hardware init)
    void load_boot_script()
    {
        // Load script from flash based on what's stored
        switch(FlashStorage::which_user_script()) {
            case USERSCRIPT_Default:
                // Load First.lua from compiled bytecode
                if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") != LUA_OK 
                    || lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
                    tud_cdc_write_str(" Failed to load First.lua\n\r");
                    tud_cdc_write_flush();
                } else {
                    tud_cdc_write_str(" Loaded: First.lua (default)\n\r");
                    tud_cdc_write_flush();
                    // Call init() like real crow does (no crow.reset() before init on startup)
                    lua_manager->evaluate_safe("if init then init() end");
                }
                break;
                
            case USERSCRIPT_User:
                {
                    uint16_t script_len = FlashStorage::get_user_script_length();
                    const char* script_addr = FlashStorage::get_user_script_addr();
                    const char* script_name = FlashStorage::get_script_name();
                    
                    // Execute directly from flash (XIP)
                    if (script_addr && luaL_loadbuffer(lua_manager->L, script_addr, script_len, "=userscript") == LUA_OK
                        && lua_pcall(lua_manager->L, 0, 0, 0) == LUA_OK) {
                        char msg[128];
                        if (script_name && script_name[0]) {
                            snprintf(msg, sizeof(msg), " Loaded: %s (%u bytes)\n\r", script_name, script_len);
                        } else {
                            snprintf(msg, sizeof(msg), " Loaded: Untitled User Script (%u bytes)\n\r", script_len);
                        }
                        tud_cdc_write_str(msg);
                        tud_cdc_write_flush();
                        // Call init() like real crow does (no crow.reset() before init on startup)
                        lua_manager->evaluate_safe("if init then init() end");
                    } else {
                        tud_cdc_write_str(" Failed to load user script from flash, loading First.lua\n\r");
                        tud_cdc_write_flush();
                        // Fallback to First.lua
                        if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") == LUA_OK 
                            && lua_pcall(lua_manager->L, 0, 0, 0) == LUA_OK) {
                            tud_cdc_write_str(" Loaded First.lua fallback\n\r");
                            tud_cdc_write_flush();
                            // Call init() like real crow does (no crow.reset() before init on startup)
                            lua_manager->evaluate_safe("if init then init() end");
                        } else {
                            tud_cdc_write_str(" Failed to load First.lua fallback\n\r");
                            tud_cdc_write_flush();
                        }
                    }
                }
                break;
                
            case USERSCRIPT_Clear:
                printf("No user script loaded (cleared)\n");
                break;
        }
    }
    
    // Core0 main control loop - handles USB, events, Lua AND timer processing
    void MainControlLoop()
    {
        // Initialize USB buffer
        g_rx_buffer_pos = 0;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        // Welcome message timing - send 1.5s after startup
        bool welcome_sent = false;
        absolute_time_t welcome_time = make_timeout_time_ms(1500);
        
        // Zero out all outputs on startup
        for (int i = 1; i <= 4; i++) {
            char zero_cmd[32];
            snprintf(zero_cmd, sizeof(zero_cmd), "output[%d].volts = 0", i);
            lua_manager->evaluate_safe(zero_cmd);
        }
        
        // Timer processing state - moved from ISR!
        static uint32_t last_timer_process_us = 0;
        // Process every 50us = 20kHz (much faster than TIMER_BLOCK_SIZE=8 @ 48kHz = 166µs)
        // Calculation: 8 samples / 48000 Hz = 0.000166s = 166us
        // Check at 20kHz to handle multiple active channels without falling behind
        const uint32_t timer_interval_us = 50;
        
        // USB TX batching timer (matches crow's 2ms interval)
        static uint32_t last_usb_tx_us = 0;
        const uint32_t usb_tx_interval_us = 2000; // 2ms like crow
        
        while (1) {
            // CRITICAL: Service TinyUSB stack regularly
            tud_task();
            
            // Send welcome message 1.5s after startup
            if (!welcome_sent && absolute_time_diff_us(get_absolute_time(), welcome_time) <= 0) {
                char card_id_str[48];
                
                tud_cdc_write_str("\n\r");
                tud_cdc_write_str(" Blackbird-v0.7\n\r");
                tud_cdc_write_str(" Music Thing Modular Workshop Computer\n\r");
                tud_cdc_write_flush();
                snprintf(card_id_str, sizeof(card_id_str), " Program Card ID: 0x%08X%08X\n\r", 
                         (uint32_t)(cached_unique_id >> 32), (uint32_t)(cached_unique_id & 0xFFFFFFFF));
                
                tud_cdc_write_str(card_id_str);
                tud_cdc_write_flush();
                welcome_sent = true;
                
                // NOW load the boot script - all hardware is initialized
                load_boot_script();
            }
            
            // Handle USB input directly - no mailbox needed
            handle_usb_input();
            
            // Check for performance warnings from ProcessSample
            if (g_performance_warning) {
                g_performance_warning = false;  // Clear flag
                char perf_msg[256];
                snprintf(perf_msg, sizeof(perf_msg), 
                         "\n\r[PERF WARNING] ProcessSample exceeded budget: worst=%luus, overruns=%lu\n\r"
                         "  Use perf_stats() in Lua to query/reset worst case timing.\n\r",
                         (unsigned long)g_worst_case_us, (unsigned long)g_overrun_count);
                tud_cdc_write_str(perf_msg);
                tud_cdc_write_flush();
                // Note: Don't reset g_worst_case_us here - let user query via perf_stats()
            }
            
            // Process queued messages from audio thread
            process_queued_messages();
            
            // *** OPTIMIZATION: Process detection events on Core 0 ***
            // This does the FP conversion and fires callbacks
            // Deferred from Core 1 ISR to eliminate FP math from audio thread
            extern void Detect_process_events_core0(void);
            Detect_process_events_core0();
            
            // Update stream-equivalent values for .volts queries (always maintained)
            update_input_stream_values();
            
            // *** CRITICAL: Process timer/slopes updates at ~1.5kHz (OUTSIDE ISR!) ***
            uint32_t now_us = time_us_32();
            if (now_us - last_timer_process_us >= timer_interval_us) {
                Timer_Process();  // Safe here - not in ISR context!
                
                // Update clock system - call every 1ms for proper clock scheduling
                uint32_t time_now_ms = to_ms_since_boot(get_absolute_time());
                clock_update(time_now_ms);
                
                last_timer_process_us = now_us;
            }
            
            // *** USB TX BATCHING: Flush every 2ms (matches crow's behavior) ***
            if (now_us - last_usb_tx_us >= usb_tx_interval_us) {
                if (tud_cdc_connected()) {
                    tud_cdc_write_flush();
                }
                last_usb_tx_us = now_us;
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
            
            // Check for switch changes and fire callback
            static ComputerCard::Switch last_switch = ComputerCard::Switch::Middle;
            static bool first_switch_check = true;
            static uint32_t last_switch_check_time = 0;
            uint32_t now = to_ms_since_boot(get_absolute_time());
            
            // Check switch every ~50ms to avoid excessive polling
            if (now - last_switch_check_time >= 50) {
                last_switch_check_time = now;
                ComputerCard::Switch current_switch = GetSwitchValue();
                
                // Only fire callback after first read (avoid firing on startup)
                if (!first_switch_check && current_switch != last_switch) {
                    const char* pos_str = (current_switch == ComputerCard::Switch::Down) ? "down" :
                                         (current_switch == ComputerCard::Switch::Middle) ? "middle" : "up";
                    
                    // Call Lua switch.change callback
                    char lua_call[128];
                    snprintf(lua_call, sizeof(lua_call),
                        "if _switch_change_callback then _switch_change_callback('%s') end",
                        pos_str);
                    
                    lua_manager->evaluate_safe(lua_call);
                }
                
                last_switch = current_switch;
                first_switch_check = false;
            }
            
            // Process clock edges deferred from ISR (avoids FP math in ISR)
            for (int i = 0; i < 2; i++) {
                if (g_pulsein_clock_edge_pending[i]) {
                    g_pulsein_clock_edge_pending[i] = false;
                    clock_crow_handle_clock();  // Safe on Core 0
                }
            }
            
            // *** LED UPDATE: Process LED updates from Core 1 snapshot (~100Hz) ***
            if (g_led_update_pending) {
                g_led_update_pending = false;
                
                // Read snapshot atomically (Core 1 writes these together)
                int32_t cv1_mv = g_led_output_snapshot[0];
                int32_t cv2_mv = g_led_output_snapshot[1];
                int32_t audio1_mv = g_led_output_snapshot[2];
                int32_t audio2_mv = g_led_output_snapshot[3];
                bool pulse1 = g_led_pulse_snapshot[0];
                bool pulse2 = g_led_pulse_snapshot[1];
                
                // Convert to absolute values for brightness
                int32_t cv1_abs = (cv1_mv < 0) ? -cv1_mv : cv1_mv;
                int32_t cv2_abs = (cv2_mv < 0) ? -cv2_mv : cv2_mv;
                int32_t audio1_abs = (audio1_mv < 0) ? -audio1_mv : audio1_mv;
                int32_t audio2_abs = (audio2_mv < 0) ? -audio2_mv : audio2_mv;
                
                // Convert mV to LED brightness (0-4095)
                // Clamp to ±6V range (6000mV), normalize to 0-4095
                // Using fixed-point: (abs_mv * 682) >> 10 ≈ abs_mv * (4095/6000)
                uint16_t led0_brightness = (audio1_abs > 6000) ? 4095 : (uint16_t)((audio1_abs * 682) >> 10);
                uint16_t led1_brightness = (audio2_abs > 6000) ? 4095 : (uint16_t)((audio2_abs * 682) >> 10);
                uint16_t led2_brightness = (cv1_abs > 6000) ? 4095 : (uint16_t)((cv1_abs * 682) >> 10);
                uint16_t led3_brightness = (cv2_abs > 6000) ? 4095 : (uint16_t)((cv2_abs * 682) >> 10);
                
                // Update LEDs (now safe on Core 0, no ISR timing impact)
                LedBrightness(0, led0_brightness);  // LED 0 - Audio1 amplitude
                LedBrightness(1, led1_brightness);  // LED 1 - Audio2 amplitude
                LedBrightness(2, led2_brightness);  // LED 2 - CV1 amplitude
                LedBrightness(3, led3_brightness);  // LED 3 - CV2 amplitude
                LedOn(4, pulse1);  // LED 4 - Pulse1
                LedOn(5, pulse2);  // LED 5 - Pulse2
            }
            
            // Check for pulse input changes and fire callbacks
            // Edge detection happens at 48kHz in ProcessSample(), we just check flags here
            for (int i = 0; i < 2; i++) {
                if (g_pulsein_edge_detected[i]) {
                    // Edge was detected at audio rate - fire callback
                    char lua_call[128];
                    snprintf(lua_call, sizeof(lua_call),
                        "if _pulsein%d_change_callback then _pulsein%d_change_callback(%s) end",
                        i + 1, i + 1, g_pulsein_edge_state[i] ? "true" : "false");
                    
                    lua_manager->evaluate_safe(lua_call);
                    
                    // Clear the edge flag
                    g_pulsein_edge_detected[i] = false;
                }
            }
            
            // Update public view monitoring (~15fps)
            static uint32_t last_pubview_time = 0;
            if (now - last_pubview_time >= 66) { // ~15fps (66ms = 1000/15)
                last_pubview_time = now;
                public_update();
            }
            
            // Reduced sleep for tighter timer loop - 50us allows 20kHz loop rate
            sleep_us(50);  // Was 100us - reduced to handle multiple simultaneous LFOs
        }
    }
    
    // Accumulate script data during upload (REPL_reception mode)
    void receive_script_data(const char* buf, uint32_t len) {
        if (g_repl_mode != REPL_reception) {
            return;  // Safety check
        }
        
        if (g_new_script_len + len >= sizeof(g_new_script)) {
            tud_cdc_write_str("!ERROR! Script is too long.\n\r");
            tud_cdc_write_flush();
            g_repl_mode = REPL_discard;
            return;
        }
        
        // Accumulate script data
        memcpy(&g_new_script[g_new_script_len], buf, len);
        g_new_script_len += len;
        
        // Add newline if not present (preserves formatting)
        if (len > 0 && buf[len-1] != '\n') {
            g_new_script[g_new_script_len++] = '\n';
        }
    }
    
    // Handle USB input directly - no mailbox complexity
    void handle_usb_input() {
        // Check for available data from TinyUSB CDC
        if (!tud_cdc_available()) {
            return;
        }
        
        // Read available characters
        uint8_t buf[64];
        uint32_t count = tud_cdc_read(buf, sizeof(buf));
        
        for (uint32_t i = 0; i < count; i++) {
            int c = buf[i];
            
            // Safety check - ensure buffer position is sane
            if (g_rx_buffer_pos < 0 || g_rx_buffer_pos >= USB_RX_BUFFER_SIZE) {
                printf("ERROR: Buffer corruption detected! Resetting...\n\r");
                g_rx_buffer_pos = 0;
                g_multiline_mode = false;
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
            }
            
            // Check for escape key (clears buffer, multiline mode, and reception mode)
            if (c == 0x1B) {  // ESC key
                g_rx_buffer_pos = 0;
                g_multiline_mode = false;
                g_repl_mode = REPL_normal;
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                continue;
            }
            
            // Check for buffer overflow BEFORE adding character
            if (g_rx_buffer_pos >= USB_RX_BUFFER_SIZE - 1) {
                // Buffer full - send error message matching crow
                tud_cdc_write_str("!chunk too long!\n\r");
                tud_cdc_write_flush();
                g_rx_buffer_pos = 0;
                g_multiline_mode = false;  // Reset multiline state on overflow
                if (g_repl_mode == REPL_reception) {
                    g_repl_mode = REPL_discard;  // Mark upload as failed
                }
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                continue;
            }
            
            // Add character to buffer
            g_rx_buffer[g_rx_buffer_pos] = (char)c;
            g_rx_buffer_pos++;
            g_rx_buffer[g_rx_buffer_pos] = '\0';
            
            // Check for triple backticks (multi-line delimiter)
            if (check_for_backticks(g_rx_buffer, g_rx_buffer_pos)) {
                // Toggle multiline mode
                g_multiline_mode = !g_multiline_mode;
                
                if (g_multiline_mode) {
                    // Started multiline - strip the opening backticks
                    g_rx_buffer_pos -= 3;
                    g_rx_buffer[g_rx_buffer_pos] = '\0';
                } else {
                    // Ended multiline - strip the closing backticks and execute
                    g_rx_buffer_pos -= 3;
                    g_rx_buffer[g_rx_buffer_pos] = '\0';
                    
                    // Execute the accumulated command if not empty
                    if (g_rx_buffer_pos > 0) {
                        handle_usb_command(g_rx_buffer);
                    }
                    
                    // Clear buffer
                    g_rx_buffer_pos = 0;
                    memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                }
                continue;
            }
            
            // In single-line mode, execute on newline
            if (!g_multiline_mode && is_packet_complete(g_rx_buffer, g_rx_buffer_pos)) {
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
            if (clean_length > 0) {
                // Check for system commands FIRST (even in reception mode)
                C_cmd_t cmd = parse_command(g_rx_buffer, clean_length);
                if (cmd != C_none) {
                    // System command - process it
                    handle_command_with_response(cmd);
                } else if (g_repl_mode == REPL_reception) {
                    // In reception mode and not a command - accumulate as script data
                    receive_script_data(g_rx_buffer, clean_length);
                } else {
                    // Normal REPL mode - execute Lua
                    handle_usb_command(g_rx_buffer);
                }
            }                // Clear buffer after processing
                g_rx_buffer_pos = 0;
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
            }
            // In multiline mode, just keep accumulating characters
        }
        
        // After processing all characters in this USB packet, check if we have a command
        // without a newline (like ^^e, ^^s, etc.) - this handles the case where druid
        // sends commands as 3-byte packets without line endings
        if (g_rx_buffer_pos >= 3 && g_rx_buffer_pos <= 10) {  // Commands are short
            C_cmd_t cmd = parse_command(g_rx_buffer, g_rx_buffer_pos);
            if (cmd != C_none) {
                // Found a command without newline - handle it
                handle_command_with_response(cmd);
                // Clear buffer
                g_rx_buffer_pos = 0;
                memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
            }
        }
    }
    
    // Handle USB commands received from Core1 via mailbox
    void handle_usb_command(const char* command)
    {
        // Parse and handle command
        C_cmd_t cmd = parse_command(command, strlen(command));
        
        // System commands are ALWAYS processed (even in reception mode)
        if (cmd != C_none) {
            handle_command_with_response(cmd);
            return;
        }
        
        // For non-command data:
        if (g_repl_mode == REPL_reception) {
            // We're in upload mode - this shouldn't normally happen
            // (script data should come through receive_script_data)
            // But handle it anyway for robustness
            receive_script_data(command, strlen(command));
        } else {
            // Normal REPL mode - execute Lua immediately
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
                tud_cdc_write_str("^^version('blackbird-0.7')\n\r");
                tud_cdc_write_flush();
                break; }
                
            case C_identity: {
                uint64_t unique_id = cached_unique_id;
                char response[80];
                snprintf(response, sizeof(response), "^^identity('0x%08X%08X')\n\r", 
                         (uint32_t)(unique_id >> 32), (uint32_t)(unique_id & 0xFFFFFFFF));
                tud_cdc_write_str(response);
                tud_cdc_write_flush();
                break;
            }
            
            case C_print: {
                // Check if there's a user script in flash and print its name
                USERSCRIPT_t script_type = FlashStorage::which_user_script();
                if (script_type == USERSCRIPT_User) {
                    const char* script_name = FlashStorage::get_script_name();
                    if (script_name && script_name[0] != '\0') {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Running: %s\n\r", script_name);
                        tud_cdc_write_str(msg);
                    } else {
                        tud_cdc_write_str("Running: user script (unnamed)\n\r");
                    }
                } else if (script_type == USERSCRIPT_Default) {
                    tud_cdc_write_str("Running: First.lua (default)\n\r");
                } else {
                    tud_cdc_write_str("No user script.\n\r");
                }
                tud_cdc_write_flush();
                sleep_ms(50);  // Give USB time to transmit before next command
                break;
            }
                
            case C_restart:
                tud_cdc_write_str("Press the RESET button to reset Workshop Computer.\n\r");
                tud_cdc_write_flush();
                // Could implement actual restart here
                break;
                
            case C_killlua:
                tud_cdc_write_str("killing lua...\n\r");
                tud_cdc_write_flush();
                if (lua_manager) {
                    // Soft reset: clear Lua state but don't reboot hardware
                    // This matches real Crow's Lua_Reset() behavior
                    
                    // 1. Stop all metros
                    Metro_stop_all();
                    
                    // 2. Clear input detectors
                    for (int i = 0; i < 2; i++) {
                        Detect_none(Detect_ix_to_p(i));
                    }

                    // 3a. Stop all noise modes
                    for (int i = 0; i < 4; i++) {
                        g_noise_active[i] = false;
                        g_noise_gain[i] = 0;
                        g_noise_lock_counter[i] = 0;
                    }
                    g_noise_active_mask = 0;  // Clear all bits in mask
                    
                    // 3b. Stop all output slopes
                    for (int i = 0; i < 4; i++) {
                        S_toward(i, 0.0, 0.0, SHAPE_Linear, NULL);
                    }
                    
                    // 4. Clear event queue
                    events_clear();
                    
                    // 5. Cancel all clock coroutines
                    clock_cancel_coro_all();
                    
                    // 6. Reset crow modules to defaults (calls crow.reset() in Lua)
                    lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
                    
                    // 7. Clear user globals using Crow's _user table approach
                    lua_manager->evaluate_safe(
                        "if _user then "
                        "  for k,_ in pairs(_user) do "
                        "    _G[k] = nil "
                        "  end "
                        "end "
                        "_G._user = {}"
                    );
                    
                    // 8. Reset init() to empty function
                    lua_manager->evaluate_safe("_G.init = function() end");
                    
                    // 9. Garbage collect twice for thorough cleanup
                    lua_gc(lua_manager->L, LUA_GCCOLLECT, 1);
                    lua_gc(lua_manager->L, LUA_GCCOLLECT, 1);
                    
                    //tud_cdc_write_str("lua environment reset\n\r");
                }
                tud_cdc_write_flush();
                break;
                
            case C_boot:
                tud_cdc_write_str("Workshop Computer does not support bootloader command sorry.\n\r");
                tud_cdc_write_flush();
                break;
                
            case C_startupload:
                // Clear and prepare for script reception
                g_new_script_len = 0;
                memset(g_new_script, 0, sizeof(g_new_script));
                g_new_script_name[0] = '\0';  // Clear script name
                g_repl_mode = REPL_reception;
                tud_cdc_write_str("script upload started\n\r");
                tud_cdc_write_flush();
                break;
                
            case C_endupload:
                if (g_repl_mode == REPL_discard) {
                    tud_cdc_write_str("upload failed, returning to normal mode\n\r");
                } else if (g_new_script_len > 0 && lua_manager) {
                    // Perform aggressive reset before loading new script (matches ^^k behavior)
                    // This ensures a clean slate with no lingering state from previous script
                    
                    // 1. Stop all metros
                    Metro_stop_all();
                    
                    // 2. Clear input detectors
                    for (int i = 0; i < 2; i++) {
                        Detect_none(Detect_ix_to_p(i));
                    }
                    
                    // 3. Stop all output slopes
                    for (int i = 0; i < 4; i++) {
                        S_toward(i, 0.0, 0.0, SHAPE_Linear, NULL);
                    }
                    
                    // 3b. Stop all noise modes
                    for (int i = 0; i < 4; i++) {
                        g_noise_active[i] = false;
                        g_noise_gain[i] = 0;
                        g_noise_lock_counter[i] = 0;
                    }
                    g_noise_active_mask = 0;  // Clear all bits in mask
                    
                    // 4. Clear event queue
                    events_clear();
                    
                    // 5. Cancel all clock coroutines
                    clock_cancel_coro_all();
                    
                    // Run script in RAM (temporary) - matches crow's REPL_upload(0)
                    if (lua_manager->evaluate_safe(g_new_script)) {
                        
                        // Clear user globals using Crow's _user table approach
                        lua_manager->evaluate_safe(
                            "if _user then "
                            "  for k,_ in pairs(_user) do "
                            "    _G[k] = nil "
                            "  end "
                            "end "
                            "_G._user = {}"
                        );
                        
                        // Garbage collect for cleanup
                        lua_gc(lua_manager->L, LUA_GCCOLLECT, 1);
                        
                        // Script loaded successfully - now call init() to start it (like Lua_crowbegin)
                        // Real crow does NOT call crow.reset() before init() on script upload
                        lua_manager->evaluate_safe("if init then init() end");
                        tud_cdc_write_str("^^ready()\n\r"); // Inform host that script is running
                        // Silent success - script is now running
                    } else {
                        tud_cdc_write_str("\\script evaluation failed\n\r");
                    }
                } else {
                    tud_cdc_write_str("\\no script data received\n\r");
                }
                g_repl_mode = REPL_normal;
                tud_cdc_write_flush();
                break;
                
            case C_flashupload: {
                if (g_repl_mode == REPL_discard) {
                    tud_cdc_write_str("upload failed, discard mode\n\r");
                    tud_cdc_write_flush();
                } else if (g_new_script_len > 0 && lua_manager) {
                    // Try to extract script name from first comment line
                    extract_script_name(g_new_script, g_new_script_len);
                    
                    // Run script AND save to flash - matches crow's REPL_upload(1)
                    // Tell user to prepare for manual reset BEFORE we write to flash
                    tud_cdc_write_flush();
                    tud_cdc_write_str("\n\r");
                    tud_cdc_write_str("========================================\n\r");
                    tud_cdc_write_flush();
                    if (g_new_script_name[0]) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Writing %s to flash...\n\r", g_new_script_name);
                        tud_cdc_write_str(msg);
                    } else {
                        tud_cdc_write_str("Writing script to flash...\n\r");
                    }
                    tud_cdc_write_flush();
                    
                    // Write to flash (this will reset core1, do the flash write, then restart core1)
                    if (FlashStorage::write_user_script_with_name(g_new_script, g_new_script_len, g_new_script_name)) {
                        // Give USB time to stabilize after core1 restart
                        
                        tud_cdc_write_flush();
                        tud_cdc_write_str("User script saved to flash!\n\r");
                        tud_cdc_write_str("\n\r");
                        tud_cdc_write_str("Press the RESET button (next to card slot)\n\r");
                        tud_cdc_write_str("on your Workshop Computer to load your script.\n\r");
                        tud_cdc_write_str("========================================\n\r");
                        tud_cdc_write_str("\n\r");
                        tud_cdc_write_flush();
                        
                        // Light up all LEDs to indicate upload complete
                        this->LedOn(0);
                        this->LedOn(1);
                        this->LedOn(2);
                        this->LedOn(3);
                        this->LedOn(4);
                        this->LedOn(5);
                    } else {
                        tud_cdc_write_str("flash write failed\n\r");
                        tud_cdc_write_flush();
                    }
                } else {
                    char debug_buf[128];
                    snprintf(debug_buf, sizeof(debug_buf), "no script data (len=%lu, lua_manager=%p)\n\r", 
                             (unsigned long)g_new_script_len, (void*)lua_manager);
                    tud_cdc_write_str(debug_buf);
                }
                g_repl_mode = REPL_normal;
                tud_cdc_write_flush();
                break;
            }
                
            case C_flashclear:
                // Output status BEFORE flash operation (which disables interrupts)
                tud_cdc_write_flush();
                tud_cdc_write_str("\n\r");
                tud_cdc_write_str("========================================\n\r");
                tud_cdc_write_str("Clearing user script...\n\r");
                
                // Write to flash (this will disable interrupts briefly)
                if (FlashStorage::set_default_script_mode()) {
                    
                    tud_cdc_write_str("User script cleared!\n\r");
                    tud_cdc_write_flush();
                    
                    // Reset Lua environment immediately (matches real crow behavior)
                    // This will call crow.reset() which calls public.clear()
                    // which sends ^^pub("_clear")
                    if (lua_manager) {
                        // Call crow.reset() to clear state and trigger public.clear()
                        lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
                        
                        // Call init() if it exists (but there's no user script now)
                        lua_manager->evaluate_safe("if init then init() end");
                    }
                    
                    tud_cdc_write_str("========================================\n\r");
                    tud_cdc_write_str("\n\r");
                    tud_cdc_write_flush();
                    
                    // Light up all LEDs to indicate operation complete
                    this->LedOn(0);
                    this->LedOn(1);
                    this->LedOn(2);
                    this->LedOn(3);
                    this->LedOn(4);
                    this->LedOn(5);

                } else {
                    tud_cdc_write_str("flash write failed\n\r");
                    tud_cdc_write_flush();
                }
                break;
                
            case C_loadFirst:
                printf("loading First.lua\n\r");
                // Load First.lua immediately without touching flash
                if (lua_manager) {
                    if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") != LUA_OK || lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
                        const char* error = lua_tostring(lua_manager->L, -1);
                        lua_pop(lua_manager->L, 1);
                        printf("error loading First.lua\n\r");
                    } else {

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

                        printf("first.lua loaded\n\r");
                    }
                } else {
                    printf("error: lua manager not available\n\r");
                }
                break;
                
            default:
                // For unimplemented commands, send a simple acknowledgment
                printf("ok\n\r");
                break;
        }
        fflush(stdout);
    }
    
    ~BlackbirdCrow() {
        if (lua_manager) {
            delete lua_manager;
        }
    }

    // Parse command from buffer
    C_cmd_t parse_command(const char* buffer, int length)
    {
        for (int i = 0; i < length - 2; i++) {
            if (buffer[i] == '^' && buffer[i + 1] == '^') {
                char cmd_char = buffer[i + 2];
                switch (cmd_char) {
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
    

    
    // ULTRA-LIGHTWEIGHT audio callback - ONLY READ INPUTS!
    // NO output processing in ISR - prevents multiplexer misalignment
    virtual void ProcessSample() override
    {
        // Performance monitoring: capture start time (low-cost)
        uint32_t start_time = time_us_32();
        
        // Increment sample counter for timer system (lightweight)
        extern volatile uint64_t global_sample_counter;
        global_sample_counter++;
        
        // Keep clock system synchronized (lightweight)
        clock_increment_sample_counter();
        
        // Read CV inputs directly - clean and simple
        int16_t cv1 = CVIn1();
        int16_t cv2 = CVIn2();
        
        // Update input state for .volts queries
        set_input_state_simple(0, cv1);
        set_input_state_simple(1, cv2);
        
        // Read audio inputs and store raw values (NO floating point in ISR)
        set_audioin_raw(0, AudioIn1());
        set_audioin_raw(1, AudioIn2());
        
        // Process detection sample-by-sample for edge accuracy
        Detect_process_sample(0, cv1);
        Detect_process_sample(1, cv2);
        
        // Pulse input edge detection (48kHz) - catches even very short pulses
        // Check for edges when change or clock mode is enabled, respecting direction filter
        // mode: 0=none, 1=change, 2=clock
        if (g_pulsein_mode[0] == 1) {
            bool rising = PulseIn1RisingEdge();
            bool falling = PulseIn1FallingEdge();
            // direction: 0=both, 1=rising only, -1=falling only
            if ((rising && g_pulsein_direction[0] != -1) || (falling && g_pulsein_direction[0] != 1)) {
                g_pulsein_edge_detected[0] = true;
                g_pulsein_edge_state[0] = PulseIn1();
            }
        } else if (g_pulsein_mode[0] == 2) {
            // Clock mode - only rising edges
            if (PulseIn1RisingEdge()) {
                g_pulsein_clock_edge_pending[0] = true;  // Defer to Core 0 (avoids FP math in ISR)
            }
        }
        if (g_pulsein_mode[1] == 1) {
            bool rising = PulseIn2RisingEdge();
            bool falling = PulseIn2FallingEdge();
            // direction: 0=both, 1=rising only, -1=falling only
            if ((rising && g_pulsein_direction[1] != -1) || (falling && g_pulsein_direction[1] != 1)) {
                g_pulsein_edge_detected[1] = true;
                g_pulsein_edge_state[1] = PulseIn2();
            }
        } else if (g_pulsein_mode[1] == 2) {
            // Clock mode - only rising edges
            if (PulseIn2RisingEdge()) {
                g_pulsein_clock_edge_pending[1] = true;  // Defer to Core 0 (avoids FP math in ISR)
            }
        }
        
        // Update current pulse state cache (for .state queries)
        g_pulsein_state[0] = PulseIn1();
        g_pulsein_state[1] = PulseIn2();
        
        // === AUDIO-RATE NOISE GENERATION (48kHz) - INTEGER MATH ONLY ===
        // Generate and output noise for any active channels
        // OPTIMIZATION: Fast check if ANY noise is active before iterating channels
        if (g_noise_active_mask) {
            for (int ch = 0; ch < 4; ch++) {
                if (g_noise_active_mask & (1 << ch)) {
                    // Generate noise in millivolts (-6000 to +6000) using integer math only
                    int32_t noise_mv = generate_audio_noise_mv(g_noise_gain[ch]);
                
                // Update state for queries
                g_output_state_mv[ch] = noise_mv;
                
                // Output directly to hardware (all integer math)
                switch (ch) {
                    case 0: CVOut1Millivolts(noise_mv); break;
                    case 1: CVOut2Millivolts(noise_mv); break;
                    case 2: {
                        // Convert mV to 12-bit DAC value: (noise_mv * 2048) / 6000
                        // Optimize: (noise_mv * 2048) / 6000 ≈ (noise_mv * 341) >> 10
                        // Exact: multiply by 2048/6000 = 0.34133... ≈ 349/1024
                        int16_t dac_value = (int16_t)((noise_mv * 349) >> 10);
                        AudioOut1(dac_value);
                        break;
                    }
                    case 3: {
                        // Same calculation for audio output 2
                        int16_t dac_value = (int16_t)((noise_mv * 349) >> 10);
                        AudioOut2(dac_value);
                        break;
                    }
                }
            }
        } 
     } // end if (g_noise_active_mask)
        
        // === PULSE OUTPUT 2: Controlled by Lua (default: follows switch) ===
        // Default behavior is set up in Lua on startup
        // Users can change by calling bb.pulseout[2]:clock() or setting bb.pulseout[2].action
        
        // === LED OUTPUT VISUALIZATION ===
        // Snapshot values for Core 0 LED update (every 800 samples = 60Hz at 48kHz)
        // 60Hz is the human eye's temporal resolution limit, no benefit to higher rates
        static int led_update_counter = 0;
        if (++led_update_counter >= 800) {
            led_update_counter = 0;
            
            // Take atomic snapshot of output states for Core 0 to process
            g_led_output_snapshot[0] = g_output_state_mv[0];
            g_led_output_snapshot[1] = g_output_state_mv[1];
            g_led_output_snapshot[2] = g_output_state_mv[2];
            g_led_output_snapshot[3] = g_output_state_mv[3];
            g_led_pulse_snapshot[0] = g_pulse_out_state[0];
            g_led_pulse_snapshot[1] = g_pulse_out_state[1];
            
            // Signal Core 0 to update LEDs
            g_led_update_pending = true;
        }
        
        // === PERFORMANCE MONITORING (low-cost) ===
        // Check if ProcessSample exceeded time budget
        // At 48kHz, each sample has ~20.8us budget. Warn at 18us (87% utilization)
        uint32_t elapsed = time_us_32() - start_time;
        
        // Always track worst-case execution time (available via perf_stats())
        if (elapsed > g_worst_case_us) {
            g_worst_case_us = elapsed;
        }
        
        // Warn if exceeding budget threshold
        if (elapsed > 18) {  // Threshold: 18 microseconds
            g_overrun_count++;
            
            static uint32_t last_report_time = 0;
            uint32_t now = time_us_32();
            
            // Report at most once per second to avoid flooding
            if ((now - last_report_time) > 1000000) {
                g_performance_warning = true;
                last_report_time = now;
            }
        }
    }
};

// CASL bridge functions
int LuaManager::lua_casl_describe(lua_State* L) {
    int raw = luaL_checkinteger(L, 1);
    int internal = raw - 1;
    //printf("[DBG] lua_casl_describe raw=%d internal=%d\n\r", raw, internal);
    
    // DON'T clear noise here - it needs to be cleared when the action actually RUNS
    // The noise() action will set it, and other actions should clear it when they start
    
    casl_describe(internal, L); // C is zero-based
    lua_pop(L, 2);
    return 0;
}

int LuaManager::lua_casl_action(lua_State* L) {
    int raw = luaL_checkinteger(L, 1);
    int act = luaL_checkinteger(L, 2);
    int internal = raw - 1;
   // printf("[DBG] lua_casl_action raw=%d internal=%d action=%d\n\r", raw, internal, act);
    
    // When an action is explicitly triggered (act == 1), clear any existing noise
    // UNLESS noise was just set in the describe phase (lock counter == 10).
    // This handles output[n].volts = x, output[n](lfo()), etc. clearing old noise,
    // while allowing output[n](bb.noise()) to work correctly.
    if (internal >= 0 && internal < 4 && act == 1 && g_noise_active[internal]) {
        // If lock counter is 10, noise was just set in describe phase, don't clear
        if (g_noise_lock_counter[internal] != 10) {
            g_noise_active[internal] = false;
            g_noise_active_mask &= ~(1 << internal);  // Clear bit in mask
            g_noise_gain[internal] = 0;
            g_noise_lock_counter[internal] = 0;
        }
    }
    
    casl_action(internal, act); // C is zero-based
    
    // After action completes, if noise is active and lock counter is still 10,
    // reduce it to 2 so that the next user action will clear the noise.
    // This allows noise to persist through immediate automatic calls but be
    // cleared by the next explicit user action.
    if (internal >= 0 && internal < 4 && g_noise_active[internal] && g_noise_lock_counter[internal] == 10) {
        g_noise_lock_counter[internal] = 2;
    }
    
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

// LL_set_noise(channel, gain) - Enable audio-rate noise output
int LuaManager::lua_LL_set_noise(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float gain = luaL_checknumber(L, 2);    // Gain multiplier (0.0-1.0)
    
    // Validate channel
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel);
    }
    
    // Clamp gain to valid range
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    
    int ch_idx = channel - 1;
    
    // Stop any ongoing slopes/actions on this channel before enabling noise
    // This prevents LFOs or other actions from interfering with noise
    S_toward(ch_idx, 0.0, 0.0, SHAPE_Linear, NULL);
    
    g_noise_active[ch_idx] = true;
    g_noise_active_mask |= (1 << ch_idx);  // Set bit in mask for fast checking
    // Convert gain to millivolts (0-6000 mV) as integer
    g_noise_gain[ch_idx] = (int32_t)(gain * 6000.0f);
    // Set lock counter to prevent immediate clearing (ignore next few hardware_output_set_voltage calls)
    g_noise_lock_counter[ch_idx] = 10;  // Ignore next 10 calls
    
    return 0;
}

// LL_clear_noise(channel) - Disable noise output
int LuaManager::lua_LL_clear_noise(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    
    // Validate channel
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel);
    }
    
    int ch_idx = channel - 1;
    g_noise_active[ch_idx] = false;
    g_noise_active_mask &= ~(1 << ch_idx);  // Clear bit in mask
    g_noise_gain[ch_idx] = 0;  // Clear integer gain
    g_noise_lock_counter[ch_idx] = 0;  // Clear lock
    
    return 0;
}

// set_output_scale(channel, scale_table, [modulo], [scaling]) - Set output quantization
int LuaManager::lua_set_output_scale(lua_State* L) {
    // Default values - shared between calls (matches crow behavior)
    static float mod = 12.0;     // default to 12TET
    static float scaling = 1.0;  // default to 1V/octave
    
    int nargs = lua_gettop(L);
    int channel = luaL_checkinteger(L, 1) - 1;  // Convert to 0-based
    
    // Validate channel
    if (channel < 0 || channel >= 4) {
        lua_pop(L, nargs);
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel + 1);
    }
    
    // Case 1: No scale argument OR empty table -> chromatic quantization (semitones)
    if (nargs == 1 || (lua_istable(L, 2) && lua_rawlen(L, 2) == 0)) {
        float divs[12] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};
        AShaper_set_scale(channel, divs, 12, 12.0, 1.0);
        lua_pop(L, nargs);
        return 0;
    }
    
    // Case 2: String argument 'none' -> disable quantization
    if (lua_isstring(L, 2)) {
        const char* str = lua_tostring(L, 2);
        if (strcmp(str, "none") == 0) {
            AShaper_unset_scale(channel);
            lua_pop(L, nargs);
            return 0;
        }
    }
    
    // Case 3: Table of scale degrees (semitones or ratios)
    if (!lua_istable(L, 2)) {
        lua_pop(L, nargs);
        return luaL_error(L, "Second argument must be table or 'none'");
    }
    
    int tlen = lua_rawlen(L, 2);
    if (tlen > MAX_DIV_LIST_LEN) {
        lua_pop(L, nargs);
        return luaL_error(L, "Scale table length must be 1-%d", MAX_DIV_LIST_LEN);
    }
    
    float divs[MAX_DIV_LIST_LEN];
    for (int i = 0; i < tlen; i++) {
        lua_pushnumber(L, i + 1);  // Lua is 1-based
        lua_gettable(L, 2);
        divs[i] = luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    
    // Optional modulo parameter (arg 3)
    if (nargs >= 3) {
        mod = luaL_checknumber(L, 3);
    }
    
    // Optional scaling parameter (arg 4)
    if (nargs >= 4) {
        scaling = luaL_checknumber(L, 4);
    }
    
    AShaper_set_scale(channel, divs, tlen, mod, scaling);
    
    lua_pop(L, nargs);
    return 0;
}

// Helper function for justvolts/just12 implementation
static int lua_justvolts_impl(lua_State* L, float mul) {
    // Apply optional offset
    float offset = 0.0f;
    int nargs = lua_gettop(L);
    
    switch(nargs) {
        case 1: 
            break;
        case 2: 
            offset = log2f(luaL_checknumber(L, 2)) * mul;
            break;
        default:
            return luaL_error(L, "justvolts: need 1 or 2 args");
    }
    
    // Handle single number or table
    int lua_type_1 = lua_type(L, 1);
    
    if (lua_type_1 == LUA_TNUMBER) {
        // Single ratio
        float result = log2f(lua_tonumber(L, 1)) * mul + offset;
        lua_settop(L, 0);
        lua_pushnumber(L, result);
        return 1;
    }
    else if (lua_type_1 == LUA_TTABLE) {
        // Table of ratios
        int telems = lua_rawlen(L, 1);
        
        // Create result table
        lua_createtable(L, telems, 0);
        
        for(int i = 1; i <= telems; i++) {
            lua_rawgeti(L, 1, i);
            float ratio = luaL_checknumber(L, -1);
            float result = log2f(ratio) * mul + offset;
            lua_pop(L, 1);
            
            lua_pushnumber(L, result);
            lua_rawseti(L, 2, i); // Store in result table
        }
        
        // Remove original table, leave result on stack
        lua_remove(L, 1);
        return 1;
    }
    else {
        return luaL_error(L, "justvolts: argument must be number or table");
    }
}

// justvolts(ratio_or_table, [offset]) - Convert JI ratios to 1V/oct voltages
// Examples:
//   justvolts(2) -> 1.0 (one octave)
//   justvolts({1/1, 2/1, 4/1}) -> {0, 1, 2}
int LuaManager::lua_justvolts(lua_State* L) {
    return lua_justvolts_impl(L, 1.0f);
}

// just12(ratio_or_table, [offset]) - Convert JI ratios to 12TET semitone voltages
// Examples:
//   just12(3/2) -> 7.02 (perfect fifth in semitones)
//   just12({1/1, 5/4, 3/2}) -> {0, 3.86, 7.02}
//   just12({1/1, 5/4, 3/2}, 2) -> {12, 15.86, 19.02} (offset by 1 octave)
int LuaManager::lua_just12(lua_State* L) {
    return lua_justvolts_impl(L, 12.0f);
}

// hztovolts(freq, [reference]) - Convert frequency to voltage
// Examples:
//   hztovolts(440) -> 0 (A4 at 0V by default)
//   hztovolts(880) -> 1.0 (A5, one octave up)
//   hztovolts(440, 261.626) -> 0.585 (A4 referenced to C4)
int LuaManager::lua_hztovolts(lua_State* L) {
    const float MIDDLE_C_INV = 1.0f / 261.626f; // 1/C4 frequency
    
    float retval = 0.0f;
    int nargs = lua_gettop(L);
    
    switch(nargs) {
        case 1: // use default middle C reference
            retval = log2f(luaL_checknumber(L, 1) * MIDDLE_C_INV);
            break;
        case 2: // use provided reference
            retval = log2f(luaL_checknumber(L, 1) / luaL_checknumber(L, 2));
            break;
        default:
            return luaL_error(L, "hztovolts: need 1 or 2 args");
    }
    
    lua_settop(L, 0);
    lua_pushnumber(L, retval);
    return 1;
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
            // Check if this is a pulse output (channels 3 & 4 are pulse outputs in logical mapping)
            if (channel == 3 || channel == 4) {
                // Pulse outputs - special handling for 1-bit digital outputs
                int pulse_idx = channel - 3;  // 0-based pulse output index (0 = pulse1, 1 = pulse2)
                
                // Check if arg 3 is a table (ASL action like pulse())
                if (lua_istable(L, 3)) {
                    // For pulse outputs, we need to extract timing from ASL action
                    // pulse(time) creates: {asl._if(polarity, {to(level,0,'now'), to(level,time), to(0,0,'now')}), ...}
                    // We need to extract the 'time' parameter and create a clock-based pulse
                    
                    // Extract pulse width from the ASL table structure
                    float pulse_time = 0.010;  // default 10ms
                    
                    // Navigate the pulse() ASL structure
                    // asl._if(pred, t) inserts {'IF', pred} at start of t and returns it
                    // So pulse(2) structure is: [1] = { {'IF',1}, to(5,0,'now'), to(5,2), to(0,0,'now') }
                    // [1][1] = {'IF', 1}
                    // [1][2] = to(5, 0, 'now')  - first jump
                    // [1][3] = to(5, 2)         - hold for TIME (this is what we want!)
                    // [1][4] = to(0, 0, 'now')  - return to zero
                    
                    lua_pushvalue(L, 3);  // push the action table
                    if (lua_istable(L, -1)) {
                        lua_geti(L, -1, 1);  // get first element (the asl._if branch for polarity=1)
                        if (lua_istable(L, -1)) {
                            lua_geti(L, -1, 2);  // get [1][2]
                            if (lua_istable(L, -1)) {
                                lua_pop(L, 1);  // pop [1][2], we don't need it
                                
                                // Get [1][3] which is the second 'to' with the pulse duration
                                lua_geti(L, -1, 3);
                                if (lua_istable(L, -1)) {
                                    // [1][3] = {'to', level, time, ...}
                                    // time is at index 3
                                    lua_geti(L, -1, 3);
                                    if (lua_isnumber(L, -1)) {
                                        pulse_time = lua_tonumber(L, -1);
                                    }
                                    lua_pop(L, 1);  // pop [1][3][3]
                                    lua_pop(L, 1);  // pop [1][3]
                                } else {
                                    lua_pop(L, 1);  // pop non-table
                                }
                            } else {
                                lua_pop(L, 1);  // pop [1][2]
                            }
                            lua_pop(L, 1);  // pop [1]
                        } else {
                            lua_pop(L, 1);  // pop [1]
                        }
                    }
                    lua_pop(L, 1);  // pop action table copy
                    
                    // Now generate the pulse using clock.run
                    // Set output high immediately
                    g_pulse_out_state[pulse_idx] = true;
                    hardware_pulse_output_set(pulse_idx + 1, true);
                    
                    // Schedule the pulse to go low after pulse_time
                    // Create a Lua coroutine to handle timing
                    lua_getglobal(L, "clock");
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, -1, "run");
                        if (lua_isfunction(L, -1)) {
                            // Create the function to run: clock.run(function() clock.sleep(time); hardware_pulse(idx, false) end)
                            const char* pulse_code_fmt = 
                                "return function() "
                                "  clock.sleep(%.6f) "
                                "  hardware_pulse(%d, false) "
                                "end";
                            char pulse_code[256];
                            snprintf(pulse_code, sizeof(pulse_code), pulse_code_fmt, pulse_time, pulse_idx + 1);
                            
                            if (luaL_dostring(L, pulse_code) == LUA_OK) {
                                // Stack: clock, run_func, generated_func
                                if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                                    lua_pop(L, 1);  // pop error (silent fail)
                                }
                            } else {
                                lua_pop(L, 1);  // pop error (silent fail)
                            }
                        } else {
                            lua_pop(L, 1);  // pop non-function
                        }
                        lua_pop(L, 1);  // pop clock table
                    } else {
                        lua_pop(L, 1);  // pop non-table
                    }
                } else {
                    // Not a table - could be a direct voltage value
                    float value = (float)luaL_checknumber(L, 3);
                    // Set pulse output based on voltage threshold
                    bool state = (value > 2.5f);  // Consider >2.5V as "high"
                    g_pulse_out_state[pulse_idx] = state;
                    hardware_pulse_output_set(pulse_idx + 1, state);
                }
            } else {
                // Regular CV outputs (channels 1-4)
                float value = (float)luaL_checknumber(L, 3);
                
                // User explicitly setting output.volts should always disable noise
                int ch_idx = channel - 1;
                if (ch_idx >= 0 && ch_idx < 4 && g_noise_active[ch_idx]) {
                    g_noise_active[ch_idx] = false;
                    g_noise_active_mask &= ~(1 << ch_idx);  // Clear bit in mask
                    g_noise_gain[ch_idx] = 0;
                    g_noise_lock_counter[ch_idx] = 0;
                }
                
                hardware_output_set_voltage(channel, value);
            }
    } else {
        // Only 'output' is handled here - all other _c.tell messages (stream, change, window, etc)
        // are handled by l_bootstrap_c_tell and sent as ^^ protocol messages
        printf("_c.tell: unexpected module '%s' (ch=%d) - should be handled by l_bootstrap_c_tell\n\r", module, channel);
    }
    
    return 0;
}

// C-callable wrapper for lua_c_tell (hardware output commands)
// Called from l_bootstrap_c_tell to handle output/stream/change messages
extern "C" int LuaManager_lua_c_tell(lua_State* L) {
    return LuaManager::lua_c_tell(L);
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

// Lock-free stream callback - posts stream-equivalent values (always maintained)
static void stream_callback(int channel, float value) {
    // Use stream-equivalent value (already denoised by update_input_stream_values)
    // This ensures stream callbacks and .volts queries always see the same value
    float stream_value = (channel >= 0 && channel < 2) ? g_input_stream_volts[channel] : value;
    
    // Post to lock-free queue
    if (!input_lockfree_post(channel, stream_value, 1)) {  // type=1 for stream
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Stream lock-free queue full, dropped %lu events", drop_count);
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
    bool state = (value > 0.5f);
    
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

// Window callback - called when input crosses window boundaries
static void window_callback(int channel, float value) {
    // Value contains signed window index (positive=up, negative=down)
    if (!input_lockfree_post(channel, value, 2)) {  // type=2 for window
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Window lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

static void scale_callback(int channel, float value) {
    Detect_t* detector = Detect_ix_to_p(channel);
    if (!detector) return;
    
    input_event_lockfree_t event;
    event.channel = channel;
    event.value = value;
    event.detection_type = 3;  // type=3 for scale
    event.timestamp_us = time_us_32();
    event.extra.scale.index = detector->scale.lastIndex;
    event.extra.scale.octave = detector->scale.lastOct;
    event.extra.scale.note = detector->scale.lastNote;
    event.extra.scale.volts = detector->scale.lastVolts;
    
    // Post the full event with scale data
    extern bool input_lockfree_post_extended(const input_event_lockfree_t* event);
    if (!input_lockfree_post_extended(&event)) {
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Scale lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Volume callback - called periodically with RMS level
static void volume_callback(int channel, float level) {
    // OPTIMIZATION: Time-based batching for volume mode
    static float last_value[8] = {0};
    static uint32_t last_post_time[8] = {0};
    
    uint32_t now = time_us_32();
    float delta = fabsf(level - last_value[channel]);
    uint32_t time_since_post = now - last_post_time[channel];
    
    // Post if significant change (>5mV) OR timeout (5ms)
    bool significant_change = (delta > 0.005f);  // 5mV threshold
    bool timeout = (time_since_post > 5000);     // 5ms timeout
    
    if (significant_change || timeout) {
        if (input_lockfree_post(channel, level, 4)) {  // type=4 for volume
            last_value[channel] = level;
            last_post_time[channel] = now;
        } else {
            static uint32_t drop_count = 0;
            if (++drop_count % 100 == 0) {
                queue_debug_message("Volume lock-free queue full, dropped %lu events", drop_count);
            }
        }
    }
}

// Peak callback - called when envelope exceeds threshold
static void peak_callback(int channel, float value) {
    if (!input_lockfree_post(channel, 0.0f, 5)) {  // type=5 for peak, no value needed
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Peak lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Freq callback - called periodically with frequency estimate
static void freq_callback(int channel, float freq) {
    if (!input_lockfree_post(channel, freq, 6)) {  // type=6 for freq
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Freq lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Lock-free input event handler - processes detection events from lock-free queue
extern "C" void L_handle_input_lockfree(input_event_lockfree_t* event) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (!lua_mgr) return;
    
    // ===============================================
    // OPTIMIZATION 2: Enable batching for input callbacks
    // ===============================================
    output_batch_begin();
    
    int channel = event->channel + 1;  // Convert to 1-based for Lua
    float value = event->value;
    int detection_type = event->detection_type;
    
    // LED indicator for lock-free input processing
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(0);
    }
    
    // Call appropriate Lua handler based on detection type
    // IMPORTANT: Call input[n].stream() / input[n].change() / input[n].window() etc. methods
    // which internally call _c.tell() to send ^^stream, ^^change, ^^window messages to druid/norns
    char lua_call[256];
    if (detection_type == 1) {
        // Stream mode - call input[n].stream(value)
        snprintf(lua_call, sizeof(lua_call),
            "if input and input[%d] and input[%d].stream then input[%d].stream(%.6f) end",
            channel, channel, channel, value);
    } else if (detection_type == 0) {
        // Change mode - call input[n].change(state)
        // IMPORTANT: Pass true/false not 1/0, because in Lua 0 is truthy!
        bool state = (value > 0.5f);
        snprintf(lua_call, sizeof(lua_call),
            "if input and input[%d] and input[%d].change then input[%d].change(%s) end",
            channel, channel, channel, state ? "true" : "false");
    } else if (detection_type == 2) {
        // Window mode
        // value is the window index (1-based) if positive, or negative index if moving down
        // Call input[n].window(win, dir) where dir indicates direction (positive=up, negative=down)
        int win = (int)fabsf(value);
        bool dir = (value > 0);
        snprintf(lua_call, sizeof(lua_call),
            "if input and input[%d] and input[%d].window then input[%d].window(%d, %s) end",
            channel, channel, channel, win, dir ? "true" : "false");
    } else if (detection_type == 3) {
        // Scale mode - call input[n].scale({index=i, octave=o, note=n, volts=v})
        // Build a table with all scale data
        snprintf(lua_call, sizeof(lua_call),
            "if input and input[%d] and input[%d].scale then "
            "input[%d].scale({index=%d,octave=%d,note=%.6f,volts=%.6f}) end",
            channel, channel, channel,
            event->extra.scale.index + 1, // Convert to 1-based
            event->extra.scale.octave,
            event->extra.scale.note,
            event->extra.scale.volts);
    } else if (detection_type == 4) {
        // Volume mode - call input[n].volume(level)
        snprintf(lua_call, sizeof(lua_call),
            "if input and input[%d] and input[%d].volume then input[%d].volume(%.6f) end",
            channel, channel, channel, value);
    } else if (detection_type == 5) {
        // Peak mode - call input[n].peak() - no arguments
        snprintf(lua_call, sizeof(lua_call),
            "if input and input[%d] and input[%d].peak then input[%d].peak() end",
            channel, channel, channel);
    } else if (detection_type == 6) {
        // Freq mode - call input[n].freq(freq)
        snprintf(lua_call, sizeof(lua_call),
            "if input and input[%d] and input[%d].freq then input[%d].freq(%.6f) end",
            channel, channel, channel, value);
    } else {
        // Unknown detection type - skip
        snprintf(lua_call, sizeof(lua_call), "-- unknown detection_type=%d", detection_type);
    }
    
    if (kDetectionDebug) {
        printf("LOCKFREE INPUT: ch%d type=%d value=%.3f\n\r",
               channel, detection_type, value);
    }
    
    lua_mgr->evaluate_safe(lua_call);
    
    // ===============================================
    // OPTIMIZATION 2: Flush batched outputs
    // ===============================================
    output_batch_flush();
    
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
        // // Use TinyUSB CDC for output
        // if (tud_cdc_connected()) {
        //     char msg[64];
        //     snprintf(msg, sizeof(msg), "Input %d: stream mode, interval %.3fs\n\r", channel, time);
        //     tud_cdc_write_str(msg);
        //     tud_cdc_write_flush();
        // }
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
        Detect_window(detector, window_callback, windows, wLen, hysteresis);
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
        Detect_scale(detector, scale_callback, scale, sLen, temp, scaling);
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
        Detect_volume(detector, volume_callback, time);
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
        Detect_peak(detector, peak_callback, threshold, hysteresis);
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
        Detect_freq(detector, freq_callback, time);
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
        // Set clock to external input source and configure division
        clock_set_source(CLOCK_SOURCE_CROW);
        clock_crow_in_div(div);
        
        // Use clock_input_handler instead of generic change_callback
        Detect_change(detector, clock_input_handler, threshold, hysteresis, 1); // Rising edge
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
    //printf("Metro %d started with interval %.3fs\n\r", id, time);
    return 0;
}

// metro_stop(id) - Stop a metro
int LuaManager::lua_metro_stop(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    
    // Call C metro backend
    Metro_stop(id);
    //printf("Metro %d stopped\n\r", id);
    return 0;
}

// metro_set_time(id, time) - Change metro interval
int LuaManager::lua_metro_set_time(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    // Call C metro backend
    Metro_set_time(id, time);
    //printf("Metro %d time set to %.3fs\n\r", id, time);
    return 0;
}

// metro_set_count(id, count) - Set metro repeat count
int LuaManager::lua_metro_set_count(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int count = luaL_checkinteger(L, 2);
    
    // Call C metro backend
    Metro_set_count(id, count);
   // printf("Metro %d count set to %d\n\r", id, count);
    return 0;
}

// Clock system Lua bindings implementation

// clock_cancel(coro_id) - Cancel a running clock coroutine
int LuaManager::lua_clock_cancel(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    clock_cancel_coro(coro_id);
    lua_pop(L, 1);
    return 0;
}

// clock_schedule_sleep(coro_id, seconds) - Schedule coroutine resume after seconds
int LuaManager::lua_clock_schedule_sleep(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    float seconds = (float)luaL_checknumber(L, 2);
    
    if (seconds <= 0) {
        L_queue_clock_resume(coro_id); // immediate callback
    } else {
        clock_schedule_resume_sleep(coro_id, seconds);
    }
    lua_pop(L, 2);
    return 0;
}

// clock_schedule_sync(coro_id, beats) - Schedule coroutine resume at beat sync point
int LuaManager::lua_clock_schedule_sync(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    float beats = (float)luaL_checknumber(L, 2);
    
    if (beats <= 0) {
        L_queue_clock_resume(coro_id); // immediate callback
    } else {
        clock_schedule_resume_sync(coro_id, beats);
    }
    lua_pop(L, 2);
    return 0;
}

// clock_schedule_beat(coro_id, beats) - Schedule coroutine resume after beats (not synced)
int LuaManager::lua_clock_schedule_beat(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    float beats = (float)luaL_checknumber(L, 2);
    
    if (beats <= 0) {
        L_queue_clock_resume(coro_id); // immediate callback
    } else {
        clock_schedule_resume_beatsync(coro_id, beats);
    }
    lua_pop(L, 2);
    return 0;
}

// clock_get_time_beats() - Get current time in beats
int LuaManager::lua_clock_get_time_beats(lua_State* L) {
    lua_pushnumber(L, clock_get_time_beats());
    return 1;
}

// clock_get_tempo() - Get current tempo in BPM
int LuaManager::lua_clock_get_tempo(lua_State* L) {
    lua_pushnumber(L, clock_get_tempo());
    return 1;
}

// clock_set_source(source) - Set clock source (1-based in Lua)
int LuaManager::lua_clock_set_source(lua_State* L) {
    int source = (int)luaL_checkinteger(L, 1);
    clock_set_source((clock_source_t)(source - 1)); // Convert to 0-based
    lua_pop(L, 1);
    return 0;
}

// clock_internal_set_tempo(bpm) - Set internal clock tempo
int LuaManager::lua_clock_internal_set_tempo(lua_State* L) {
    float bpm = (float)luaL_checknumber(L, 1);
    clock_internal_set_tempo(bpm);
    lua_pop(L, 1);
    return 0;
}

// clock_internal_start(new_beat) - Start internal clock at specified beat
int LuaManager::lua_clock_internal_start(lua_State* L) {
    float new_beat = (float)luaL_checknumber(L, 1);
    clock_set_source(CLOCK_SOURCE_INTERNAL);
    clock_internal_start(new_beat, true);
    lua_pop(L, 1);
    return 0;
}

// clock_internal_stop() - Stop internal clock
int LuaManager::lua_clock_internal_stop(lua_State* L) {
    clock_set_source(CLOCK_SOURCE_INTERNAL);
    clock_internal_stop();
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

// Implementation of lua_unique_id function - crow-compatible version
// Returns 3 integers like crow's unique_id() which returns getUID_Word(0), getUID_Word(4), getUID_Word(8)
int LuaManager::lua_unique_id(lua_State* L) {
    // Get the cached 64-bit ID from the global instance
    if (g_blackbird_instance) {
        uint64_t id = ((BlackbirdCrow*)g_blackbird_instance)->cached_unique_id;
        
        // Split the 64-bit ID into three parts to mimic crow's behavior
        // Crow returns 3 32-bit words from different offsets of the STM32 UID
        // We'll split our 64-bit value into 3 parts with some bit mixing
        uint32_t word0 = (uint32_t)(id & 0xFFFFFFFF);           // Lower 32 bits
        uint32_t word1 = (uint32_t)((id >> 32) & 0xFFFFFFFF);   // Upper 32 bits
        uint32_t word2 = (uint32_t)(word0 ^ word1);             // XOR for third word
        
        lua_pushinteger(L, (lua_Integer)word0);
        lua_pushinteger(L, (lua_Integer)word1);
        lua_pushinteger(L, (lua_Integer)word2);
        return 3;
    }
    
    // Return zeros if no instance available
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    return 3;
}

// Public view tracking - global state for monitoring channels
static bool g_view_chans[6] = {false, false, false, false, false, false};
static float g_last_view_values[6] = {-6.0f, -6.0f, -6.0f, -6.0f, -6.0f, -6.0f};

// pub_view_in(channel, state) - Enable/disable viewing of input channel
int LuaManager::lua_pub_view_in(lua_State* L) {
    int chan = luaL_checkinteger(L, 1) - 1; // lua is 1-based
    bool state;
    if (lua_isboolean(L, 2)) { 
        state = lua_toboolean(L, 2); 
    } else { 
        state = (bool)lua_tointeger(L, 2); 
    }
    
    // Inputs are channels 4-5 in the view_chans array (outputs are 0-3)
    int view_idx = chan + 4;
    if (view_idx >= 0 && view_idx < 6) {
        g_view_chans[view_idx] = state;
        if (state) {
            g_last_view_values[view_idx] = -6.0f; // reset to force update
        }
    }
    
    lua_pop(L, 2);
    return 0;
}

// pub_view_out(channel, state) - Enable/disable viewing of output channel
int LuaManager::lua_pub_view_out(lua_State* L) {
    int chan = luaL_checkinteger(L, 1) - 1; // lua is 1-based
    bool state;
    if (lua_isboolean(L, 2)) { 
        state = lua_toboolean(L, 2); 
    } else { 
        state = (bool)lua_tointeger(L, 2); 
    }
    
    // Outputs are channels 0-3 in the view_chans array
    if (chan >= 0 && chan < 4) {
        g_view_chans[chan] = state;
        if (state) {
            g_last_view_values[chan] = -6.0f; // reset to force update
        }
    }
    
    lua_pop(L, 2);
    return 0;
}

// public_update() - Send ^^pubview messages to norns/druid for monitored channels
// Called at ~15fps from main loop
static void public_update() {
    const float VDIFF = 0.1f; // 100mV hysteresis
    static int chan = 0; // rotate through channels 0-5
    
    if (g_view_chans[chan]) {
        float new_val;
        char msg_buf[64]; // Buffer for the message
        
        if (chan < 4) { // outputs
            new_val = S_get_state(chan);
            if (new_val + VDIFF < g_last_view_values[chan] ||
                new_val - VDIFF > g_last_view_values[chan]) {
                g_last_view_values[chan] = new_val;
                
                // Format the pubview message
                int len = snprintf(msg_buf, sizeof(msg_buf), 
                                  "^^pubview('output',%d,%g)\n\r", 
                                  chan + 1, new_val);
                
                // Write to buffer - batched flush happens every 2ms in main loop
                if (len > 0 && tud_cdc_connected()) {
                    tud_cdc_write(msg_buf, len);
                    // REMOVED: tud_cdc_write_flush(); - batched in main loop
                }
            }
        } else { // inputs (4-5 -> 0-1)
            int input_chan = chan - 4;
            new_val = get_input_state_simple(input_chan);
            if (new_val + VDIFF < g_last_view_values[chan] ||
                new_val - VDIFF > g_last_view_values[chan]) {
                g_last_view_values[chan] = new_val;
                
                // Format the pubview message
                int len = snprintf(msg_buf, sizeof(msg_buf), 
                                  "^^pubview('input',%d,%g)\n\r", 
                                  input_chan + 1, new_val);
                
                // Write to buffer - batched flush happens every 2ms in main loop
                if (len > 0 && tud_cdc_connected()) {
                    tud_cdc_write(msg_buf, len);
                    // REMOVED: tud_cdc_write_flush(); - batched in main loop
                }
            }
        }
    }
    
    chan = (chan + 1) % 6; // round-robin through all 6 channels
}

// tell(event, ...) - Send ^^event messages to druid/norns
// This is the core function that input.stream() and input.change() use
int LuaManager::lua_tell(lua_State* L) {
    int nargs = lua_gettop(L);
    
    // Need at least the event name
    if (nargs == 0) {
        return luaL_error(L, "tell: no event name provided");
    }
    
    switch(nargs) {
        case 1:
            Caw_printf("^^%s()", luaL_checkstring(L, 1));
            break;
        case 2:
            Caw_printf("^^%s(%s)", luaL_checkstring(L, 1),
                                   luaL_checkstring(L, 2));
            break;
        case 3:
            Caw_printf("^^%s(%s,%s)", luaL_checkstring(L, 1),
                                      luaL_checkstring(L, 2),
                                      luaL_checkstring(L, 3));
            break;
        case 4:
            Caw_printf("^^%s(%s,%s,%s)", luaL_checkstring(L, 1),
                                         luaL_checkstring(L, 2),
                                         luaL_checkstring(L, 3),
                                         luaL_checkstring(L, 4));
            break;
        case 5:
            Caw_printf("^^%s(%s,%s,%s,%s)", luaL_checkstring(L, 1),
                                            luaL_checkstring(L, 2),
                                            luaL_checkstring(L, 3),
                                            luaL_checkstring(L, 4),
                                            luaL_checkstring(L, 5));
            break;
        default:
            return luaL_error(L, "tell: too many arguments (max 5)");
    }
    
    lua_pop(L, nargs);
    lua_settop(L, 0);
    return 0;
}

// hardware_pulse(channel, state) - Set pulse output state
// channel: 1 or 2
// state: boolean (true = high, false = low)
int LuaManager::lua_hardware_pulse(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    bool state = lua_toboolean(L, 2);
    
    if (channel < 1 || channel > 2) {
        return luaL_error(L, "hardware_pulse: channel must be 1 or 2");
    }
    
    hardware_pulse_output_set(channel, state);
    return 0;
}

// Hardware knob access functions - return normalized 0.0-1.0 values
// (Integer to float conversion happens here, outside ISR)
int LuaManager::lua_get_knob_main(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        int value = crow->GetKnobValue(ComputerCard::Knob::Main);
        float value_f = value / 4095.0; // Convert to 0.0-1.0
       if (value_f < 0.01) {
            value_f = 0.0;
        } else if (value_f > 0.99) {
            value_f = 1.0; 
        }
        lua_pushnumber(L, value_f);  
        return 1;
    }
    lua_pushnumber(L, 0.0);
    return 1;
}

int LuaManager::lua_get_knob_x(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        int value = crow->GetKnobValue(ComputerCard::Knob::X);
        float value_f = value / 4095.0; // Convert to 0.0-1.0
       if (value_f < 0.01) {
            value_f = 0.0;
        } else if (value_f > 0.99) {
            value_f = 1.0; 
        }
        lua_pushnumber(L, value_f);  
        return 1;
    }
    lua_pushnumber(L, 0.0);
    return 1;
}

int LuaManager::lua_get_knob_y(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        int value = crow->GetKnobValue(ComputerCard::Knob::Y);
       float value_f = value / 4095.0; // Convert to 0.0-1.0
       if (value_f < 0.01) {
            value_f = 0.0;
        } else if (value_f > 0.99) {
            value_f = 1.0; 
        }
        lua_pushnumber(L, value_f);  
        return 1;
    }
    lua_pushnumber(L, 0.0);
    return 1;
}

// Hardware switch position query - returns 'down', 'middle', or 'up'
int LuaManager::lua_get_switch_position(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        ComputerCard::Switch pos = crow->GetSwitchValue();
        const char* pos_str = (pos == ComputerCard::Switch::Down) ? "down" :
                              (pos == ComputerCard::Switch::Middle) ? "middle" : "up";
        lua_pushstring(L, pos_str);
        return 1;
    }
    lua_pushstring(L, "middle");
    return 1;
}

// Implementation of lua_memstats (memory statistics and debugging)
int LuaManager::lua_memstats(lua_State* L) {
    if (!tud_cdc_connected()) return 0;
    
    int kb_used = lua_gc(L, LUA_GCCOUNT, 0);
    int bytes = lua_gc(L, LUA_GCCOUNTB, 0);
    float total_kb = kb_used + (bytes / 1024.0f);
    
    // Use TinyUSB CDC functions for output (printf doesn't work with TinyUSB)
    char buffer[128];
    
    tud_cdc_write_str("Lua Memory Usage:\n\r");
    tud_cdc_write_flush();
    
    snprintf(buffer, sizeof(buffer), "  Current: %.2f KB (%d KB + %d bytes)\n\r", 
             total_kb, kb_used, bytes);
    tud_cdc_write_str(buffer);
    tud_cdc_write_flush();
    
    // Trigger a GC cycle and report change
    lua_gc(L, LUA_GCCOLLECT, 0);
    int kb_after = lua_gc(L, LUA_GCCOUNT, 0);
    int bytes_after = lua_gc(L, LUA_GCCOUNTB, 0);
    float total_after = kb_after + (bytes_after / 1024.0f);
    float freed = total_kb - total_after;
    
    snprintf(buffer, sizeof(buffer), "  After GC: %.2f KB (freed %.2f KB)\n\r", 
             total_after, freed);
    tud_cdc_write_str(buffer);
    tud_cdc_write_flush();
    
    return 0;
}

// Implementation of lua_perf_stats (performance monitoring statistics)
// Usage: perf_stats()              -- prints formatted stats, resets counter
//        perf_stats(false)         -- prints formatted stats, doesn't reset
//        val = perf_stats("raw")   -- returns raw value (microseconds), resets
//        val = perf_stats("raw", false) -- returns raw value, doesn't reset
int LuaManager::lua_perf_stats(lua_State* L) {
    int nargs = lua_gettop(L);
    bool reset = true;
    bool raw_mode = false;
    
    // Parse arguments
    if (nargs >= 1) {
        if (lua_isstring(L, 1)) {
            const char* mode = lua_tostring(L, 1);
            if (strcmp(mode, "raw") == 0) {
                raw_mode = true;
            }
            // Check for second argument (reset flag)
            if (nargs >= 2) {
                reset = lua_toboolean(L, 2);
            }
        } else {
            // First arg is boolean (reset flag)
            reset = lua_toboolean(L, 1);
        }
    }
    
    // Get current worst-case value
    uint32_t worst = g_worst_case_us;
    uint32_t overruns = g_overrun_count;
    
    if (raw_mode) {
        // Raw mode: just return the value
        lua_pushinteger(L, worst);
        if (reset) {
            g_worst_case_us = 0;
        }
        return 1;
    } else {
        // Default mode: print formatted output via TinyUSB CDC
        if (tud_cdc_connected()) {
            char msg[256];
            snprintf(msg, sizeof(msg), 
                     "ProcessSample Performance:\n\r"
                     "  Worst case: %lu microseconds\n\r"
                     "  Budget: 20.8us (48kHz sample rate)\n\r"
                     "  Utilization: %.1f%%\n\r"
                     "  Overruns (>18us): %lu\n\r",
                     (unsigned long)worst,
                     (worst / 20.8f) * 100.0f,
                     (unsigned long)overruns);
            tud_cdc_write_str(msg);
            tud_cdc_write_flush();
        }
        
        // Optionally reset the tracker
        if (reset) {
            g_worst_case_us = 0;
        }
        
        return 0;  // No return value in print mode
    }
}

// Implementation of C interface function (after BlackbirdCrow class is fully defined)
extern "C" void hardware_output_set_voltage(int channel, float voltage) {
    int ch_idx = channel - 1;  // channel is 1-based
    
    // If noise is currently active and locked, ignore this call completely
    // The lock counter protects noise from automatic hardware_output_set_voltage calls
    // (from slopes, LFOs, etc.) but explicit user actions via casl_action will clear noise
    if (ch_idx >= 0 && ch_idx < 4) {
        if (g_noise_active[ch_idx] && g_noise_lock_counter[ch_idx] > 0) {
            // Noise is active and protected - ignore this automatic call
            // Do NOT decrement counter - keep noise protected indefinitely
            return;
        }
        
        // If noise is active but not locked, a new action is trying to output
        // Clear noise and let the new action take control
        if (g_noise_active[ch_idx]) {
            g_noise_active[ch_idx] = false;
            g_noise_active_mask &= ~(1 << ch_idx);  // Clear bit in mask
            g_noise_gain[ch_idx] = 0;
            g_noise_lock_counter[ch_idx] = 0;
        }
    }
    
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_output(channel, voltage);
    }
}

// C interface functions for pulse outputs (called from Lua)
extern "C" void hardware_pulse_output_set(int channel, bool state) {
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_pulse(channel, state);
    }
}

// NOTE: pulseout_has_custom_action() and pulseout_execute_action() are no longer needed
// Pulse outputs are now controlled entirely via Lua :clock() coroutines

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

// Expose card unique ID for USB descriptors
// This ensures the USB serial number follows the card, not the board
extern "C" uint64_t get_card_unique_id(void) {
    // Access through the blackbird instance if available
    if (g_blackbird_instance) {
        return ((BlackbirdCrow*)g_blackbird_instance)->cached_unique_id;
    }
    // Fallback to 0 during early initialization
    return 0;
}

void core1_entry() {
        printf("[boot] core1 audio engine starting\n\r");
        //Normalisation probe was causing issues so disabling.
        //crow.EnableNormalisationProbe();
        crow.Run(); 
}

// Redirect stdio functions to TinyUSB CDC
extern "C" {
    // Override putchar for printf support
    int putchar(int c) {
        if (tud_cdc_connected()) {
            uint8_t ch = (uint8_t)c;
            tud_cdc_write(&ch, 1);
            if (c == '\n' || c == '\r') {
                tud_cdc_write_flush();
            }
        }
        return c;
    }
    
    // Override puts for string output
    int puts(const char* s) {
        if (tud_cdc_connected()) {
            tud_cdc_write_str(s);
            tud_cdc_write_char('\n');
            tud_cdc_write_flush();
        }
        return 1;
    }
    
    // Newlib write function
    int _write(int handle, char *data, int size) {
        if (handle == 1 || handle == 2) { // stdout or stderr
            if (tud_cdc_connected()) {
                tud_cdc_write(data, size);
                tud_cdc_write_flush();
            }
            return size;
        }
        return -1;
    }
}

int main()
{
    set_sys_clock_khz(200000, true);

    // Initialize TinyUSB (replaces stdio_init_all)
    tusb_init();
    
    // Disable stdio buffering to ensure immediate visibility of debug prints
    setvbuf(stdout, NULL, _IONBF, 0);
    
    // Wait briefly (up to 1500ms) for a USB serial host to connect so the boot banner is visible
    {
        absolute_time_t until = make_timeout_time_ms(1500);
        while (!tud_cdc_connected() && absolute_time_diff_us(get_absolute_time(), until) > 0) {
            tud_task();  // Must service TinyUSB while waiting
            tight_loop_contents();
        }
    }

    multicore_launch_core1(core1_entry);
    
    // Allow core1 to start before entering control loop
    sleep_ms(500);
    
    // Start Core0 main control loop (handles commands and events)
    crow.MainControlLoop();
}
