-- Test multicore safety fixes
-- This test verifies that callbacks can run without crashing

print("=== MULTICORE SAFETY TEST ===")
print("Testing callback stability after fixes...")

-- Simple callback that just increments a counter
local callback_count = 0

function change_handler(channel, state)
    callback_count = callback_count + 1
    print("SAFE CALLBACK " .. callback_count .. ": ch" .. channel .. " = " .. tostring(state))
    
    -- Test basic functionality without complex operations
    if callback_count <= 5 then
        print("  - No sleep, no complex operations")
        print("  - Callback " .. callback_count .. " completed safely")
    end
    
    -- After 5 callbacks, report success
    if callback_count == 5 then
        print("SUCCESS: 5 callbacks completed without crash!")
        print("Multicore safety fixes are working.")
    end
end

-- Set up basic change detection on input 1
print("Setting up change detection on input[1]...")
input[1].mode('change', 2.0, 0.5, 'both')

print("Change detection configured:")
print("  - Threshold: 2.0V")
print("  - Hysteresis: 0.5V") 
print("  - Direction: both")
print("")
print("Apply voltage changes to input 1 to test...")
print("Expected: Stable callbacks without system freeze")
