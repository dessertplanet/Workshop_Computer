-- knob_asl_dynamics.lua
-- Using knobs to control ASL parameters via dynamics

function init()
    print("Knob -> ASL Dynamics Demo")
    print("=========================")
    print("Main knob: LFO rate (time)")
    print("X knob: LFO depth (voltage)")
    print("Y knob: Shape amount")
    print("")
    
    -- Set up ASL action with dynamics (uses default values initially)
    output[1]{ 
        to(dyn{depth=3}, dyn{rate=0.5}),
        to(0, dyn{rate=0.5})
    }
    
    -- Update dynamic values from knobs, then retrigger action
    metro[1].event = function()
        -- Set dynamic values based on current knob positions
        output[1].dyn.rate = knob.main * 2 + 0.1   -- 0.1-2.1 seconds
        output[1].dyn.depth = knob.x * 6            -- 0-6V
        
        -- Retrigger the action with updated dynamics
        output[1]:action()
    end
    metro[1].time = 0.5
    metro[1]:start()
    
    print("LFO running! Adjust knobs to control rate and depth.")
end
