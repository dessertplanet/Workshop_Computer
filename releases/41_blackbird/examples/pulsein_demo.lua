-- Pulse Input Detection Demo
-- This example shows how to detect changes on the pulse inputs
-- Even very short pulses (< 100us) will be detected reliably at 48kHz

-- Enable change detection on pulse input 1
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'both'  -- 'both' (default), 'rising', or 'falling'

-- Set up callback for pulse input 1
bb.pulsein[1].change = function(state)
    print("Pulse 1 changed to: " .. tostring(state))
    
    if state then
        print("  -> Rising edge detected (LOW to HIGH)")
        -- Example: trigger an output action
        output[1].volts = 5.0
    else
        print("  -> Falling edge detected (HIGH to LOW)")
        output[1].volts = 0.0
    end
end

-- Enable change detection on pulse input 2
bb.pulsein[2].mode = 'change'
bb.pulsein[2].direction = 'rising'  -- Only detect rising edges

-- Set up callback for pulse input 2 with a counter
local pulse2_count = 0

bb.pulsein[2].change = function(state)
    -- With direction='rising', state will always be true
    pulse2_count = pulse2_count + 1
    print("Pulse 2 rising edge #" .. pulse2_count)
    
    -- Flash LED on each pulse
    output[2].volts = 3.0
    output[2](function() output[2].volts = 0 end, 0.05) -- 50ms pulse
end

-- You can also query the current state at any time
function init()
    print("Pulse input detection initialized")
    print("Pulse 1 mode: " .. bb.pulsein[1].mode)
    print("Pulse 2 mode: " .. bb.pulsein[2].mode)
    print("Current states:")
    print("  Pulse 1: " .. tostring(bb.pulsein[1].state))
    print("  Pulse 2: " .. tostring(bb.pulsein[2].state))
end

-- Disable change detection (set mode back to 'none')
-- bb.pulsein[1].mode = 'none'
-- bb.pulsein[2].mode = 'none'
