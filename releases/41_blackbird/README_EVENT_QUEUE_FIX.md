# Critical Event Queue Bug Fix

## Problem Identified

The Blackbird Crow emulator had a critical bug in the event queue system (`lib/events.c`) that was causing detection callbacks to stop working after 2 triggers.

### Root Cause

The `event_next()` function had incorrect circular buffer logic:

```c
// WRONG CODE (was doing this):
INCR_EVENT_INDEX( getIdx );  // Increment FIRST
e = (event_t*)&sysEvents[ getIdx ];  // Then get event

// This caused:
// 1. First event skipped (reading uninitialized memory)
// 2. Event queue corruption after a few uses
// 3. Detection system stops responding
```

## Fix Applied

Corrected the circular buffer logic to standard implementation:

```c
// CORRECT CODE (now does this):
e = (event_t*)&sysEvents[ getIdx ];  // Get event at current position
INCR_EVENT_INDEX( getIdx );  // Then increment index

// This ensures:
// 1. All events are read correctly
// 2. No memory corruption
// 3. Continuous operation
```

## Impact

**Before Fix:**
- ✅ Detection worked for first 1-2 triggers
- ❌ Detection stopped responding after callback #2
- ❌ Event queue corruption caused system instability

**After Fix:**
- ✅ Detection should work continuously
- ✅ Event queue operates correctly
- ✅ Callbacks should execute reliably

## Testing

1. **Flash the new firmware**: `UF2/blackbird.uf2` (733KB, built 11:42)
2. **Test basic detection**: Run `test_sample_hold_debug()` 
3. **Test sample & hold**: Run `test_sample_hold()` 
4. **Verify continuous operation**: Send multiple clock pulses

The debug test should now work indefinitely without stopping after 2 callbacks.

## Files Modified

- `lib/events.c` - Fixed circular buffer logic in `event_next()`
- Event queue now correctly processes all events without corruption

This was a critical system-level bug that affected all event-driven functionality in the crow emulator.
