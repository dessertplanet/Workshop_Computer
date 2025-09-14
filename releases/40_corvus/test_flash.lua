-- Test script for flash storage functionality
-- This script tests basic flash write/read operations

print("Flash storage test script loaded!")

function init()
    print("Flash test script initialized")
    print("Testing basic functionality...")
    
    -- Test output control
    output[1].volts = 1.0
    output[2].volts = -1.0
    
    print("Flash test complete - outputs set to ±1V")
end

-- Simple metro callback
function step()
    print("Flash script step called")
end

-- Test that the script persisted correctly
print("Script source: Flash storage")
print("Ready to test flash persistence!")
