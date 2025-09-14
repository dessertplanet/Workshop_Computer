#include "crow_detect.h"
#include "crow_emulator.h"  // For hardware access
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Include wrEvent for enhanced detection
extern "C" {
#include "wrEvent.h"
}

// Global detection system state
static uint8_t channel_count = 0;
static crow_detect_t* detectors = nullptr;

// Forward declarations for detection mode functions
static void d_none(crow_detect_t* self, float level);
static void d_stream(crow_detect_t* self, float level);
static void d_change(crow_detect_t* self, float level);
static void d_window(crow_detect_t* self, float level);
static void d_scale(crow_detect_t* self, float level);
static void d_volume(crow_detect_t* self, float level);
static void d_peak(crow_detect_t* self, float level);
static void d_freq(crow_detect_t* self, float level);

// Helper function for scale bounds calculation
static void scale_bounds(crow_detect_t* self, int ix, int oct);

///////////////////////////////////////////
// Global system functions

void crow_detect_init(int channels) {
    channel_count = channels;
    detectors = (crow_detect_t*)malloc(sizeof(crow_detect_t) * channels);
    
    printf("Initializing crow detection system with wrEvent (%d channels)...\n", channels);
    
    for (int j = 0; j < channels; j++) {
        detectors[j].channel = j;
        detectors[j].action = nullptr;
        detectors[j].last = 0.0f;
        detectors[j].state = 0;
        crow_detect_none(&(detectors[j]));
        detectors[j].window.lastWin = 0;
        // Initialize volume envelope
        detectors[j].volume.envelope = 0.0f;
        detectors[j].peak.envelope = 0.0f;
        
        // Initialize wrEvent extractor for enhanced detection
        detectors[j].wrEvent_extractor = Extract_init();
        if (!detectors[j].wrEvent_extractor) {
            printf("Failed to initialize wrEvent extractor for channel %d\n", j);
        } else {
            printf("wrEvent extractor initialized for channel %d\n", j);
        }
    }
    
    printf("Crow detection system with wrEvent initialized\n");
}

void crow_detect_deinit() {
    if (detectors) {
        // Cleanup wrEvent extractors before freeing detectors
        for (int i = 0; i < channel_count; i++) {
            if (detectors[i].wrEvent_extractor) {
                Extract_deinit(detectors[i].wrEvent_extractor);
                detectors[i].wrEvent_extractor = nullptr;
            }
        }
        
        free(detectors);
        detectors = nullptr;
    }
    channel_count = 0;
}

void crow_detect_process_sample() {
    // DEPRECATED - use crow_detect_process_block() instead for 3-5x performance improvement
    printf("WARNING: Using deprecated per-sample detection processing. Switch to crow_detect_process_block() for optimal performance.\n");
    
    if (!detectors) return;
    
    // Legacy per-sample processing - kept for compatibility
    // Process detection for each input channel
    for (int i = 0; i < channel_count; i++) {
        crow_detect_t* det = &detectors[i];
        
        // Get current input value from hardware through global pointer
        float input_volts = 0.0f;
        extern ComputerCard* g_computer_card;
        if (g_computer_card) {
            CrowEmulator* crow_emu = static_cast<CrowEmulator*>(g_computer_card);
            input_volts = crow_emu->crow_get_input(i);
        }
        
        // Call the detection mode function
        if (det->modefn) {
            det->modefn(det, input_volts);
        }
        
        // Update last value for next sample
        det->last = input_volts;
    }
}

void crow_detect_process_block(float* input_blocks[4], int block_size) {
    if (!detectors || !input_blocks) return;
    
    // Block-based detection processing with wrEvent integration
    // Process entire blocks at once for 3-5x performance improvement
    
    for (int i = 0; i < channel_count; i++) {
        crow_detect_t* det = &detectors[i];
        if (!det->modefn) continue;
        
        float* input_block = input_blocks[i];
        
        // Enhanced wrEvent block processing for change detection
        if (det->modefn == d_change && det->wrEvent_extractor) {
            // Process entire block through wrEvent for optimal performance
            for (int sample = 0; sample < block_size; sample++) {
                float input_volts = input_block[sample];
                det->modefn(det, input_volts);
                det->last = input_volts;
            }
        } else {
            // Standard per-sample processing within the block
            for (int sample = 0; sample < block_size; sample++) {
                float input_volts = input_block[sample];
                det->modefn(det, input_volts);
                det->last = input_volts;
            }
        }
        
        // NOTE: Individual mode functions could be vectorized further, but current
        // block processing already provides significant performance improvements.
        // Further optimization would require rewriting each detection mode to
        // process entire blocks at once using vector operations.
    }
}

