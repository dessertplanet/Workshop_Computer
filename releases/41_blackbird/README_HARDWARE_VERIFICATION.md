# Hardware Verification Firmware

This firmware adds comprehensive hardware verification to isolate the root cause of the detection system failure.

## LED Debug Indicators

The firmware uses LEDs to show exactly where the system is working or failing:

### LED 5: Heartbeat (ProcessSample Running)
- **Purpose**: Proves the main audio processing loop is running
- **Pattern**: Blinks every 1 second (ON for 1 second, OFF for 1 second)
- **If NOT blinking**: ProcessSample() isn't running - serious firmware issue

### LED 4: Input Activity Detection
- **Purpose**: Shows when significant input signals are detected
- **Threshold**: Any input above ±0.5V on either channel
- **Behavior**: ON when signal present, OFF when quiet
- **If NEVER lights**: ADC/input hardware issue

### LED 3: Detection Processing Activity
- **Purpose**: Shows detection system is being called
- **Pattern**: Flickers rapidly (every 32 samples = ~1.5kHz)
- **If NOT flickering**: Detection processing isn't running

### LEDs 0, 1, 2: Event System Status (from previous debug)
- **LED 0**: Event handler called (proves event system works)
- **LED 1**: Lua callback execution started
- **LED 2**: Lua callback completed successfully

## Serial Debug Output

The firmware outputs detailed debug information via USB serial:

### Hardware Status (every ~20ms)
```
HW: raw1=<value> raw2=<value>, volt1=<voltage> volt2=<voltage>
```

### Detection Processing (every detection cycle)
```
DETECT: Processing ch1=<voltage> ch2=<voltage>
```

### Event Processing
```
EVENTS: Processing event queue
```

## Troubleshooting

### Scenario 1: No LEDs at all
- **Problem**: Power/firmware not loading
- **Next**: Check USB connection, try different firmware

### Scenario 2: Only LED 5 (heartbeat) works
- **Problem**: ProcessSample running but no input signals detected
- **Next**: Test with audio input, check ADC hardware

### Scenario 3: LEDs 5+4 work, LED 3 doesn't flicker
- **Problem**: Input detected but detection processing not running
- **Next**: Check Detect_process_sample() function

### Scenario 4: LEDs 5+4+3 work, no LEDs 0+1+2
- **Problem**: Detection processing but no events generated
- **Next**: Check detection thresholds, callback registration

### Scenario 5: LED 0 lights but not 1+2
- **Problem**: Events posted but Lua callbacks failing
- **Next**: Check Lua error messages in serial output

## Testing Procedure

1. **Flash firmware**: Copy `UF2/blackbird.uf2` to Workshop Computer
2. **Check heartbeat**: LED 5 should blink every second
3. **Test input**: Connect audio/CV to inputs, LED 4 should respond
4. **Monitor serial**: Connect USB serial terminal (115200 baud)
5. **Check detection**: LED 3 should flicker when detection runs
6. **Verify events**: Set up change detection, test with input signals

## Expected Results

With working hardware and no input:
- **LED 5**: Blinking (heartbeat)
- **LED 4**: OFF (no input)
- **LED 3**: Flickering (detection processing)
- **LEDs 0,1,2**: OFF (no events)

With input signal above 0.5V:
- **LED 5**: Blinking (heartbeat)
- **LED 4**: ON (input detected)
- **LED 3**: Flickering (detection processing)
- **LEDs 0,1,2**: May flash if change detection configured

This systematic approach will pinpoint exactly where the detection chain breaks.
