-- Quick verification that stream timing is now correct

print("=== Stream Timing Fix Verification ===")

-- Test with a very short interval to make the fix obvious
print("Testing stream mode with 0.5 second intervals...")
print("Before fix: would fire every ~0.016s (32x too fast)")
print("After fix: should fire every 0.5s (correct)")

local count = 0
local start_time = time()

function stream_handler(channel, value) 
    count = count + 1
    local elapsed = time() - start_time
    print(string.format("Event #%d at %.3fs (expected: %.1fs)", count, elapsed, count * 0.5))
end

input[1].mode('stream', 0.5)

-- Wait for a few events
while count < 6 do
    -- Events will fire automatically
end

local total_time = time() - start_time
print(string.format("\n6 events in %.3fs (expected: 3.0s)", total_time))

if math.abs(total_time - 3.0) < 0.2 then
    print("✓ PASS: Timing is correct!")
else
    print("✗ FAIL: Timing is still wrong")
end

-- Clean up
input[1].mode('none')
print("Verification complete.")
