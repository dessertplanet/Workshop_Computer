-- PULSEOUT QUICK REFERENCE
-- 1-bit digital outputs (on/off only, no voltage control)

-- ============================================
-- BASIC USAGE
-- ============================================

-- Set fixed pulse duration
bb.pulseout[1].action = pulse(0.020)  -- 20ms pulse

-- Set very short trigger
bb.pulseout[2].action = pulse(0.001)  -- 1ms trigger

-- ============================================
-- DYNAMIC PULSE WIDTH
-- ============================================

-- Pulse width controlled by knob
bb.pulseout[1].action = function()
    local pw = 0.005 + (bb.knob.main * 0.095)  -- 5-100ms
    hardware_pulse(1, true)
    clock.run(function()
        clock.sleep(pw)
        hardware_pulse(1, false)
    end)
end

-- ============================================
-- CLOCK PATTERNS
-- ============================================

-- Every Nth beat
local counter = 0
bb.pulseout[1].action = function()
    counter = counter + 1
    if counter % 4 == 0 then  -- Every 4th beat
        hardware_pulse(1, true)
        clock.run(function()
            clock.sleep(0.010)
            hardware_pulse(1, false)
        end)
    end
end

-- Euclidean rhythm
local pattern = sequins{1, 0, 1, 0, 1, 0, 0, 0}
bb.pulseout[1].action = function()
    if pattern() == 1 then
        hardware_pulse(1, true)
        clock.run(function()
            clock.sleep(0.010)
            hardware_pulse(1, false)
        end)
    end
end

-- ============================================
-- SWITCH TRIGGERED
-- ============================================

bb.switch.change = function(state)
    if state == 'down' then
        hardware_pulse(2, true)
        clock.run(function()
            clock.sleep(0.050)
            hardware_pulse(2, false)
        end)
    end
end

-- ============================================
-- CLEAR ACTION (disable output)
-- ============================================

-- Property syntax
bb.pulseout[1].action = 'none'  -- No pulses generated
bb.pulseout[2].action = 'none'  -- Output stays low

-- Call syntax (crow-style)
bb.pulseout[1]('none')  -- Same as above
bb.pulseout[2]('none')  -- Same as above

-- ============================================
-- RESET TO DEFAULTS
-- ============================================

bb.pulseout[1].action = pulse(0.010)    -- 10ms default
bb.pulseout[2].action = function() end  -- Switch following

-- ============================================
-- WHAT DOESN'T WORK (1-bit outputs)
-- ============================================

-- ❌ Voltage ramping
-- bb.pulseout[1].action = {to(5, 0.1), to(0, 0.1)}  -- NO!

-- ❌ Variable voltage levels
-- bb.pulseout[1].action = pulse(0.010, 3.3, 1)  -- voltage ignored

-- ❌ Complex ASL sequences
-- bb.pulseout[1].action = {loop{to(5), to(0)}}  -- NO!

-- ✅ ONLY pulse() and hardware_pulse(channel, true/false) work!
