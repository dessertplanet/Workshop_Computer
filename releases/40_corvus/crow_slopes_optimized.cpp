#include "crow_slopes_optimized.h"
#include "crow_slopes.h"
#include <cmath>
#include <cstring>
#include <cstdio>

// Global lookup tables
shape_lut_t sine_lut = {0};
shape_lut_t exp_lut = {0};
shape_lut_t log_lut = {0};
shape_lut_t over_lut = {0};
shape_lut_t under_lut = {0};
shape_lut_t rebound_lut = {0};

// Performance monitoring
uint32_t slopes_opt_cycles_saved = 0;
bool slopes_opt_enable_profiling = false;

// Internal optimized slopes (parallel to original slopes array)
static optimized_slope_t opt_slopes[CROW_SLOPE_CHANNELS];
static bool opt_initialized = false;

void crow_slopes_opt_init(void) {
    if (opt_initialized) return;
    
    printf("Initializing optimized slopes system...\n");
    
    // Generate lookup tables
    generate_sine_lut();
    generate_exp_lut();
    generate_log_lut();
    generate_over_lut();
    generate_under_lut();
    generate_rebound_lut();
    
    // Initialize optimized slopes
    for (int i = 0; i < CROW_SLOPE_CHANNELS; i++) {
        opt_slopes[i].here_fix = 0;
        opt_slopes[i].delta_fix = 0;
        opt_slopes[i].scale_fix = 0;
        opt_slopes[i].last_fix = 0;
        opt_slopes[i].countdown = -1.0f;
        opt_slopes[i].shape = CROW_SHAPE_Linear;
        opt_slopes[i].action = nullptr;
        opt_slopes[i].index = i;
        opt_slopes[i].shaped = 0.0f;
        opt_slopes[i].use_fixed_point = true;  // Enable by default
    }
    
    opt_initialized = true;
    printf("Optimized slopes system initialized with lookup tables\n");
}

void crow_slopes_opt_deinit(void) {
    opt_initialized = false;
}

// Generate sine lookup table
void generate_sine_lut(void) {
    if (sine_lut.initialized) return;
    
    for (int i = 0; i < SHAPE_LUT_SIZE; i++) {
        float x = (float)i / (float)(SHAPE_LUT_SIZE - 1);
        float sine_val = -0.5f * (cosf(M_PI * x) - 1.0f);
        // Store as 16-bit fixed point (0.16 format for 0.0-1.0 range)
        sine_lut.values[i] = (int16_t)(sine_val * 32767.0f);
    }
    sine_lut.initialized = true;
}

// Generate exponential lookup table
void generate_exp_lut(void) {
    if (exp_lut.initialized) return;
    
    for (int i = 0; i < SHAPE_LUT_SIZE; i++) {
        float x = (float)i / (float)(SHAPE_LUT_SIZE - 1);
        float exp_val = powf(2.0f, 10.0f * (x - 1.0f));
        exp_lut.values[i] = (int16_t)(exp_val * 32767.0f);
    }
    exp_lut.initialized = true;
}

// Generate logarithmic lookup table
void generate_log_lut(void) {
    if (log_lut.initialized) return;
    
    for (int i = 0; i < SHAPE_LUT_SIZE; i++) {
        float x = (float)i / (float)(SHAPE_LUT_SIZE - 1);
        float log_val = 1.0f - powf(2.0f, -10.0f * x);
        log_lut.values[i] = (int16_t)(log_val * 32767.0f);
    }
    log_lut.initialized = true;
}

// Generate over (ease out back) lookup table
void generate_over_lut(void) {
    if (over_lut.initialized) return;
    
    for (int i = 0; i < SHAPE_LUT_SIZE; i++) {
        float x = (float)i / (float)(SHAPE_LUT_SIZE - 1);
        float x_1 = x - 1.0f;
        float over_val = x_1 * x_1 * (2.70158f * x_1 + 1.70158f) + 1.0f;
        over_lut.values[i] = (int16_t)(over_val * 32767.0f);
    }
    over_lut.initialized = true;
}

// Generate under (ease in back) lookup table
void generate_under_lut(void) {
    if (under_lut.initialized) return;
    
    for (int i = 0; i < SHAPE_LUT_SIZE; i++) {
        float x = (float)i / (float)(SHAPE_LUT_SIZE - 1);
        float under_val = x * x * (2.70158f * x - 1.70158f);
        under_lut.values[i] = (int16_t)(under_val * 32767.0f);
    }
    under_lut.initialized = true;
}

