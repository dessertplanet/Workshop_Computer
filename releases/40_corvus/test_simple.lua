-- Simple test script for crow emulator
print("Hello from test script!")

function init()
    print("init() called successfully")
    output[1].volts = 2.5
    output[2].volts = -1.0
end

function step()
    -- Simple test step function
end
