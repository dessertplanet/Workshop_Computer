#pragma once

#include <stdint.h>

// Sample rate configuration (matching ComputerCard)
#define CROW_SAMPLE_RATE 48000
#define CROW_iSAMPLE_RATE (1.0f/(float)CROW_SAMPLE_RATE)
#define CROW_SAMPLES_PER_MS ((float)CROW_SAMPLE_RATE/1000.0f)

// Shape types for envelopes and LFOs
typedef enum {
    CROW_SHAPE_Linear = 0,
    CROW_SHAPE_Sine,
    CROW_SHAPE_Log,
    CROW_SHAPE_Expo,
    CROW_SHAPE_Now,
    CROW_SHAPE_Wait,
    CROW_SHAPE_Over,
    CROW_SHAPE_Under,
    CROW_SHAPE_Rebound
} crow_shape_t;

// Callback function type for slope completion
typedef void (*crow_slope_callback_t)(int channel);

// Slope state structure (one per output channel)
typedef struct {
    int index;                    // Channel index (0-3)
    
    // Destination parameters
    float dest;                   // Target voltage
    float last;                   // Previous target (starting point)
    crow_shape_t shape;           // Curve shape
    crow_slope_callback_t action; // Completion callback
    
    // Runtime state
    float here;                   // Current interpolation position (0.0-1.0)
    float delta;                  // Increment per sample
    float countdown;              // Samples remaining until completion
    
    // Cached calculations
    float scale;                  // Voltage range (dest - last)
    float shaped;                 // Current shaped output voltage
} crow_slope_t;

#define CROW_SLOPE_CHANNELS 4

// Core slopes API
void crow_slopes_init(void);
void crow_slopes_deinit(void);

// Shape conversion
crow_shape_t crow_str_to_shape(const char* str);

// Slope control
float crow_slopes_get_state(int channel);
void crow_slopes_toward(int channel, float destination, float ms, 
                       crow_shape_t shape, crow_slope_callback_t callback);

// Real-time processing
void crow_slopes_process_sample(void);
float crow_slopes_get_output(int channel);

// Shape functions (normalized 0.0-1.0 input/output)
float crow_shape_linear(float in);
float crow_shape_sine(float in);
float crow_shape_log(float in);
float crow_shape_exp(float in);
float crow_shape_now(float in);
float crow_shape_wait(float in);
float crow_shape_over(float in);
float crow_shape_under(float in);
float crow_shape_rebound(float in);
