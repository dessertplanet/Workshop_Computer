#include "lib/detect.h"
#include <stdlib.h>
#include <string.h>

// Stub implementations for RP2040 build

static Detect_t* detectors = NULL;
static int detector_count = 0;

void Detect_init(int channels) {
    detector_count = channels;
    detectors = (Detect_t*)calloc(channels, sizeof(Detect_t));
    
    for (int i = 0; i < channels; i++) {
        detectors[i].channel = i;
        detectors[i].modefn = NULL;
        detectors[i].action = NULL;
        detectors[i].last = 0.0f;
        detectors[i].state = 0;
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

// Stub mode functions - minimal implementations
void Detect_none(Detect_t* self) {
    if (!self) return;
    self->modefn = NULL;
    self->action = NULL;
}

void Detect_stream(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    self->action = cb;
    self->stream.blocks = (int)(interval * 48000.0f); // assuming 48kHz sample rate
    self->stream.countdown = 0;
}

void Detect_change(Detect_t* self, Detect_callback_t cb, float threshold, float hysteresis, int8_t direction) {
    if (!self) return;
    self->action = cb;
    self->change.threshold = threshold;
    self->change.hysteresis = hysteresis;
    self->change.direction = direction;
}

void Detect_scale(Detect_t* self, Detect_callback_t cb, float* scale, int sLen, float divs, float scaling) {
    if (!self || !scale || sLen > SCALE_MAX_COUNT) return;
    
    self->action = cb;
    self->scale.sLen = sLen;
    self->scale.divs = divs;
    self->scale.scaling = scaling;
    
    for (int i = 0; i < sLen && i < SCALE_MAX_COUNT; i++) {
        self->scale.scale[i] = scale[i];
    }
}

void Detect_window(Detect_t* self, Detect_callback_t cb, float* windows, int wLen, float hysteresis) {
    if (!self || !windows || wLen > WINDOW_MAX_COUNT) return;
    
    self->action = cb;
    self->win.wLen = wLen;
    self->win.hysteresis = hysteresis;
    
    for (int i = 0; i < wLen && i < WINDOW_MAX_COUNT; i++) {
        self->win.windows[i] = windows[i];
    }
}

void Detect_volume(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    self->action = cb;
    self->volume.blocks = (int)(interval * 48000.0f);
    self->volume.countdown = 0;
}

void Detect_peak(Detect_t* self, Detect_callback_t cb, float threshold, float hysteresis) {
    if (!self) return;
    self->action = cb;
    self->peak.threshold = threshold;
    self->peak.hysteresis = hysteresis;
    self->peak.release = 0.9f; // default release
    self->peak.envelope = 0.0f;
}

void Detect_freq(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    self->action = cb;
    // Stub implementation - frequency detection would need more complex logic
}
