#include "slopes.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

// TODO: Port STM32 dependencies to RP2040
// #include "stm32f7xx.h" // STM32-specific, removed

#include "ashapes.h" // Use our local ashapes instead of shapes
// TODO: Port wrDsp dependency to RP2040  
// #include "submodules/wrDsp/wrBlocks.h" // wrDsp dependency, need to stub

// TODO: Implement RP2040-specific audio sample rate constants
#ifndef SAMPLES_PER_MS
#define SAMPLES_PER_MS 48.0f // TODO: Define proper sample rate for RP2040
#endif

#ifndef SLOPE_CHANNELS  
#define SLOPE_CHANNELS 4 // TODO: Define proper channel count for Workshop Computer
#endif

// ========================================================================
// Q11 Fixed-Point LUT System for RP2040 (Cortex-M0+ has no FPU)
// ========================================================================
// Q11 format: signed 12-bit in 16-bit container (-2048 to +2047)
// Directly matches bipolar 12-bit DAC: -2048 = -6V, 0 = 0V, +2047 = +6V
// Performance: ~40-60 cycles vs ~1500+ cycles for powf()
// Memory: 1.5KB vs 3KB for float LUTs (50% savings)
// ========================================================================

#define LUT_SIZE 256
#define Q11_MAX 2047      // 2^11 - 1
#define Q11_MIN -2048     // -2^11
#define Q11_SCALE 2047.0f // Scale factor for [0,1] → Q11

typedef int16_t q11_t;

// Shape LUTs in Q11 format - aligned for fast access on RP2040
static q11_t lut_sin[LUT_SIZE] __attribute__((aligned(4)));
static q11_t lut_exp[LUT_SIZE] __attribute__((aligned(4)));
static q11_t lut_log[LUT_SIZE] __attribute__((aligned(4)));
static bool luts_initialized = false;

// Convert float [0.0, 1.0] to Q11 [0, 2047]
static inline q11_t float_to_q11(float x) {
    if (x >= 1.0f) return Q11_MAX;
    if (x <= 0.0f) return 0;
    return (q11_t)(x * Q11_SCALE + 0.5f);  // +0.5 for rounding
}

// Convert Q11 [0, 2047] to float [0.0, 1.0]
static inline float q11_to_float(q11_t x) {
    return (float)x / Q11_SCALE;
}

// Initialize shape LUTs - called once at startup
static void init_shape_luts(void) {
    if (luts_initialized) return;
    
    printf("Initializing Q11 shape LUTs for RP2040...\n");
    
    for (int i = 0; i < LUT_SIZE; i++) {
        float t = (float)i / (float)(LUT_SIZE - 1);  // 0.0 to 1.0
        
        // Pre-calculate expensive shape functions (done once at startup)
        // These return values in [0, 1] range
        float sin_val = -0.5f * (cosf(M_PI * t) - 1.0f);
        float exp_val = powf(2.0f, 10.0f * (t - 1.0f));
        float log_val = 1.0f - powf(2.0f, -10.0f * t);
        
        // Convert to Q11 format
        lut_sin[i] = float_to_q11(sin_val);
        lut_exp[i] = float_to_q11(exp_val);
        lut_log[i] = float_to_q11(log_val);
    }
    
    luts_initialized = true;
    printf("Q11 Shape LUTs initialized: %d entries, %d bytes total\n", 
           LUT_SIZE, LUT_SIZE * 3 * sizeof(q11_t));
    printf("  Memory savings vs float: %d bytes (%.1f%% reduction)\n",
           LUT_SIZE * 3 * (sizeof(float) - sizeof(q11_t)),
           (1.0f - (float)sizeof(q11_t) / sizeof(float)) * 100.0f);
    printf("  Expected speedup: ~30-50x for exp/log, ~10x for sin\n");
}

// Export LUT table pointers for S_step_one_sample_q16 in ISR
const q11_t* lut_sin_ptr = NULL;
const q11_t* lut_log_ptr = NULL;
const q11_t* lut_exp_ptr = NULL;

