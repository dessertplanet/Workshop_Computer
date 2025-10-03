-- test_chromatic.lua
-- Focused test for chromatic scale quantization fix

print("=== Chromatic Scale Quantization Test ===\n")

-- Enable chromatic quantization
output[3].scale()
print("Enabled chromatic quantization on output[3]\n")

-- Test various voltages
print("Testing voltage quantization to nearest semitone:")
print("(Each semitone = 1/12 V = 0.0833V)\n")

local test_voltages = {
    {input = 0.0,   expected = 0.000, note = "C"},
    {input = 0.5,   expected = 0.500, note = "F"},
    {input = 1.0,   expected = 1.000, note = "C"},
    {input = 1.05,  expected = 1.083, note = "C#"},
    {input = 1.5,   expected = 1.500, note = "F#"},
    {input = 2.0,   expected = 2.000, note = "C"},
    {input = 2.35,  expected = 2.333, note = "E"},
    {input = 2.78,  expected = 2.750, note = "G#"},
    {input = 3.0,   expected = 3.000, note = "C"},
    {input = 3.78,  expected = 3.750, note = "A"},
}

for _, test in ipairs(test_voltages) do
    output[3].volts = test.input
    print(string.format("  %.3fV → should be %.3fV (%s)", 
          test.input, test.expected, test.note))
end

print("\n=== Compare with unquantized output ===\n")
output[4].scale('none')
print("Output 4 has no quantization (should pass through)\n")

output[3].volts = 2.35
output[4].volts = 2.35
print("Both set to 2.35V:")
print("  Output 3 (chromatic): should be 2.333V (quantized to E)")
print("  Output 4 (none):      should be 2.35V (unquantized)")

print("\n=== Test with slew ===\n")
output[3].slew = 1.0
output[3].volts = 0
print("Output 3 set to 0V with 1 second slew")
output[3].volts = 1
print("Now slewing to 1V - should quantize during the slew")
print("Monitor with scope to see stepped output")

print("\n=== Chromatic Test Complete ===")
print("Measure outputs with multimeter to verify:")
print("  Expected: Output 3 at quantized voltages")
print("  Expected: Output 4 at unquantized 2.35V")
