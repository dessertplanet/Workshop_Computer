-- Test script for crow detection system
print("Testing Blackbird Crow Detection System v1.1")
print("============================================")

-- Test basic input reading
print("Testing input voltage reading:")
for i = 1, 2 do
    local voltage = input[i].volts
    print("Input " .. i .. ": " .. string.format("%.3f", voltage) .. "V")
end

print("\nTesting detection modes:")

-- Test stream mode - sample input 1 every 500ms
print("Setting input 1 to stream mode (500ms intervals)")
input[1]:mode('stream', 0.5)

-- Test change mode - detect threshold crossings on input 2
print("Setting input 2 to change mode (1.0V threshold, 0.1V hysteresis, both directions)")
input[2]:mode('change', 1.0, 0.1, 'both')

print("\nDetection system active!")
print("Try feeding audio/CV signals to inputs 1 and 2")
print("Stream events from input 1 will appear every 500ms")
print("Threshold crossings on input 2 (around 1V) will trigger change events")

-- Test window mode example
print("\nTesting window detection on input 1:")
local windows = {-2.0, -1.0, 0.0, 1.0, 2.0}
input[1]:mode('window', windows, 0.1)

-- Test volume mode example  
print("Testing volume detection on input 2 (200ms intervals):")
input[2]:mode('volume', 0.2)

print("\nAll detection tests configured.")
print("Monitor the serial output for detection events.")
