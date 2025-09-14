-- Test script for ASL (Attack/Sustain/Release) system
-- This script demonstrates basic ASL envelope functionality

function init()
    print("ASL Test Script initialized")
    
    -- Simple ASL envelope test
    output[1].action = to(3, 0.5, 'sine')
    
    -- Start a metro to trigger envelopes
    metro[1].start(2.0)  -- 2 second interval
end

function metro_handler(id, stage) 
    if id == 1 then
        print("Metro tick - triggering envelope")
        
        -- Create a simple attack-release envelope
        -- Attack to 4V over 0.2s with sine curve
        -- Then release to 0V over 1.5s with exponential curve
        output[1].action = {
            to(4, 0.2, 'sine'),    -- Attack
            to(0, 1.5, 'exp')      -- Release
        }
        
        -- Also test slopes_toward function directly
        slopes_toward(2, math.random() * 6 - 3, 1.0, 'linear')  -- Random voltage on output 2
    end
end

-- Test different envelope shapes
function step()
    -- This could be used for continuous envelope updates
    -- For now, just let metros handle timing
end

print("ASL test script loaded - metro will trigger envelopes every 2 seconds")