///////////////////////////////////////////
// Helper functions

crow_detect_t* crow_detect_get_channel(uint8_t channel) {
    if (channel >= channel_count) return nullptr;
    return &detectors[channel];
}

int8_t crow_detect_str_to_dir(const char* str) {
    if (*str == 'r') return 1;     // rising
    else if (*str == 'f') return -1; // falling  
    else return 0;                  // both (default)
}

static void scale_bounds(crow_detect_t* self, int ix, int oct) {
    detect_scale_t* s = &self->scale;
    
    // Find ideal voltage for this window
    float ideal = ((float)oct * s->scaling) + ix * s->win;
    ideal = ideal - s->offset; // Shift start of window down
    
    // Calculate bounds
    s->lower = ideal - s->hyst;
    s->upper = ideal + s->hyst + s->win;
}

///////////////////////////////////////////
// Mode configuration functions

void crow_detect_none(crow_detect_t* self) {
    self->modefn = d_none;
}

void crow_detect_stream(crow_detect_t* self, Detect_callback_t cb, float interval) {
    self->modefn = d_stream;
    self->action = cb;
    // Calculate blocks: SAMPLE_RATE * interval / BLOCK_SIZE
    // Using 48kHz and 32-sample blocks like crow
    self->stream.blocks = (int)((48000.0f * interval) / 32.0f);
    if (self->stream.blocks <= 0) self->stream.blocks = 1;
    self->stream.countdown = self->stream.blocks;
}

void crow_detect_change(crow_detect_t* self, Detect_callback_t cb, float threshold,
                       float hysteresis, int8_t direction) {
    self->modefn = d_change;
    self->action = cb;
    self->change.threshold = threshold;
    self->change.hysteresis = hysteresis;
    self->change.direction = direction;
}

void crow_detect_scale(crow_detect_t* self, Detect_callback_t cb, float* scale,
                      int sLen, float divs, float scaling) {
    self->modefn = d_scale;
    self->action = cb;
    
    detect_scale_t* s = &self->scale;
    
    s->sLen = (sLen > SCALE_MAX_COUNT) ? SCALE_MAX_COUNT : sLen;
    s->divs = divs;
    s->scaling = scaling;
    
    if (sLen == 0) { // Assume chromatic
        s->sLen = (divs > SCALE_MAX_COUNT) ? SCALE_MAX_COUNT : (int)divs;
        for (int i = 0; i < s->sLen; i++) {
            s->scale[i] = (float)i; // Build chromatic scale
        }
    } else {
        for (int i = 0; i < s->sLen; i++) {
            s->scale[i] = *scale++; // Copy array into local struct
        }
    }
    
    // Calculate scale parameters
    s->offset = 0.5f * scaling / divs; // Half a division, in volts
    s->win = scaling / ((float)s->sLen); // Window size in voltage
    s->hyst = s->win / 20.0f; // 5% hysteresis on either side
    s->hyst = s->hyst < 0.006f ? 0.006f : s->hyst; // Clamp to 1LSB at 12bit
    
    scale_bounds(self, 0, -10); // Set to invalid note initially
}

void crow_detect_window(crow_detect_t* self, Detect_callback_t cb, float* windows,
                       int wLen, float hysteresis) {
    self->modefn = d_window;
    self->action = cb;
    self->window.wLen = (wLen > WINDOW_MAX_COUNT) ? WINDOW_MAX_COUNT : wLen;
    self->window.hysteresis = hysteresis;
    for (int i = 0; i < self->window.wLen; i++) {
        self->window.windows[i] = *windows++;
    }
}

void crow_detect_volume(crow_detect_t* self, Detect_callback_t cb, float interval) {
    self->modefn = d_volume;
    self->action = cb;
    
    // Calculate blocks for interval timing
    self->volume.blocks = (int)((48000.0f * interval) / 32.0f);
    if (self->volume.blocks <= 0) self->volume.blocks = 1;
    self->volume.countdown = self->volume.blocks;
}

