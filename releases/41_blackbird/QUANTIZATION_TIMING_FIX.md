# Critical Fix: Quantization Timing Issue

## Problem
User reported that `output[3].scale()` with slew was producing smooth voltage transitions instead of quantized steps:

```lua
output[3].scale()      -- Enable chromatic quantization
output[3].slew = 0.5   -- 500ms slew
output[3].volts = 1    -- Should create stepped/quantized ramp
```

**Expected:** Stepped voltage output (quantized to semitones)
**Actual:** Smooth voltage ramp (no quantization applied)

## Root Cause

The quantization was happening AFTER the hardware DAC had already been updated!

### Original (Broken) Flow:
```
lib/ll_timers.c:
  → S_step_v(ch, buffer, size)        // Generate slope samples
    → lib/slopes.c: shaper_v()
      → hardware_output_set_voltage() // ❌ Send to DAC BEFORE quantization
  → AShaper_v(ch, buffer, size)       // ⚠️ Quantize AFTER DAC update (too late!)
```

The hardware output was being set inside `S_step_v()` in `slopes.c`, but we were calling `AShaper_v()` after `S_step_v()` returned. By then, the DAC had already received the unquantized values!

## Solution

Move quantization to happen **immediately before** each hardware output update.

### Fixed Flow:
```
lib/ll_timers.c:
  → S_step_v(ch, buffer, size)
    → lib/slopes.c: shaper_v()
      → AShaper_quantize_single()     // ✅ Quantize FIRST
      → hardware_output_set_voltage() // ✅ Then send to DAC
```

## Implementation Details

### 1. Added New Function: `AShaper_quantize_single()` 

**File:** `lib/ashapes.c` and `lib/ashapes.h`

Single-sample quantization function that applies the same algorithm as `AShaper_v()` but for one sample at a time:

```c
float AShaper_quantize_single( int index, float voltage )
{
    if( index < 0 || index >= ASHAPER_CHANNELS ){ return voltage; }
    AShape_t* self = &ashapers[index];

    if( !self->active ){ return voltage; } // Pass-through if disabled

    float samp = voltage + self->offset;
    float n_samp = samp / self->scaling;
    float divs = floorf(n_samp);
    float phase = n_samp - divs;
    int note = (int)(phase * self->dlLen);
    float note_map = self->divlist[note];
    note_map /= self->modulo;
    
    return self->scaling * (divs + note_map);
}
```

### 2. Modified Three Locations in `lib/slopes.c`

#### A. `shaper_v()` - Block Processing (line ~467)
```c
// save last state
self->shaped = out[size-1];

// Apply quantization before hardware output
extern float AShaper_quantize_single(int index, float voltage);
float quantized = AShaper_quantize_single(self->index, self->shaped);

// Update hardware output directly for real-time response
extern void hardware_output_set_voltage(int channel, float voltage);
hardware_output_set_voltage(self->index + 1, quantized);  // ✅ Use quantized value
```

#### B. `shaper()` - Single Sample at Breakpoints (line ~493)
```c
// map to output range
out = (out * self->scale) + self->last;
self->shaped = out;

// Apply quantization before hardware output
extern float AShaper_quantize_single(int index, float voltage);
float quantized = AShaper_quantize_single(self->index, self->shaped);

// Update hardware output directly
extern void hardware_output_set_voltage(int channel, float voltage);
hardware_output_set_voltage(self->index + 1, quantized);  // ✅ Use quantized value
```

#### C. `S_toward()` - Instant Transitions (line ~305)
```c
// Immediate hardware update for zero-time (instant) transitions
// Apply quantization before hardware output
extern float AShaper_quantize_single(int index, float voltage);
float quantized = AShaper_quantize_single(index, self->shaped);
extern void hardware_output_set_voltage(int channel, float voltage);
hardware_output_set_voltage(index+1, quantized);  // ✅ Use quantized value
```

### 3. Removed Misplaced Call in `lib/ll_timers.c`

**Before:**
```c
S_step_v(ch, slope_buffer, TIMER_BLOCK_SIZE);
AShaper_v(ch, slope_buffer, TIMER_BLOCK_SIZE);  // ❌ Wrong place!
```

**After:**
```c
S_step_v(ch, slope_buffer, TIMER_BLOCK_SIZE);  // ✅ Quantization happens inside
// Quantization is applied inside S_step_v before hardware output
```

## Result

Now quantization is applied at the exact moment before each sample is sent to the DAC:

```
Sample Generation → Quantization → Hardware DAC
        ↓               ↓              ↓
      Linear         Stepped        Stepped
      Ramp           Ramp           Output
```

## Testing

```lua
-- Test stepped slew with chromatic scale
output[3].scale()       -- Enable chromatic quantization
output[3].slew = 0.5    -- 500ms slew time
output[3].volts = 0     -- Start at 0V
output[3].volts = 1     -- Slew to 1V

-- Expected: Stepped ramp with 12 quantized levels (semitones)
-- Each step = 1/12V = ~83.3mV
-- Steps: 0V, 0.083V, 0.167V, 0.250V, ... 1.0V
```

Monitor with an oscilloscope to see the stepped (staircase) output instead of a smooth ramp.

## Files Modified

1. **`lib/ashapes.h`** - Added `AShaper_quantize_single()` declaration
2. **`lib/ashapes.c`** - Implemented `AShaper_quantize_single()`
3. **`lib/slopes.c`** - Added quantization calls in 3 locations
4. **`lib/ll_timers.c`** - Removed misplaced quantization call

## Verification

✅ Code compiles successfully
✅ Ready for hardware testing

Expected behavior:
- ✅ `output[n].scale()` with slew produces stepped output
- ✅ `output[n].scale({0,2,4,5,7,9,11})` quantizes to scale notes
- ✅ `output[n].scale('none')` produces smooth output (no quantization)
- ✅ ASL actions are quantized correctly
- ✅ Instant transitions (`slew = 0`) are quantized

---

**Status:** ✅ FIXED - October 3, 2025
**Issue:** Quantization applied after DAC update
**Solution:** Apply quantization immediately before DAC update in slopes.c
