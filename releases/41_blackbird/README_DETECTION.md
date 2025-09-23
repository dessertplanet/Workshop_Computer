# Crow Detection System Implementation

## Overview

This document describes the implementation of crow's detection system in the Blackbird Crow emulator for the Workshop Computer. The detection system provides real-time analysis of input signals with multiple detection modes compatible with crow's Lua API.

## Architecture

### Core Components

1. **Detection Engine** (`lib/detect.c/.h`)
   - Multi-channel detection processing (2 input channels)
   - Multiple detection modes: stream, change, window, scale, volume, peak, freq, clock
   - Hardware abstraction through ComputerCard interface
   - Decimated processing at ~1.5kHz (every 32 samples at 48kHz)

2. **Input.lua Integration** 
   - Full compatibility with crow's Input.lua class
   - Lua callback system for detection events
   - Hardware abstraction functions for voltage reading

3. **Hardware Interface**
   - AudioIn1/AudioIn2 for input voltage sampling
   - Voltage range: ±6V (matching crow specification)
   - 48kHz audio processing with detection decimation

## Detection Modes

### Stream Mode
```lua
input[1].stream = function(v) print("Input 1:", v) end
```
- Continuous voltage sampling at regular intervals
- Configurable sample rate (default: stream events every N seconds)

### Change Mode  
```lua
input[1].change = function(v) print("Change:", v) end
```
- Triggers on voltage threshold crossings
- Configurable threshold, hysteresis, and direction (rising/falling/both)

### Window Mode
```lua
input[1].window = function(w) print("Window:", w) end  
```
- Quantizes input to predefined voltage windows
- Configurable window boundaries and hysteresis
- Returns window index (1-based)

### Scale Mode
```lua
input[1].scale = function(n) print("Note:", n) end
```
- Musical note detection and quantization
- Configurable scale (array of semitone offsets)
- Equal temperament support (12-TET default)
- Voltage scaling factor

### Volume Mode
```lua
input[1].volume = function(v) print("Volume:", v) end
```
- RMS volume detection over time windows
- Useful for audio envelope following

### Peak Mode
```lua
input[1].peak = function(p) print("Peak:", p) end
```
- Peak detection with threshold and hysteresis
- Triggers on signal peaks above threshold

### Clock Mode
```lua
input[1].clock = function() print("Clock tick") end
```
- Digital clock/trigger detection
- Based on change detection with clock-specific parameters

## Implementation Details

### Detection Processing
```c
// Called at 48kHz with decimation to ~1.5kHz
static int detect_counter = 0;
if (++detect_counter >= 32) {
    detect_counter = 0;
    
    // Get input voltages for both channels
    float input1 = hardware_get_input(1);
    float input2 = hardware_get_input(2);
    
    // Process detection for both channels
    Detect_process_sample(0, input1); // Channel 0 (input[1])
    Detect_process_sample(1, input2); // Channel 1 (input[2])
}
```

### Lua Backend Functions
The following C functions bridge the detection system to Lua:

- `lua_io_get_input()` - Read current input voltage
- `lua_set_input_stream()` - Configure stream mode
- `lua_set_input_change()` - Configure change detection  
- `lua_set_input_window()` - Configure window quantization
- `lua_set_input_scale()` - Configure scale/note detection
- `lua_set_input_volume()` - Configure volume detection
- `lua_set_input_peak()` - Configure peak detection
- `lua_set_input_freq()` - Configure frequency detection
- `lua_set_input_clock()` - Configure clock detection
- `lua_set_input_none()` - Disable detection

### Hardware Abstraction
```c
float hardware_get_input(int channel) {
    if (channel < 1 || channel > 2) return 0.0f;
    
    int16_t raw_value = 0;
    if (channel == 1) {
        raw_value = AudioIn1();
    } else if (channel == 2) {
        raw_value = AudioIn2();
    }
    
    // Convert from ComputerCard range (-2048 to 2047) to crow voltage range (±6V)
    return (float)raw_value * 6.0f / 2048.0f;
}
```

## Testing

### Embedded Test Script
A comprehensive test script is included (`test_detection.lua`) that exercises all detection modes:

```lua
-- Test all detection modes
print("=== Crow Detection System Test ===")

-- Test stream mode
print("Testing stream mode...")
input[1].stream = function(v) 
    print(string.format("Stream: %.3fV", v)) 
end

-- Test change detection  
print("Testing change detection...")
input[1].change = function(v)
    print(string.format("Change: %.3fV", v))
end

-- Test window quantization
print("Testing window mode...")
input[1].window = function(w)
    print(string.format("Window: %d", w))
end

-- Test scale quantization
print("Testing scale mode...")
input[1].scale = function(n)
    print(string.format("Note: %d", n))
end

-- And more...
```

### Running Tests
Connect to the device via serial terminal and send:
```
test_detection
```

## Usage Examples

### Basic Voltage Monitoring
```lua
input[1].stream = function(voltage)
    print("CV Input:", voltage .. "V")
    if voltage > 5.0 then
        output[1].volts = voltage  -- Pass-through high voltages
    end
end
```

### Gate/Trigger Detection  
```lua
input[1].change = function(voltage)
    if voltage > 2.5 then
        print("Gate ON")
        output[2].volts = 5.0  -- Output gate
    else  
        print("Gate OFF")
        output[2].volts = 0.0
    end
end
```

### Musical Applications
```lua
-- Quantize input to major scale
local major_scale = {0, 2, 4, 5, 7, 9, 11}  -- Major scale semitones
input[1].scale = function(note_index)
    local note_voltage = (note_index - 1) / 12.0  -- Convert to 1V/oct
    output[1].volts = note_voltage
    print("Playing note:", note_index)
end
```

## Performance Considerations

- Detection processing runs at ~1.5kHz (decimated from 48kHz audio rate)
- Minimal CPU overhead through efficient C implementation
- PicoSDK timing functions used for reliable operation
- Safe for real-time audio processing

## Compatibility

- Full compatibility with crow's Input.lua API
- All detection modes supported
- Voltage ranges match crow specification (±6V)  
- Callback system identical to crow

## Build Integration

The detection system is fully integrated into the CMake build:
- `lib/detect.c/.h` compiled automatically
- Test script compiled to bytecode header (`test_detection.h`)
- Available via serial command: `test_detection`

## Future Enhancements

Potential improvements for future versions:
- Advanced frequency detection algorithms
- Spectral analysis capabilities  
- Polyphonic note detection
- Custom detection mode scripting
- MIDI integration

---

The detection system provides a solid foundation for interactive musical applications and matches crow's capabilities while leveraging the Workshop Computer's hardware efficiently.
