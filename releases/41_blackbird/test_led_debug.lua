-- LED Debug Test - Ultra minimal callback for memory debugging
-- This test uses the most minimal callback possible

print("=== LED Debug Test ===")
print("Testing with absolutely minimal callback")

-- Clear all LEDs first
for i = 0, 5 do
    -- LEDs will be controlled by C code, this is just documentation
end

-- Ultra-minimal callback - no operations, no variables, no print
input[1].change = function(state) 
    -- Completely empty callback - does absolutely nothing
    -- This tests if even the most basic callback works
end

-- Set up change detection
input[1].mode('change', 1.0, 0.1, 'rising')

print("")
print("LED Debug: Ultra-minimal callback ready!")
print("Watch LEDs 0, 1, 2 when you send clock pulses:")
print("LED 0 = Event handler called")
print("LED 1 = About to call Lua")  
print("LED 2 = Lua callback completed")
print("")
print("If only LED 0 lights: Event system works, Lua never called")
print("If LEDs 0+1 light: Lua called but crashes/hangs")
print("If all LEDs 0+1+2 light: Everything works!")
