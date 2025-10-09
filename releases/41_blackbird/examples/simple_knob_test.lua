-- simple_knob_test.lua
-- Simplest possible test of knobs and switch

function init()
    print("Simple Knob Test")
    print("Knobs control output voltages")
    print("Switch changes between 3 modes")
    print("")
    
    -- Print initial switch position
    print("Switch: " .. switch.position)
    
    -- React to switch changes
    switch.change = function(pos)
        print("Switch -> " .. pos)
    end
    
    -- Update outputs based on knobs
    metro[1].event = function()
        output[1].volts = knob.main * 6  -- Main: 0-6V
        output[2].volts = knob.x * 6     -- X: 0-6V
        output[3].volts = knob.y * 6     -- Y: 0-6V
    end
    metro[1].time = 0.05  -- 20Hz
    metro[1]:start()
end
