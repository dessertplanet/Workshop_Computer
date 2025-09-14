-- Test script for Phase 3 Hardware Abstraction Layer
-- Tests voltage scaling and I/O mapping

-- Initialize test
function init()
    print("Hardware Abstraction Test Started")
    print("Testing voltage scaling and I/O mapping")
    
    -- Set initial output voltages
    output[1].volts = 0.0   -- Audio Out 1 → 0V
    output[2].volts = 0.0   -- Audio Out 2 → 0V  
    output[3].volts = 0.0   -- CV Out 1 → 0V
    output[4].volts = 0.0   -- CV Out 2 → 0V
    
    -- Start test metro
    metro[1].event = voltage_sweep_test
    metro[1].time = 0.5     -- 500ms intervals
    metro[1]:start()
    
    test_phase = 1
    test_counter = 0
end

-- Test counter and phase tracking
test_phase = 1
test_counter = 0

-- Voltage sweep test function
function voltage_sweep_test(stage)
    test_counter = test_counter + 1
    
    if test_phase == 1 then
        -- Phase 1: Test audio outputs with sine wave voltages
        local angle = (test_counter * 0.1) % (2 * math.pi)
        local voltage = math.sin(angle) * 3.0  -- ±3V sine wave
        
        output[1].volts = voltage
        output[2].volts = -voltage  -- Inverted on output 2
        
        print(string.format("Audio Test: Out1=%.3fV, Out2=%.3fV", voltage, -voltage))
        
        if test_counter >= 20 then  -- Run for 10 seconds
            test_phase = 2
            test_counter = 0
        end
        
    elseif test_phase == 2 then
        -- Phase 2: Test CV outputs with ramp voltages
        local ramp_voltage = ((test_counter % 20) / 10.0) - 1.0  -- -1V to +1V ramp
        
        output[3].volts = ramp_voltage
        output[4].volts = ramp_voltage * 2.0  -- Double voltage on output 4
        
        print(string.format("CV Test: Out3=%.3fV, Out4=%.3fV", ramp_voltage, ramp_voltage * 2.0))
        
        if test_counter >= 40 then  -- Run for 20 seconds
            test_phase = 3
            test_counter = 0
        end
        
    elseif test_phase == 3 then
        -- Phase 3: Input monitoring test
        local in1 = input[1].volts
        local in2 = input[2].volts
        
        -- Echo inputs to outputs with gain
        output[1].volts = in1 * 0.5  -- Half gain
        output[2].volts = in2 * 0.5
        
        print(string.format("Input Test: In1=%.3fV→Out1=%.3fV, In2=%.3fV→Out2=%.3fV", 
                            in1, in1 * 0.5, in2, in2 * 0.5))
        
        if test_counter >= 20 then  -- Run for 10 seconds
            test_phase = 4
            test_counter = 0
        end
        
    else
        -- Phase 4: Range testing (test ±6V limits)
        local extreme_voltages = {-6.0, -3.0, 0.0, 3.0, 6.0, 8.0}  -- Include out-of-range
        local volt_index = (test_counter % #extreme_voltages) + 1
        local test_voltage = extreme_voltages[volt_index]
        
        output[1].volts = test_voltage
        output[2].volts = test_voltage
        output[3].volts = test_voltage
        output[4].volts = test_voltage
        
        print(string.format("Range Test: All outputs set to %.1fV", test_voltage))
        
        if test_counter >= 30 then
            print("Hardware Abstraction Test Complete!")
            metro[1]:stop()
            
            -- Reset outputs to 0V
            output[1].volts = 0.0
            output[2].volts = 0.0
            output[3].volts = 0.0
            output[4].volts = 0.0
        end
    end
end

-- Input change handler (if detection is working)
function input_change(channel, value)
    print(string.format("Input %d changed to %.3fV", channel, value))
end
