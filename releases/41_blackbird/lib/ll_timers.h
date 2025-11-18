#pragma once

// Timer functionality for RP2040 Workshop Computer with block processing optimization
// Replaces ../ll/timers.h from the original crow implementation

#include <stdint.h>

// Timer callback function type
typedef void (*timer_callback_t)(int timer_id);

// Block processing configuration - runtime adjustable via bb.priority
// bb.priority values:
//   'timing'   -> larger block (240) for lower CPU usage, higher scheduling latency (default)
//   'balanced' -> medium block (120) for balanced CPU usage and latency
//   'accuracy' -> block size 2 for minimal latency, higher CPU usage
// Any other assignment maps back to 'timing'.
//
// Implementation notes:
// - We keep a MAX size for static allocations; actual working size is g_timer_block_size
// - Change at runtime is allowed; switching from a large to small block may cause a burst
//   of catch-up processing (expected). Best set early in init.
// - Existing code that referenced TIMER_BLOCK_SIZE now uses a macro that resolves to the
//   mutable variable, except where a compile-time constant is required (buffers).
//
#define TIMER_BLOCK_SIZE_MAX 480
extern int g_timer_block_size;           // current active timer processing block size
#define TIMER_BLOCK_SIZE (g_timer_block_size)

// Runtime control (exposed indirectly to Lua via bb.priority)
// Mid-run changes are deferred: request schedules new size, applied after the
// current processing loop completes (at a block boundary). Always returns 1.
int  Timer_Set_Block_Size(int size);     // clamps to [1, TIMER_BLOCK_SIZE_MAX], schedules change
int  Timer_Get_Block_Size(void);
int  Timer_Block_Size_Change_Pending(void); // 1 if a deferred change is waiting

// Timer functions
void Timer_Init(int num_timers);
void Timer_Start(int timer_id, timer_callback_t callback);
void Timer_Stop(int timer_id);
void Timer_Set_Params(int timer_id, float seconds);
void Timer_Process(void);        // Called from ProcessSample() - now uses block processing
void Timer_Process_Block(void);  // Internal block processing function
