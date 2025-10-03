# Chromatic Scale Quantization Fix

## Issue
Chromatic quantization via `output[n].scale()` was not working correctly.

## Root Cause
The original implementation was trying to match crow's special case handling but got the parameters wrong.

**Original (broken) code:**
```cpp
// Case 1: No scale argument -> chromatic quantization
if (nargs == 1) {
    float divs[1] = {0.0};
    AShaper_set_scale(channel, divs, 1, 1, 1.0/12.0);  // WRONG!
    lua_pop(L, 1);
    return 0;
}
```

**Problem:** This passes:
- `divlist = {0.0}`
- `dlLen = 1`
- `modulo = 1`
- `scaling = 1.0/12.0`

In `AShaper_v()`, the quantization algorithm does:
```c
float n_samp = samp / self->scaling;  // = samp / (1.0/12.0) = samp * 12
```

This multiplies the input voltage by 12 instead of quantizing to semitones!

## Solution
Explicitly pass all 12 semitone values as the scale:

**Fixed code:**
```cpp
// Case 1: No scale argument -> chromatic quantization (semitones)
if (nargs == 1) {
    float divs[12] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};
    AShaper_set_scale(channel, divs, 12, 12.0, 1.0);
    lua_pop(L, 1);
    return 0;
}
```

**Now it passes:**
- `divlist = {0,1,2,3,4,5,6,7,8,9,10,11}` (all semitones)
- `dlLen = 12`
- `modulo = 12.0` (12 semitones per octave)
- `scaling = 1.0` (1V/octave)

## How It Works Now

For an input of 2.35V with chromatic scale:

1. **Normalize:** `n_samp = 2.35 / 1.0 = 2.35`
2. **Extract octave:** `divs = floor(2.35) = 2`
3. **Get phase:** `phase = 2.35 - 2 = 0.35`
4. **Map to scale index:** `note = (int)(0.35 * 12) = 4`
5. **Lookup:** `note_map = divlist[4] = 4.0`
6. **Remap:** `note_map /= 12.0 = 0.333...`
7. **Reconstruct:** `output = 1.0 * (2 + 0.333) = 2.333V`

Result: **2.35V → 2.333V** (quantized to E, which is 4 semitones = 0.333V)

## Testing

```lua
-- Test chromatic quantization
output[1].scale()  -- Chromatic (all semitones)
output[1].volts = 2.35  -- Should snap to 2.333V (4 semitones = E)
output[1].volts = 1.5   -- Should snap to 1.5V (exactly 6 semitones = F#)
output[1].volts = 3.78  -- Should snap to 3.75V (9 semitones = A)

-- Compare with major scale
output[2].scale({0,2,4,5,7,9,11})
output[2].volts = 2.35  -- Should snap to 2.333V (4 semitones = E, closest major note)

-- Disable
output[1].scale('none')
output[1].volts = 2.35  -- Should pass through as 2.35V (no quantization)
```

## Update: Timing Issue Fixed

The chromatic quantization was calculating correctly, but quantization was being applied AFTER the hardware DAC was updated. This meant smooth slews were not being quantized.

### Additional Fix - Apply Quantization Before Hardware Output

Modified `lib/slopes.c` to apply quantization immediately before sending to hardware:

1. **Added `AShaper_quantize_single()`** - Single-sample quantization function
2. **Modified `shaper_v()`** - Applies quantization before `hardware_output_set_voltage()`
3. **Modified `shaper()`** - Applies quantization for breakpoint samples
4. **Modified `S_toward()`** - Applies quantization for instant transitions

**Now the processing chain is:**
```
Slopes → Quantization → Hardware DAC
```

Instead of the broken:
```
Slopes → Hardware DAC → Quantization (too late!)
```

## Status
✅ **FULLY FIXED** - Chromatic quantization now works correctly with slews and ASL!