// Ultra-fast Q11 LUT lookup with linear interpolation
// Performance: ~40-60 cycles on Cortex-M0+ (vs ~1500+ for powf())
// CRITICAL HOT PATH: Place in RAM for deterministic timing
__attribute__((section(".time_critical.lut_lookup_q11")))
static float lut_lookup_q11(const q11_t* lut, float in) {
    // Clamp input to [0, 1] range
    in = (in < 0.0f) ? 0.0f : ((in > 1.0f) ? 1.0f : in);
    
    // Convert to fixed-point index with 8-bit sub-precision
    // This avoids float multiply in the interpolation hot path
    uint32_t fidx = (uint32_t)(in * (LUT_SIZE - 1) * 256.0f);
    uint32_t idx = fidx >> 8;          // Table index (integer part)
    uint32_t frac = fidx & 0xFF;       // Fractional part (0-255)
    
    // Load two Q11 values from LUT
    int32_t v0 = (int32_t)lut[idx];      // Sign-extend to 32-bit
    int32_t v1 = (int32_t)lut[idx + 1];
    
    // Linear interpolation in fixed-point
    // result = v0 + (frac/256) * (v1 - v0)
    int32_t delta = v1 - v0;
    int32_t result = v0 + ((delta * (int32_t)frac) >> 8);  // Scale back down
    
    // Convert Q11 result back to float [0, 1]
    return (float)result / Q11_SCALE;
}

// Q16-native LUT lookup - eliminates redundant float conversions in hot path
// Directly converts Q16 input to Q16 output via Q11 LUT
// Performance: Same as lut_lookup_q11 but saves 2 float conversions per call
__attribute__((section(".time_critical.lut_lookup_q16")))
static inline q16_t lut_lookup_q16(const q11_t* lut, q16_t in_q16) {
    // Clamp Q16 input to [0, 1] range (0 to Q16_ONE)
    if (in_q16 <= 0) return 0;
    if (in_q16 >= Q16_ONE) in_q16 = Q16_ONE - 1;  // Prevent overflow
    
    // Convert Q16 [0, Q16_ONE] to fixed-point index with 8-bit sub-precision
    // fidx = in_q16 * (LUT_SIZE - 1) / Q16_ONE * 256
    // Simplified: fidx = (in_q16 * (LUT_SIZE - 1) * 256) >> Q16_SHIFT
    uint32_t fidx = ((uint32_t)in_q16 * (LUT_SIZE - 1) * 256) >> Q16_SHIFT;
    uint32_t idx = fidx >> 8;          // Table index (integer part)
    uint32_t frac = fidx & 0xFF;       // Fractional part (0-255)
    
    // Load two Q11 values from LUT
    int32_t v0 = (int32_t)lut[idx];      // Sign-extend to 32-bit
    int32_t v1 = (int32_t)lut[idx + 1];
    
    // Linear interpolation in fixed-point
    int32_t delta = v1 - v0;
    int32_t result_q11 = v0 + ((delta * (int32_t)frac) >> 8);
    
    // Convert Q11 [0, 2047] to Q16 [0, Q16_ONE]
    // Q16 = Q11 * (Q16_ONE / Q11_MAX) = Q11 * (65536 / 2047)
    // Use shift instead: Q16 = Q11 << (Q16_SHIFT - 11) with rounding
    return (q16_t)((result_q11 << (Q16_SHIFT - 11)) + ((result_q11 * 65536) / 2047 - (result_q11 << (Q16_SHIFT - 11))));
}


// TODO: Add missing shape function stubs
// Q16.16 step helpers avoid float conversion for common gate-like shapes
static inline q16_t shapes_step_now_q16(q16_t here_q16)
{
    // Output stays at 0 until we reach the end of the segment
    // Equivalent to: (in >= 1.0f) ? 1.0f : 0.0f;
    return (here_q16 >= Q16_ONE) ? Q16_ONE : 0;
}

static inline q16_t shapes_step_wait_q16(q16_t here_q16)
{
    // Output is 0 while here > 0, and jumps to 1 at the end.
    // Equivalent to: (in <= 0.0f) ? 0.0f : 1.0f;
    return (here_q16 <= 0) ? 0 : Q16_ONE;
}

// The more complex back/rebound shapes still use float helpers for now.
// They remain stubs (identity) until fully implemented.
static float shapes_ease_out_back(float in) { return in; } // TODO: Implement proper back easing
static float shapes_ease_in_back(float in) { return in; } // TODO: Implement proper back easing  
static float shapes_ease_out_rebound(float in) { return in; } // TODO: Implement proper rebound

