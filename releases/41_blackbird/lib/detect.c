#include "lib/detect.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "pico/stdlib.h"

#define DETECT_DEBUG 0

// Detection system state
static Detect_t* detectors = NULL;
static int detector_count = 0;

#define DETECT_CANARY 0xD37EC7u

// Detection processing sample rate (matches audio engine)
#define DETECT_SAMPLE_RATE 48000.0f
// Crow evaluates detectors once per 32-sample audio block. We still call
// Detect_process_sample() every sample for edge fidelity, but interval-based
// modes (stream / volume / peak) should interpret the user-specified interval
// the same way crow does: in units of 32-sample blocks. So we scale the
// interval by (sample_rate / block_size) rather than raw sample_rate.
#define DETECT_BLOCK_SIZE 32.0f
#define DETECT_BLOCK_RATE (DETECT_SAMPLE_RATE / DETECT_BLOCK_SIZE)

// VU Meter implementation
VU_meter_t* VU_init(void) {
    VU_meter_t* vu = (VU_meter_t*)malloc(sizeof(VU_meter_t));
    if (vu) {
        vu->level = 0.0f;
        vu->time_constant = 0.018f; // Default 18ms time constant
        vu->attack_coeff = 0.99f;   // Fast attack
        vu->release_coeff = 0.999f; // Slower release
    }
    return vu;
}

void VU_deinit(VU_meter_t* vu) {
    if (vu) {
        free(vu);
    }
}

void VU_time(VU_meter_t* vu, float time_seconds) {
    if (!vu) return;
    vu->time_constant = time_seconds;
    // Calculate coefficients based on time constant
    // Processing runs at the full audio sample rate
    float rate = DETECT_SAMPLE_RATE;
    vu->attack_coeff = expf(-1.0f / (time_seconds * rate * 0.1f)); // Fast attack
    vu->release_coeff = expf(-1.0f / (time_seconds * rate));       // Slower release
}

float VU_step(VU_meter_t* vu, float input) {
    if (!vu) return 0.0f;
    
    float abs_input = fabsf(input);
    
    if (abs_input > vu->level) {
        // Attack: fast response to rising levels
        vu->level = abs_input + vu->attack_coeff * (vu->level - abs_input);
    } else {
        // Release: slower response to falling levels
        vu->level = abs_input + vu->release_coeff * (vu->level - abs_input);
    }
    
    return vu->level;
}

// Forward declarations for detection mode functions
static void d_none(Detect_t* self, float level, bool block_boundary);
static void d_stream(Detect_t* self, float level, bool block_boundary);
static void d_change(Detect_t* self, float level, bool block_boundary);
static void d_window(Detect_t* self, float level, bool block_boundary);
static void d_scale(Detect_t* self, float level, bool block_boundary);
static void d_volume(Detect_t* self, float level, bool block_boundary);
static void d_peak(Detect_t* self, float level, bool block_boundary);

// Helper functions
static void scale_bounds(Detect_t* self, int ix, int oct);

void Detect_init(int channels) {
    detector_count = channels;
    detectors = (Detect_t*)calloc(channels, sizeof(Detect_t));
    
    for (int i = 0; i < channels; i++) {
        detectors[i].channel = i;
        detectors[i].last = 0.0f;
        detectors[i].state = 0;
        detectors[i].last_sample = 0.0f;
        detectors[i].canary = DETECT_CANARY;
        detectors[i].change_rise_count = 0;
        detectors[i].change_fall_count = 0;
        
        // CRITICAL: Initialize all detectors to "none" mode like real crow
        Detect_none(&detectors[i]);
    }
}

void Detect_deinit(void) {
    if (detectors) {
        free(detectors);
        detectors = NULL;
    }
    detector_count = 0;
}

Detect_t* Detect_ix_to_p(uint8_t index) {
    if (index < detector_count && detectors) {
        return &detectors[index];
    }
    return NULL;
}

int8_t Detect_str_to_dir(const char* str) {
    if (!str) return 0;
    
    if (strcmp(str, "rising") == 0 || strcmp(str, "up") == 0) {
        return 1;
    } else if (strcmp(str, "falling") == 0 || strcmp(str, "down") == 0) {
        return -1;
    } else if (strcmp(str, "both") == 0) {
        return 0;
    }
    
    return 0; // default to both
}

// Mode configuration functions
void Detect_none(Detect_t* self) {
    if (!self) return;
    self->modefn = d_none;
    self->action = NULL;
}

void Detect_stream(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    self->modefn = d_stream;
    self->action = cb;
    // Crow semantics: interval (seconds) * (sample_rate / 32)
    self->stream.blocks = (int)(interval * DETECT_BLOCK_RATE);
    if (self->stream.blocks <= 0) self->stream.blocks = 1;
    self->stream.countdown = self->stream.blocks;
}

