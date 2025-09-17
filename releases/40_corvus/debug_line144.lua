-- Extract and analyze crow_globals around line 144
-- This is just for debugging

local crow_globals = [[
-- Crow globals initialization (single environment like real crow)
print("Crow Lua initializing...")

-- Create global output and input tables (matches crow architecture)
output = {}
input = {}

-- crow table with reset
crow = {}
function crow.reset()
    crow_reset()
end

-- Initialize output tables with crow-style interface (matching real crow)
for i = 1, 4 do
    output[i] = {
        channel = i,
        slew = 0,
        shape = 'linear',
        action = function(self, func)
            if func then self._action = func end
        end,
        dyn = function(self, ...) end
    }

    local ch = i

    setmetatable(output[i], {
        __newindex = function(t, k, v)
            if k == "volts" then
                -- Real crow behavior: use slew if set, otherwise immediate
                local slew_time = rawget(t, "slew") or 0
                local shape = rawget(t, "shape") or "linear"
                
                if slew_time > 0 then
                    -- Use slopes system for slewed transition
                    slopes_toward(ch, v or 0, slew_time, shape)
                    print("[DEBUG] Set output[" .. ch .. "].volts = " .. tostring(v) .. " with slew " .. tostring(slew_time) .. "s")
                else
                    -- Immediate execution like before
                    crow_set_output_volts(ch, v or 0)
                    print("[DEBUG] Set output[" .. ch .. "].volts = " .. tostring(v) .. " (immediate)")
                end
                return
            elseif k == "action" and type(v) == "function" then
                rawset(t, "_action", v)
                return
            elseif k == "scale" then
                if v == nil or (type(v) == 'string' and v == 'none') or
                   (type(v) == 'table' and v.degrees == nil and #v == 0) then
                    set_output_scale(ch, 'none')
                    rawset(t, k, 'none')
                    return
                elseif type(v) == 'table' then
                    local degrees_tbl = v.degrees or v
                    local mod = v.mod or v.divs or 12
                    local scaling = v.scaling or v.vpo or 1.0
                    set_output_scale(ch, degrees_tbl, mod, scaling)
                    rawset(t, k, v)
                    return
                end
                return
            end
            rawset(t, k, v)
        end,
        __index = function(t, k)
            if k == "volts" then
                -- Get current voltage from hardware like real crow
                return crow_get_output_volts(ch)
            end
            return rawget(t, k)
        end,
        __call = function(t, ...)
            local args = {...}
            if #args > 0 then
                t.volts = args[1]
            else
                -- No arguments - trigger action if available
                if t._action and type(t._action) == 'function' then
                    t._action()
                elseif t._ar_config then
                    -- Trigger AR envelope directly
                    local attack_val = t._ar_config.level
                    local attack_time = t._ar_config.attack
                    local release_time = t._ar_config.release
                    
                    -- Handle dynamic parameters
                    if type(attack_time) == 'table' and attack_time.type == 'dyn' then
                        attack_time = attack_time.value
                    end
                    if type(release_time) == 'table' and release_time.type == 'dyn' then 
                        release_time = release_time.value
                    end
                    
                    -- Trigger AR envelope
                    slopes_toward(i, attack_val, attack_time, t._ar_config.shape)
                    print("[DEBUG] AR envelope triggered on output[" .. i .. "] via output[" .. i .. "]()")
                end
            end
            return t.volts
        end
    })

    -- Backwards-compatible function form for scale
    output[i].scale = function(arg, mod, scaling)
        if type(arg) == 'string' and arg == 'none' then
            set_output_scale(ch, 'none')
            return
        elseif type(arg) == 'table' then
            set_output_scale(ch, arg, mod or 12, scaling or 1.0)
            return
        end
    end

    -- Clock helpers
    output[i].clock = function(self, period, width)
        if type(period) == 'string' and period == 'stop' then
            clear_output_clock(ch)
            return
        end
        set_output_clock(ch, period, width or 0.01)
    end
    output[i].unclock = function(self)
        clear_output_clock(ch)
    end
end

-- Initialize input tables
for i = 1, 2 do  -- Only inputs 1 and 2 for audio inputs
    input[i] = {
        volts = 0,
        _last_volts = 0,
        _change_handler = nil,
        _change_threshold = 0.1
    }
    
    setmetatable(input[i], {
        __newindex = function(t, k, v)
            if k == "change" and type(v) == "function" then
                -- Direct assignment: input[1].change = function(s) ... end
                t._change_handler = v
                -- Set up change detection with default threshold
                set_input_change(i, t._change_threshold or 0.1, 'rising')
                print("[DEBUG] Set input[" .. i .. "] change handler via direct assignment")
                return
            end
            rawset(t, k, v)
        end,
        __index = function(t, k)
            if k == "change" then
                -- Return a function that can be called like input[1].change(func, threshold)
                return function(self, func, threshold)
                    if func then 
                        self._change_handler = func 
                        self._change_threshold = threshold or 0.1
                        -- Configure input change detection (simplified - real crow uses detection engine)
                        set_input_change(i, self._change_threshold, 'rising')
                    end
                end
            elseif k == "stream" then
                return function(self, func)
                    if func then self._stream_handler = func end
                end
            end
            return rawget(t, k)
        end,
        __call = function(t, args)   -- THIS IS LINE 144!
            -- Handle input[n]{mode='change', direction='rising'} syntax
            if type(args) == 'table' then
                if args.mode == 'change' then
                    local direction = args.direction or 'both'
                    local threshold = args.threshold or 0.1
                    -- Set up change detection
                    set_input_change(i, threshold, direction)  -- ERROR IS LIKELY HERE!
                    print("[DEBUG] Set input[" .. i .. "] change detection: direction=" .. direction .. ", threshold=" .. threshold)
                end
                return t
            else
                -- Allow input[n]() to return current volts
                return t.volts
            end
        end
    })
end
]]

-- Count lines to find line 144
local lines = {}
for line in crow_globals:gmatch("[^\r\n]+") do
    table.insert(lines, line)
end

print("Line 144: " .. (lines[144] or "NOT FOUND"))
print("Line 143: " .. (lines[143] or "NOT FOUND"))
print("Line 145: " .. (lines[145] or "NOT FOUND"))
