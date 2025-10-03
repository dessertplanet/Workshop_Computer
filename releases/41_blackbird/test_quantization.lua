--test_quantization.lua
-- Test script for output quantization
-- Tests the newly implemented scale quantization feature

print("=== Output Quantization Test Suite ===\n")

-- Test 1: Chromatic quantization (semitone steps)
print("Test 1: Chromatic quantization")
output[1].scale()  -- Default chromatic (12 semitones)
output[1].volts = 2.35  -- Should snap to nearest semitone
print("Set to 2.35V, should quantize to nearest semitone")
print()

-- Test 2: Major scale
print("Test 2: Major scale quantization")
output[2].scale({0,2,4,5,7,9,11})  -- C major scale
output[2].volts = 1.5  -- Should snap to nearest major scale note
print("Set to 1.5V with major scale")
print()

-- Test 3: Pentatonic scale  
print("Test 3: Pentatonic scale")
output[3].scale({0,2,4,7,9})  -- Pentatonic
output[3].volts = 3.0
print("Set to 3.0V with pentatonic scale")
print()

-- Test 4: Disable quantization
print("Test 4: Disable quantization")
output[4].scale('none')
output[4].volts = 2.567  -- Should pass through unquantized
print("Set to 2.567V with no quantization (should pass through)")
print()

-- Test 5: Dynamic scale switching with slew
print("Test 5: Dynamic scale changes with slew")
output[1].scale({0,2,4,5,7,9,11})  -- Major
output[1].slew = 0.5
output[1].volts = 0
output[1].volts = 1
print("Output 1: Slewing from 0V to 1V with major scale")
print("Should quantize during the slew")
print()

-- Test 6: Multiple scales at once
print("Test 6: All outputs with different scales")
output[1].scale({0,2,4,5,7,9,11})  -- Major
output[2].scale({0,3,5,7,10})       -- Minor pentatonic
output[3].scale({0,2,4,6,8,10})     -- Whole tone
output[4].scale('none')              -- No quantization

output[1].volts = 2.0
output[2].volts = 1.5
output[3].volts = 2.5
output[4].volts = 3.333

print("Output 1: 2.0V with major scale")
print("Output 2: 1.5V with minor pentatonic")
print("Output 3: 2.5V with whole tone scale")
print("Output 4: 3.333V unquantized")
print()

-- Test 7: ASL with quantization
print("Test 7: ASL action with scale quantization")
output[1].scale({0,2,4,7,9})  -- Pentatonic
output[1].action = loop{
    to(0, 0.5),
    to(1, 0.5),
    to(2, 0.5)
}
print("Output 1: Looping ASL with pentatonic scale")
print("Should quantize all voltages in the loop")
print()

print("=== Test Suite Complete ===")
print("Monitor CV outputs with a multimeter or scope")
print("to verify quantization is working correctly")
