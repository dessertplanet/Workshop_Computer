#pragma once

// Timer functionality for RP2040 Workshop Computer with block processing optimization
// Replaces ../ll/timers.h from the original crow implementation

#include <stdint.h>

// Timer callback function type
typedef void (*timer_callback_t)(int timer_id);

// Block processing configuration - ALIGNED with audio block size for consistent timing
// Simply adjust TIMER_BLOCK_SIZE to experiment with latency vs CPU tradeoff
// All timing parameters auto-calculate from this value
// 
// Block size guide:
//   1 = 21µs latency, 48kHz processing (high CPU, no optimization benefit)
//   2 = 42µs latency, 24kHz processing (still high CPU, minimal optimization)
//   4 = 83µs latency, 12kHz processing (recommended: good balance)
//   8 = 166µs latency, 6kHz processing (stable baseline, excellent efficiency)
//
#define TIMER_BLOCK_SIZE 4  // ← ADJUST THIS VALUE ONLY (1, 2, 4, 8, etc.)

// Timer functions
void Timer_Init(int num_timers);
void Timer_Start(int timer_id, timer_callback_t callback);
void Timer_Stop(int timer_id);
void Timer_Set_Params(int timer_id, float seconds);
void Timer_Process(void);        // Called from ProcessSample() - now uses block processing
void Timer_Process_Block(void);  // Internal block processing function
