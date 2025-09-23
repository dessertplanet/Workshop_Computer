-- Hardware Change Detection Test with Debug Output
-- This test adds diagnostic output to help identify where the issue is

print("Setting up hardware change detection test with diagnostics...")

-- First, let's check if we can read input values
print("Current input readings:")
print("Input 1 voltage: " .. string.format("%.3f", input[1].volts) .. "V")
print("Input 2 voltage: " .. string.format("%.3f", input[2].volts) .. "V")

-- Set up a very verbose callback
input[1].change = function(state)
    print("*** INPUT CHANGE CALLBACK TRIGGERED ***")
    print("Channel: 1")
    print("New state: " .. tostring(state))
    print("Current voltage: " .. string.format("%.3f", input[1].volts) .. "V")
    if state then
        print("==> INPUT 1 WENT HIGH (above 1.0V)")
    else
        print("==> INPUT 1 WENT LOW (below 0.9V)")
    end
    print("*** END CALLBACK ***")
    print()
end

-- Set change detection parameters with lower threshold for testing
input[1].mode('change', 0.5, 0.1, 'both')

print("Change detection configured:")
print("- Channel: 1")
print("- Threshold: 0.5V (lowered for easier testing)")
print("- Hysteresis: 0.1V")
print("- Direction: both (rising and falling)")
print("")
print("Try these tests:")
print("1. Apply > 0.5V to trigger HIGH")
print("2. Apply < 0.4V to trigger LOW") 
print("3. Check if callbacks appear above")
print("")

-- Add a simple voltage monitor function (call manually if needed)
local function monitor_inputs()
    print("Monitor: Input1=" .. string.format("%.3fV", input[1].volts) .. 
          ", Input2=" .. string.format("%.3fV", input[2].volts))
end

-- Make monitor function available globally
_G.monitor_inputs = monitor_inputs

print("Manual monitoring available - type 'monitor_inputs()' to check voltages")
print("Apply signals now and watch for callbacks!")
print("Expected: Verbose callback messages when crossing 0.5V threshold")
