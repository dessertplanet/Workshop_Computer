-- Pulse Input API Syntax Examples
-- Demonstrates three equivalent ways to configure pulse input detection

-- ============================================================
-- SYNTAX 1: Property Assignment (Simple and Clear)
-- ============================================================

print("=== Syntax 1: Property Assignment ===")

-- Configure pulse input 1
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'rising'
bb.pulsein[1].change = function(state)
    print("[Syntax 1] Pulse 1 rising edge")
end

-- Query current settings
print("Mode: " .. bb.pulsein[1].mode())  -- Call mode() with no args to query
print("Direction: " .. bb.pulsein[1].direction)
print("State: " .. tostring(bb.pulsein[1].state))


-- ============================================================
-- SYNTAX 2: Function Call (Crow-style, Concise)
-- ============================================================

print("\n=== Syntax 2: Function Call ===")

-- Configure pulse input 2 - mode and direction in one call
bb.pulsein[2].mode('change', 'falling')
bb.pulsein[2].change = function(state)
    print("[Syntax 2] Pulse 2 falling edge")
end

-- Can also call with just mode
-- bb.pulsein[2].mode('none')  -- Disable


-- ============================================================
-- SYNTAX 3: Table Configuration (Crow-style, Most Concise)
-- ============================================================

print("\n=== Syntax 3: Table Configuration ===")

-- Configure with table - sets multiple properties at once
bb.pulsein[1]{
    mode = 'change',
    direction = 'both'
}

bb.pulsein[1].change = function(state)
    if state then
        print("[Syntax 3] Pulse 1 rising")
    else
        print("[Syntax 3] Pulse 1 falling")
    end
end


-- ============================================================
-- PRACTICAL EXAMPLES WITH DIFFERENT SYNTAXES
-- ============================================================

print("\n=== Practical Examples ===")

-- Example 1: Simple trigger counter (property style)
local count = 0
bb.pulsein[1].mode = 'change'
bb.pulsein[1].direction = 'rising'
bb.pulsein[1].change = function(state)
    count = count + 1
    print("Trigger " .. count)
end

-- Example 2: Gate follower (function style)
bb.pulsein[2].mode('change', 'both')
bb.pulsein[2].change = function(state)
    output[1].volts = state and 5.0 or 0.0
end

-- Example 3: Complex gate processor (table style)
bb.pulsein[1]{mode = 'change', direction = 'both'}
bb.pulsein[1].change = function(state)
    if state then
        -- Gate start
        output[1].volts = 5.0
        output[2].volts = 8.0
    else
        -- Gate end
        output[1].volts = 0.0
        output[2].volts = 0.0
    end
end


-- ============================================================
-- MIXING SYNTAXES (All work together)
-- ============================================================

print("\n=== Mixing Syntaxes ===")

-- Start with table configuration
bb.pulsein[1]{mode = 'change', direction = 'rising'}

-- Later change just direction using property
bb.pulsein[1].direction = 'falling'

-- Or reconfigure completely using function
bb.pulsein[1].mode('change', 'both')

-- Or disable using property
bb.pulsein[1].mode = 'none'


-- ============================================================
-- QUERYING STATE
-- ============================================================

print("\n=== Querying State ===")

-- All syntaxes support querying
print("Current mode: " .. bb.pulsein[1].mode())  -- Function call with no args
print("Current direction: " .. bb.pulsein[1].direction)  -- Property access
print("Current state: " .. tostring(bb.pulsein[1].state))  -- Property access

-- Note: mode can be called as function or accessed as string
-- bb.pulsein[1].mode()        -- Returns mode as string when called
-- bb.pulsein[1].mode = 'none' -- Sets mode when assigned


print("\n=== Initialization Complete ===")
print("Use any syntax you prefer - they all work the same!")
