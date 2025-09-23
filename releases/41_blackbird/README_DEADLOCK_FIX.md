# Deadlock Fix: Non-Blocking Event Handler

## Problem Summary

The monome crow emulator was experiencing deadlocks during multicore testing, specifically hanging on the `EVENT POST` line. The system would successfully post events but then hang when trying to process them.

## Root Cause Analysis

**The Deadlock Chain:**
1. Test thread (Core 1) holds `lua_mutex` while running Lua tests
2. Audio processing (Core 0) triggers input detection → posts event to queue
3. Event system calls `L_handle_change_safe()` → tries to call Lua
4. `evaluate_safe_thread_safe()` tries to acquire `lua_mutex` → **BLOCKS INDEFINITELY**
5. **DEADLOCK**: Core 0 waits for Core 1's mutex, Core 1 continues testing

## The Solution: Complete Multicore Thread Safety

### Key Changes Made

1. **Added `evaluate_safe_non_blocking()` function** in `main.cpp`:
   ```cpp
   bool evaluate_safe_non_blocking(const char* code) {
       if (!mutex_try_enter(&lua_mutex, nullptr)) {
           printf("Lua mutex busy - skipping event handler call\n\r");
           return false;  // Skip instead of blocking
       }
       // ... execute Lua code ...
       mutex_exit(&lua_mutex);
       return result;
   }
   ```

2. **Updated `L_handle_change_safe()` event handler** to use non-blocking calls:
   ```cpp
   if (!lua_mgr->evaluate_safe_non_blocking(lua_call)) {
       printf("Skipped change_handler for channel %d (mutex busy or error)\n\r", channel);
       return;  // Graceful degradation instead of deadlock
   }
   ```

3. **Added slopes system mutex protection** to prevent race conditions:
   ```cpp
   // Global slopes mutex for thread safety
   static mutex_t slopes_mutex;
   static bool slopes_mutex_initialized = false;
   
   // Protected slopes calls in output metamethod
   mutex_enter_blocking(&slopes_mutex);
   S_toward(output_data->channel - 1, volts, 0.0, SHAPE_Linear, nullptr);
   mutex_exit(&slopes_mutex);
   
   // Protected slopes processing in audio thread
   mutex_enter_blocking(&slopes_mutex);
   for(int i = 0; i < 4; i++) {
       S_step_v(i, sample_buffer, 1);
       hardware_set_output(i+1, sample_buffer[0]);
   }
   mutex_exit(&slopes_mutex);
   ```

### Why This Works

- **Real-time Safety**: Event handlers should never block in audio/real-time systems
- **Graceful Degradation**: If Lua is busy, events are safely skipped rather than causing deadlock
- **Thread Safety**: Slopes system now protected from race conditions between cores
- **Maintains Functionality**: Normal operation (when Lua isn't busy) works exactly as before
**Functions Modified**: `L_handle_change_safe()`
**Core Principle**: Never block in real-time event handlers

This fix maintains crow compatibility while ensuring the system remains responsive under all multicore conditions.
