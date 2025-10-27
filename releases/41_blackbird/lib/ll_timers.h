#pragma once

// Timer functionality for RP2040 Workshop Computer with block processing optimization
// Replaces ../ll/timers.h from the original crow implementation

#include <stdint.h>

// Timer callback function type
typedef void (*timer_callback_t)(int timer_id);

// Block processing configuration - ALIGNED with audio block size for consistent timing
// Optimized for low latency: 8 samples = 166µs @ 48kHz
#define TIMER_BLOCK_SIZE 8  // Process timers every 8 samples (6kHz, 166µs blocks)

// Timer functions
void Timer_Init(int num_timers);
void Timer_Start(int timer_id, timer_callback_t callback);
void Timer_Stop(int timer_id);
void Timer_Set_Params(int timer_id, float seconds);
void Timer_Process(void);        // Called from ProcessSample() - now uses block processing
void Timer_Process_Block(void);  // Internal block processing function
