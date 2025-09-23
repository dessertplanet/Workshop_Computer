# Sample & Hold Debug Guide - Isolating Callback Crash

## Problem Summary

The sample & hold test (`test_sample_hold()`) successfully detects input triggers but the device becomes unresponsive after the first callback execution. The detection system is working correctly, but something in the Lua callback causes a crash.

## Debug Test Available

The debug version `test_sample_hold_debug()` provides a minimal callback to isolate the issue:

```lua
-- Flash the updated UF2/blackbird.uf2 firmware to your device
-- Connect USB and open serial terminal
-- Run the debug test:
test_sample_hold_debug()
```

## Debug Test Behavior

The debug test uses a minimal callback that only prints messages:

```lua
input[1].change = function(state)
    if state then
        print("DEBUG: Rising edge detected - NO OTHER OPERATIONS")
    else
        print("DEBUG: Falling edge detected")
    end
end
```

## Expected Results

**If the debug test works without crashes:**
- The issue is in the main callback's operations (output assignment, math.random(), or string formatting)
- We can incrementally add functionality to find the exact cause

**If the debug test also crashes:**
- The issue is in the fundamental event/callback system
- Likely in the `L_handle_change_safe()` or `change_handler()` implementation

## Progressive Debug Steps

1. **Step 1**: Test minimal callback (current debug test)
2. **Step 2**: Add just the output assignment: `output[1].volts = 1.5`
3. **Step 3**: Add just random generation: `local x = math.random()`
4. **Step 4**: Add just string formatting: `string.format("Test %.3f", 1.234)`

## Suspected Root Causes

Based on the crash pattern, likely causes are:

1. **Stack corruption** in Lua callback execution
2. **Memory corruption** in slopes/output system 
3. **Interrupt conflicts** between detection and slopes processing
4. **Event queue overflow** causing infinite recursion

## Key Evidence

- ✅ Detection trigger works: `CALLBACK #1: ch1 state=HIGH` 
- ✅ Event system works: Events are being queued and processed
- ✅ Lua callback gets called: The function starts executing
- ❌ System hangs after callback: Something in the callback corrupts memory/stack

## Next Steps

Test the debug version and report results. This will help isolate whether the issue is:
- A) In the callback operations themselves
- B) In the fundamental callback mechanism

## Files Changed

- `test_sample_hold_debug.lua` - Minimal test callback
- `main.cpp` - Added `test_sample_hold_debug()` function
- `CMakeLists.txt` - Added debug test to build system
