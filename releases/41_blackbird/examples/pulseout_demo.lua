-- bb.pulseout Demo
-- Demonstrates how to use pulse output actions on Blackbird
--
-- IMPORTANT: Pulse outputs are 1-bit digital outputs (on/off only)
-- They only support pulse() for timing - no voltage control
--
-- Default behavior:
-- - bb.pulseout[1]: 10ms pulse triggered by internal clock
-- - bb.pulseout[2]: follows switch position (high when switch is down)
--
-- This demo shows how to override these defaults with custom actions

-- Example 1: Change pulse 1 to longer pulses
bb.pulseout[1].action = pulse(0.050)  -- 50ms pulses

-- Example 2: Make pulse 2 produce 20ms pulses instead of following switch
bb.pulseout[2].action = pulse(0.020)  -- 20ms pulses

-- Example 3: Custom function for pulse 2
-- Trigger a pulse when switch changes to down
bb.switch.change = function(state)
    if state == 'down' then
        -- Set pulse 2 high for 100ms
        hardware_pulse(2, true)
        clock.run(function()
            clock.sleep(0.100)
            hardware_pulse(2, false)
        end)
    end
end

-- Example 4: Variable pulse width based on knob position
-- (commented out - uncomment to try)
--[[
bb.pulseout[1].action = function()
    local duration = 0.005 + (bb.knob.main * 0.095)  -- 5ms to 100ms based on knob
    hardware_pulse(1, true)
    clock.run(function()
        clock.sleep(duration)
        hardware_pulse(1, false)
    end)
end
--]]

-- Example 5: Clock division - trigger every 4th beat
-- (commented out - uncomment to try)
--[[
local counter = 0
bb.pulseout[1].action = function()
    counter = counter + 1
    if counter % 4 == 0 then
        hardware_pulse(1, true)
        clock.run(function()
            clock.sleep(0.020)
            hardware_pulse(1, false)
        end)
    end
end
--]]

-- To clear output entirely (no pulses):
-- bb.pulseout[1].action = 'none'
-- bb.pulseout[2].action = 'none'

-- To reset to default behavior:
-- bb.pulseout[1].action = pulse(0.010)
-- bb.pulseout[2].action = function() end  -- or remove custom switch.change function

print("Pulseout demo loaded!")
print("Pulse 1: Custom 50ms pulses")
print("Pulse 2: Custom 20ms pulses")
print("Remember: Pulse outputs are 1-bit (on/off only)")
print("Set action = 'none' to disable output")
