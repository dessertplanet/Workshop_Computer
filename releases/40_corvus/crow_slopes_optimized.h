#pragma once

#include "crow_slopes.h"
#include <stdint.h>

// Fixed-point optimization for slopes
// Uses 16.16 fixed-point format for internal calculations

// Fixed-point types and macros
typedef int32_t fix16_t;
#define FIX16_SHIFT 16
#define FIX16_ONE (1 << FIX16_SHIFT)
#define FIX16_HALF (FIX16_ONE >> 1)
#define FIX16_MAX 0x7FFFFFFF
#define FIX16_MIN 0x80000000

// Conversion macros
#define FLOAT_TO_FIX16(x) ((fix16_t)((x) * FIX16_ONE))
#define FIX16_TO_FLOAT(x) ((float)(x) / FIX16_ONE)
#define INT_TO_FIX16(x) ((x) << FIX16_SHIFT)
#define FIX16_TO_INT(x) ((x) >> FIX16_SHIFT)

// Fixed-point arithmetic macros
#define FIX16_MUL(a, b) (((int64_t)(a) * (b)) >> FIX16_SHIFT)
#define FIX16_DIV(a, b) (((int64_t)(a) << FIX16_SHIFT) / (b))
#define FIX16_FRAC(x) ((x) & (FIX16_ONE - 1))

// Lookup table sizes (power of 2 for efficient indexing)
#define SHAPE_LUT_BITS 8
#define SHAPE_LUT_SIZE (1 << SHAPE_LUT_BITS)
#define SHAPE_LUT_MASK (SHAPE_LUT_SIZE - 1)

// Lookup table data structures
typedef struct {
    int16_t values[SHAPE_LUT_SIZE];
    bool initialized;
} shape_lut_t;

// Global lookup tables
extern shape_lut_t sine_lut;
extern shape_lut_t exp_lut;
extern shape_lut_t log_lut;
extern shape_lut_t over_lut;
extern shape_lut_t under_lut;
extern shape_lut_t rebound_lut;

// Optimized slope structure for internal use
typedef struct {
    // Fixed-point internal state
    fix16_t here_fix;       // Current interpolation position (0.0-1.0 in 16.16)
    fix16_t delta_fix;      // Step size per sample in 16.16
    fix16_t scale_fix;      // Output scaling factor in 16.16
    fix16_t last_fix;       // Starting value in 16.16
    
    // Countdown remains float for compatibility
    float countdown;
    
    // Shape and callback
    crow_shape_t shape;
    crow_slope_callback_t action;
    int index;
    
    // Output cache
    float shaped;
    bool use_fixed_point;   // Performance mode flag
} optimized_slope_t;

// Initialization
void crow_slopes_opt_init(void);
void crow_slopes_opt_deinit(void);

// Lookup table generation
void generate_sine_lut(void);
void generate_exp_lut(void);
void generate_log_lut(void);
void generate_over_lut(void);
void generate_under_lut(void);
void generate_rebound_lut(void);

// Fast shape functions using lookup tables + interpolation
float crow_shape_sine_fast(float in);
float crow_shape_exp_fast(float in);
float crow_shape_log_fast(float in);
float crow_shape_over_fast(float in);
float crow_shape_under_fast(float in);
float crow_shape_rebound_fast(float in);

// Fixed-point block processing
void slopes_process_block_optimized(float* input_blocks[4], float* output_blocks[4], int block_size);

// Utility functions
fix16_t interpolate_lut(const int16_t* lut, fix16_t index_fix);
void slopes_motion_v_optimized(optimized_slope_t* slope, float* out, int block_size);
void slopes_shaper_v_optimized(optimized_slope_t* slope, float* out, int block_size);

// Performance monitoring
extern uint32_t slopes_opt_cycles_saved;
extern bool slopes_opt_enable_profiling;
