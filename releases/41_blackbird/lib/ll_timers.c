#include "ll_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Timer implementation stubs for RP2040 Workshop Computer

typedef struct {
    timer_callback_t callback;
    float period_seconds;
    bool active;
    uint32_t last_tick;
} timer_t;

static timer_t* timers = NULL;
static int max_timers = 0;

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
        timers[i].last_tick = 0;
    }
    printf("Timer: Init %d timers\n", num_timers);
}

void Timer_Start(int timer_id, timer_callback_t callback) {
    if (timer_id < 0 || timer_id >= max_timers) {
        printf("Timer: Invalid timer ID %d\n", timer_id);
        return;
    }
    
    timers[timer_id].callback = callback;
    timers[timer_id].active = true;
    timers[timer_id].last_tick = 0; // Reset tick counter
    printf("Timer: Start timer %d\n", timer_id);
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
    printf("Timer: Set timer %d period to %.3f seconds\n", timer_id, seconds);
}

void Timer_Process(void) {
    // Stub implementation - in a real system this would be called from main loop
    // and would check elapsed time to trigger callbacks
    // For now, this is just a placeholder
}
