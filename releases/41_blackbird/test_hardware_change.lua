-- Hardware Change Detection Test
-- This test sets up basic change detection and should trigger callbacks when input crosses thresholds

print("Setting up hardware change detection test...")

-- Configure input[1] for change detection
-- Threshold: 1.0V, Hysteresis: 0.1V, Direction: both
input[1].change = function(state)
    print("INPUT CHANGE DETECTED: Channel 1 = " .. tostring(state))
    if state then
        print("Input 1 went HIGH (above 1.0V)")
    else
        print("Input 1 went LOW (below 0.9V)")
    end
end

-- Set change detection parameters
input[1].mode('change', 1.0, 0.1, 'both')

print("Change detection configured:")
print("- Channel: 1")
print("- Threshold: 1.0V") 
print("- Hysteresis: 0.1V")
print("- Direction: both (rising and falling)")
print("")
print("Test by applying CV/audio to input 1:")
print("- Apply > 1.0V to trigger HIGH")
print("- Apply < 0.9V to trigger LOW")
print("- Should see callback messages above")
