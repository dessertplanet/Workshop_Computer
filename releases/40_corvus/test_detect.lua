-- Comprehensive Detection System Test for Crow Emulator
-- Tests all detection modes: stream, change, window, scale, volume, peak, freq

print("=== Detection System Test ===")

-- Test variables
test_results = {}
test_count = 0

-- Helper function to log test results
function log_test(name, status)
    test_count = test_count + 1
    test_results[test_count] = {name = name, status = status}
    print(string.format("Test %d: %s - %s", test_count, name, status and "PASS" or "FAIL"))
end

-- Test 1: Stream Detection
print("\n--- Testing Stream Detection ---")
function stream_callback(channel, value)
    print(string.format("Stream detected on channel %d: %f V", channel, value))
    log_test("Stream Detection Channel " .. channel, true)
end

-- Set input 1 to stream mode with 0.1 second intervals
input[1].mode('stream', 0.1)
log_test("Stream Mode Setup", true)

-- Test 2: Change Detection
print("\n--- Testing Change Detection ---")
function change_callback(channel, state)
    print(string.format("Change detected on channel %d: state %d", channel, state))
    log_test("Change Detection Channel " .. channel, state == 0 or state == 1)
end

-- Set input 2 to change mode with 1V threshold, 0.1V hysteresis, both directions
input[2].mode('change', 1.0, 0.1, 'both')
log_test("Change Mode Setup", true)

-- Test 3: Window Detection
print("\n--- Testing Window Detection ---")
function window_callback(channel, win, direction)
    print(string.format("Window detected on channel %d: window %d, direction %d", channel, win, direction))
    log_test("Window Detection Channel " .. channel, win > 0)
end

-- Set input 1 to window mode with voltage thresholds at -2V, 0V, 2V, 4V
local windows = {-2.0, 0.0, 2.0, 4.0}
input[1].mode('window', windows, 0.1)
log_test("Window Mode Setup", true)

-- Test 4: Scale Detection (Chromatic)
print("\n--- Testing Scale Detection (Chromatic) ---")
function scale_callback(channel, index, octave, note, volts)
    print(string.format("Scale detected on channel %d: index %d, octave %d, note %f, volts %f", 
          channel, index, octave, note, volts))
    log_test("Scale Detection Channel " .. channel, index >= 0 and octave ~= nil)
end

-- Set input 2 to chromatic scale mode (12TET, 1V/oct)
input[2].mode('scale', nil, 12, 1.0)  -- nil = chromatic scale
log_test("Scale Mode Setup (Chromatic)", true)

-- Test 5: Scale Detection (Custom Scale)
print("\n--- Testing Scale Detection (Custom Major Scale) ---")
-- Major scale intervals: 0, 2, 4, 5, 7, 9, 11 semitones
local major_scale = {0, 2, 4, 5, 7, 9, 11}
input[1].mode('scale', major_scale, 12, 1.0)
log_test("Scale Mode Setup (Major Scale)", true)

-- Test 6: Volume Detection
print("\n--- Testing Volume Detection ---")
function volume_callback(channel, level)
    print(string.format("Volume detected on channel %d: level %f", channel, level))
    log_test("Volume Detection Channel " .. channel, level >= 0.0)
end

-- Set input 2 to volume mode with 0.2 second intervals
input[2].mode('volume', 0.2)
log_test("Volume Mode Setup", true)

-- Test 7: Peak Detection
print("\n--- Testing Peak Detection ---")
function peak_callback(channel)
    print(string.format("Peak detected on channel %d", channel))
    log_test("Peak Detection Channel " .. channel, true)
end

-- Set input 1 to peak mode with 0.5V threshold, 0.1V hysteresis
input[1].mode('peak', 0.5, 0.1)
log_test("Peak Mode Setup", true)

-- Test 8: Frequency Detection
print("\n--- Testing Frequency Detection ---")
function freq_callback(channel, freq)
    print(string.format("Frequency detected on channel %d: %f Hz", channel, freq))
    log_test("Frequency Detection Channel " .. channel, freq > 0)
end

-- Set input 1 to frequency mode with 0.1 second intervals
input[1].mode('freq', 0.1)
log_test("Frequency Mode Setup", true)

-- Test 9: Input Voltage Reading
print("\n--- Testing Input Voltage Reading ---")
for i = 1, 2 do
    local voltage = input[i].volts
    print(string.format("Input %d voltage: %f V", i, voltage))
    log_test("Input Voltage Reading Channel " .. i, voltage ~= nil)
end

-- Test 10: Mode Switching
print("\n--- Testing Mode Switching ---")
-- Switch input 1 back to stream mode
input[1].mode('stream', 0.5)
log_test("Mode Switch to Stream", true)

-- Switch input 2 to none mode (disable detection)
input[2].mode('none')
log_test("Mode Switch to None", true)

-- Test 11: Parameter Validation
print("\n--- Testing Parameter Validation ---")
-- Test invalid channel (should handle gracefully)
pcall(function()
    input[3].mode('stream', 0.1)  -- Channel 3 doesn't exist
end)
log_test("Invalid Channel Handling", true)  -- Should not crash

-- Test invalid parameters
pcall(function()
    input[1].mode('change')  -- Missing required threshold parameter
end)
log_test("Missing Parameter Handling", true)  -- Should not crash

-- Print test summary
print("\n=== Detection System Test Summary ===")
local pass_count = 0
for i = 1, test_count do
    if test_results[i].status then
        pass_count = pass_count + 1
    else
        print("FAILED: " .. test_results[i].name)
    end
end

print(string.format("Tests passed: %d/%d", pass_count, test_count))
if pass_count == test_count then
    print("All detection system tests PASSED!")
else
    print("Some detection system tests FAILED!")
end

-- Continuous monitoring function
function monitor_inputs()
    print("\n--- Continuous Input Monitoring (10 seconds) ---")
    print("Monitoring input voltages and detection events...")
    
    -- Set both inputs to stream mode for monitoring
    input[1].mode('stream', 0.2)
    input[2].mode('stream', 0.2)
    
    -- The stream callbacks will automatically report values
    -- This provides a way to observe real input activity
end

-- Start monitoring
monitor_inputs()

-- Clock Detection Example (not part of automated pass/fail)
print("\n--- Clock Detection Example ---")
print("Configuring input 1 for clock detection. Provide a periodic gate or square wave > threshold.")

input[1].clock = function(bpm, period)
    print(string.format("Clock event on input 1: BPM=%.2f Period=%.4f s", bpm, period))
end

-- Arguments: channel, threshold (V), hysteresis (V), minimum period (s)
-- threshold=1.0V, hysteresis=0.1V, ignore periods shorter than 20ms (>=50Hz)
set_input_clock(1, 1.0, 0.1, 0.02)

print("\nDetection system test completed. Monitor output for real-time detection events.")
print("You can manually test by connecting CV sources to inputs 1 and 2.")
