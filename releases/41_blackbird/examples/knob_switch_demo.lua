-- knob_switch_demo.lua
-- Demo script showing knob and switch functionality on Workshop Computer

function init()
    print("Knob & Switch Demo")
    print("==================")
    print("Main Knob -> Output 1 voltage (0-6V)")
    print("X Knob -> Output 2 slew time")
    print("Y Knob -> LFO frequency")
    print("Switch: down=sine, middle=linear, up=expo")
    print("")
    
    -- Set up switch change handler
    switch.change = function(position)
        print("Switch: " .. position)
        
        -- Change output shape based on switch position
        if position == 'down' then
            output[1].shape = 'sine'
            output[2].shape = 'sine'
        elseif position == 'middle' then
            output[1].shape = 'linear'
            output[2].shape = 'linear'
        else  -- up
            output[1].shape = 'expo'
            output[2].shape = 'expo'
        end
    end
    
    -- Set up a metro to read knobs and update outputs
    metro[1].event = function()
        -- Main knob directly controls output 1 voltage (0-6V)
        output[1].volts = knob.main * 6
        
        -- X knob controls output 2 slew (0.01-2 seconds)
        output[2].slew = knob.x * 2 + 0.01
        
        -- Y knob controls LFO frequency (0.1-5 Hz)
        local freq = knob.y * 4.9 + 0.1
        metro[2].time = 1 / freq
    end
    metro[1].time = 0.05  -- Check knobs at 20Hz
    metro[1]:start()
    
    -- Set up LFO on metro 2
    metro[2].event = function()
        output[2](function()
            to(5, 0.5)
            to(0, 0.5)
        end)
    end
    metro[2].time = 1.0  -- Will be updated by Y knob
    metro[2]:start()
    
    print("Demo running!")
end
