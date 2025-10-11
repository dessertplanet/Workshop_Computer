# Pulse Output Actions API

The Blackbird hardware has two pulse outputs that can be controlled via Lua. This document describes how to use the `bb.pulseout[1]` and `bb.pulseout[2]` API to control these outputs with custom actions.

## Overview

- **`bb.pulseout[1]`** - First pulse output (internally connected to clock)
- **`bb.pulseout[2]`** - Second pulse output (connected to switch by default)

**Important:** Pulse outputs are **1-bit digital outputs** (on/off only). They only support `pulse()` actions and clock-synced timing. Voltage ramps, levels, and other complex ASL actions are not supported since these outputs cannot produce variable voltages.

## Default Behavior

### Pulse Output 1
By default, `bb.pulseout[1]` generates a **10ms pulse at 5V** triggered by the internal clock on each beat.

```lua
-- Default action (set automatically)
bb.pulseout[1].action = pulse(0.010, 5, 1)
```

### Pulse Output 2
By default, `bb.pulseout[2]` **follows the switch position** - it's high when the switch is in the down position, low otherwise. This behavior is handled in the C code until you override it with a custom action.

## Properties

### `.action`
Get or set the action for the pulse output.

```lua
-- Get current action
local current = bb.pulseout[1].action

-- Set a new action
bb.pulseout[1].action = pulse(0.050, 5, 1)  -- 50ms pulses
```

### `.state`
Read-only. Returns the current state of the pulse output (true = high, false = low).

```lua
local is_high = bb.pulseout[1].state
```

## Setting Custom Actions

### Using pulse() Function

Since pulse outputs are 1-bit digital outputs, use the `pulse()` function to define pulse duration. You can use either property assignment or call syntax:

```lua
-- Property syntax
bb.pulseout[1].action = pulse(duration)

-- Call syntax (crow-style)
bb.pulseout[1](pulse(duration))
```

Parameters:
- `duration` - Pulse duration in seconds (how long the output stays high)

**Note:** The `level` and `polarity` parameters from crow's `pulse()` are ignored for these digital outputs - they only go high/low.

Examples:
```lua
-- 20ms pulses
bb.pulseout[1].action = pulse(0.020)

-- 5ms pulses
bb.pulseout[2].action = pulse(0.005)

-- 100ms pulses (for triggering external gear)
bb.pulseout[1].action = pulse(0.100)

-- Very short 1ms trigger pulses
bb.pulseout[2].action = pulse(0.001)
```

### Using Lua Functions

For dynamic pulse durations or complex logic, use a Lua function:

```lua
bb.pulseout[1].action = function()
    -- Variable duration based on knob position
    local duration = 0.005 + (bb.knob.main * 0.095)  -- 5-100ms
    
    hardware_pulse(1, true)  -- Set pulse high
    clock.run(function()
        clock.sleep(duration)
        hardware_pulse(1, false)  -- Set pulse low
    end)
end
```

**Note:** Since these are 1-bit outputs, only use `hardware_pulse(channel, true/false)` for on/off control. Variable voltage commands won't work.

## Advanced Examples

### Variable Pulse Width Based on Control
```lua
-- Pulse duration controlled by main knob (1-100ms)
bb.pulseout[1].action = function()
    local pw = 0.001 + (bb.knob.main * 0.099)
    hardware_pulse(1, true)
    clock.run(function()
        clock.sleep(pw)
        hardware_pulse(1, false)
    end)
end
```

### Pulse on Switch Changes
```lua
-- Trigger pulse 2 when switch moves to down position
bb.switch.change = function(state)
    if state == 'down' then
        -- Trigger a 50ms pulse
        hardware_pulse(2, true)
        clock.run(function()
            clock.sleep(0.050)
            hardware_pulse(2, false)
        end)
    end
end
```

### Euclidean Rhythm Pulses
```lua
-- Use sequins for euclidean patterns
local pattern = sequins{1, 0, 0, 1, 0, 1, 0, 0}

bb.pulseout[1].action = function()
    if pattern() == 1 then
        hardware_pulse(1, true)
        clock.run(function()
            clock.sleep(0.010)
            hardware_pulse(1, false)
        end)
    end
    -- If pattern returns 0, no pulse is generated
end
```

### Clock Division/Multiplication
```lua
-- Generate pulses at different clock divisions
local counter = 0

bb.pulseout[1].action = function()
    counter = counter + 1
    if counter % 4 == 0 then  -- Every 4th beat
        hardware_pulse(1, true)
        clock.run(function()
            clock.sleep(0.020)
            hardware_pulse(1, false)
        end)
    end
end
```

## Integration with Clock System

Pulse output 1 is triggered automatically by the internal clock system. When you set a custom action:

```lua
bb.pulseout[1].action = pulse(0.020, 5, 1)
```

The clock system will execute this action on each beat. The action is called at the clock rate, so you can create rhythmic patterns that sync with your clock.

## Clearing and Resetting Actions

### Clear Action (disable output)

To completely disable a pulse output, you can use either syntax:

```lua
-- Property syntax
bb.pulseout[1].action = 'none'
bb.pulseout[2].action = 'none'

-- Call syntax (crow-style)
bb.pulseout[1]('none')
bb.pulseout[2]('none')
```

When set to `'none'`, the output will be forced low and no pulses will be generated (even clock-triggered pulses on output 1).

### Reset to Defaults

To restore default behavior:

```lua
-- Reset pulse 1 to default 10ms pulses
bb.pulseout[1].action = pulse(0.010)

-- Reset pulse 2 to default (switch following)
bb.pulseout[2].action = function() end
```

Note: For pulse 2, an empty function disables the custom action and the hardware will resume following the switch position.

## Hardware Functions

When using Lua function actions, you can directly control the pulse outputs:

```lua
hardware_pulse(channel, state)
```

- `channel` - 1 or 2 for pulse output 1 or 2
- `state` - true for high (5V), false for low (0V)

Example:
```lua
bb.pulseout[1].action = function()
    hardware_pulse(1, true)   -- Set high
    clock.sleep(0.025)        -- Wait 25ms
    hardware_pulse(1, false)  -- Set low
end
```

## Limitations

Since pulse outputs are **1-bit digital outputs**, they have the following limitations:

- ✅ **Supported:** `pulse(duration)` for timed pulses
- ✅ **Supported:** Clock-synced triggering
- ✅ **Supported:** Lua functions with `hardware_pulse(channel, true/false)`
- ❌ **Not Supported:** Voltage ramping (e.g., `to(5, 0.1)`)
- ❌ **Not Supported:** Variable voltage levels
- ❌ **Not Supported:** Complex ASL sequences with multiple voltage targets
- ❌ **Not Supported:** LFOs, envelopes, or other voltage-based actions

These outputs are purely on/off - perfect for gates, triggers, and clock signals, but not for CV.

## Notes

- Pulse outputs are **5V digital outputs** (high = 5V, low = 0V)
- Only `pulse()` and clock-based timing work with these outputs
- Custom actions override the default C-level behavior
- You can check the current state with `.state` property
- The `g_pulse_out_custom_action` flag tracks whether a custom action is set

## See Also

- `examples/pulseout_demo.lua` - Working examples
- `KNOBS_WITH_ASL.md` - ASL examples (note: voltage features don't apply to pulse outputs)
- crow documentation - Action system reference (note: only pulse/clock features apply)
