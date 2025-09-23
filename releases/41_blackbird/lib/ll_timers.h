#pragma once

// Timer functionality stubs for RP2040 Workshop Computer
// Replaces ../ll/timers.h from the original crow implementation

#include <stdint.h>

// Timer callback function type
typedef void (*timer_callback_t)(int timer_id);

// Timer functions (stubs for RP2040)
void Timer_Init(int num_timers);
void Timer_Start(int timer_id, timer_callback_t callback);
void Timer_Stop(int timer_id);
void Timer_Set_Params(int timer_id, float seconds);
void Timer_Process(void);  // Called from main loop