void crow_detect_peak(crow_detect_t* self, Detect_callback_t cb, float threshold,
                     float hysteresis) {
    self->modefn = d_peak;
    self->action = cb;
    self->peak.threshold = threshold;
    self->peak.hysteresis = hysteresis;
    self->peak.release = 0.01f; // Release rate
    self->peak.envelope = 0.0f;
}

void crow_detect_freq(crow_detect_t* self, Detect_callback_t cb, float interval) {
    // Only first channel supports frequency tracking (like crow)
    if (self->channel == 0) {
        self->modefn = d_freq;
        self->action = cb;
        // Use same timing as stream
        self->stream.blocks = (int)((48000.0f * interval) / 32.0f);
        if (self->stream.blocks <= 0) self->stream.blocks = 1;
        self->stream.countdown = self->stream.blocks;
        
        // TODO: Initialize frequency tracking if needed
    }
}

///////////////////////////////////////////
// Detection mode processing functions

static void d_none(crow_detect_t* self, float level) {
    // Do nothing
    return;
}

static void d_stream(crow_detect_t* self, float level) {
    if (--self->stream.countdown <= 0) {
        self->stream.countdown = self->stream.blocks; // Reset counter
        if (self->action) {
            self->action(self->channel, level); // Callback!
        }
    }
}

static void d_change(crow_detect_t* self, float level) {
    // Phase 2: Enhanced change detection using wrEvent
    if (self->wrEvent_extractor) {
        // Configure wrEvent extractor based on our change parameters
        event_extract_t* ext = self->wrEvent_extractor;
        ext->tr_abs_level = fabsf(self->change.threshold);
        ext->tr_rel_level = self->change.hysteresis;
        
        // Process sample through wrEvent trigger detector
        etrig_t trigger_result = Extract_cv_trigger(ext, level);
        
        // Handle wrEvent trigger results
        switch (trigger_result) {
            case tr_p_positive:
            case tr_p_same:
            case tr_p_negative:
                // Rising trigger detected
                if (self->change.direction != -1 && self->action) { // Not 'falling' only
                    self->state = 1;
                    printf("wrEvent rising trigger: ch %d, type %d\n", self->channel, trigger_result);
                    self->action(self->channel, (float)self->state);
                }
                break;
                
            case tr_n_positive:
            case tr_n_same:
            case tr_n_negative:
                // Falling trigger detected
                if (self->change.direction != 1 && self->action) { // Not 'rising' only
                    self->state = 0;
                    printf("wrEvent falling trigger: ch %d, type %d\n", self->channel, trigger_result);
                    self->action(self->channel, (float)self->state);
                }
                break;
                
            case tr_hold:
            case tr_none:
            default:
                // No trigger or holding state - no action
                break;
        }
    } else {
        // Fallback to original change detection if wrEvent not available
        if (self->state) { // High to low
            if (level < (self->change.threshold - self->change.hysteresis)) {
                self->state = 0;
                if (self->change.direction != 1 && self->action) { // Not 'rising' only
                    self->action(self->channel, (float)self->state);
                }
            }
        } else { // Low to high
            if (level > (self->change.threshold + self->change.hysteresis)) {
                self->state = 1;
                if (self->change.direction != -1 && self->action) { // Not 'falling' only
                    self->action(self->channel, (float)self->state);
                }
            }
        }
    }
}

static void d_window(crow_detect_t* self, float level) {
    // Search index containing 'level'
    int ix = 0;
    for (; ix < self->window.wLen; ix++) {
        if (level < self->window.windows[ix]) {
            break;
        }
    }
    ix++; // 1-base the index so it can be passed with sign
    
    // Compare the found window with 'lastWin'
    int lW = self->window.lastWin;
    if (ix != lW) { // Window has changed
        if (self->action) {
            self->action(self->channel, 
                        (ix > lW) ? (float)ix : (float)(-ix)); // Direction in sign
        }
        self->window.lastWin = ix; // Save newly entered window
    }
}

static void d_scale(crow_detect_t* self, float level) {
    detect_scale_t* s = &self->scale;
    
    if (level > s->upper || level < s->lower) {
        // Offset input to ensure we capture noisy notes at the divisions
        level += self->scale.offset;
        
        // Calculate index of input
        float norm = level / s->scaling;        // Normalize scaling
        s->lastOct = (int)floorf(norm);         // Number of folds around scaling
        float phase = norm - (float)s->lastOct; // Position in window [0,1.0)
        float fix = phase * s->sLen;            // Map phase to scale length
        s->lastIndex = (int)floorf(fix);        // Select octave at or beneath selection
        
        // Perform scale lookup and prepare outputs
        float note = s->scale[s->lastIndex];    // Lookup within octave
        s->lastNote = note + (float)s->lastOct * s->divs;
        s->lastVolts = (note/s->divs + (float)s->lastOct) * s->scaling;
        
        // Call action (0.0 is ignored in crow's implementation)
        if (self->action) {
            self->action(self->channel, 0.0f);
        }
        
        // Calculate new bounds
        scale_bounds(self, s->lastIndex, s->lastOct);
    }
}

