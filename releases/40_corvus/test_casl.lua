-- CASL (Crow ASL) System Test Script
-- Tests complex envelope sequencing and control flow
print("CASL Test Script Loading...")

-- Simple CASL sequence test
function test_simple_casl()
    print("Testing simple CASL sequence...")
    
    -- Basic envelope: to 5V in 1s linear, then to 0V in 0.5s exponential
    local simple_seq = {
        {"T", 5, 1.0, "linear"},
        {"T", 0, 0.5, "exponential"}
    }
    
    -- Load the sequence on output 1
    casl_describe(1, simple_seq)
    
    -- Start the sequence
    casl_action(1, 1)  -- Action 1 = start
    
    print("Simple CASL sequence started on output 1")
end

-- Complex nested sequence test
function test_nested_casl()
    print("Testing nested CASL sequence...")
    
    -- Nested sequence with control flow
    local nested_seq = {
        {"T", 3, 0.5, "sine"},     -- Rise to 3V
        {"H"},                     -- Hold state
        {                          -- Nested sub-sequence
            {"T", 1, 0.2, "linear"},
            {"T", 4, 0.3, "exponential"},
            {"T", 2, 0.1, "sine"}
        },
        {"U"},                     -- Unheld (release)
        {"T", 0, 1.0, "logarithmic"}
    }
    
    -- Load on output 2
    casl_describe(2, nested_seq)
    casl_action(2, 1)
    
    print("Nested CASL sequence started on output 2")
end

-- Control flow test with recur and conditionals
function test_control_flow()
    print("Testing CASL control flow...")
    
    -- Sequence with looping
    local loop_seq = {
        {"T", 2, 0.5, "linear"},
        {"T", -2, 0.5, "linear"},
        {"R"}  -- Recur - loop back to beginning
    }
    
    -- Load on output 3
    casl_describe(3, loop_seq)
    casl_action(3, 1)
    
    print("Looping CASL sequence started on output 3")
end

-- Dynamic variables test
function test_dynamics()
    print("Testing CASL dynamics...")
    
    -- Clear any existing dynamics for channel 4
    casl_cleardynamics(4)
    
    -- Define some dynamic variables
    local dyn1 = casl_defdynamic(4)
    local dyn2 = casl_defdynamic(4)
    
    print("Created dynamics: dyn1=" .. dyn1 .. ", dyn2=" .. dyn2)
    
    -- Set dynamic values
    casl_setdynamic(4, dyn1, 3.5)
    casl_setdynamic(4, dyn2, 0.8)
    
    -- Read back values
    local val1 = casl_getdynamic(4, dyn1)
    local val2 = casl_getdynamic(4, dyn2)
    
    print("Dynamic values: dyn1=" .. val1 .. ", dyn2=" .. val2)
    
    -- TODO: Test dynamics in actual sequences (requires more complex parsing)
end

-- Held/Unheld pattern test
function test_held_pattern()
    print("Testing held/unheld pattern...")
    
    -- Attack-Hold-Release envelope
    local ahr_seq = {
        {"T", 5, 0.3, "exponential"},  -- Attack
        {"H"},                         -- Hold at peak
        {"W"},                         -- Wait for release trigger
        {"T", 0, 1.2, "logarithmic"}   -- Release
    }
    
    -- Load on output 4
    casl_describe(4, ahr_seq)
    casl_action(4, 1)  -- Start (attack)
    
    print("AHR envelope started on output 4")
    print("Send casl_action(4, 0) to trigger release")
end

-- Metro-triggered CASL test
local casl_metro_count = 0

function metro_handler(id, stage)
    if id == 5 then  -- Metro 5 triggers CASL actions
        casl_metro_count = casl_metro_count + 1
        print("Metro " .. id .. " stage " .. stage .. " (count: " .. casl_metro_count .. ")")
        
        -- Alternate between different CASL actions
        if casl_metro_count % 3 == 1 then
            test_simple_casl()
        elseif casl_metro_count % 3 == 2 then
            test_nested_casl()
        else
            test_control_flow()
        end
    end
end

function test_metro_casl()
    print("Testing metro-triggered CASL...")
    
    -- Set up metro to trigger CASL sequences
    metro[5].start(2.0)  -- Every 2 seconds
    
    print("Metro 5 started - will trigger different CASL sequences")
end

-- Initialization function
function init()
    print("=== CASL Test Script Initialized ===")
    print("Available test functions:")
    print("  test_simple_casl() - Basic envelope sequence")
    print("  test_nested_casl() - Complex nested sequences")  
    print("  test_control_flow() - Looping and control flow")
    print("  test_dynamics() - Dynamic variable management")
    print("  test_held_pattern() - Attack-hold-release envelope")
    print("  test_metro_casl() - Metro-triggered sequences")
    print("")
    
    -- Start with a simple test
    test_simple_casl()
    
    -- Test dynamics
    test_dynamics()
    
    print("=== CASL Tests Running ===")
end

-- Step function (called every audio sample)
function step()
    -- Monitor output states (optional debug)
    -- Could add CASL state monitoring here
end

print("CASL Test Script Loaded Successfully")
