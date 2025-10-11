-- PULSEOUT QUICK REFERENCE
-- 1-bit digital outputs (on/off only, no voltage control)

-- ============================================
-- CLOCK-SYNCED PULSES (Recommended)
-- ============================================

-- Start clocked pulses
bb.pulseout[1]:clock(1)      -- Every beat (default: 10ms pulses)
bb.pulseout[1]:clock(2)      -- Every 2 beats
bb.pulseout[1]:clock(0.5)    -- Twice per beat
bb.pulseout[1]:clock(1/3)    -- Three times per beat

-- Change pulse width
bb.pulseout[1].action = pulse(0.020)   -- 20ms pulses
bb.pulseout[1].action = pulse(0.001)   -- 1ms trigger
bb.pulseout[1].action = pulse(0.100)   -- 100ms gate

-- Stop clock
bb.pulseout[1]:clock('off')

-- ============================================
-- MANUAL CONTROL
-- ============================================

bb.pulseout[1]:high()        -- Set high indefinitely
bb.pulseout[1]:low()         -- Set low indefinitely

-- ============================================
-- IMMEDIATE PULSE (No clock)
-- ============================================

bb.pulseout[1](pulse(0.050))         -- Single 50ms pulse right now
bb.pulseout[2](pulse(0.010))         -- Single 10ms pulse on output 2

-- ============================================
-- DYNAMIC PULSE WIDTH
-- ============================================

-- Variable pulse width based on knob
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = function()
    local pw = 0.001 + (bb.knob.main * 0.099)  -- 1-100ms
    _c.tell('output', 3, pulse(pw))
end

-- ============================================
-- PATTERNS WITH SEQUINS
-- ============================================

-- Euclidean rhythm
local pattern = sequins{1, 0, 1, 0, 1, 0, 0, 0}
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = function()
    if pattern() == 1 then
        _c.tell('output', 3, pulse(0.010))
    end
end

-- ============================================
-- SWITCH INTEGRATION
-- ============================================

-- Trigger pulse on switch down
bb.switch.change = function(state)
    if state == 'down' then
        bb.pulseout[2](pulse(0.100))  -- Single 100ms pulse
    end
end

-- Gate follows switch (like default pulseout[2])
bb.pulseout[2]:clock(1)
bb.pulseout[2].action = function()
    if bb.switch() == 'down' then
        _c.tell('output', 5, pulse(999))  -- Long pulse = gate
    else
        _c.tell('output', 5, pulse(0))    -- 0 duration = off
    end
end

-- ============================================
-- CLEANUP
-- ============================================

clock.cleanup()              -- Stops all pulse output clocks
bb.pulseout[1]:clock('off')  -- Stop just pulse 1 clock
bb.pulseout[1].action = 'none'  -- Clear action

-- ============================================
-- CHECK STATE
-- ============================================

print(bb.pulseout[1].state)  -- true if high, false if low

-- ============================================
-- NOTES
-- ============================================
-- - Pulse outputs are 1-bit digital (5V high, 0V low)
-- - Output 1 = channel 3 in _c.tell()
-- - Output 2 = channel 5 in _c.tell()
-- - Only pulse() timing works, no voltage control
-- - Use :clock() for rhythmic patterns
-- - Use :high()/:low() for gates
-- - clock.cleanup() stops all pulse clocks automatically

-- ❌ Complex ASL sequences
-- bb.pulseout[1].action = {loop{to(5), to(0)}}  -- NO!

-- ✅ ONLY pulse() and hardware_pulse(channel, true/false) work!