void Detect_change(Detect_t* self, Detect_callback_t cb, float threshold, float hysteresis, int8_t direction) {
    if (!self) return;
    self->modefn = d_change;
    self->action = cb;
    self->change.threshold = threshold;
    self->change.hysteresis = hysteresis;
    // Safety clamp: extremely small or zero hysteresis leads to chatter/noise-triggered floods.
    // crow firmware enforces minimum hysteresis in some modes (e.g. scale); we mirror that spirit here.
    if (self->change.hysteresis < 0.001f) {
        self->change.hysteresis = 0.001f; // ~1mV in crow volts domain (adjust if scaling differs)
    }
    self->change.direction = direction;
    self->state = 0; // Reset state
}

void Detect_scale(Detect_t* self, Detect_callback_t cb, float* scale, int sLen, float divs, float scaling) {
    if (!self || !scale || sLen > SCALE_MAX_COUNT) return;
    
    self->modefn = d_scale;
    self->action = cb;
    
    D_scale_t* s = &self->scale;
    s->sLen = (sLen > SCALE_MAX_COUNT) ? SCALE_MAX_COUNT : sLen;
    s->divs = divs;
    s->scaling = scaling;
    
    if (sLen == 0) {
        // Assume chromatic
        s->sLen = (divs > SCALE_MAX_COUNT) ? SCALE_MAX_COUNT : (int)divs;
        for (int i = 0; i < s->sLen; i++) {
            s->scale[i] = (float)i;
        }
    } else {
        for (int i = 0; i < s->sLen; i++) {
            s->scale[i] = scale[i];
        }
    }
    
    // Calculate parameters
    s->offset = 0.5f * scaling / divs;
    s->win = scaling / ((float)s->sLen);
    s->hyst = s->win / 20.0f;
    if (s->hyst < 0.006f) s->hyst = 0.006f;
    
    // Set to invalid note initially
    scale_bounds(self, 0, -10);
}

void Detect_window(Detect_t* self, Detect_callback_t cb, float* windows, int wLen, float hysteresis) {
    if (!self || !windows || wLen > WINDOW_MAX_COUNT) return;
    
    self->modefn = d_window;
    self->action = cb;
    self->win.wLen = (wLen > WINDOW_MAX_COUNT) ? WINDOW_MAX_COUNT : wLen;
    self->win.hysteresis = hysteresis;
    self->win.lastWin = 0;
    
    for (int i = 0; i < self->win.wLen; i++) {
        self->win.windows[i] = windows[i];
    }
}

void Detect_volume(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    self->modefn = d_volume;
    self->action = cb;

    // Initialize VU meter if not already done
    if (!self->vu) {
        self->vu = VU_init();
        VU_time(self->vu, 0.018f); // 18ms time constant
    }

    // Crow semantics (see Detect_stream)
    self->volume.blocks = (int)(interval * DETECT_BLOCK_RATE);
    if (self->volume.blocks <= 0) self->volume.blocks = 1;
    self->volume.countdown = self->volume.blocks;
}

void Detect_peak(Detect_t* self, Detect_callback_t cb, float threshold, float hysteresis) {
    if (!self) return;
    self->modefn = d_peak;
    self->action = cb;

    // Initialize VU meter if not already done
    if (!self->vu) {
        self->vu = VU_init();
        VU_time(self->vu, 0.18f); // 180ms time constant for peak detection
    }

    self->peak.threshold = threshold;
    self->peak.hysteresis = hysteresis;
    self->peak.release = 0.01f;
    self->peak.envelope = 0.0f;
    self->state = 0; // Reset state
}

void Detect_freq(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    // Frequency detection not implemented - just stub for now
    self->modefn = d_none;
    self->action = cb;
}

// Detection mode processing functions
static void d_none(Detect_t* self, float level, bool block_boundary) {
    // Do nothing
    (void)level;
    (void)block_boundary;
    return;
}

static void d_stream(Detect_t* self, float level, bool block_boundary) {
    // Stream mode: Only decrement countdown on block boundaries for correct timing
    if (block_boundary) {
        if (--self->stream.countdown <= 0) {
            self->stream.countdown = self->stream.blocks;
            if (self->action) {
                (*self->action)(self->channel, level);
            }
        }
    }
}

static void d_change(Detect_t* self, float level, bool block_boundary) {
    // Change mode: Process every sample for sample-accurate edge detection
    (void)block_boundary; // Not used for change detection
    
    if (self->state) { // high to low
        if (level < (self->change.threshold - self->change.hysteresis)) {
            if (DETECT_DEBUG) {
                printf("[detect] ch%d FALL level=%.3f thresh=%.3f hyst=%.3f dir=%d\r\n",
                       self->channel, level, self->change.threshold, self->change.hysteresis, self->change.direction);
            }
            self->state = 0;
            self->change_fall_count++;
            if (self->change.direction != 1) { // not 'rising' only
                if (self->action) {
                    (*self->action)(self->channel, (float)self->state);
                }
            }
        }
    } else { // low to high
        if (level > (self->change.threshold + self->change.hysteresis)) {
            if (DETECT_DEBUG) {
                printf("[detect] ch%d RISE level=%.3f thresh=%.3f hyst=%.3f dir=%d\r\n",
                       self->channel, level, self->change.threshold, self->change.hysteresis, self->change.direction);
            }
            self->state = 1;
            self->change_rise_count++;
            if (self->change.direction != -1) { // not 'falling' only
                if (self->action) {
                    (*self->action)(self->channel, (float)self->state);
                }
            }
        }
    }
}

