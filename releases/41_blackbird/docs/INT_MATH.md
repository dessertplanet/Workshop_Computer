# Integer‑only Lua on RP2040 (advanced birds only)

> **When to care**: Stick with normal Lua math unless you’re doing **complex/inner‑loop DSP-ish math** and actually **hit a performance wall** on RP2040. Only then consider integer/fixed‑point techniques.

---

## Lua numeric configuration (this project)
- **Lua**: 5.4.8
- **Defines**: `LUA_32BITS=1` → **32‑bit signed integers** and **32‑bit floats**
- **Standard libs** loaded: `base`, `package`, `coroutine`, `table`, `string`, `math`
- **Not loaded**: `utf8`, `debug`, `io`, `os`, `bit32`
- **Bitwise ops**: native `& | ~ << >>` available in expressions

## Core patterns for int math
1. **Division**: use floor division `//` (never `/`).
2. **Fixed‑point scaling**: represent fractions as scaled ints.
   - Example (milli‐units): `SCALE = 1000`
   - $$\text{mul}(a,b) = \lfloor (a \times b) / \text{SCALE} \rfloor$$
   - $$\text{div}(a,b) = \lfloor (a \times \text{SCALE}) / b \rfloor$$
3. **Overflow guard**: 32‑bit signed range ≈ ±2,147,483,647. With `SCALE=1000`, keep \(|a|,|b| \lesssim 46340\times \text{SCALE}\).
4. **Avoid runtime trig**: prefer lookup tables or linear ramps. Precompute tables off‑device when possible.
5. **Random**: use an LCG (integer‑only), e.g. `sd = (1103515245*sd + 12345) & 0x7fffffff`.
6. **Modulo & masks**: `%`, `&`, `|`, `~`, shifts for wraps and quantization.

## I/O boundary conversions
- **Outputs**: `output[n].volts` accepts numbers. Do internal math in ints; convert once: `output[1].volts = mv / 1000.0`.
- **Metros**: expect seconds (float). Keep scheduling in ints (e.g., ms); convert once: `time = TICK_MS / 1000.0`.

## Example
See `examples/int_only.lua` for a full pattern. Key excerpt:

```lua
local IM = { SCALE = 1000 }
function IM.mul(a,b) return (a * b) // IM.SCALE end
function IM.div(a,b) return (a * IM.SCALE) // b end

local TICK_MS   = 5
local PERIOD_MS = 500  -- 2 Hz
local STEPS     = PERIOD_MS // TICK_MS
local HALF      = STEPS // 2
local MAX_MV    = 5000
local DELTA_MV  = (MAX_MV * 2) // STEPS

local step, mv = 0, 0
local function tick()
  step = (step + 1) % STEPS
  local idx = (step < HALF) and step or (STEPS - step)
  mv = idx * DELTA_MV
  output[1].volts = mv / 1000.0  -- convert once
end

function init()
  output[1].slew = 0
  metro.init{ event = tick, time = TICK_MS / 1000.0 }:start()
end
```

## Performance tips
- Precompute tables offline; embed as Lua arrays.
- Reuse tables; avoid allocations in tight loops.
- Keep hot loops pure integer; float only at boundaries.
- Use integer indices and bounds checks to avoid surprises.

## Summary
- **Default Lua floats are fine** for most crow scripts.
- **Switch to integer/fixed‑point** only when profiling shows math bottlenecks.
- The project already builds Lua in 32‑bit mode; native bitwise ops are available.
- Start from `examples/int_only.lua` when you need fixed‑point patterns.