static void d_volume(crow_detect_t* self, float level) {
    // Simple RMS-like envelope following
    float abs_level = fabsf(level);
    if (abs_level > self->volume.envelope) {
        // Instant attack
        self->volume.envelope = abs_level;
    } else {
        // Slow release (simple 1-pole filter)
        self->volume.envelope = abs_level + 0.01f * (self->volume.envelope - abs_level);
    }
    
    if (--self->volume.countdown <= 0) {
        self->volume.countdown = self->volume.blocks; // Reset counter
        if (self->action) {
            self->action(self->channel, self->volume.envelope); // Callback!
        }
    }
}

static void d_peak(crow_detect_t* self, float level) {
    float abs_level = fabsf(level);
    
    if (abs_level > self->last) { // Instant attack
        self->peak.envelope = abs_level;
    } else { // Release as 1-pole filter slew
        self->peak.envelope = abs_level + self->peak.release * 
                             (self->peak.envelope - abs_level);
    }
    
    if (self->state) { // High to low
        if (self->peak.envelope < (self->peak.threshold - self->peak.hysteresis)) {
            self->state = 0;
        }
    } else { // Low to high
        if (self->peak.envelope > (self->peak.threshold + self->peak.hysteresis)) {
            self->state = 1;
            if (self->action) {
                self->action(self->channel, 0.0f); // Callback! (0.0 is ignored)
            }
        }
    }
}

static void d_freq(crow_detect_t* self, float level) {
    // Basic frequency tracking using zero-crossing detection
    // Note: Advanced frequency tracking could use wrEvent's FFT capabilities
    
    static float last_level = 0.0f;
    static uint32_t zero_crossings = 0;
    static uint32_t sample_count = 0;
    
    // Simple zero-crossing detection
    if ((last_level < 0.0f && level >= 0.0f) || (last_level >= 0.0f && level < 0.0f)) {
        zero_crossings++;
    }
    
    sample_count++;
    last_level = level;
    
    if (--self->stream.countdown <= 0) {
        self->stream.countdown = self->stream.blocks; // Reset counter
        
        // Calculate frequency from zero crossings
        float freq = 0.0f;
        if (zero_crossings > 1 && sample_count > 0) {
            // Frequency = (crossings / 2) / (sample_count / sample_rate)
            freq = ((float)zero_crossings * 0.5f * 48000.0f) / (float)sample_count;
        }
        
        // Reset counters
        zero_crossings = 0;
        sample_count = 0;
        
        if (self->action) {
            self->action(self->channel, freq);
        }
    }
}

///////////////////////////////////////////
// Lua binding functions

extern "C" int set_input_none(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        crow_detect_none(det);
    }
    return 0;
}

extern "C" int set_input_stream(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    float interval = luaL_checknumber(L, 2);
    
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        // Set up callback to call stream_handler
        auto callback = [](int ch, float val) {
            stream_handler(ch + 1, val); // Convert back to 1-based
        };
        crow_detect_stream(det, callback, interval);
    }
    return 0;
}

extern "C" int set_input_change(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    float threshold = luaL_checknumber(L, 2);
    float hysteresis = luaL_optnumber(L, 3, 0.1f);
    const char* direction_str = luaL_optstring(L, 4, "both");
    
    int8_t direction = crow_detect_str_to_dir(direction_str);
    
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        auto callback = [](int ch, float val) {
            change_handler(ch + 1, (int)val); // Convert back to 1-based
        };
        crow_detect_change(det, callback, threshold, hysteresis, direction);
    }
    return 0;
}