// Missing shape functions using local wrBlocks - similar to crow implementation
#include <math.h>
#include "wrblocks.h"

#ifndef M_PI
#define M_PI (3.141592653589793)
#endif

// Single sample shape functions - NOW USING Q11 LUTs for 30-50x speedup!
// HOT PATH: In RAM for consistent timing during high-frequency slope processing
__attribute__((section(".time_critical.shapes_sin")))
static float shapes_sin(float in) {
    if (!luts_initialized) {
        // Fallback to slow math if LUTs not ready (should never happen)
        return -0.5f * (cosf(M_PI * in) - 1.0f);
    }
    return lut_lookup_q11(lut_sin, in);
}

__attribute__((section(".time_critical.shapes_exp")))
static float shapes_exp(float in) {
    if (!luts_initialized) {
        // Fallback to slow math if LUTs not ready (should never happen)
        return powf(2.0f, 10.0f * (in - 1.0f));
    }
    return lut_lookup_q11(lut_exp, in);
}

__attribute__((section(".time_critical.shapes_log")))
static float shapes_log(float in) {
    if (!luts_initialized) {
        // Fallback to slow math if LUTs not ready (should never happen)
        return 1.0f - powf(2.0f, -10.0f * in);
    }
    return lut_lookup_q11(lut_log, in);
}

// Helper function for pow2
static float pow2(float in) { 
    return powf(2.0, in); 
}

// Vector shape functions using wrBlocks - matching crow implementation
static float* shapes_v_sin(float* in, int size) {
    return b_mul(
            b_add(
                b_map(cosf,
                    b_mul(in, M_PI, size)
                     , size)
                 , -1.0
                 , size)
                , -0.5
                , size);
}

static float* shapes_v_exp(float* in, int size) {
    return b_map(pow2,
            b_mul(
                b_add(in, -1.0, size)
                 , 10.0
                 , size)
                , size);
}

static float* shapes_v_log(float* in, int size) {
    return b_sub(
            b_map(pow2,
                b_mul(in, -10.0, size)
                 , size)
                , 1.0
                , size);
}


////////////////////////////////
// global vars

static uint8_t slope_count = 0;
Slope_t* slopes = NULL; // Exported for ll_timers.c conditional processing

// Compute normalized progress (0-1 in Q16) from elapsed samples
static inline q16_t slope_progress_from_elapsed(const Slope_t* self)
{
    if( self->duration_q16 <= 0 ){
        return (self->elapsed_q16 >= 0) ? Q16_ONE : 0;
    }

    int64_t elapsed = self->elapsed_q16;
    if( elapsed <= 0 ){
        return 0;
    }
    if( elapsed >= self->duration_q16 ){
        return Q16_ONE;
    }
    return (q16_t)(((elapsed << Q16_SHIFT)) / self->duration_q16);
}

// Advance the slope by 'samples_q16' (Q16 samples) and refresh cached progress
static inline void slope_advance(Slope_t* self, int64_t samples_q16)
{
    if( samples_q16 <= 0 ){
        return;
    }

    if( self->duration_q16 <= 0 ){
        // Zero-duration slews still use countdown for callback scheduling
        self->countdown_q16 -= samples_q16;
        if( self->countdown_q16 < 0 ){
            self->countdown_q16 = 0;
        }
        return;
    }

    self->elapsed_q16 += samples_q16;
    if( self->elapsed_q16 > self->duration_q16 ){
        self->elapsed_q16 = self->duration_q16;
    }

    self->countdown_q16 -= samples_q16;
    if( self->countdown_q16 < 0 ){
        self->countdown_q16 = 0;
    }

    self->here_q16 = slope_progress_from_elapsed(self);
}


////////////////////////////////
// private declarations
// Forward declarations for RAM-placed functions
static float* step_v( Slope_t* self, float* out, int size );
static float* static_v( Slope_t* self, float* out, int size );
static float* motion_v( Slope_t* self, float* out, int size );
static float* breakpoint_v( Slope_t* self, float* out, int size );
static float* shaper_v( Slope_t* self, float* out, int size );
// shaper_v now applies the shape directly; no separate shaper() helper.

