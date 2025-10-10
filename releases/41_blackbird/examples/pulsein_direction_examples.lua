-- Direction Filter Examples for Pulse Input Detection

-- Example 1: Clock divider using rising edges only
-- Count every 4th pulse and output a slower clock
local div_count = 0
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'rising'
bb.pulsein[1].change = function(state)
    div_count = div_count + 1
    if div_count % 4 == 0 then
        output[1].volts = 5.0
        output[1](function() output[1].volts = 0 end, 0.01)
    end
end

-- Example 2: Gate follower - track gate state on both edges
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'both'
bb.pulsein[1].change = function(state)
    if state then
        print("Gate ON")
        output[1].volts = 5.0
    else
        print("Gate OFF")
        output[1].volts = 0.0
    end
end

-- Example 3: Trigger on gate release (falling edge only)
bb.pulsein[2].mode = 'change'
bb.pulsein[2].direction = 'falling'
bb.pulsein[2].change = function(state)
    print("Gate released - trigger envelope release")
    output[2].volts = 0.0
    -- Could trigger ADSR release phase here
end

-- Example 4: Rising edge counter with statistics
local stats = {
    count = 0,
    last_time = 0,
    intervals = {}
}

bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'rising'
bb.pulsein[1].change = function(state)
    local now = util.time()
    stats.count = stats.count + 1
    
    if stats.last_time > 0 then
        local interval = now - stats.last_time
        table.insert(stats.intervals, interval)
        print(string.format("Pulse #%d, interval: %.3fs", stats.count, interval))
    end
    
    stats.last_time = now
end

-- Example 5: Bidirectional gate detection
-- Use two different pulse inputs to detect different directions
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'rising'
bb.pulsein[1].change = function(state)
    print("Forward trigger")
    output[1].volts = 3.0
end

bb.pulsein[2].mode = 'change'
bb.pulsein[2].direction = 'rising'
bb.pulsein[2].change = function(state)
    print("Reverse trigger")
    output[1].volts = -3.0
end

-- Example 6: Dynamic direction switching
-- Change detection direction based on some condition
local use_rising = true

function toggle_direction()
    if use_rising then
        bb.pulsein[1].direction = 'falling'
        use_rising = false
        print("Now detecting falling edges")
    else
        bb.pulsein[1].direction = 'rising'
        use_rising = true
        print("Now detecting rising edges")
    end
end

bb.pulsein[1].mode = 'change'
bb.pulsein[1].change = function(state)
    print("Edge detected: " .. tostring(state))
end

-- Call toggle_direction() to switch between rising/falling detection
