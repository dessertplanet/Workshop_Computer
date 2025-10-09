# Using Knobs with ASL Dynamics

ASL's **dynamic variables** (`dyn`) allow you to map knobs to any ASL parameter. Dynamics are **numeric values** that you update, and ASL uses them when the action runs.

## Basic Concept

```lua
-- 1. Define an ASL action that uses dynamics (with default values)
output[1]{ to(dyn{level=3}, dyn{time=0.5}) }

-- 2. Update dynamic values from knobs
output[1].dyn.level = knob.main * 6  -- Set to current knob value
output[1].dyn.time = knob.x * 2

-- 3. Retrigger the action with updated values
output[1]:action()
```

The `dyn{level=3}` in the action definition uses the **current value** of `output[1].dyn.level` when the action runs.

## Simple Examples

### Control Slew Time
```lua
function init()
    -- Input trigger fires envelope with knob-controlled time
    input[1].change = function(state)
        if state then
            -- Update dynamic from knob, then trigger
            output[1].dyn.rate = knob.main * 2 + 0.1  -- 0.1-2.1 seconds
            output[1]{ to(5, dyn{rate=1}), to(0, dyn{rate=1}) }
        end
    end
end
```

### Control Target Voltage
```lua
function init()
    input[1].change = function(state)
        if state then
            -- Knob sets the peak voltage
            output[1].dyn.level = knob.main * 6  -- 0-6V
            output[1]{ to(dyn{level=3}, 0.1), to(0, 0.5) }
        end
    end
end
```

### Continuous LFO with Knob Control
```lua
function init()
    -- Define ASL action once (with default dynamic value)
    output[1]{ 
        to(dyn{depth=3}, dyn{rate=0.5}),
        to(0, dyn{rate=0.5})
    }
    
    -- Update dynamics and retrigger continuously
    metro[1].event = function()
        output[1].dyn.rate = knob.main * 2 + 0.1   -- 0.1-2.1s
        output[1].dyn.depth = knob.x * 6            -- 0-6V
        output[1]:action()  -- Retrigger with new values
    end
    metro[1].time = 0.5
    metro[1]:start()
end
```

## Multiple Dynamics

You can use multiple knobs to control different parameters:

```lua
function init()
    -- Define ASL with multiple dynamics
    output[1]{ 
        to(dyn{offset=0} + dyn{depth=3}, dyn{rate=0.5}),
        to(dyn{offset=0}, dyn{rate=0.5})
    }
    
    -- Update all dynamics from knobs and retrigger
    metro[1].event = function()
        output[1].dyn.rate = knob.main * 2 + 0.1
        output[1].dyn.depth = knob.x * 6
        output[1].dyn.offset = knob.y * 6
        output[1]:action()
    end
    metro[1].time = 0.5
    metro[1]:start()
end
```

## Advanced: Quantized Control

Use knobs with discrete values:

```lua
function init()
    input[1].change = function(state)
        if state then
            -- Knob selects from preset values
            local notes = {0, 2, 4, 7, 9, 12}  -- Major scale (semitones)
            local index = math.floor(knob.main * (#notes - 0.01)) + 1
            output[1].dyn.note = notes[index] / 12  -- Convert to volts
            
            -- Jump to quantized note
            output[1]{ to(dyn{note=0}, 0.01) }
        end
    end
end
```

## Switch + Knob Combination

Use switch to select mode, knobs to control parameters:

```lua
function init()
    -- Switch changes envelope shape
    switch.change = function(pos)
        if pos == 'down' then
            output[1].shape = 'linear'
        elseif pos == 'middle' then
            output[1].shape = 'sine'
        else
            output[1].shape = 'expo'
        end
    end
    
    input[1].change = function(state)
        if state then
            -- Update dynamics from knobs
            output[1].dyn.time = knob.main * 2 + 0.05
            output[1].dyn.level = knob.x * 6
            
            -- Trigger envelope (uses shape set by switch)
            output[1]{ 
                to(dyn{level=3}, dyn{time=0.5}), 
                to(0, dyn{time=0.5} * 2) 
            }
        end
    end
end
```

## Real-Time Modulation

For continuous modulation, poll knobs in a metro:

```lua
function init()
    -- Define ASL action with dynamics (default values)
    output[1]{ 
        to(dyn{depth=3}, dyn{half_period=0.5}), 
        to(0, dyn{half_period=0.5}) 
    }
    
    -- Continuously update dynamics from knobs and retrigger
    metro[1].event = function()
        local freq = knob.main * 10 + 0.1  -- 0.1-10.1 Hz
        output[1].dyn.half_period = 1 / (freq * 2)
        output[1].dyn.depth = knob.x * 5
        output[1]:action()  -- Retrigger with new values
    end
    metro[1].time = 0.1  -- Check knobs at 10Hz
    metro[1]:start()
end
```

## Important Notes

1. **Dynamics are numeric values**: Set them with `output[1].dyn.name = value`, not functions
2. **Update before retriggering**: Set dynamic values, then call `:action()` to use them
3. **Define with defaults**: Use `dyn{name=default}` in ASL to provide fallback values
4. **Use descriptive names**: `dyn.rate` is clearer than `dyn.k1`
5. **Scale appropriately**: Always scale knob values (0.0-1.0) to your parameter range
6. **Combine with switch**: Use switch for modes, knobs for continuous parameters
7. **Metro frequency**: Higher metro rates = more responsive knob control

## ASL Function Reference

```lua
-- Set a dynamic value (do this from event handlers)
output[n].dyn.name = value

-- Define ASL action with dynamics (specify defaults)
output[1]{ to(dyn{voltage=3}, dyn{time=0.5}) }

-- Use in ASL actions
to(volts, time)                    -- Static values
to(dyn{name=3}, time)              -- Dynamic voltage (3 is default)
to(volts, dyn{name=1})             -- Dynamic time (1 is default)
to(dyn{v=3}, dyn{t=1})             -- Both dynamic
to(volts, time, 'expo', dyn{s=0.5})-- Dynamic shape amount

-- Math with dynamics
dyn{a=0} + dyn{b=0}                -- Add dynamics
dyn{a=1} * 2                       -- Scale dynamic

-- Workflow
output[1].dyn.rate = knob.main * 2 -- Set value
output[1]:action()                  -- Retrigger action with new value
```

## See Also

- `examples/knob_asl_dynamics.lua` - Working example
- Crow ASL documentation for more on dynamics