////////////////////////////////
// public definitions

void S_init( int channels )
{
    // Initialize Q11 LUTs first for optimal performance
    init_shape_luts();
    
    // Export LUT pointers for S_step_one_sample_q16 in Core 1 ISR
    lut_sin_ptr = lut_sin;
    lut_log_ptr = lut_log;
    lut_exp_ptr = lut_exp;
    
    slope_count = channels;
    slopes = malloc( sizeof ( Slope_t ) * channels );
    if( !slopes ){ printf("slopes malloc failed\n"); return; }
    for( int j=0; j<SLOPE_CHANNELS; j++ ){
        slopes[j].index  = j;
        slopes[j].dest_q16   = 0;
        slopes[j].last_q16   = 0;
        slopes[j].scale_q16  = 0;
        slopes[j].shaped_q16 = 0;
        slopes[j].shape  = SHAPE_Linear;
        slopes[j].action = NULL;

        slopes[j].here_q16      = 0;
        slopes[j].countdown_q16 = -(int64_t)Q16_ONE;  // -1.0 in Q16
        slopes[j].duration_q16  = 0;
        slopes[j].elapsed_q16   = 0;
    }
}

void S_reset(void)
{
    if( !slopes ){
        return;
    }
    for( int j = 0; j < slope_count; j++ ){
        slopes[j].dest_q16 = 0;
        slopes[j].last_q16 = 0;
        slopes[j].scale_q16 = 0;
        slopes[j].shaped_q16 = 0;
        slopes[j].shape = SHAPE_Linear;
        slopes[j].action = NULL;
        slopes[j].here_q16 = 0;
        slopes[j].countdown_q16 = -(int64_t)Q16_ONE;  // -1.0 in Q16
        slopes[j].duration_q16 = 0;
        slopes[j].elapsed_q16 = 0;
    }
}

Shape_t S_str_to_shape( const char* s )
{
    char ps = (char)*s;
    if( ps < 0x61 ){ ps += 0x20; } // convert upper to lowercase
    switch( ps ){ // match on first char unless necessary
        case 's': return SHAPE_Sine;
        case 'e': return SHAPE_Expo;
        case 'n': return SHAPE_Now;
        case 'w': return SHAPE_Wait;
        case 'o': return SHAPE_Over;
        case 'u': return SHAPE_Under;
        case 'r': return SHAPE_Rebound;
        case 'l': if( s[1]=='o' ){ return SHAPE_Log; } // else flows through
        default: return SHAPE_Linear; // unmatched
    }
}

// Q16 API - returns fixed-point voltage
q16_t S_get_state_q16( int index )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return 0; }
    Slope_t* self = &slopes[index]; // safe pointer
    return self->shaped_q16;
}

// Float API - wraps Q16 for backward compatibility
float S_get_state( int index )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return 0.0; }
    return Q16_TO_FLOAT(slopes[index].shaped_q16);
}

