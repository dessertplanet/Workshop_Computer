-- Test script to verify r vs u commands work
-- This should run when loaded with either r or u

print("Test script loaded successfully!")
print("Setting up input 1 change handler...")

input[1].change = function(val)
    print("Input 1 changed to: " .. val)
    output[1].volts = val * 2  -- Double the input voltage
end

print("Input change handler installed")
print("Send clock signals to input 1 to test")

function init()
    print("init() called - script is running")
end
