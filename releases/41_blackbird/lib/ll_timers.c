#include "ll_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Timer implementation for RP2040 Workshop Computer using 48kHz ProcessSample() timing

typedef struct {
    timer_callback_t callback;
    float period_seconds;
    bool active;
    uint32_t period_samples;      // Period in 48kHz samples
    uint32_t next_trigger_sample; // When to trigger next
} timer_t;

static timer_t* timers = NULL;
static int max_timers = 0;
static uint32_t global_sample_counter = 0; // Incremented every ProcessSample()

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
    // Convert seconds to samples at 48kHz
    timers[timer_id].period_samples = (uint32_t)(seconds * 48000.0f);
    printf("Timer: Set timer %d period to %.3f seconds (%u samples)\n", 
           timer_id, seconds, timers[timer_id].period_samples);
}

void Timer_Process(void) {
    // Called from ProcessSample() at exactly 48kHz
    global_sample_counter++;
    
    // Check all active timers
    for (int i = 0; i < max_timers; i++) {
        if (timers[i].active && timers[i].callback) {
            // Check if it's time to trigger this timer
            if (global_sample_counter >= timers[i].next_trigger_sample) {
                // Trigger callback
                timers[i].callback(i);
                
                // Schedule next trigger (repeating timer)
                timers[i].next_trigger_sample += timers[i].period_samples;
                
                // Handle wrap-around for very long-running systems
                if (timers[i].next_trigger_sample < global_sample_counter) {
                    // If we've wrapped around, just schedule for next period
                    timers[i].next_trigger_sample = global_sample_counter + timers[i].period_samples;
                }
            }
        }
    }
}