// Single-sample slope processing for Core 1 ISR
// Returns shaped, quantized output voltage in Q16 format
// CRITICAL: Place in RAM for deterministic ISR timing
__attribute__((section(".time_critical.S_step_one_sample_q16")))
q16_t S_step_one_sample_q16(int index)
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return 0; }
    Slope_t* self = &slopes[index];
    
    // Check if slope is active (countdown > 0)
    if( self->countdown_q16 <= 0 ) {
        // Slope finished - return last output without processing
        return self->shaped_q16;
    }
    
    // Advance countdown by 1 sample
    self->countdown_q16--;
    self->elapsed_q16++;
    
    if( self->elapsed_q16 > self->duration_q16 ) {
        self->elapsed_q16 = self->duration_q16;
    }
    
    // Calculate progress [0.0, 1.0] in Q16 format
    q16_t here_q16;
    if( self->duration_q16 <= 0 ) {
        // Instant transition - already at destination
        here_q16 = Q16_ONE;
    } else if( self->elapsed_q16 <= 0 ) {
        here_q16 = 0;
    } else if( self->elapsed_q16 >= self->duration_q16 ) {
        here_q16 = Q16_ONE;
    } else {
        // Progress = elapsed / duration (in Q16)
        here_q16 = (q16_t)(((self->elapsed_q16 << Q16_SHIFT)) / self->duration_q16);
    }
    self->here_q16 = here_q16;
    
    // Apply shape function (pure Q16 integer math)
    q16_t shaped_q16;
    extern const q11_t* lut_sin_ptr;
    extern const q11_t* lut_log_ptr;
    extern const q11_t* lut_exp_ptr;
    extern q16_t lut_lookup_q16(const q11_t* lut, q16_t in_q16);
    
    switch( self->shape ) {
        case SHAPE_Sine:
            shaped_q16 = lut_lookup_q16(lut_sin_ptr, here_q16);
            break;
        case SHAPE_Log:
            shaped_q16 = lut_lookup_q16(lut_log_ptr, here_q16);
            break;
        case SHAPE_Expo:
            shaped_q16 = lut_lookup_q16(lut_exp_ptr, here_q16);
            break;
        case SHAPE_Now:
            shaped_q16 = (here_q16 >= Q16_ONE) ? Q16_ONE : 0;
            break;
        case SHAPE_Wait:
            shaped_q16 = (here_q16 <= 0) ? 0 : Q16_ONE;
            break;
        case SHAPE_Linear:
        default:
            shaped_q16 = here_q16;
            break;
    }
    
    // Map to output range: shaped * scale + last (Q16 arithmetic)
    q16_t voltage_q16 = Q16_MUL(shaped_q16, self->scale_q16) + self->last_q16;
    self->shaped_q16 = voltage_q16;
    
    // Apply quantization
    extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
    q16_t quantized_q16 = AShaper_quantize_single_q16(index, voltage_q16);
    
    // Update hardware output
    extern void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16);
    hardware_output_set_voltage_q16(index + 1, quantized_q16);
    
    // Check for action callback at end of slope
    if( self->countdown_q16 == 0 && self->action != NULL ) {
        // Queue callback to Core 0 via event system
        extern void queue_slope_action_callback(int channel);
        queue_slope_action_callback(index);
    }
    
    return quantized_q16;
}

// Q16.16 Fixed-Point Slope Engine - Core Implementation
// All arithmetic in integer math for 5-6x performance improvement on RP2040
void S_toward_q16( int        index
                 , q16_t      destination_q16
                 , q16_t      ms_q16
                 , Shape_t    shape
                 , Callback_t cb
                 )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return; }
    Slope_t* self = &slopes[index]; // safe pointer

    // update destination
    self->dest_q16  = destination_q16;
    self->shape     = shape;
    self->action    = cb;

    // direct update & callback if ms = 0 (ie instant)
    if( ms_q16 <= 0 ){
        self->last_q16      = self->dest_q16;
        self->shaped_q16    = self->dest_q16;
        self->scale_q16     = 0;
        self->here_q16      = Q16_ONE; // 1.0 in Q16 - end of range
        self->duration_q16  = 0;
        self->elapsed_q16   = 0;
        if(self->countdown_q16 > 0){
            // only happens when assynchronously updating S_toward
            self->countdown_q16 = 0; // inactive
        }
        // Immediate hardware update for zero-time (instant) transitions
        // Apply quantization before hardware output
        extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
        q16_t quantized_q16 = AShaper_quantize_single_q16(index, self->shaped_q16);
        extern void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16);
        hardware_output_set_voltage_q16(index+1, quantized_q16);  // Direct Q16, no conversion!
        
        // Schedule callback for instant transitions
        // Fire on next audio cycle to allow ASL sequences to continue
        if(self->action){
            self->countdown_q16 = Q16_ONE; // Fire callback after 1 sample
        }
    } else {
        // save current output level as new starting point
        self->last_q16  = self->shaped_q16;
        self->scale_q16 = self->dest_q16 - self->last_q16;
        q16_t overflow_q16 = 0;

        // CRITICAL FIX: Check for pending callbacks from instant transitions
        // If countdown is positive and small (e.g., 1.0 from ms=0 instant transition),
        // treat it as overflow time so the new slope starts from the correct position
        const int64_t threshold_100_q16 = ((int64_t)100 << Q16_SHIFT);   // 100.0 in Q16
        const int64_t threshold_1023_q16 = ((int64_t)1023 << Q16_SHIFT); // 1023.0 in Q16

        if( self->countdown_q16 > 0 && self->countdown_q16 < threshold_100_q16 ){
            // Pending instant callback - clear it and use as overflow
            overflow_q16 = (q16_t)self->countdown_q16;
            self->action = NULL;  // Clear pending action since we're starting a new slope
        } else if( self->countdown_q16 < 0 && self->countdown_q16 > -threshold_1023_q16 ){
            overflow_q16 = (q16_t)(-self->countdown_q16);
        }

        // Convert ms to samples: ms * SAMPLES_PER_MS
        // Use wide math so we can support multi-second slews without overflow
        int64_t samples_q16 = Q16_MUL_WIDE(ms_q16, SAMPLES_PER_MS_Q16);
        if( samples_q16 <= 0 ){
            // Never allow zero or negative sample windows (would div/0)
            samples_q16 = (int64_t)Q16_ONE; // minimum of 1 sample
        }

        self->duration_q16  = samples_q16;
        self->countdown_q16 = samples_q16;
        self->elapsed_q16   = 0;
        self->here_q16      = 0; // start of slope

        if( overflow_q16 > 0 ){
            slope_advance(self, (int64_t)overflow_q16);
            if( self->countdown_q16 <= 0 ){ // guard against overflow hitting callback
                printf("FIXME near immediate callback\n");
                // FIXME this should apply the destination & call self->action
                self->countdown_q16 = (int64_t)(Q16_ONE >> 16); // force callback on next sample (0.00001)
                self->here_q16 = Q16_ONE; // set to destination
            }
        }
    }
}

