-- Sample & Hold DEBUG Test for Blackbird Crow Emulator
-- Minimal test to isolate crash issue

print("=== Sample & Hold DEBUG Test ===")
print("Testing minimal callback without output changes")

-- Step 1: Minimal callback that only prints
input[1].change = function(state)
    if state then
        print("DEBUG: Rising edge detected - NO OTHER OPERATIONS")
    else
        print("DEBUG: Falling edge detected")
    end
end

-- Set up change detection (using the working syntax)
input[1].mode('change', 1.0, 0.1, 'rising')

print("")
print("DEBUG: Minimal callback ready!")
print("Send clock pulses to input 1")
print("This should only print messages - no voltage changes")
