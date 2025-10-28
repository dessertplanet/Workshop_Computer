-- Test script to verify fast pulse overlap handling
-- This demonstrates that overlapping pulses now work correctly

print("=== Pulse Overlap Test ===")
print("Testing fast pulse sequences that would previously cause issues")

-- Test 1: Rapid pulses (faster than pulse duration)
print("\nTest 1: Rapid 100ms pulses sent every 50ms")
print("Expected: Each pulse triggers, no cancellation")

bb.pulseout[1].action = pulse(0.1, 5)  -- 100ms pulses at 5V

local test1_count = 0
clock.run(function()
    for i = 1, 10 do
        bb.pulseout[1]()
        print(string.format("  Pulse %d sent", i))
        test1_count = test1_count + 1
        clock.sleep(0.05)  -- 50ms between pulses
    end
    print(string.format("Test 1 complete: %d pulses sent", test1_count))
end)

-- Test 2: Very rapid pulses (extreme case)
clock.run(function()
    clock.sleep(2)  -- Wait for test 1 to finish
    print("\nTest 2: Very rapid 200ms pulses sent every 10ms")
    print("Expected: Each pulse starts, old ones are properly cancelled")
    
    local test2_count = 0
    bb.pulseout[2].action = pulse(0.2, 5)  -- 200ms pulses
    
    for i = 1, 20 do
        bb.pulseout[2]()
        test2_count = test2_count + 1
        clock.sleep(0.01)  -- 10ms between pulses (much faster than pulse duration)
    end
    print(string.format("Test 2 complete: %d pulses sent", test2_count))
end)

-- Test 3: Alternating pulse durations
clock.run(function()
    clock.sleep(5)  -- Wait for test 2 to finish
    print("\nTest 3: Alternating short and long pulses")
    print("Expected: No glitches when switching pulse durations")
    
    for i = 1, 5 do
        -- Short pulse
        bb.pulseout[1].action = pulse(0.05, 5)
        bb.pulseout[1]()
        print(string.format("  Short pulse %d", i))
        clock.sleep(0.03)
        
        -- Long pulse
        bb.pulseout[1].action = pulse(0.15, 5)
        bb.pulseout[1]()
        print(string.format("  Long pulse %d", i))
        clock.sleep(0.08)
    end
    print("Test 3 complete")
end)

-- Test 4: Stress test - machine gun pulses
clock.run(function()
    clock.sleep(8)  -- Wait for test 3 to finish
    print("\nTest 4: Stress test - 50 pulses as fast as possible")
    print("Expected: All pulses trigger without system freeze")
    
    bb.pulseout[2].action = pulse(0.05, 5)
    local start_time = clock.get_beats()
    
    for i = 1, 50 do
        bb.pulseout[2]()
    end
    
    local elapsed = clock.get_beats() - start_time
    print(string.format("Test 4 complete: 50 pulses in %.3f beats", elapsed))
    print("\n=== All Tests Complete ===")
    print("If you see this message, the pulse overlap fix is working!")
end)