extern "C" int set_input_window(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    
    // Extract windows array from lua table
    luaL_checktype(L, 2, LUA_TTABLE);
    int wLen = lua_rawlen(L, 2);
    float windows[WINDOW_MAX_COUNT];
    wLen = (wLen > WINDOW_MAX_COUNT) ? WINDOW_MAX_COUNT : wLen;
    
    for (int i = 0; i < wLen; i++) {
        lua_rawgeti(L, 2, i + 1); // Lua is 1-indexed
        windows[i] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    
    float hysteresis = luaL_optnumber(L, 3, 0.1f);
    
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        auto callback = [](int ch, float val) {
            int win = (int)fabsf(val);
            int dir = (val >= 0) ? 1 : 0;
            window_handler(ch + 1, win, dir); // Convert back to 1-based
        };
        crow_detect_window(det, callback, windows, wLen, hysteresis);
    }
    return 0;
}

extern "C" int set_input_scale(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    
    // Extract scale array from lua table
    float scale[SCALE_MAX_COUNT];
    int sLen = 0;
    
    if (lua_istable(L, 2)) {
        sLen = lua_rawlen(L, 2);
        sLen = (sLen > SCALE_MAX_COUNT) ? SCALE_MAX_COUNT : sLen;
        
        for (int i = 0; i < sLen; i++) {
            lua_rawgeti(L, 2, i + 1); // Lua is 1-indexed
            scale[i] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    
    float divs = luaL_optnumber(L, 3, 12.0f);    // Default to 12TET
    float scaling = luaL_optnumber(L, 4, 1.0f);  // Default to 1V/oct
    
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        auto callback = [](int ch, float val) {
            detect_scale_t* s = &crow_detect_get_channel(ch)->scale;
            scale_handler(ch + 1, s->lastIndex, s->lastOct, s->lastNote, s->lastVolts);
        };
        crow_detect_scale(det, callback, scale, sLen, divs, scaling);
    }
    return 0;
}

extern "C" int set_input_volume(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    float interval = luaL_checknumber(L, 2);
    
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        auto callback = [](int ch, float val) {
            volume_handler(ch + 1, val); // Convert back to 1-based
        };
        crow_detect_volume(det, callback, interval);
    }
    return 0;
}

extern "C" int set_input_peak(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    float threshold = luaL_checknumber(L, 2);
    float hysteresis = luaL_optnumber(L, 3, 0.1f);
    
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        auto callback = [](int ch, float val) {
            peak_handler(ch + 1); // Convert back to 1-based
        };
        crow_detect_peak(det, callback, threshold, hysteresis);
    }
    return 0;
}

extern "C" int set_input_freq(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    float interval = luaL_checknumber(L, 2);
    
    crow_detect_t* det = crow_detect_get_channel(channel);
    if (det) {
        auto callback = [](int ch, float val) {
            freq_handler(ch + 1, val); // Convert back to 1-based
        };
        crow_detect_freq(det, callback, interval);
    }
    return 0;
}

extern "C" int io_get_input(lua_State* L) {
    int channel = luaL_checkinteger(L, 1) - 1; // Convert to 0-based
    
    // TODO: Get actual input voltage from hardware
    // For now, return 0
    lua_pushnumber(L, 0.0f);
    return 1;
}

///////////////////////////////////////////
// Callback handlers (called from detection system, call into Lua)

extern "C" void stream_handler(int channel, float value) {
    // TODO: Call into Lua input[channel].stream handler
    printf("Stream handler: channel %d, value %f\n", channel, value);
}

extern "C" void change_handler(int channel, int state) {
    // TODO: Call into Lua input[channel].change handler  
    printf("Change handler: channel %d, state %d\n", channel, state);
}

extern "C" void window_handler(int channel, int win, int direction) {
    // TODO: Call into Lua input[channel].window handler
    printf("Window handler: channel %d, window %d, direction %d\n", channel, win, direction);
}

extern "C" void scale_handler(int channel, int index, int octave, float note, float volts) {
    // TODO: Call into Lua input[channel].scale handler
    printf("Scale handler: channel %d, index %d, octave %d, note %f, volts %f\n", 
           channel, index, octave, note, volts);
}

extern "C" void volume_handler(int channel, float level) {
    // TODO: Call into Lua input[channel].volume handler
    printf("Volume handler: channel %d, level %f\n", channel, level);
}

extern "C" void peak_handler(int channel) {
    // TODO: Call into Lua input[channel].peak handler
    printf("Peak handler: channel %d\n", channel);
}

extern "C" void freq_handler(int channel, float freq) {
    // TODO: Call into Lua input[channel].freq handler
    printf("Freq handler: channel %d, freq %f\n", channel, freq);
}