// Generate rebound (bounce) lookup table
void generate_rebound_lut(void) {
    if (rebound_lut.initialized) return;
    
    for (int i = 0; i < SHAPE_LUT_SIZE; i++) {
        float x = (float)i / (float)(SHAPE_LUT_SIZE - 1);
        float rebound_val;
        
        if (x < (1.0f/2.75f)) {
            rebound_val = 7.5625f * x * x;
        } else if (x < (2.0f/2.75f)) {
            float x_c = x - 1.5f/2.75f;
            rebound_val = 7.5625f * x_c * x_c + 0.75f;
        } else if (x < (2.5f/2.75f)) {
            float x_c = x - 2.25f/2.75f;
            rebound_val = 7.5625f * x_c * x_c + 0.9375f;
        } else {
            float x_c = x - 2.625f/2.75f;
            rebound_val = 7.5625f * x_c * x_c + 0.984375f;
        }
        
        rebound_lut.values[i] = (int16_t)(rebound_val * 32767.0f);
    }
    rebound_lut.initialized = true;
}

// Linear interpolation between lookup table entries
fix16_t interpolate_lut(const int16_t* lut, fix16_t index_fix) {
    // Extract integer and fractional parts
    int index_int = FIX16_TO_INT(index_fix) & SHAPE_LUT_MASK;
    fix16_t frac = FIX16_FRAC(index_fix);
    
    // Get table values (convert from 16-bit to 16.16 fixed point)
    fix16_t val0 = INT_TO_FIX16(lut[index_int]) >> 1;  // Shift to avoid overflow
    fix16_t val1 = INT_TO_FIX16(lut[(index_int + 1) & SHAPE_LUT_MASK]) >> 1;
    
    // Linear interpolation: val0 + frac * (val1 - val0)
    return val0 + FIX16_MUL(frac, val1 - val0);
}

// Fast shape functions using lookup tables
float crow_shape_sine_fast(float in) {
    if (!sine_lut.initialized) generate_sine_lut();
    
    fix16_t index_fix = FLOAT_TO_FIX16(in * (SHAPE_LUT_SIZE - 1));
    fix16_t result_fix = interpolate_lut(sine_lut.values, index_fix);
    return FIX16_TO_FLOAT(result_fix * 2); // Compensate for earlier shift
}

float crow_shape_exp_fast(float in) {
    if (!exp_lut.initialized) generate_exp_lut();
    
    fix16_t index_fix = FLOAT_TO_FIX16(in * (SHAPE_LUT_SIZE - 1));
    fix16_t result_fix = interpolate_lut(exp_lut.values, index_fix);
    return FIX16_TO_FLOAT(result_fix * 2);
}

float crow_shape_log_fast(float in) {
    if (!log_lut.initialized) generate_log_lut();
    
    fix16_t index_fix = FLOAT_TO_FIX16(in * (SHAPE_LUT_SIZE - 1));
    fix16_t result_fix = interpolate_lut(log_lut.values, index_fix);
    return FIX16_TO_FLOAT(result_fix * 2);
}

float crow_shape_over_fast(float in) {
    if (!over_lut.initialized) generate_over_lut();
    
    fix16_t index_fix = FLOAT_TO_FIX16(in * (SHAPE_LUT_SIZE - 1));
    fix16_t result_fix = interpolate_lut(over_lut.values, index_fix);
    return FIX16_TO_FLOAT(result_fix * 2);
}

float crow_shape_under_fast(float in) {
    if (!under_lut.initialized) generate_under_lut();
    
    fix16_t index_fix = FLOAT_TO_FIX16(in * (SHAPE_LUT_SIZE - 1));
    fix16_t result_fix = interpolate_lut(under_lut.values, index_fix);
    return FIX16_TO_FLOAT(result_fix * 2);
}

float crow_shape_rebound_fast(float in) {
    if (!rebound_lut.initialized) generate_rebound_lut();
    
    fix16_t index_fix = FLOAT_TO_FIX16(in * (SHAPE_LUT_SIZE - 1));
    fix16_t result_fix = interpolate_lut(rebound_lut.values, index_fix);
    return FIX16_TO_FLOAT(result_fix * 2);
}

// Optimized motion processing using fixed-point arithmetic
void slopes_motion_v_optimized(optimized_slope_t* slope, float* out, int block_size) {
    if (!slope->use_fixed_point) {
        // Fallback to original floating point processing
        return; // Would call original function here
    }
    
    if (slope->scale_fix == 0 || slope->delta_fix == 0) {
        // Constant output - use fast fill
        float const_val = FIX16_TO_FLOAT(slope->here_fix);
        for (int i = 0; i < block_size; i++) {
            out[i] = const_val;
        }
        return;
    }
    
    // Generate linear ramp using fixed-point arithmetic
    fix16_t here_fix = slope->here_fix;
    for (int i = 0; i < block_size; i++) {
        here_fix += slope->delta_fix;
        out[i] = FIX16_TO_FLOAT(here_fix);
    }
    
    slope->here_fix = here_fix;
    slope->countdown -= (float)block_size;
}

