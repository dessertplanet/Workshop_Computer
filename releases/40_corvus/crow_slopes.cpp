#include "crow_slopes.h"
#include "crow_lua.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>

extern "C" {
    #include "wrBlocks.h"
}

// Forward declarations for internal functions
static void slopes_motion_v(crow_slope_t* slope, float* out, int block_size);
static void slopes_breakpoint_v(crow_slope_t* slope, float* out, int block_size);
static void slopes_shaper_v(crow_slope_t* slope, float* out, int block_size);

// Global state
static crow_slope_t slopes[CROW_SLOPE_CHANNELS];
static bool slopes_initialized = false;

// Math constants
#define M_PI   3.141592653589793f
#define M_PI_2 (M_PI/2.0f)

// Initialize slopes system
void crow_slopes_init(void) {
    if (slopes_initialized) {
        return;
    }
    
    printf("Initializing crow slopes system (%d channels)...\n", CROW_SLOPE_CHANNELS);
    
    for (int i = 0; i < CROW_SLOPE_CHANNELS; i++) {
        slopes[i].index = i;
        slopes[i].dest = 0.0f;
        slopes[i].last = 0.0f;
        slopes[i].shape = CROW_SHAPE_Linear;
        slopes[i].action = nullptr;
        
        slopes[i].here = 0.0f;
        slopes[i].delta = 0.0f;
        slopes[i].countdown = -1.0f;  // Inactive
        slopes[i].scale = 0.0f;
        slopes[i].shaped = 0.0f;
    }
    
    slopes_initialized = true;
    printf("Crow slopes system initialized\n");
}

void crow_slopes_deinit(void) {
    slopes_initialized = false;
}

// Convert string to shape type
crow_shape_t crow_str_to_shape(const char* str) {
    if (!str) return CROW_SHAPE_Linear;
    
    char c = str[0];
    if (c >= 'A' && c <= 'Z') c += 32; // Convert to lowercase
    
    switch (c) {
        case 's': return CROW_SHAPE_Sine;
        case 'e': return CROW_SHAPE_Expo;
        case 'n': return CROW_SHAPE_Now;
        case 'w': return CROW_SHAPE_Wait;
        case 'o': return CROW_SHAPE_Over;
        case 'u': return CROW_SHAPE_Under;
        case 'r': return CROW_SHAPE_Rebound;
        case 'l': 
            if (str[1] == 'o' || str[1] == 'O') return CROW_SHAPE_Log;
            // Fall through to default
        default: return CROW_SHAPE_Linear;
    }
}

// Get current output state
float crow_slopes_get_state(int channel) {
    if (!slopes_initialized || channel < 0 || channel >= CROW_SLOPE_CHANNELS) {
        return 0.0f;
    }
    return slopes[channel].shaped;
}

// Start a slope toward destination
void crow_slopes_toward(int channel, float destination, float ms, 
                       crow_shape_t shape, crow_slope_callback_t callback) {
    if (!slopes_initialized || channel < 0 || channel >= CROW_SLOPE_CHANNELS) {
        printf("crow_slopes_toward: invalid channel %d\n", channel);
        return;
    }
    
    crow_slope_t* slope = &slopes[channel];
    
    // Update parameters
    slope->dest = destination;
    slope->shape = shape;
    slope->action = callback;
    
    // Handle instant updates (ms = 0)
    if (ms <= 0.0f) {
        slope->last = slope->dest;
        slope->shaped = slope->dest;
        slope->scale = 0.0f;
        slope->here = 1.0f;
        slope->countdown = -1.0f; // Inactive
        
        // Call callback immediately if present
        if (slope->action) {
            crow_slope_callback_t action = slope->action;
            slope->action = nullptr;
            action(channel);
        }
        return;
    }
    
    // Set up slope calculation
    slope->last = slope->shaped; // Start from current position
    slope->scale = slope->dest - slope->last;
    slope->countdown = ms * CROW_SAMPLES_PER_MS;
    slope->delta = 1.0f / slope->countdown;
    slope->here = 0.0f;
    
    printf("Slope %d: %.3fV -> %.3fV over %.1fms (%s)\n", 
           channel, slope->last, slope->dest, ms, 
           (shape == CROW_SHAPE_Linear) ? "linear" : "shaped");
}

// Get current output voltage for a channel
float crow_slopes_get_output(int channel) {
    if (!slopes_initialized || channel < 0 || channel >= CROW_SLOPE_CHANNELS) {
        return 0.0f;
    }
    return slopes[channel].shaped;
}

