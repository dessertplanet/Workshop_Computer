# Sample & Hold Test for Blackbird Crow Emulator

## Overview

This test validates the complete event/trigger/detect flow by implementing a basic sample and hold function where clock pulses trigger random voltage generation.

## What Was Fixed

**✅ Critical Event Queue Bug Fixed**
- Fixed the event queue index ordering in `lib/events.c`
- Events now process correctly instead of being stuck

**✅ RP2040 Interrupt Protection Added**
- Added proper `save_and_disable_interrupts()` protection
- Prevents race conditions between cores

**✅ Sample & Hold Test Integration**
- Embedded test script compiled to bytecode
- Available via Lua command `test_sample_hold()`

## Hardware Setup

1. **Flash the UF2**: Copy `UF2/blackbird.uf2` to your Workshop Computer
2. **Connect Clock Input**: Send clock/trigger pulses to **Input 1** 
3. **Connect Output**: Monitor **Output 1** for random voltages
4. **Serial Connection**: Connect via USB for control

## Testing the Sample & Hold

### 1. Connect via Serial Terminal
- Baud rate: 115200
- Send commands to test functionality

### 2. Load the Test Script
```lua
test_sample_hold()
```

This will:
- Initialize the sample & hold system
- Set up change detection on input 1 (threshold: 2.5V, hysteresis: 0.5V)
- Display random number test results
- Set up the trigger callback

### 3. Expected Behavior

**Clock Input → Output Response:**
- Send clock pulse >3V to Input 1
- Output 1 immediately changes to random voltage (0-2V range)
- Each trigger generates a new random voltage
- Serial monitor shows: `TRIGGER! New voltage: X.XXXv`

### 4. Manual Testing Commands

```lua
-- Check current output voltage
print("Output 1:", output[1].volts)

-- Manually set a voltage to test output
output[1].volts = 1.5

-- Test random generation
for i=1,5 do print(math.random() * 2.0) end

-- Check input voltage
print("Input 1:", input[1]:volts())
```

## Expected Serial Output

```
=== Sample & Hold Test ===
Connect clock/trigger to input 1
Random voltage 0-2V will appear on output 1

Testing random generation:
  Test random 1: 0.372V
  Test random 2: 1.849V
  Test random 3: 0.103V
  Test random 4: 1.627V
  Test random 5: 0.891V

Input 1: change mode, thresh 2.500, hyst 0.500, dir both

Sample & Hold ready!
Send clock pulses to input 1 (>3V for trigger)
Watch output 1 for random voltages
Current output: 0.000V
Sample & hold test loaded successfully!

-- When you send a clock pulse:
CALLBACK #1: ch1 state=HIGH
TRIGGER! New voltage: 1.234V

CALLBACK #2: ch1 state=LOW  
Trigger released
```

## Troubleshooting

**No response to triggers:**
- Check input voltage >3V for reliable triggering
- Verify connections to Input 1
- Check serial output for detection messages

**No voltage on output:**
- Check Output 1 connection
- Verify hardware DAC routing (AudioOut1/CVOut1)
- Test manual voltage setting: `output[1].volts = 3.0`

**Event system issues:**
- The critical event queue bug has been fixed
- Detection callbacks should now work reliably
- Check for "event queue full!" messages

## Technical Details

### Clock Detection Parameters
- **Threshold**: 2.5V (trigger point)
- **Hysteresis**: 0.5V (prevents double-triggers)
- **Direction**: Both rising and falling edges detected
- **Processing Rate**: ~1.5kHz (decimated from 48kHz audio rate)

### Random Voltage Generation
- **Range**: 0.0V to 2.0V
- **Resolution**: 12-bit DAC (~2.93mV steps)
- **Update Rate**: Immediate on trigger
- **Random Seed**: Initialized from system time

### Event Flow
1. **Hardware**: Clock pulse → AudioIn1()
2. **Detection**: `Detect_process_sample()` at 1.5kHz
3. **Event Queue**: `detection_callback()` → `event_post()`
4. **Lua Callback**: `change_handler()` → `input[1].change()`
5. **Output**: `output[1].volts = random_voltage`

## Success Criteria

✅ **Clock Detection Working**: Serial shows detection messages  
✅ **Event Processing Fixed**: Callbacks execute reliably  
✅ **Random Generation**: Each trigger produces new voltage  
✅ **Output Control**: Voltage changes visible on Output 1  
✅ **Crow Compatibility**: Uses standard crow input/output API  

The sample and hold test validates that the entire crow emulator trigger/event/output chain is working correctly and is ready for more complex musical applications.
