-- Metro Lock-Free Timing Test
-- Tests the new lock-free metro queue for improved timing precision

print("Metro Lock-Free Timing Test")
print("===========================")

-- Variables to track metro performance
local metro_count = 0
local start_time = 0
local last_metro_time = 0
local timing_errors = {}

-- Metro handler function
function metro_handler(id, stage)
    local current_time = time()
    
    if metro_count == 0 then
        start_time = current_time
        print("Metro started at time:", start_time)
    else
        -- Calculate timing error
        local expected_interval = 0.1  -- 100ms intervals
        local actual_interval = current_time - last_metro_time
        local error = actual_interval - expected_interval
        
        table.insert(timing_errors, error)
        
        -- Print every 10th event to show progress
        if metro_count % 10 == 0 then
            print(string.format("Metro %d: stage=%d, error=%.6fs", metro_count, stage, error))
        end
    end
    
    last_metro_time = current_time
    metro_count = metro_count + 1
    
    -- Stop test after 50 events
    if metro_count >= 50 then
        metro.stop(1)
        
        -- Calculate statistics
        local total_error = 0
        local max_error = 0
        local min_error = math.huge
        
        for i = 1, #timing_errors do
            local err = math.abs(timing_errors[i])
            total_error = total_error + err
            if err > max_error then max_error = err end
            if err < min_error then min_error = err end
        end
        
        local avg_error = total_error / #timing_errors
        local total_time = current_time - start_time
        
        print("\nTiming Test Results:")
        print("==================")
        print(string.format("Total events: %d", metro_count))
        print(string.format("Total time: %.3fs", total_time))
        print(string.format("Average error: %.6fs (%.3fms)", avg_error, avg_error * 1000))
        print(string.format("Max error: %.6fs (%.3fms)", max_error, max_error * 1000))
        print(string.format("Min error: %.6fs (%.3fms)", min_error, min_error * 1000))
        
        -- Assess timing quality
        if max_error < 0.001 then  -- Less than 1ms jitter
            print("✓ EXCELLENT timing precision!")
        elseif max_error < 0.005 then  -- Less than 5ms jitter
            print("✓ Good timing precision")
        else
            print("⚠ Timing could be improved")
        end
        
        print("\nTest completed. Lock-free queues should show improved precision!")
    end
end

-- Start the test
print("Starting 100ms metro for 50 events...")
print("Lock-free queues should provide better timing precision than mutex-based queues")

metro.start(1, 0.1)  -- 100ms intervals