// Optimized shaper processing
void slopes_shaper_v_optimized(optimized_slope_t* slope, float* out, int block_size) {
    if (!slope->use_fixed_point) {
        // Use original floating point shapes for accuracy if needed
        return;
    }
    
    // Apply shape using fast lookup tables
    switch (slope->shape) {
        case CROW_SHAPE_Sine:
            for (int i = 0; i < block_size; i++) {
                out[i] = crow_shape_sine_fast(out[i]);
            }
            break;
            
        case CROW_SHAPE_Expo:
            for (int i = 0; i < block_size; i++) {
                out[i] = crow_shape_exp_fast(out[i]);
            }
            break;
            
        case CROW_SHAPE_Log:
            for (int i = 0; i < block_size; i++) {
                out[i] = crow_shape_log_fast(out[i]);
            }
            break;
            
        case CROW_SHAPE_Over:
            for (int i = 0; i < block_size; i++) {
                out[i] = crow_shape_over_fast(out[i]);
            }
            break;
            
        case CROW_SHAPE_Under:
            for (int i = 0; i < block_size; i++) {
                out[i] = crow_shape_under_fast(out[i]);
            }
            break;
            
        case CROW_SHAPE_Rebound:
            for (int i = 0; i < block_size; i++) {
                out[i] = crow_shape_rebound_fast(out[i]);
            }
            break;
            
        case CROW_SHAPE_Now:
            // Jump to end immediately
            for (int i = 0; i < block_size; i++) {
                out[i] = 1.0f;
            }
            break;
            
        case CROW_SHAPE_Wait:
            // Stay at start until very end
            for (int i = 0; i < block_size; i++) {
                out[i] = (out[i] < 0.99999f) ? 0.0f : 1.0f;
            }
            break;
            
        case CROW_SHAPE_Linear:
        default:
            // No shaping needed
            break;
    }
    
    // Apply scaling and offset using fixed-point for precision
    fix16_t scale_fix = slope->scale_fix;
    fix16_t last_fix = slope->last_fix;
    
    for (int i = 0; i < block_size; i++) {
        fix16_t shaped_fix = FLOAT_TO_FIX16(out[i]);
        fix16_t result_fix = FIX16_MUL(shaped_fix, scale_fix) + last_fix;
        out[i] = FIX16_TO_FLOAT(result_fix);
    }
    
    // Cache final output
    slope->shaped = out[block_size - 1];
}

// Main optimized block processing function
void slopes_process_block_optimized(float* input_blocks[4], float* output_blocks[4], int block_size) {
    if (!opt_initialized) {
        crow_slopes_opt_init();
    }
    
    // Process each channel using optimized fixed-point arithmetic
    for (int ch = 0; ch < CROW_SLOPE_CHANNELS; ch++) {
        optimized_slope_t* slope = &opt_slopes[ch];
        float* out = output_blocks[ch];
        
        if (slope->countdown <= 0.0f) {
            // Slope inactive - constant output
            for (int i = 0; i < block_size; i++) {
                out[i] = slope->shaped;
            }
            if (slope->countdown > -1024.0f) {
                slope->countdown -= (float)block_size;
            }
        } else if (slope->countdown > (float)block_size) {
            // Pure motion - no callback in this block
            slopes_motion_v_optimized(slope, out, block_size);
            slopes_shaper_v_optimized(slope, out, block_size);
        } else {
            // Callback occurs within block
            int callback_sample = (int)slope->countdown;
            
            // Process samples before callback
            if (callback_sample > 0 && callback_sample < block_size) {
                // Partial block before callback
                float temp_out[callback_sample];
                slopes_motion_v_optimized(slope, temp_out, callback_sample);
                slopes_shaper_v_optimized(slope, temp_out, callback_sample);
                
                // Copy to output
                for (int i = 0; i < callback_sample; i++) {
                    out[i] = temp_out[i];
                }
            }
            
            // Handle callback
            slope->here_fix = FIX16_ONE; // Clamp to endpoint
            slope->shaped = FIX16_TO_FLOAT(slope->last_fix + slope->scale_fix);
            
            // Fill remaining samples with final value
            for (int i = callback_sample; i < block_size; i++) {
                out[i] = slope->shaped;
            }
            
            slope->countdown = -1.0f;
            slope->delta_fix = 0;
            
            // Post completion event if needed
            if (slope->action) {
                crow_slope_callback_t action = slope->action;
                slope->action = nullptr;
                // Would post event here - crow_event_post_slope_complete(slope->index, action);
            }
        }
    }
}
