-- Test script for crow metro functionality
-- This demonstrates the metro system working with voltage output

print("Metro test script loaded")

-- Initialize counter
counter = 0

function init()
    print("init() called - starting metro test")
    -- Start metro 1 with 1 second period
    metro[1].start(1.0)
    output[1].volts = 0
end

function metro_handler(id, stage)
    print("metro_handler called: metro " .. id .. ", stage " .. stage)
    
    if id == 1 then
        counter = counter + 1
        -- Alternate output voltage between 0V and 5V
        if counter % 2 == 1 then
            output[1].volts = 5.0
            print("Output 1: 5V (counter: " .. counter .. ")")
        else
            output[1].volts = 0.0
            print("Output 1: 0V (counter: " .. counter .. ")")
        end
        
        -- Stop after 10 beats
        if counter >= 10 then
            metro[1].stop()
            print("Metro test complete - stopped after 10 beats")
        end
    end
end

function step()
    -- Called every sample - can be used for continuous processing
    -- For this test, we don't need to do anything here
end
