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


// TODO: Add missing shape function stubs
static float shapes_step_now(float in) { return (in >= 1.0f) ? 1.0f : 0.0f; }
static float shapes_step_wait(float in) { return (in <= 0.0f) ? 0.0f : 1.0f; }
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


////////////////////////////////
// private declarations
// Forward declarations for RAM-placed functions
static float* step_v( Slope_t* self, float* out, int size );
static float* static_v( Slope_t* self, float* out, int size );
static float* motion_v( Slope_t* self, float* out, int size );
static float* breakpoint_v( Slope_t* self, float* out, int size );
static float* shaper_v( Slope_t* self, float* out, int size );
static float shaper( Slope_t* self, float out );

////////////////////////////////
// public definitions

void S_init( int channels )
{
    // Initialize Q11 LUTs first for optimal performance
    init_shape_luts();
    
    slope_count = channels;
    slopes = malloc( sizeof ( Slope_t ) * channels );
    if( !slopes ){ printf("slopes malloc failed\n"); return; }
    for( int j=0; j<SLOPE_CHANNELS; j++ ){
        slopes[j].index  = j;
        slopes[j].dest   = 0.0;
        slopes[j].last   = 0.0;
        slopes[j].shape  = SHAPE_Linear;
        slopes[j].action = NULL;

        slopes[j].here   = 0.0;
        slopes[j].delta  = 0.0;
        slopes[j].countdown = -1.0;
        slopes[j].scale = 0.0;
        slopes[j].shaped = 0.0;
    }
}

void S_reset(void)
{
    if( !slopes ){
        return;
    }
    for( int j = 0; j < slope_count; j++ ){
        slopes[j].dest = 0.0f;
        slopes[j].last = 0.0f;
        slopes[j].shape = SHAPE_Linear;
        slopes[j].action = NULL;
        slopes[j].here = 0.0f;
        slopes[j].delta = 0.0f;
        slopes[j].countdown = -1.0f;
        slopes[j].scale = 0.0f;
        slopes[j].shaped = 0.0f;
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

float S_get_state( int index )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return 0.0; }
    Slope_t* self = &slopes[index]; // safe pointer
    return self->shaped;
}

// register a new destination
void S_toward( int        index
             , float      destination
             , float      ms
             , Shape_t    shape
             , Callback_t cb
             )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return; }
    Slope_t* self = &slopes[index]; // safe pointer

    // update destination
    self->dest   = destination;
    self->shape  = shape;
    self->action = cb;

    // direct update & callback if ms = 0 (ie instant)
    if( ms <= 0.0 ){
        self->last      = self->dest;
        self->shaped    = self->dest;
        self->scale     = 0.0;
        self->here      = 1.0; // hard set to end of range
        if(self->countdown > 0.0){
            // only happens when assynchronously updating S_toward
            self->countdown = -0.0; // inactive.
        }
        // Immediate hardware update for zero-time (instant) transitions
        // Apply quantization before hardware output
        extern float AShaper_quantize_single(int index, float voltage);
        float quantized = AShaper_quantize_single(index, self->shaped);
        extern void hardware_output_set_voltage(int channel, float voltage);
        hardware_output_set_voltage(index+1, quantized);
        
        // Schedule callback for instant transitions
        // Fire on next audio cycle to allow ASL sequences to continue
        if(self->action){
            self->countdown = 1.0; // Fire callback after 1 sample
        }
    } else {
        // save current output level as new starting point
        self->last   = self->shaped;
        self->scale  = self->dest - self->last;
        float overflow = 0.0;
        // CRITICAL FIX: Check for pending callbacks from instant transitions
        // If countdown is positive and small (e.g., 1.0 from ms=0 instant transition),
        // treat it as overflow time so the new slope starts from the correct position
        if( self->countdown > 0.0 && self->countdown < 100.0 ){
            // Pending instant callback - clear it and use as overflow
            overflow = self->countdown;
            self->action = NULL;  // Clear pending action since we're starting a new slope
        } else if( self->countdown < 0.0 && self->countdown > -1023.0 ){
            overflow = -(self->countdown);
        }
        self->countdown = ms * SAMPLES_PER_MS; // samples until callback
        self->delta     = 1.0 / self->countdown;
        self->here      = 0.0; // start of slope
        if( overflow > 0.0 ){
            self->here += overflow * self->delta;
            self->countdown -= overflow;
            if( self->countdown <= 0.0 ){ // guard against overflow hitting callback
                printf("FIXME near immediate callback\n");
                // FIXME this should apply the destination & call self->action
                self->countdown = 0.00001; // force callback on next sample
                self->here = 1.0; // set to destination
            }
        }
    }
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
    if( self->countdown <= 0.0 ){ // at destination
        static_v( self, out, size );
    } else if( self->countdown > (float)size ){ // no edge case
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
    out[size-1] = self->here;
    
    if( self->countdown > -1024.0 ){ // count overflow samples
        self->countdown -= (float)size;
    }
    return shaper_v( self, out, size );
}

