#pragma once

#ifndef CROW_DETECT_H
#define CROW_DETECT_H

#include <stdbool.h>
#include <stdint.h>

// Lua integration for callback functions
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

/* Detection event queue + Lua dispatch support */
typedef enum {
    DETECT_EVENT_STREAM = 0,
    DETECT_EVENT_CHANGE,
    DETECT_EVENT_WINDOW,
    DETECT_EVENT_SCALE,
    DETECT_EVENT_VOLUME,
    DETECT_EVENT_PEAK,
    DETECT_EVENT_FREQ,
    DETECT_EVENT_CLOCK
} detect_event_type_t;

typedef struct {
    uint8_t channel;
    uint8_t type;
    float a;
    float b;
    float c;
    float d;
} detect_event_t;

/* Drain queued detection events on the Lua (core1) side */
void crow_detect_drain_events(lua_State* L);

// Detection system constants
#define CROW_DETECT_CHANNELS     2    // Workshop Computer has 2 inputs
#define SCALE_MAX_COUNT          16   // Maximum scale notes
#define WINDOW_MAX_COUNT         16   // Maximum window thresholds

// Callback function types
typedef void (*Detect_callback_t)(int channel, float value);

// Detection mode function pointer
typedef struct crow_detect crow_detect_t;
typedef void (*detect_mode_fn_t)(crow_detect_t* self, float level);

// Stream detection state
typedef struct {
    int blocks;                       // Blocks between callbacks
    int countdown;                    // Current countdown
} detect_stream_t;

// Change detection state
typedef struct {
    float threshold;                  // Trigger threshold
    float hysteresis;                 // Hysteresis amount
    int8_t direction;                 // -1=falling, 0=both, 1=rising
} detect_change_t;

// Scale detection state (quantized note detection)
typedef struct {
    float scale[SCALE_MAX_COUNT];     // Scale note ratios
    int sLen;                         // Scale length
    float divs;                       // Divisions per octave (12 for 12TET)
    float scaling;                    // Voltage per octave (1.0 for v/oct)
    // State and pre-computation
    float offset;                     // Half-division offset
    float win;                        // Window size in volts
    float hyst;                       // Hysteresis amount
    // Bounds for current note
    float upper;                      // Upper bound
    float lower;                      // Lower bound
    // Last detected values
    int lastIndex;                    // Scale index
    int lastOct;                      // Octave
    float lastNote;                   // Note value
    float lastVolts;                  // Voltage value
} detect_scale_t;

// Window detection state (threshold crossing)
typedef struct {
    float windows[WINDOW_MAX_COUNT];  // Threshold levels
    int wLen;                         // Number of windows
    float hysteresis;                 // Hysteresis amount
    int lastWin;                      // Last window index
} detect_window_t;

// Volume detection state (RMS level)
typedef struct {
    int blocks;                       // Blocks between callbacks
    int countdown;                    // Current countdown
    float level;                      // Current RMS level
    float envelope;                   // Envelope follower state
} detect_volume_t;

 // Peak detection state
typedef struct {
    float threshold;                  // Peak threshold
    float hysteresis;                 // Hysteresis amount
    float release;                    // Release rate
    float envelope;                   // Peak envelope
} detect_peak_t;

// Clock detection state
typedef struct {
    float threshold;                  // Rising edge threshold
    float hysteresis;                 // Hysteresis band
    uint32_t min_gap_samples;         // Minimum samples between edges (debounce / max tempo)
    uint8_t armed;                    // Armed for rising edge
    uint64_t last_edge_sample;        // Sample counter of last edge
    float last_period_s;              // Last measured period (s)
    float last_bpm;                   // Last raw BPM
    float smooth_bpm;                 // Smoothed BPM (EMA)
} detect_clock_t;

// Forward declaration for wrEvent
struct event_extract;

// Main detection structure
typedef struct crow_detect {
    uint8_t channel;                  // Input channel (0-1)
    detect_mode_fn_t modefn;          // Current mode function
    Detect_callback_t action;         // Lua callback function
    
    // State memory
    float last;                       // Last input value
    uint8_t state;                    // Hysteresis state (0/1)
    
    // wrEvent integration for enhanced detection
    struct event_extract* wrEvent_extractor;  // wrEvent trigger extractor
    
    // Mode-specific states
    detect_stream_t stream;
    detect_change_t change;
    detect_window_t window;
    detect_scale_t scale;
    detect_volume_t volume;
    detect_peak_t peak;
    detect_clock_t clock;
} crow_detect_t;

// Global detection system functions
void crow_detect_init(int channels);
void crow_detect_deinit();
void crow_detect_process_sample();
void crow_detect_process_block(float* input_blocks[4], int block_size);

// Per-channel detection functions
crow_detect_t* crow_detect_get_channel(uint8_t channel);
int8_t crow_detect_str_to_dir(const char* str);

// Detection mode configuration
void crow_detect_none(crow_detect_t* self);
void crow_detect_stream(crow_detect_t* self, Detect_callback_t cb, float interval);
void crow_detect_change(crow_detect_t* self, Detect_callback_t cb, float threshold, 
                       float hysteresis, int8_t direction);
void crow_detect_scale(crow_detect_t* self, Detect_callback_t cb, float* scale, 
                      int sLen, float divs, float scaling);
void crow_detect_window(crow_detect_t* self, Detect_callback_t cb, float* windows, 
                       int wLen, float hysteresis);
void crow_detect_volume(crow_detect_t* self, Detect_callback_t cb, float interval);
void crow_detect_peak(crow_detect_t* self, Detect_callback_t cb, float threshold, 
                     float hysteresis);
void crow_detect_freq(crow_detect_t* self, Detect_callback_t cb, float interval);
void crow_detect_clock(crow_detect_t* self, Detect_callback_t cb, float threshold,
                       float hysteresis, float min_period_s);

// Lua binding functions for C interface
extern "C" int set_input_none(lua_State* L);
extern "C" int set_input_stream(lua_State* L);
extern "C" int set_input_change(lua_State* L);
extern "C" int set_input_window(lua_State* L);
extern "C" int set_input_scale(lua_State* L);
extern "C" int set_input_volume(lua_State* L);
extern "C" int set_input_peak(lua_State* L);
extern "C" int set_input_freq(lua_State* L);
extern "C" int set_input_clock(lua_State* L);
extern "C" int io_get_input(lua_State* L);

// Callback handlers (called from C, call into Lua)
extern "C" void stream_handler(int channel, float value);
extern "C" void change_handler(int channel, int state);
extern "C" void window_handler(int channel, int win, int direction);
extern "C" void scale_handler(int channel, int index, int octave, float note, float volts);
extern "C" void volume_handler(int channel, float level);
extern "C" void peak_handler(int channel);
extern "C" void freq_handler(int channel, float freq);
extern "C" void clock_handler(int channel, float bpm, float period);

#endif // CROW_DETECT_H
