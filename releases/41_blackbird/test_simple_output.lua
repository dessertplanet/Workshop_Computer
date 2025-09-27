-- Simple Output Test for Hardware Verification
print("=== Simple Output Hardware Test ===")

-- Check if output system exists
print("Checking output system...")
print("output global:", output)

if not output then
    print("ERROR: output global not found!")
    return
end

-- Check each output channel
for i = 1, 4 do
    print("output[" .. i .. "]:", output[i])
    if output[i] then
        print("  current volts:", output[i].volts)
    end
end

-- Set a simple test voltage on output 1
print("\nSetting output[1] to 3.3V...")
if output[1] then
    output[1].volts = 3.3
    print("Set output[1].volts = 3.3V")
    print("Current value:", output[1].volts)
else
    print("ERROR: output[1] not available")
end

-- Test all outputs with clear voltages
print("\nTesting all outputs with distinct voltages:")
local test_voltages = {2.5, -2.5, 1.0, -1.0}

for i = 1, 4 do
    if output[i] then
        print("Setting output[" .. i .. "] = " .. test_voltages[i] .. "V")
        output[i].volts = test_voltages[i]
    else
        print("output[" .. i .. "] not available")
    end
end

print("\nCheck hardware outputs now!")
print("Expected:")
print("Output 1: 2.5V")
print("Output 2: -2.5V") 
print("Output 3: 1.0V")
print("Output 4: -1.0V")
