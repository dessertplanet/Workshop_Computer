-- knob_asl_dynamics.lua
-- Using knobs to control ASL parameters via dynamics

function init()
    print("Knob -> ASL Dynamics Demo")
    print("=========================")
    print("Main knob: LFO rate (time)")
    print("X knob: LFO depth (voltage)")
    print("Switch: Changes LFO shape")
    print("")
    
    -- Set up ASL action with dynamics (uses default values initially)
    output[1]{ 
        to(dyn{depth=3}, dyn{rate=0.5}),
        to(0, dyn{rate=0.5})
    }
    
    -- Set up switch to change shape
    ws.switch.change = function(pos)
        if pos == 'down' then
            output[1].shape = 'sine'
            print("Shape: sine")
        elseif pos == 'middle' then
            output[1].shape = 'linear'
            print("Shape: linear")
        else
            output[1].shape = 'expo'
            print("Shape: expo")
        end
    end
    
    -- Set initial shape
    ws.switch.change(ws.switch.position)
    
    -- Update dynamic values from knobs, then retrigger action
    metro[1].event = function()
        -- Set dynamic values based on current knob positions
        output[1].dyn.rate = ws.knob.main * 2 + 0.1   -- 0.1-2.1 seconds
        output[1].dyn.depth = ws.knob.x * 6            -- 0-6V
        
        -- Retrigger the action with updated dynamics
        output[1]:action()
    end
    metro[1].time = 0.5
    metro[1]:start()
    
    print("LFO running! Adjust knobs to control rate and depth.")
end
