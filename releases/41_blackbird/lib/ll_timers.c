#include "ll_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pico/stdlib.h"

// Timer implementation for RP2040 Workshop Computer with block processing optimization
// Aligned block size (32 samples) for consistent timing with audio processing

typedef struct {
    timer_callback_t callback;
    float period_seconds;
    bool active;
    uint32_t period_samples;      // Period in 48kHz samples
    uint64_t next_trigger_sample; // When to trigger next (64-bit for long-running systems)
    float period_error;           // Accumulated fractional sample error for precision
} timer_t;

static timer_t* timers = NULL;
static int max_timers = 0;
static uint64_t global_sample_counter = 0; // Incremented every ProcessSample() - 64-bit for precision

// Block processing state - aligned with audio blocks for consistent timing
static int sample_accumulator = 0; // Count samples until next block processing

void Timer_Init(int num_timers) {
    max_timers = num_timers;
    if (timers) {
        free(timers);
    }
    timers = malloc(sizeof(timer_t) * max_timers);
    
    for (int i = 0; i < max_timers; i++) {
        timers[i].callback = NULL;
        timers[i].period_seconds = 1.0f;
        timers[i].active = false;
        timers[i].period_samples = 48000; // Default 1 second at 48kHz
        timers[i].next_trigger_sample = 0;
    }
    global_sample_counter = 0;
    printf("Timer: Init %d timers\n", num_timers);
}

void Timer_Start(int timer_id, timer_callback_t callback) {
    if (timer_id < 0 || timer_id >= max_timers) {
        printf("Timer: Invalid timer ID %d\n", timer_id);
        return;
    }
    
    timers[timer_id].callback = callback;
    timers[timer_id].active = true;
    // Schedule first trigger based on current sample counter
    timers[timer_id].next_trigger_sample = global_sample_counter + timers[timer_id].period_samples;
    printf("Timer: Start timer %d (next trigger at sample %u)\n", timer_id, timers[timer_id].next_trigger_sample);
}

void Timer_Stop(int timer_id) {
    if (timer_id < 0 || timer_id >= max_timers) {
        printf("Timer: Invalid timer ID %d\n", timer_id);
        return;
    }
    
    timers[timer_id].active = false;
    printf("Timer: Stop timer %d\n", timer_id);
}

void Timer_Set_Params(int timer_id, float seconds) {
    if (timer_id < 0 || timer_id >= max_timers) {
        printf("Timer: Invalid timer ID %d\n", timer_id);
        return;
    }
    
    timers[timer_id].period_seconds = seconds;
    // Convert seconds to samples at 48kHz with precise fractional handling
    float precise_samples = seconds * 48000.0f;
    timers[timer_id].period_samples = (uint32_t)precise_samples;
    timers[timer_id].period_error = precise_samples - (float)timers[timer_id].period_samples;
    
    printf("Timer: Set timer %d period to %.3f seconds (%u samples + %.6f fractional)\n", 
           timer_id, seconds, timers[timer_id].period_samples, timers[timer_id].period_error);
}

// Time-critical: Called at 48kHz sample rate - must be in RAM for optimal performance
void __not_in_flash_func(Timer_Process)(void) {
    // Called from ProcessSample() at exactly 48kHz - optimized block processing
    global_sample_counter++;
    sample_accumulator++;
    
    // Only process timers every TIMER_BLOCK_SIZE samples (~1kHz instead of 48kHz)
    if (sample_accumulator >= TIMER_BLOCK_SIZE) {
        Timer_Process_Block();
        sample_accumulator = 0;
    }
}

// Critical: Timer callback processing - place in RAM for consistent timing
void __not_in_flash_func(Timer_Process_Block)(void) {
    // Process all timer events that occurred in this block
    // This function runs at 1.5kHz instead of 48kHz, reducing CPU overhead by 97%
    
    // OPTIMIZATION: Only process slopes that are actually changing
    // Check which channels have active slopes before expensive S_step_v calls
    extern float S_get_state(int index);
    extern float* S_step_v(int index, float* out, int size);
    
    // Access slope internals to check if processing is needed
    typedef struct {
        int index;
        float dest;
        float last;
        int shape;
        void* action;
        float here;
        float delta;
        float countdown;
        float scale;
        float shaped;
    } Slope_t;
    extern Slope_t* slopes; // Defined in slopes.c
    
    static float slope_buffer[TIMER_BLOCK_SIZE];
    
    for (int ch = 0; ch < 4; ch++) {
        // Skip channels that are idle (no active slope/slew/action)
        if (slopes && slopes[ch].countdown <= 0.0f && slopes[ch].action == NULL) {
            continue; // Channel is static, no processing needed
        }
        
        // Process this channel's slope over the block
        S_step_v(ch, slope_buffer, TIMER_BLOCK_SIZE);
        // Hardware output update happens inside S_step_v via hardware_output_set_voltage
    }
    
    // Process timer callbacks
    for (int i = 0; i < max_timers; i++) {
        if (timers[i].active && timers[i].callback) {
            static float accumulated_error[8] = {0}; // Track error for up to 8 timers
            
            while (timers[i].next_trigger_sample <= global_sample_counter) {
                // Timer should have triggered - fire it now
                timers[i].callback(i);
                
                // Schedule next trigger with precise fractional error tracking
                timers[i].next_trigger_sample += timers[i].period_samples;
                
                // Accumulate fractional sample error for long-term precision
                if (i < 8) { // Safety check for static array
                    accumulated_error[i] += timers[i].period_error;
                    
                    // When fractional error accumulates to >= 1 sample, add it
                    if (accumulated_error[i] >= 1.0f) {
                        timers[i].next_trigger_sample += 1;
                        accumulated_error[i] -= 1.0f;
                    } else if (accumulated_error[i] <= -1.0f) {
                        timers[i].next_trigger_sample -= 1;
                        accumulated_error[i] += 1.0f;
                    }
                }
                
                // Handle wrap-around for very long-running systems
                if (timers[i].next_trigger_sample < global_sample_counter) {
                    // If we've wrapped around, just schedule for next period
                    timers[i].next_trigger_sample = global_sample_counter + timers[i].period_samples;
                    if (i < 8) accumulated_error[i] = 0.0f; // Reset error on wrap
                    break; // Exit the while loop to prevent infinite loop
                }
                
                // Prevent infinite loop for very short periods
                if (timers[i].period_samples < TIMER_BLOCK_SIZE) {
                    break;
                }
            }
        }
    }
}