// CRITICAL: Motion calculation in RAM - most common path for LFOs
__attribute__((section(".time_critical.motion_v")))
static float* motion_v( Slope_t* self, float* out, int size )
{
    // OPTIMIZATION: Only calculate final sample since we discard the rest
    // This reduces work by 87.5% for size=8 blocks
    
    if( self->scale == 0.0 || self->delta == 0.0 ){ // delay only
        // Static value, just use current position
        self->here = self->here; // No change needed
    } else {
        // Calculate final position after 'size' samples
        // Instead of iterating: for(i=0; i<size; i++) value += delta
        // Just do: value += delta * size
        self->here = self->here + (self->delta * (float)size);
    }
    
    // Store final value in last position for shaper_v to use
    out[size-1] = self->here;
    
    self->countdown -= (float)size;
    return shaper_v( self, out, size );
}

// CRITICAL: Breakpoint handler in RAM - handles slope transitions and callbacks
__attribute__((section(".time_critical.breakpoint_v")))
static float* breakpoint_v( Slope_t* self, float* out, int size )
{
    if( size <= 0 ){ return out; }

    self->here += self->delta; // no collision can happen on first samp

    self->countdown -= 1.0;
    if( self->countdown <= 0.0 ){
        // TODO unroll overshoot and apply proportionally to the post-*act sample
        self->here = 1.0; // clamp for overshoot
        if( self->action != NULL ){
            Callback_t act = self->action;
            self->action = NULL;
            self->shaped = self->dest; // save real destination into shaped to actually reach it
            (*act)(self->index);
            // side-affects: self->{dest, shape, action, countdown, delta, (here)}
        }
        if( self->action != NULL ){ // instant callback
            *out++ = shaper( self, self->here );
            // 1. unwind self->countdown (ADD it to countdown)
            // 2. recalc current sample with new slope
            // 3. below call should be on out[0] and size
            if(size > 1){
                return step_v( self, out, size-1 );
            } else { // handle breakpoint on last sample of frame
                return out;
            }
        } else { // slope complete, or queued response
            self->here  = 1.0;
            self->delta = 0.0;
            *out++ = shaper( self, self->here );
            return static_v( self, out, size-1 );
        }
    } else {
        *out++ = shaper( self, self->here );
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
    
    float final_value = out[size-1]; // Get the already-calculated final position
    
    // Apply shape function to final value only
    switch( self->shape ){
        case SHAPE_Sine:    final_value = shapes_sin( final_value ); break;
        case SHAPE_Log:     final_value = shapes_log( final_value ); break;
        case SHAPE_Expo:    final_value = shapes_exp( final_value ); break;
        case SHAPE_Now:     final_value = shapes_step_now( final_value ); break;
        case SHAPE_Wait:    final_value = shapes_step_wait( final_value ); break;
        case SHAPE_Over:    final_value = shapes_ease_out_back( final_value ); break;
        case SHAPE_Under:   final_value = shapes_ease_in_back( final_value ); break;
        case SHAPE_Rebound: final_value = shapes_ease_out_rebound( final_value ); break;
        case SHAPE_Linear: 
        default: break; // Linear - no transformation needed
    }
    
    // Map to output range
    final_value = (final_value * self->scale) + self->last;
    
    // Save last state
    self->shaped = final_value;
    
    // Apply quantization before hardware output
    extern float AShaper_quantize_single(int index, float voltage);
    float quantized = AShaper_quantize_single(self->index, self->shaped);
    
    // Update hardware output directly for real-time response
    extern void hardware_output_set_voltage(int channel, float voltage);
    hardware_output_set_voltage(self->index + 1, quantized);  // Convert to 1-based channel
    
    return out;
}

// CRITICAL: Single-sample shaper in RAM - used by breakpoint_v for callbacks
// single sample for breakpoint segment
__attribute__((section(".time_critical.shaper")))
static float shaper( Slope_t* self, float out )
{
    switch( self->shape ){
        case SHAPE_Sine:    out = shapes_sin( out ); break;
        case SHAPE_Log:     out = shapes_log( out ); break;
        case SHAPE_Expo:    out = shapes_exp( out ); break;
        case SHAPE_Now:     out = shapes_step_now( out ); break;
        case SHAPE_Wait:    out = shapes_step_wait( out ); break;
        case SHAPE_Over:    out = shapes_ease_out_back( out ); break;
        case SHAPE_Under:   out = shapes_ease_in_back( out ); break;
        case SHAPE_Rebound: out = shapes_ease_out_rebound( out ); break;
        case SHAPE_Linear: default: break; // Linear falls through
    }
    // map to output range
    out = (out * self->scale) + self->last;
    // save last state
    self->shaped = out;
    
    // Apply quantization before hardware output
    extern float AShaper_quantize_single(int index, float voltage);
    float quantized = AShaper_quantize_single(self->index, self->shaped);
    
    // Update hardware output directly for immediate response
    extern void hardware_output_set_voltage(int channel, float voltage);
    hardware_output_set_voltage(self->index + 1, quantized);  // Convert to 1-based channel
    
    return out;
}
