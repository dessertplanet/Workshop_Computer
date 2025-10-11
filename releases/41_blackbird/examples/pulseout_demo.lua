-- bb.pulseout Demo
-- Demonstrates pulse output control on Blackbird
--
-- IMPORTANT: Pulse outputs are 1-bit digital outputs (on/off only)
-- They support pulse() timing and clock-synced patterns
--
-- Default behavior:
-- - bb.pulseout[1]: 10ms pulse on every beat (:clock(1))
-- - bb.pulseout[2]: follows switch position (high when down)

print("=== Pulseout Demo ===")
print("")

-- Example 1: Clock-synced pulses (crow-style API)
print("Example 1: Clock-synced pulses")
bb.pulseout[1]:clock(1)                    -- Every beat
bb.pulseout[1].action = pulse(0.050)       -- 50ms pulses
print("  Pulse 1: 50ms pulses on every beat")

-- Example 2: Clock division
print("Example 2: Clock division")
bb.pulseout[2]:clock(4)                    -- Every 4 beats
bb.pulseout[2].action = pulse(0.020)       -- 20ms pulses
print("  Pulse 2: 20ms pulses every 4 beats")

-- Example 3: Manual gate control
print("Example 3: Manual control")
print("  Try these commands:")
print("    bb.pulseout[1]:high()  -- Set high indefinitely")
print("    bb.pulseout[1]:low()   -- Set low indefinitely")

-- Example 4: Execute pulse immediately (no clock)
print("Example 4: Immediate pulse")
print("  bb.pulseout[1](pulse(0.100))  -- Single 100ms pulse")

-- Example 5: Variable pulse width based on knob
print("Example 5: Dynamic pulse width")
print("  (Uncomment to try)")
--[[
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = function()
    local pw = 0.001 + (bb.knob.main * 0.099)  -- 1-100ms based on knob
    _c.tell('output', 3, pulse(pw))
end
--]]

-- Example 6: Euclidean rhythm with sequins
print("Example 6: Euclidean pattern")
print("  (Uncomment to try)")
--[[
local pattern = sequins{1, 0, 0, 1, 0, 1, 0, 0}
bb.pulseout[1]:clock(1)
bb.pulseout[1].action = function()
    if pattern() == 1 then
        _c.tell('output', 3, pulse(0.010))
    end
end
--]]

-- Example 7: Switch-triggered pulses
print("Example 7: Switch trigger")
bb.switch.change = function(state)
    if state == 'down' then
        bb.pulseout[2](pulse(0.100))  -- 100ms pulse on switch down
        print("  Switch down - pulse triggered!")
    end
end

-- Stopping and cleanup
print("")
print("=== Control Commands ===")
print("  bb.pulseout[1]:clock('off')  -- Stop clock")
print("  bb.pulseout[1]:high()        -- Manual high")
print("  bb.pulseout[1]:low()         -- Manual low")
print("  clock.cleanup()              -- Stop all clocks")
print("")
print("Demo loaded! Pulse outputs active.")