// Float API wrapper - converts float to Q16, calls Q16 implementation
void S_toward( int        index
             , float      destination
             , float      ms
             , Shape_t    shape
             , Callback_t cb
             )
{
    S_toward_q16(index, 
                 FLOAT_TO_Q16(destination), 
                 FLOAT_TO_Q16(ms), 
                 shape, 
                 cb);
}

// CRITICAL: Place in RAM - called from Timer_Process_Block at high frequency
__attribute__((section(".time_critical.S_step_v")))
float* S_step_v( int     index
               , float*  out
               , int     size
               )
{
    // turn index into pointer
    if( index < 0 || index >= SLOPE_CHANNELS ){ return out; }
    Slope_t* self = &slopes[index]; // safe pointer

    return step_v( self, out, size );
}


///////////////////////
// private defns

// CRITICAL: Dispatcher in RAM for consistent timing
__attribute__((section(".time_critical.step_v")))
static float* step_v( Slope_t* self
                    , float*   out
                    , int      size
                    )
{
    if( self->countdown_q16 <= 0 ){ // at destination (Q16 comparison)
        static_v( self, out, size );
    } else if( self->countdown_q16 > ((int64_t)size << Q16_SHIFT) ){ // no edge case (size as Q16)
        motion_v( self, out, size );
    } else {
        breakpoint_v( self, out, size );
    }
    return out;
}

// CRITICAL: Static value handler in RAM
__attribute__((section(".time_critical.static_v")))
static float* static_v( Slope_t* self, float* out, int size )
{
    // OPTIMIZATION: Only set final sample since we discard the rest
    // Skip the loop - value is static anyway
    out[size-1] = Q16_TO_FLOAT(self->here_q16);  // Convert Q16 to float for buffer
    
    int64_t threshold_q16 = -((int64_t)1024 << Q16_SHIFT); // -1024.0 in Q16
    if( self->countdown_q16 > threshold_q16 ){ // count overflow samples
        self->countdown_q16 -= ((int64_t)size << Q16_SHIFT); // size as Q16
    }
    return shaper_v( self, out, size );
}

// CRITICAL: Motion calculation in RAM - most common path for LFOs
__attribute__((section(".time_critical.motion_v")))
static float* motion_v( Slope_t* self, float* out, int size )
{
    // OPTIMIZATION: Only calculate final sample since we discard the rest
    // This reduces work by 87.5% for size=8 blocks
    
    // Advance by the whole block in one shot (Q16 precision, 64-bit safe)
    slope_advance(self, ((int64_t)size << Q16_SHIFT));
    
    // Store final value (convert Q16 to float for buffer compatibility)
    out[size-1] = Q16_TO_FLOAT(self->here_q16);
    
    return shaper_v( self, out, size );
}

