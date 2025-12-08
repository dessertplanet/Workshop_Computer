-- int_only.lua
-- Integer-only scripting style for the RP2040 crow emulator (Lua 5.4.8, LUA_32BITS)
-- Core logic avoids floating-point ops; conversion to float happens only at I/O boundaries.

-- Fixed-point helpers (Q0.1000-style: milli-units)
IM = {}
IM.SCALE = 1000  -- 1 unit = 1/1000
function IM.mul(a, b)  -- (a * b) / SCALE
    -- Safe if |a|,|b| <= 46340 * SCALE (to avoid 32-bit overflow)
    return (a * b) // IM.SCALE
end
function IM.div(a, b)  -- (a / b) * SCALE
    return (a * IM.SCALE) // b
end
function IM.round_to(x, scale)
    scale = scale or IM.SCALE
    return (x + scale // 2) // scale
end
function IM.from_s(s) return s * 1000 end   -- seconds -> ms (int)
function IM.to_s(ms) return ms // 1000 end   -- ms -> seconds (int floor)

-- Deterministic LCG RNG (integer-only)
sd = 1
function lcg_loc(seed)
    if seed then sd = seed end
    sd = (1103515245 * sd + 12345) & 0x7fffffff -- mask to 31 bits
    return sd
end

-- Example: 2 Hz triangle LFO using integer math (milli-volts & milli-seconds)
-- Note: 5ms metro ticks were overwhelming the control loop on hardware.
-- Use a slightly slower tick to keep Lua responsive while still producing
-- a smooth 2 Hz ramp (50 steps per cycle at 10ms).
TICK_MS   = 18     -- scheduler tick in ms
PERIOD_MS = 500        -- 2 Hz
STEPS     = PERIOD_MS // TICK_MS
HALF      = STEPS // 2
MAX_MV    = 5000
DELTA_MV  = (MAX_MV * 2) // STEPS  -- step size in mV

step = 0
mv   = 0

function tick()
    step = (step + 1) % STEPS
    rising = step < HALF
    idx = rising and step or (STEPS - step)
    mv = idx * DELTA_MV
    -- Convert to volts once at the boundary (float here is acceptable)
    output[1].volts = mv / 1000.0
    -- print(idx)
end

function init()

    -- Metro uses seconds; convert once to float at the boundary
    t = TICK_MS / 1000.0
    m = metro.init{ event = tick, time = t }
    if m then m:start() end
end
