-- Test script to verify stream timing fix and sample-accurate change detection

print("Starting stream timing and change detection test...")

-- Test 1: Stream timing accuracy
print("\n=== Test 1: Stream Timing ===")
print("Setting input[1] to stream mode with 2-second intervals...")

-- Set up stream mode with 2-second interval
input[1].mode('stream', 2.0)

-- Counter for stream events
local stream_count = 0
local stream_start_time = time()

-- Override the default stream handler to track timing
function stream_handler(channel, value)
    stream_count = stream_count + 1
    local elapsed = time() - stream_start_time
    
    print(string.format("Stream #%d: ch%d=%.3fV at t=%.3fs (expected: %.1fs)", 
          stream_count, channel, value, elapsed, stream_count * 2.0))
    
    -- Check timing accuracy (allow ±0.1s tolerance)
    local expected_time = stream_count * 2.0
    local error = math.abs(elapsed - expected_time)
    
    if error < 0.1 then
        print("  ✓ Timing accurate!")
    else
        print(string.format("  ✗ Timing error: %.3fs (should be ~%.1fs)", error, expected_time))
    end
end

-- Test 2: Change detection sample accuracy  
print("\n=== Test 2: Change Detection ===")
print("Setting input[2] to change mode...")

-- Set up change detection
input[2].mode('change', 1.0, 0.1, 'both')

-- Counter for change events
local change_count = 0

-- Override the default change handler to track events
function change_handler(channel, state)
    change_count = change_count + 1
    local state_str = (state == 1) and "HIGH" or "LOW"
    
    print(string.format("Change #%d: ch%d=%s", change_count, channel, state_str))
end

print("Stream mode: expecting events every 2 seconds")
print("Change mode: will trigger on voltage crossings above/below 1V")
print("\nMonitoring for 10 seconds... (connect signals to inputs to test)")

-- Monitor for 10 seconds
local start_time = time()
while (time() - start_time) < 10.0 do
    -- Let the system run and process events
    -- The detection callbacks will fire automatically
end

print(string.format("\n=== Results ==="))
print(string.format("Total stream events: %d (expected: ~5)", stream_count))
print(string.format("Total change events: %d (depends on input signals)", change_count))

-- Reset to none mode to clean up
input[1].mode('none')
input[2].mode('none')

print("Test completed!")