// Process one sample for all channels
void crow_slopes_process_sample(void) {
    if (!slopes_initialized) {
        return;
    }
    
    // TODO: Phase 2 - Convert to vector operations using wrDsp
    // TODO: Replace per-sample processing with crow_slopes_process_block()
    // TODO: Use wrDsp b_add/b_mul functions for vector envelope processing
    // FUTURE: Process 32 samples at once for 3-5x performance improvement
    
    for (int i = 0; i < CROW_SLOPE_CHANNELS; i++) {
        crow_slope_t* slope = &slopes[i];
        
        if (slope->countdown <= 0.0f) {
            // Slope is inactive or completed
            continue;
        }
        
        // Update interpolation position
        slope->here += slope->delta;
        slope->countdown -= 1.0f;
        
        // Check for completion
        if (slope->countdown <= 0.0f) {
            slope->here = 1.0f; // Clamp to endpoint
            
            // Apply final shaping and scaling
            float shaped_value;
            switch (slope->shape) {
                case CROW_SHAPE_Sine:    shaped_value = crow_shape_sine(slope->here); break;
                case CROW_SHAPE_Log:     shaped_value = crow_shape_log(slope->here); break;
                case CROW_SHAPE_Expo:    shaped_value = crow_shape_exp(slope->here); break;
                case CROW_SHAPE_Now:     shaped_value = crow_shape_now(slope->here); break;
                case CROW_SHAPE_Wait:    shaped_value = crow_shape_wait(slope->here); break;
                case CROW_SHAPE_Over:    shaped_value = crow_shape_over(slope->here); break;
                case CROW_SHAPE_Under:   shaped_value = crow_shape_under(slope->here); break;
                case CROW_SHAPE_Rebound: shaped_value = crow_shape_rebound(slope->here); break;
                case CROW_SHAPE_Linear:
                default:                 shaped_value = slope->here; break;
            }
            
            slope->shaped = (shaped_value * slope->scale) + slope->last;
            slope->countdown = -1.0f; // Mark as inactive
            
            // Call completion callback if present
            if (slope->action) {
                crow_slope_callback_t action = slope->action;
                slope->action = nullptr;
                action(i);
            }
        } else {
            // Apply shaping and scaling for intermediate sample
            float shaped_value;
            switch (slope->shape) {
                case CROW_SHAPE_Sine:    shaped_value = crow_shape_sine(slope->here); break;
                case CROW_SHAPE_Log:     shaped_value = crow_shape_log(slope->here); break;
                case CROW_SHAPE_Expo:    shaped_value = crow_shape_exp(slope->here); break;
                case CROW_SHAPE_Now:     shaped_value = crow_shape_now(slope->here); break;
                case CROW_SHAPE_Wait:    shaped_value = crow_shape_wait(slope->here); break;
                case CROW_SHAPE_Over:    shaped_value = crow_shape_over(slope->here); break;
                case CROW_SHAPE_Under:   shaped_value = crow_shape_under(slope->here); break;
                case CROW_SHAPE_Rebound: shaped_value = crow_shape_rebound(slope->here); break;
                case CROW_SHAPE_Linear:
                default:                 shaped_value = slope->here; break;
            }
            
            slope->shaped = (shaped_value * slope->scale) + slope->last;
        }
    }
}

// Process a block of samples for all channels using vector operations
// This is the Phase 2 vector processing implementation
void crow_slopes_process_block(float* input_blocks[4], float* output_blocks[4], int block_size) {
    if (!slopes_initialized) {
        // Zero output blocks if not initialized
        for (int ch = 0; ch < CROW_SLOPE_CHANNELS; ch++) {
            b_cp(output_blocks[ch], 0.0f, block_size);
        }
        return;
    }
    
    // Process each channel's slope using vector operations
    for (int ch = 0; ch < CROW_SLOPE_CHANNELS; ch++) {
        crow_slope_t* slope = &slopes[ch];
        float* out = output_blocks[ch];
        
        if (slope->countdown <= 0.0f) {
            // Slope is inactive - output constant value
            b_cp(out, slope->shaped, block_size);
            if (slope->countdown > -1024.0f) {
                slope->countdown -= (float)block_size; // Count overflow samples
            }
        } else if (slope->countdown > (float)block_size) {
            // No callback within this block - pure motion
            slopes_motion_v(slope, out, block_size);
        } else {
            // Callback occurs within this block - handle breakpoint
            slopes_breakpoint_v(slope, out, block_size);
        }
    }
}

// Vector motion processing (no callback in this block)
static void slopes_motion_v(crow_slope_t* slope, float* out, int block_size) {
    if (slope->scale == 0.0f || slope->delta == 0.0f) {
        // Delay only - constant output
        b_cp(out, slope->here, block_size);
    } else {
        // Generate linear interpolation ramp
        out[0] = slope->here + slope->delta;
        for (int i = 1; i < block_size; i++) {
            out[i] = out[i-1] + slope->delta;
        }
    }
    
    slope->countdown -= (float)block_size;
    slope->here = out[block_size - 1];
    
    // Apply shaping and scaling using vector operations
    slopes_shaper_v(slope, out, block_size);
}

