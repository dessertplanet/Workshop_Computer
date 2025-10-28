#pragma once

// Timer functionality for RP2040 Workshop Computer with block processing optimization
// Replaces ../ll/timers.h from the original crow implementation

#include <stdint.h>

// Timer callback function type
typedef void (*timer_callback_t)(int timer_id);

// Block processing configuration - ALIGNED with audio block size for consistent timing
// Optimized for best latency/efficiency balance: 4 samples = 83µs @ 48kHz
// Sweet spot: 2× faster than size=8, 4× more efficient than size=1
#define TIMER_BLOCK_SIZE 4  // Process timers every 4 samples (12kHz, 83µs blocks)

// Timer functions
void Timer_Init(int num_timers);
void Timer_Start(int timer_id, timer_callback_t callback);
void Timer_Stop(int timer_id);
void Timer_Set_Params(int timer_id, float seconds);
void Timer_Process(void);        // Called from ProcessSample() - now uses block processing
void Timer_Process_Block(void);  // Internal block processing function
