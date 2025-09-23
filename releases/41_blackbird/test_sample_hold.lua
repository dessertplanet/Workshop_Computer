-- Sample & Hold Test for Blackbird Crow Emulator
-- Clock input on input[1] triggers random voltage 0-2V on output[1]

print("=== Sample & Hold Test ===")
print("Connect clock/trigger to input 1")
print("Random voltage 0-2V will appear on output 1")

-- Initialize random seed using time
math.randomseed(math.floor(time() * 1000) % 2147483647)

-- Test random generation first
print("Testing random generation:")
for i = 1, 5 do
    local rand_val = math.random() * 2.0
    print(string.format("  Test random %d: %.3fV", i, rand_val))
end

-- Set up change detection on input 1 with sample & hold behavior
input[1].change = function(state)
    if state then -- Rising edge (trigger detected)
        -- Generate random voltage between 0 and 2V
        local new_voltage = math.random() * 2.0
        
        -- Set output 1 to the new random voltage
        output[1].volts = new_voltage
        
        print(string.format("TRIGGER! New voltage: %.3fV", new_voltage))
    else
        print("Trigger released")
    end
end

-- Set change detection parameters
-- Threshold: 2.5V, Hysteresis: 0.5V, Direction: both (rising and falling)
input[1].mode('change',1.0,0.1,'rising')

print("")
print("Sample & Hold ready!")
print("Send clock pulses to input 1 (>3V for trigger)")
print("Watch output 1 for random voltages")
print("Current output: " .. tostring(output[1].volts) .. "V")
