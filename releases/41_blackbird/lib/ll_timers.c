#include "ll_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "ashapes.h"  // For output quantization
#include "slopes.h"   // For q16_t and Q16_SHIFT

// Timer implementation for RP2040 Workshop Computer with block processing optimization
// Aligned block size (32 samples) for consistent timing with audio processing

typedef struct {
    timer_callback_t callback;
    float period_seconds;
    bool active;
    uint32_t period_samples;      // Period in 24kHz samples
    uint64_t next_trigger_sample; // When to trigger next (64-bit for long-running systems)
    float period_error;           // Accumulated fractional sample error for precision
} timer_t;

static timer_t* timers = NULL;
static int max_timers = 0;
volatile uint64_t global_sample_counter = 0; // Incremented in ProcessSample() ISR - 64-bit for precision

// Runtime-adjustable block size (defaults to 'timing')
int g_timer_block_size = 480; // default mapping for bb.priority='timing'

// Block processing state - aligned with audio blocks for consistent timing
static int sample_accumulator = 0; // Count samples until next block processing

// Deferred block size change state
static int pending_block_size = 0; // 0 means no pending change

int Timer_Block_Size_Change_Pending(void) { return pending_block_size != 0; }

int Timer_Set_Block_Size(int size) {
    if (size < 1) size = 1;
    if (size > TIMER_BLOCK_SIZE_MAX) size = TIMER_BLOCK_SIZE_MAX;
    // If timers not yet initialized or first call scenario just apply directly
    // We detect that by pending_block_size==0 and global_sample_counter==0 and g_timer_block_size defaulted
    // Always defer to boundary for simplicity; apply at end of Timer_Process
    pending_block_size = size;
    return 1;
}

int Timer_Get_Block_Size(void) { return g_timer_block_size; }

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
    // printf("Timer: Start timer %d (next trigger at sample %u)\n", timer_id, timers[timer_id].next_trigger_sample);
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
    // Convert seconds to samples at 24kHz with precise fractional handling
    float precise_samples = seconds * 24000.0f;
    timers[timer_id].period_samples = (uint32_t)precise_samples;
    timers[timer_id].period_error = precise_samples - (float)timers[timer_id].period_samples;
    
    // printf("Timer: Set timer %d period to %.3f seconds (%u samples + %.6f fractional)\n", 
    //        timer_id, seconds, timers[timer_id].period_samples, timers[timer_id].period_error);
}

// Timer processing - called from MainControlLoop at ~20kHz
// NO LONGER IN ISR! Safe to take time for complex calculations
// CRITICAL: Place in RAM for consistent timing at high poll rates
__attribute__((section(".time_critical.Timer_Process")))
void Timer_Process(void) {
    // Apply any deferred block size change from previous cycle BEFORE measuring catch-up
    if (pending_block_size != 0) {
        g_timer_block_size = pending_block_size;
        pending_block_size = 0;
    }
    // Check if enough samples have passed for next block
    // global_sample_counter incremented by ProcessSample() ISR
    static uint64_t last_processed_sample = 0;
    
    // Process missed blocks to maintain accurate countdown timing
    // BUT limit catch-up to prevent infinite loops if CPU can't keep up
    int blocks_processed = 0;
    // Adaptive catch-up limit based on block size:
    // - Small blocks (≤4): Need more tolerance for Lua callback overhead
    // - Larger blocks (≥8): Less likely to fall behind significantly
    const int MAX_CATCHUP_BLOCKS = (TIMER_BLOCK_SIZE <= 4) ? 16 : 8;
    
    while (global_sample_counter - last_processed_sample >= TIMER_BLOCK_SIZE 
           && blocks_processed < MAX_CATCHUP_BLOCKS) {
        Timer_Process_Block();
        // CRITICAL: Advance by exactly TIMER_BLOCK_SIZE to maintain precise timing
        last_processed_sample += TIMER_BLOCK_SIZE;
        blocks_processed++;
    }
    
    // If we're STILL behind after catch-up limit, we're overloaded
    // Skip ahead to prevent system freeze, but this WILL cause timing drift
    if (global_sample_counter - last_processed_sample >= TIMER_BLOCK_SIZE * MAX_CATCHUP_BLOCKS) {
        // Emergency: System is overloaded, skip ahead to prevent freeze
        last_processed_sample = global_sample_counter - TIMER_BLOCK_SIZE;
        // This will cause frequency drift, but better than a frozen system
    }

    // Defer applying size if it was requested during callbacks in this processing cycle
    if (pending_block_size != 0) {
        g_timer_block_size = pending_block_size;
        pending_block_size = 0;
    }
}

// Critical: Timer callback processing - place in RAM for consistent timing
void __not_in_flash_func(Timer_Process_Block)(void) {
    // Process all timer events that occurred in this block
    // NOTE: Slope processing has moved to Core 1 ProcessSample() for sample-accurate output
    // This function now only handles timer callbacks (metros, ASL actions, etc.)
    
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
