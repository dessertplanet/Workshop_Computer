# Blackbird Crow Output Functionality

This document explains how to use the crow-compatible output functionality on the Blackbird Workshop Computer.

## Hardware Mapping

The Blackbird has 4 outputs that map to crow's output channels:

- `output[1]` → AudioOut1 (left audio output)
- `output[2]` → AudioOut2 (right audio output)  
- `output[3]` → CVOut1 (CV output 1)
- `output[4]` → CVOut2 (CV output 2)

Each output accepts voltages in the ±6V range and uses 12-bit DACs for output.

## Basic Usage

### Setting Voltages (Crow-Compatible Assignment Syntax)

```lua
-- Set individual outputs to specific voltages using crow-style assignment
output[1].volts = 0.0    -- 0V
output[2].volts = 2.5    -- 2.5V
output[3].volts = -1.2   -- -1.2V
output[4].volts = 5.0    -- 5V
```

### Reading Current Voltage (Crow-Compatible Property Access)

```lua
-- Read current output voltage using crow-style property access
local voltage = output[1].volts
print("Output 1 is at " .. voltage .. "V")
```

### Legacy Function-Based Syntax (Still Supported)

```lua
-- The old function-based syntax still works for backward compatibility
output[1].volts(3.5)    -- Set voltage using function call
local v = output[1].volts()  -- Get voltage using function call
```

### Voltage Range

- **Range**: -6V to +6V
- **Resolution**: 12-bit (4096 steps)
- **Step size**: ~2.93mV per step
- Voltages outside ±6V are automatically clamped

## Testing

Run the provided test script to verify output functionality:

```lua
dofile("test_outputs.lua")
```

This will:
1. Set each output to different voltages
2. Read back and display current voltages
3. Test voltage clamping at ±6V limits
4. Perform a voltage sweep demonstration

## Serial Monitor Usage

1. Connect Blackbird via USB
2. Open serial monitor at 115200 baud
3. Send crow commands or Lua code directly

Example session:
```
^^v                     -- Get version
output[1].volts(3.5)   -- Set output 1 to 3.5V
print(output[1].volts()) -- Read back value
```

## RP2040 Optimizations

The implementation includes several optimizations for the RP2040:

- **Integer Math**: Voltages stored internally as millivolts (int32_t)
- **Multicore**: USB/Lua processing runs on Core 1, audio on Core 0
- **Efficiency**: Minimal floating point operations in audio callback
- **Memory**: Judicious use of floats only where necessary

## Hardware Considerations

- **Sample Rate**: 48kHz audio processing
- **Latency**: Low latency for real-time control
- **Precision**: 12-bit DAC resolution matches crow hardware
- **Safety**: Automatic voltage clamping prevents hardware damage

## Crow Compatibility

This implementation provides basic crow output compatibility:
- Same `output[n].volts()` API
- Same voltage ranges (±6V)
- Same behavior for getting/setting voltages
- Compatible with most crow scripts using basic output functionality

## Future Extensions

The architecture supports future additions:
- ASL (Action-Syntax-Language) for envelope generation
- Dynamic variables and sequencing
- Clock synchronization
- Additional crow library functions

## Example Scripts

### Simple CV Generator
```lua
-- Generate ascending CV sequence using crow-style assignment
for i = 1, 8 do
    output[3].volts = i * 0.5  -- 0.5V steps
    -- Add timing/clock sync as needed
end
```

### Dual Output Control
```lua
-- Control two outputs inversely using crow-style assignment
local level = 2.0
output[1].volts = level
output[2].volts = -level
```

### Range Mapping
```lua
-- Map 0-1 range to full ±6V output range
function map_output(channel, value)
    local voltage = (value * 12.0) - 6.0  -- 0-1 → ±6V
    output[channel].volts = voltage
end

map_output(1, 0.75)  -- 0.75 → 3V
