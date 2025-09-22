-- Test script for crow output functionality on Blackbird
-- This demonstrates crow-compatible assignment syntax

print("Testing Blackbird Crow output emulation...")

-- Test basic voltage output using crow-style assignment syntax
print("Setting outputs to different voltages:")

output[1].volts = 0.0    -- Output 1 to 0V
output[2].volts = 2.5    -- Output 2 to 2.5V  
output[3].volts = -1.2   -- Output 3 to -1.2V
output[4].volts = 5.0    -- Output 4 to 5V

print("Current output voltages:")
for i = 1, 4 do
    local v = output[i].volts
    print("output[" .. i .. "] = " .. v .. "V")
end

-- Test voltage clamping (should clamp to ±6V range)
print("\nTesting voltage clamping:")
output[1].volts = 10.0   -- Should clamp to 6V
output[2].volts = -10.0  -- Should clamp to -6V

print("After clamping test:")
print("output[1] = " .. output[1].volts .. "V (should be 6.0)")
print("output[2] = " .. output[2].volts .. "V (should be -6.0)")

-- Test voltage sweep
print("\nPerforming voltage sweep on output[1]:")
for v = -5, 5, 1 do
    output[1].volts = v
    print("Set to " .. v .. "V, read back: " .. output[1].volts .. "V")
end

print("\nOutput test complete!")
print("This script now uses crow-compatible assignment syntax!")