static void d_window(Detect_t* self, float level, bool block_boundary) {
    // Window mode: Process every sample for accurate threshold detection
    (void)block_boundary; // Not used for window detection
    
    // Find which window contains the level
    int ix = 0;
    for (; ix < self->win.wLen; ix++) {
        if (level < self->win.windows[ix]) {
            break;
        }
    }
    ix++; // 1-based index
    
    // Check if window has changed
    int lastWin = self->win.lastWin;
    if (ix != lastWin) {
        if (self->action) {
            (*self->action)(self->channel, (ix > lastWin) ? ix : -ix);
        }
        self->win.lastWin = ix;
    }
}

static void d_scale(Detect_t* self, float level, bool block_boundary) {
    // Scale mode: Process every sample for accurate note detection
    (void)block_boundary; // Not used for scale detection
    
    D_scale_t* s = &self->scale;
    
    if (level > s->upper || level < s->lower) {
        // Offset input to capture noisy notes at divisions
        level += s->offset;
        
        // Calculate scale position
        float norm = level / s->scaling;
        s->lastOct = (int)floorf(norm);
        float phase = norm - (float)s->lastOct;
        float fix = phase * s->sLen;
        s->lastIndex = (int)floorf(fix);
        
        // Ensure index is within bounds
        if (s->lastIndex >= s->sLen) s->lastIndex = s->sLen - 1;
        if (s->lastIndex < 0) s->lastIndex = 0;
        
        // Calculate output values
        float note = s->scale[s->lastIndex];
        s->lastNote = note + (float)s->lastOct * s->divs;
        s->lastVolts = (note / s->divs + (float)s->lastOct) * s->scaling;
        
        // Trigger callback
        if (self->action) {
            (*self->action)(self->channel, 0.0f); // Value is accessed via scale members
        }
        
        // Update bounds for next detection
        scale_bounds(self, s->lastIndex, s->lastOct);
    }
}

static void d_volume(Detect_t* self, float level, bool block_boundary) {
    if (self->vu) {
        level = VU_step(self->vu, level);
    }
    
    // Volume mode: Only decrement countdown on block boundaries for correct timing
    if (block_boundary) {
        if (--self->volume.countdown <= 0) {
            self->volume.countdown = self->volume.blocks;
            if (self->action) {
                (*self->action)(self->channel, level);
            }
        }
    }
}

static void d_peak(Detect_t* self, float level, bool block_boundary) {
    (void)block_boundary; // Process every sample for accurate peak detection
    
    if (self->vu) {
        level = VU_step(self->vu, level);
    }
    
    // Peak envelope processing
    if (level > self->last) {
        self->peak.envelope = level; // Instant attack
    } else {
        // Release with 1-pole filter
        self->peak.envelope = level + self->peak.release * (self->peak.envelope - level);
    }
    
    // Threshold detection with hysteresis
    if (self->state) { // high to low
        if (self->peak.envelope < (self->peak.threshold - self->peak.hysteresis)) {
            self->state = 0;
        }
    } else { // low to high
        if (self->peak.envelope > (self->peak.threshold + self->peak.hysteresis)) {
            self->state = 1;
            if (self->action) {
                (*self->action)(self->channel, 0.0f); // Peak detected
            }
        }
    }
    
    self->last = level;
}

// Helper function for scale bounds calculation
static void scale_bounds(Detect_t* self, int ix, int oct) {
    D_scale_t* s = &self->scale;
    
    // Find ideal voltage for this window
    float ideal = ((float)oct * s->scaling) + ix * s->win;
    ideal = ideal - s->offset;
    
    // Calculate bounds with hysteresis
    s->lower = ideal - s->hyst;
    s->upper = ideal + s->hyst + s->win;
}

// Main detection processing function - CRITICAL: Called from ProcessSample at audio rate
void __not_in_flash_func(Detect_process_sample)(int channel, float level) {
    if (channel >= detector_count || !detectors) return;
    
    Detect_t* detector = &detectors[channel];
    // Canary check
    if (detector->canary != DETECT_CANARY) {
        static int warned = 0;
        if (!warned) {
            printf("[detect] CANARY CORRUPTION on ch%d!\r\n", channel);
            warned = 1;
        }
        detector->canary = DETECT_CANARY; // restore to continue operation
    }
    
    // Track block boundaries for timing-based modes
    detector->samples_in_current_block++;
    bool block_boundary = (detector->samples_in_current_block >= DETECT_BLOCK_SIZE);
    if (block_boundary) {
        detector->samples_in_current_block = 0;
    }
    
    detector->last_sample = level;
    if (detector->modefn) {
        detector->modefn(detector, level, block_boundary);
    }
}
