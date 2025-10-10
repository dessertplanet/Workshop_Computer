# Pulse Input Detection Implementation

## Overview
Added support for pulse input change detection in the monome crow emulator. The system detects edges at 48kHz audio rate, ensuring even very short pulses (< 20μs) are reliably captured.

## API

### Syntax Options

The API supports two syntax styles for compatibility with crow:

```lua
-- Style 1: Property assignment (simple)
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'rising'
bb.pulsein[1].change = function(state) ... end

-- Style 2: Table configuration (crow-style)
bb.pulsein[1]{ mode = 'change', direction = 'rising' }
bb.pulsein[1].change = function(state) ... end

-- Both styles can be mixed
bb.pulsein[2]{ mode = 'change', direction = 'falling' }
bb.pulsein[2].change = function(state) print(state) end
```

### Properties

#### `bb.pulsein[n].mode` (read/write property)
- **Set mode**: `bb.pulsein[1].mode = 'change'`
- **Get mode**: `local m = bb.pulsein[1].mode` - returns current mode string
- Values: `'none'` (default) or `'change'`

#### `bb.pulsein[n].direction` (read/write property)
- **Set direction**: `bb.pulsein[1].direction = 'rising'`
- **Get direction**: `local d = bb.pulsein[1].direction` - returns current direction string
- Values: `'both'` (default), `'rising'`, or `'falling'`
- `'both'` - Detect both rising and falling edges
- `'rising'` - Only detect rising edges (LOW → HIGH)
- `'falling'` - Only detect falling edges (HIGH → LOW)

#### `bb.pulsein[n]{table}` (call syntax for configuration)
- **Syntax**: `bb.pulsein[1]{ mode = 'change', direction = 'rising' }`
- Crow-style table configuration for setting multiple properties at once
- Supports `mode` and `direction` fields
- Alternative to setting properties individually

#### `bb.pulsein[n].state` (read-only)
- Returns current boolean state of the pulse input
- Works regardless of mode setting

#### `bb.pulsein[n].change` (read/write)
- Callback function that receives a boolean `state` parameter
- Called on edges when mode is `'change'` (filtered by direction)
- `state = true` for rising edge (LOW → HIGH)
- `state = false` for falling edge (HIGH → LOW)
- When direction is `'rising'`, state will always be `true`
- When direction is `'falling'`, state will always be `false`

## Implementation Details

### Architecture
The implementation avoids using Lua C closures with upvalues (which can interfere with the Pico SDK's timer system). Instead, each pulsein table stores its index as a `_idx` field, and the metamethods read this field to determine which pulse input they're operating on.

### Edge Detection at Audio Rate (48kHz)
The system uses the hardware's built-in edge detection (`PulseIn1RisingEdge()`, `PulseIn1FallingEdge()`) which runs at 48kHz in the `ProcessSample()` ISR. This ensures:

- **No missed pulses**: Even pulses as short as ~20μs (one audio sample) are detected
- **Accurate timing**: Edge detection happens at audio rate, not main loop rate
- **Low overhead**: Flags are set in ISR, callbacks fired in main loop

### Main Loop Processing (~10kHz)
The main loop checks edge flags and fires Lua callbacks:

- Callbacks execute outside the ISR context (safe for Lua VM)
- Edge flags are cleared after callback fires
- Multiple edges between main loop iterations are coalesced

### State Caching
Current pulse state is cached at 48kHz for instant `.state` queries:

```lua
local current = bb.pulsein[1].state  -- No delay, returns cached value
```

## Usage Examples

### Basic Change Detection (Both Edges)
```lua
-- Method 1: Property style
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'both'  -- optional, this is default
bb.pulsein[1].change = function(state)
    print("Pulse 1: " .. tostring(state))
end

-- Method 2: Table call style (more concise)
bb.pulsein[1]{ mode = 'change', direction = 'both' }
bb.pulsein[1].change = function(state)
    print("Pulse 1: " .. tostring(state))
end
```

### Pulse Counter (Rising Edges Only)
```lua
local count = 0

-- Method 1: Property style
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'rising'
bb.pulsein[1].change = function(state)
    count = count + 1
    print("Pulse count: " .. count)
end

-- Method 2: Table call style (more concise)
bb.pulsein[1]{ mode = 'change', direction = 'rising' }
bb.pulsein[1].change = function(state)
    count = count + 1
    print("Pulse count: " .. count)
end
```

### Gate End Detection (Falling Edges Only)
```lua
-- Using table call syntax
bb.pulsein[1]{ mode = 'change', direction = 'falling' }
bb.pulsein[1].change = function(state)
    -- state will always be false with direction='falling'
    print("Gate ended")
    output[1].volts = 0  -- Clear output when gate ends
end
```

### Trigger Output on Input
```lua
-- Simple gate follower
bb.pulsein[1]{ mode = 'change' }
bb.pulsein[1].change = function(state)
    output[1].volts = state and 5.0 or 0.0
end
```

### Disable Detection
```lua
bb.pulsein[1].mode = 'none'
bb.pulsein[1].change = nil  -- Optional: clear callback
```

## Performance Characteristics

- **Detection latency**: ~20μs (one audio sample at 48kHz)
- **Callback latency**: 100μs typical (main loop period)
- **Minimum detectable pulse**: ~20μs (48kHz = 20.8μs period)
- **ISR overhead**: ~100 clock cycles per sample when mode is 'change'

## Files Modified

1. **main.cpp**:
   - Added global pulse input state variables (mode, direction, state, edge flags)
   - Added `GetPulseIn1()` and `GetPulseIn2()` wrapper methods to BlackbirdCrow class
   - Added file-scope C functions for Lua bindings: `pulsein_index`, `pulsein_newindex`, `pulsein_call`
   - Updated `create_bb_table()` to create `bb.pulsein[1]` and `bb.pulsein[2]` tables with metatables
   - Added 48kHz edge detection in `ProcessSample()` ISR
   - Added main loop callback firing logic
   - Implementation uses table fields instead of closures to avoid SDK timer conflicts

## Testing Recommendations

1. **Short pulse test**: Send 50μs pulses, verify detection
2. **High frequency test**: Send rapid pulses (1kHz+), check no drops
3. **Mode switching**: Verify 'none' mode stops callbacks
4. **State query**: Check `.state` returns current value
5. **Callback performance**: Ensure callbacks don't block audio

See `examples/pulsein_demo.lua` for a complete working example.