// CRITICAL: Breakpoint handler in RAM - handles slope transitions and callbacks
__attribute__((section(".time_critical.breakpoint_v")))
static float* breakpoint_v( Slope_t* self, float* out, int size )
{
    if( size <= 0 ){ return out; }

    slope_advance(self, (int64_t)Q16_ONE); // Advance by one sample
    if( self->countdown_q16 <= 0 ){
        // TODO unroll overshoot and apply proportionally to the post-*act sample
        self->here_q16 = Q16_ONE; // clamp for overshoot (1.0 in Q16)
        if( self->action != NULL ){
            Callback_t act = self->action;
            self->action = NULL;
            self->shaped_q16 = self->dest_q16; // save real destination
            (*act)(self->index);
            // side-affects: self->{dest, shape, action, countdown, delta, (here)}
        }
        if( self->action != NULL ){ // instant callback
            *out++ = Q16_TO_FLOAT(self->here_q16);
            // 1. unwind self->countdown (ADD it to countdown)
            // 2. recalc current sample with new slope
            // 3. below call should be on out[0] and size
            if(size > 1){
                return step_v( self, out, size-1 );
            } else { // handle breakpoint on last sample of frame
                return out;
            }
        } else { // slope complete, or queued response
            self->here_q16  = Q16_ONE; // 1.0 in Q16
            *out++ = Q16_TO_FLOAT(self->here_q16);
            return static_v( self, out, size-1 );
        }
    } else {
        *out++ = Q16_TO_FLOAT(self->here_q16);
        return breakpoint_v( self, out, size-1 ); // recursive call
    }
}


///////////////////////////////
// shapers

// CRITICAL: Shape application in RAM - applies expensive sin/exp/log
// vectors for optimized segments (assume: self->shape is constant)
__attribute__((section(".time_critical.shaper_v")))
static float* shaper_v( Slope_t* self, float* out, int size )
{
    // OPTIMIZATION: Only process final sample since we discard the rest
    // This avoids expensive vectorized processing of 7 unused samples

    q16_t here_q16 = self->here_q16; // Already in Q16 [0.0, 1.0]
    q16_t shaped_q16;

    // Apply shape function. For the common step shapes we stay in Q16,
    // for others we still use the existing float-based helpers.
    switch( self->shape ){
        case SHAPE_Sine:
            shaped_q16 = lut_lookup_q16(lut_sin, here_q16);  // Direct Q16→Q16, no float conversions!
            break;
        case SHAPE_Log:
            shaped_q16 = lut_lookup_q16(lut_log, here_q16);  // Direct Q16→Q16, no float conversions!
            break;
        case SHAPE_Expo:
            shaped_q16 = lut_lookup_q16(lut_exp, here_q16);  // Direct Q16→Q16, no float conversions!
            break;
        case SHAPE_Now:
            shaped_q16 = shapes_step_now_q16(here_q16);
            break;
        case SHAPE_Wait:
            shaped_q16 = shapes_step_wait_q16(here_q16);
            break;
        case SHAPE_Over:
            shaped_q16 = FLOAT_TO_Q16(shapes_ease_out_back(Q16_TO_FLOAT(here_q16)));
            break;
        case SHAPE_Under:
            shaped_q16 = FLOAT_TO_Q16(shapes_ease_in_back(Q16_TO_FLOAT(here_q16)));
            break;
        case SHAPE_Rebound:
            shaped_q16 = FLOAT_TO_Q16(shapes_ease_out_rebound(Q16_TO_FLOAT(here_q16)));
            break;
        case SHAPE_Linear:
        default:
            shaped_q16 = here_q16; // Linear shape already in Q16
            break;
    }

    // Map to output range: shaped * scale + last (all Q16 arithmetic)
    q16_t voltage_q16 = Q16_MUL(shaped_q16, self->scale_q16) + self->last_q16;

    // Save last state
    self->shaped_q16 = voltage_q16;

    // Apply quantization before hardware output
    extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
    q16_t quantized_q16 = AShaper_quantize_single_q16(self->index, voltage_q16);

    // Update hardware output directly for real-time response
    extern void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16);
    hardware_output_set_voltage_q16(self->index + 1, quantized_q16);  // Direct Q16, no conversion!

    return out;
}
