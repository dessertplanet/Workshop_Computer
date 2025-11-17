#pragma once

#include <stdint.h>

// ========================================================================
// Q16.16 Fixed-Point System for RP2040 (Cortex-M0+ has no FPU)
// ========================================================================
// Q16.16 format: 16-bit integer, 16-bit fractional
// Range: ±32768 (covers ±6V with headroom for arithmetic overflow)
// Precision: 1/65536 ≈ 0.000015 (sub-millivolt precision)
// ========================================================================

typedef int32_t q16_t;

#define Q16_SHIFT 16
#define Q16_ONE (1 << Q16_SHIFT)        // 1.0 = 65536
#define Q16_HALF (1 << (Q16_SHIFT - 1))  // 0.5 = 32768

// Fast conversion macros with rounding
#define FLOAT_TO_Q16(f) ((q16_t)((f) * (float)Q16_ONE + ((f) >= 0.0f ? 0.5f : -0.5f)))
#define Q16_TO_FLOAT(q) ((float)(q) / (float)Q16_ONE)

// Q16.16 arithmetic (compiler will optimize these)
#define Q16_MUL(a, b) ((q16_t)(((int64_t)(a) * (int64_t)(b)) >> Q16_SHIFT))
#define Q16_DIV(a, b) ((q16_t)(((int64_t)(a) << Q16_SHIFT) / (int64_t)(b)))

// Q16 to Q12 conversion for 12-bit DAC output (±2048 range)
#define Q16_TO_Q12(q) ((int16_t)((q) >> (Q16_SHIFT - 12)))
#define Q12_TO_Q16(q) ((q16_t)((q) << (Q16_SHIFT - 12)))

// need an Init() fn. send SR as an argument
#define SAMPLE_RATE 48000
#define iSAMPLE_RATE (1.0/(float)SAMPLE_RATE)
#define SAMPLES_PER_MS ((float)SAMPLE_RATE/1000.0)
#define SAMPLES_PER_MS_Q16 FLOAT_TO_Q16(SAMPLES_PER_MS)  // Pre-calculated Q16 constant

typedef enum{ SHAPE_Linear
            , SHAPE_Sine
            , SHAPE_Log
            , SHAPE_Expo
            , SHAPE_Now
            , SHAPE_Wait
            , SHAPE_Over
            , SHAPE_Under
            , SHAPE_Rebound
} Shape_t;

typedef void (*Callback_t)(int channel);

typedef struct{
    int         index;
    
    // Q16.16 fixed-point state - eliminates FPU operations
    q16_t       dest_q16;      // destination voltage
    q16_t       last_q16;      // previous dest voltage
    q16_t       scale_q16;     // voltage range (dest - last)
    q16_t       shaped_q16;    // current shaped output voltage
    
    // Q16.16 interpolation state
    q16_t       here_q16;      // current interp position [0.0, 1.0]
    q16_t       delta_q16;     // increment per sample
    q16_t       countdown_q16; // samples until breakpoint (sub-sample precision)
    
    Shape_t     shape;
    Callback_t  action;
} Slope_t;

#define SLOPE_CHANNELS 4

// refactor for dynamic SLOPE_CHANNELS
// refactor for dynamic SAMPLE_RATE
// refactor to S_init returning pointers, but internally tracking indexes?

void S_init( int channels );

Shape_t S_str_to_shape( const char* s );

// Q16.16 fixed-point API (new, optimized)
void S_toward_q16( int        index
                 , q16_t      destination_q16
                 , q16_t      ms_q16
                 , Shape_t    shape
                 , Callback_t cb
                 );
q16_t S_get_state_q16( int index );

// Float API (legacy, wraps Q16 internally for backward compatibility)
float S_get_state( int index );
void S_toward( int        index
             , float      destination
             , float      ms
             , Shape_t    shape
             , Callback_t cb
             );
float* S_step_v( int     index
               , float*  out
               , int     size
               );

void S_reset(void);
