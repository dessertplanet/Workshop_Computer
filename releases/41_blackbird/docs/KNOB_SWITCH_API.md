# Workshop Computer Knob & Switch API

The Blackbird crow emulator provides access to the Workshop Computer's hardware knobs and 3-position switch through the `ws` (Workshop) namespace.

## Quick Reference

```lua
-- Knobs (always return current value 0.0-1.0)
ws.knob.main  -- Main knob (normalized 0.0-1.0)
ws.knob.x     -- X knob (normalized 0.0-1.0)
ws.knob.y     -- Y knob (normalized 0.0-1.0)

-- Switch (query position and react to changes)
ws.switch.position     -- Query: 'down', 'middle', or 'up'
ws.switch.change = function(position)  -- Callback when switch moves
    -- position is 'down', 'middle', or 'up'
end
```

## Knobs

The three knobs (Main, X, and Y) are accessible via `ws.knob`. They return normalized values from **0.0 to 1.0**.

### Reading Knob Values

```lua
-- Access knobs directly - values are always current
local main_value = ws.knob.main  -- 0.0 to 1.0
local x_value = ws.knob.x        -- 0.0 to 1.0
local y_value = ws.knob.y        -- 0.0 to 1.0
```

### Usage Examples

```lua
-- Direct voltage control (0-6V)
output[1].volts = ws.knob.main * 6

-- Control slew time (10ms to 2s)
output[2].slew = ws.knob.x * 2 + 0.01

-- Control frequency (0.1-10 Hz)
local freq = ws.knob.y * 9.9 + 0.1
metro[1].time = 1 / freq

-- Crossfade between two values
local mix = ws.knob.main
output[1].volts = valueA * (1 - mix) + valueB * mix

-- Probability control
if math.random() < ws.knob.main then
    trigger()
end
```

## Switch

The 3-position switch is accessible via `ws.switch`.

### Reading Switch Position

```lua
-- Query current position
local pos = ws.switch.position  -- Returns: 'down', 'middle', or 'up'

if ws.switch.position == 'down' then
    -- Do something
end
```

### Switch Change Callback

The switch fires a callback when its position changes:

```lua
function init()
    ws.switch.change = function(position)
        -- position is 'down', 'middle', or 'up'
        print("Switch moved to: " .. position)
        
        if position == 'down' then
            -- Mode A
            output[1].shape = 'sine'
        elseif position == 'middle' then
            -- Mode B
            output[1].shape = 'linear'
        else  -- up
            -- Mode C
            output[1].shape = 'expo'
        end
    end
end
```

**Note:** The callback is only fired when the switch position **changes**, not on every read.

## Complete Example

```lua
-- Multi-mode LFO with knob control
function init()
    -- Switch selects waveform
    ws.switch.change = function(position)
        if position == 'down' then
            output[1].shape = 'sine'
            print("Sine wave")
        elseif position == 'middle' then
            output[1].shape = 'linear'
            print("Triangle wave")
        else
            output[1].shape = 'expo'
            print("Exponential wave")
        end
    end
    
    -- Knobs control LFO parameters
    metro[1].event = function()
        local freq = ws.knob.main * 19.9 + 0.1   -- 0.1-20 Hz
        local depth = ws.knob.x * 6               -- 0-6V
        local offset = ws.knob.y * 6              -- 0-6V
        
        output[1](function()
            to(offset + depth, 1/(freq*2))
            to(offset, 1/(freq*2))
        end)
    end
    metro[1].time = 0.1  -- Check knobs at 10Hz
    metro[1]:start()
end
```

## Implementation Details

- **Knobs** return values from 0.0 to 1.0 (hardware range 0-4095 is automatically scaled)
- **Integer-to-float conversion** happens in the Lua C binding layer (outside ISR)
- **Switch changes** are detected every ~50ms in the main control loop
- **No detection modes** needed - knobs always return current values when read
- **Read-only** - the `knob` table cannot be modified

## Tips

1. **Scale appropriately**: Always scale knob values to your desired range
   ```lua
   local value = knob.main * range + offset
   ```

2. **Polling frequency**: Read knobs at a reasonable rate (10-50 Hz is typical)
   ```lua
   metro[1].time = 0.05  -- Check at 20Hz
   ```

3. **Mode switching**: Use the switch for discrete modes, knobs for continuous parameters

4. **Smooth transitions**: Consider adding hysteresis or smoothing for cleaner control
   ```lua
   local smoothed = smoothed * 0.9 + ws.knob.main * 0.1
   ```