// Vector breakpoint processing (callback occurs within block)
static void slopes_breakpoint_v(crow_slope_t* slope, float* out, int block_size) {
    int callback_sample = (int)slope->countdown;
    
    // Process samples before callback
    if (callback_sample > 0) {
        for (int i = 0; i < callback_sample && i < block_size; i++) {
            slope->here += slope->delta;
            out[i] = slope->here;
        }
        slope->countdown -= (float)callback_sample;
    }
    
    // Handle callback
    if (callback_sample < block_size) {
        slope->here = 1.0f; // Clamp to endpoint
        
        // Trigger callback
        if (slope->action) {
            crow_slope_callback_t action = slope->action;
            slope->action = nullptr;
            slope->shaped = slope->dest; // Ensure we reach destination
            action(slope->index);
        }
        
        // Fill remaining samples with final value
        for (int i = callback_sample; i < block_size; i++) {
            out[i] = slope->here;
        }
        
        slope->countdown = -1.0f; // Mark inactive
        slope->delta = 0.0f;
    }
    
    // Apply shaping and scaling
    slopes_shaper_v(slope, out, block_size);
}

// Vector shaping operations
static void slopes_shaper_v(crow_slope_t* slope, float* out, int block_size) {
    // Apply shape curve using vector operations where possible
    switch (slope->shape) {
        case CROW_SHAPE_Sine:
            // Apply sine shaping using b_map with function pointer
            b_map(crow_shape_sine, out, block_size);
            break;
            
        case CROW_SHAPE_Log:
            // Apply log shaping
            b_map(crow_shape_log, out, block_size);
            break;
            
        case CROW_SHAPE_Expo:
            // Apply exponential shaping  
            b_map(crow_shape_exp, out, block_size);
            break;
            
        case CROW_SHAPE_Linear:
            // No shaping needed
            break;
            
        default:
            // Apply other shapes per-sample (fallback)
            for (int i = 0; i < block_size; i++) {
                switch (slope->shape) {
                    case CROW_SHAPE_Now:     out[i] = crow_shape_now(out[i]); break;
                    case CROW_SHAPE_Wait:    out[i] = crow_shape_wait(out[i]); break;
                    case CROW_SHAPE_Over:    out[i] = crow_shape_over(out[i]); break;
                    case CROW_SHAPE_Under:   out[i] = crow_shape_under(out[i]); break;
                    case CROW_SHAPE_Rebound: out[i] = crow_shape_rebound(out[i]); break;
                }
            }
            break;
    }
    
    // Apply scaling and offset using wrDsp vector operations:
    // output = (shaped_value * scale) + last
    b_add(b_mul(out, slope->scale, block_size), slope->last, block_size);
    
    // Save final state
    slope->shaped = out[block_size - 1];
}

// Shape functions (all normalized 0.0-1.0 input/output)

float crow_shape_linear(float in) {
    return in;
}

float crow_shape_sine(float in) {
    return -0.5f * (cosf(M_PI * in) - 1.0f);
}

float crow_shape_exp(float in) {
    return powf(2.0f, 10.0f * (in - 1.0f));
}

float crow_shape_log(float in) {
    return 1.0f - powf(2.0f, -10.0f * in);
}

float crow_shape_now(float in) {
    return 1.0f;  // Jump immediately to end value
}

float crow_shape_wait(float in) {
    return (in < 0.99999f) ? 0.0f : 1.0f;  // Stay at start until very end
}

float crow_shape_over(float in) {
    // Ease out back (overshoot)
    float in_1 = in - 1.0f;
    return in_1 * in_1 * (2.70158f * in_1 + 1.70158f) + 1.0f;
}

float crow_shape_under(float in) {
    // Ease in back (undershoot)
    return in * in * (2.70158f * in - 1.70158f);
}

float crow_shape_rebound(float in) {
    // Bounce effect
    if (in < (1.0f/2.75f)) {
        return 7.5625f * in * in;
    } else if (in < (2.0f/2.75f)) {
        float in_c = in - 1.5f/2.75f;
        return 7.5625f * in_c * in_c + 0.75f;
    } else if (in < (2.5f/2.75f)) {
        float in_c = in - 2.25f/2.75f;
        return 7.5625f * in_c * in_c + 0.9375f;
    } else {
        float in_c = in - 2.625f/2.75f;
        return 7.5625f * in_c * in_c + 0.984375f;
    }
}
