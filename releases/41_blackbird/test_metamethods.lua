-- Test script for crow-compatible metamethod syntax
-- This tests the new assignment-based API: output[n].volts = value

print("Testing Blackbird Crow metamethod implementation...")
print("This should work with crow-compatible assignment syntax")

-- Test assignment syntax (new metamethod approach)
print("\n=== Testing Assignment Syntax ===")
print("Setting outputs using assignment syntax...")

output[1].volts = 0.0    -- Set output 1 to 0V
output[2].volts = 2.5    -- Set output 2 to 2.5V  
output[3].volts = -1.2   -- Set output 3 to -1.2V
output[4].volts = 5.0    -- Set output 4 to 5V

-- Test reading syntax (new metamethod approach)
print("\nReading outputs using property access:")
print("output[1].volts = " .. output[1].volts)
print("output[2].volts = " .. output[2].volts)
print("output[3].volts = " .. output[3].volts)
print("output[4].volts = " .. output[4].volts)

-- Test voltage clamping with assignment syntax
print("\n=== Testing Voltage Clamping ===")
output[1].volts = 10.0   -- Should clamp to 6V
output[2].volts = -10.0  -- Should clamp to -6V

print("After clamping test:")
print("output[1].volts = " .. output[1].volts .. " (should be 6.0)")
print("output[2].volts = " .. output[2].volts .. " (should be -6.0)")

-- Test voltage sweep with assignment syntax
print("\n=== Testing Voltage Sweep ===")
print("Performing voltage sweep on output[1] using assignment syntax:")
for v = -5, 5, 1 do
    output[1].volts = v
    print("Set to " .. v .. "V, read back: " .. output[1].volts .. "V")
end

-- Test mixed usage
print("\n=== Testing Crow-style Usage Patterns ===")
-- Typical crow script patterns
output[3].volts = 1.0  -- Note voltage
output[4].volts = output[3].volts * 0.5  -- Harmony

print("Note: " .. output[3].volts .. "V")
print("Harmony: " .. output[4].volts .. "V")

-- Test error handling with unknown properties
print("\n=== Testing Error Handling ===")
output[1].unknown_property = 42  -- Should be silently ignored
print("Setting unknown property completed (should be ignored)")

local unknown_val = output[1].unknown_property  -- Should return nil
print("Reading unknown property: " .. tostring(unknown_val) .. " (should be nil)")

print("\nMetamethod test complete!")
print("If you see voltage values, the crow-compatible assignment syntax is working!")
