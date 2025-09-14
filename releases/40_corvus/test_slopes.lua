-- Test script for the slopes/envelopes system
-- This demonstrates crow's envelope capabilities with different curve shapes

print("Testing Crow Slopes System")

function init()
    print("Slopes test initialized")
    
    -- Start a simple envelope demonstration
    -- Output 1: Linear envelope from 0V to 3V over 2 seconds
    -- This would be done using slopes in the C layer via lua bindings
    
    print("Demo: Linear envelope on output 1")
    output[1].volts = 0
    
    -- Set up a metro to trigger envelopes every 3 seconds
    metro[1].time = 3.0
    metro[1].event = envelope_trigger
    metro[1]:start()
    
    print("Metro started - envelope will trigger every 3 seconds")
end

function envelope_trigger(stage)
    print("Triggering envelope - stage: " .. tostring(stage))
    
    -- Demo different envelope shapes on different outputs
    if stage == 1 then
        -- Linear rise on output 1
        print("Output 1: Linear rise 0V -> 3V over 1.5s")
        output[1].volts = 0
        -- slopes_toward(1, 3.0, 1500, 'linear', envelope_complete_1)
        
        -- Sine rise on output 2  
        print("Output 2: Sine rise 0V -> 2V over 1.5s")
        output[2].volts = 0
        -- slopes_toward(2, 2.0, 1500, 'sine', envelope_complete_2)
        
    else
        -- Fall back to zero
        print("Envelope fall - returning to 0V")
        output[1].volts = 0
        output[2].volts = 0
    end
end

function envelope_complete_1()
    print("Output 1 envelope complete - starting decay")
    -- slopes_toward(1, 0.0, 500, 'exp', nil)
end

function envelope_complete_2() 
    print("Output 2 envelope complete - starting decay")
    -- slopes_toward(2, 0.0, 500, 'exp', nil)
end

-- For now, without lua bindings, demonstrate basic voltage output
function step()
    -- Simple test pattern for outputs 3 and 4 (CV outputs)
    local t = time_us_64() / 1000000.0  -- Convert to seconds
    
    -- Output 3: Slow sine wave
    output[3].volts = 2 * math.sin(t * 0.5)  -- 0.5 Hz sine, ±2V
    
    -- Output 4: Triangle wave using basic math
    local phase = (t * 0.2) % 1.0  -- 0.2 Hz triangle
    if phase < 0.5 then
        output[4].volts = 4 * phase - 1     -- Rise from -1 to 1V
    else
        output[4].volts = 3 - 4 * phase     -- Fall from 1 to -1V
    end
end

print("Slopes test script loaded")
