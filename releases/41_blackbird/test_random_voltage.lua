-- Test script for random voltage output on rising edge detection
print("Testing Random Voltage Output on Rising Edge")
print("=============================================")

-- Debug: Check if set_input_change function exists
print("DEBUG: set_input_change type:", type(set_input_change))
if set_input_change then
    print("DEBUG: set_input_change function is available")
else
    print("DEBUG: set_input_change function is NOT available!")
end

-- Configure input 1 for rising edge detection at 5V threshold
print("DEBUG: About to call input[1].mode('change', 1.0, 0.1, 'rising')")
input[1].mode('change', 1.0, 0.1, 'rising')
print("DEBUG: mode() call completed")

-- Set up the change handler to generate random voltage on rising edge
input[1].change = function(state)
        local random_voltage = math.random() * 2.0  -- 0 to 2V
        output[1].volts = random_voltage
        print("Rising edge detected! Output voltage set to: " .. string.format("%.3f", random_voltage) .. "V")
end

-- Initialize output to 0V
-- output[1].volts = 0.0

print("Random voltage generator ready!")
print("Input 1 threshold: 1.0V (rising edge detection)")
print("Output 1: Random 0-2V on each rising edge")
print("Feed a signal above 1V to input 1 to trigger")
-- print("Current output voltage: " .. string.format("%.3f", output[1].volts) .. "V")
