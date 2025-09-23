-- ASL/CASL Integration Test for Workshop Computer
-- Note: In embedded system, ASL libraries are pre-loaded by load_embedded_asl()
-- The libraries create global functions like to(), loop(), lfo(), ar(), adsr(), etc.
-- We need to create an ASL instance manually since Asl table may not be global

print("Testing ASL/CASL system...")

-- Test basic ASL creation
print("Creating ASL instance...")
-- Since the Asl constructor might not be global, we'll test basic functions first
-- and create ASL instance if available through _G.Asl
local asl_constructor = _G.Asl or Asl -- Try to get Asl table
local a = nil
if asl_constructor and asl_constructor.new then
    a = asl_constructor.new(1)
    print("ASL instance created with id:", a.id)
else
    print("ASL constructor not available - testing global functions only")
end

-- Test basic to() function
print("Testing basic to() function...")
local basic_to = to(5.0, 1.0, 'linear')
print("Basic to created:", basic_to[1], basic_to[2], basic_to[3], basic_to[4])

-- Test simple ASL sequence
print("Testing simple ASL sequence...")
local simple_seq = {
    to(1.0, 0.5, 'linear'),
    to(0.0, 0.5, 'linear')
}

-- Test loop construct
print("Testing loop construct...")
local looped_seq = loop{
    to(2.0, 0.25, 'sine'),
    to(-2.0, 0.25, 'sine')
}
print("Loop created with", #looped_seq, "elements")

-- Test dynamic variables
print("Testing dynamic variables...")
local dyn_seq = {
    to(dyn{level=3.0}, dyn{time=1.0}, 'linear')
}

-- Test ASL library functions
print("Testing ASL library functions...")
local lfo_seq = lfo(2.0, 5.0, 'sine')
print("LFO created with", #lfo_seq, "elements")

local ar_seq = ar(0.1, 1.0, 7.0, 'log')
print("AR envelope created with", #ar_seq, "elements")

local adsr_seq = adsr(0.05, 0.3, 2.0, 1.5)
print("ADSR envelope created with", #adsr_seq, "elements")

-- Test mutable variables
print("Testing mutable variables...")
local times_seq = times(3, {to(1.0, 0.1)})
print("Times sequence created")

print("All ASL/CASL tests completed successfully!")
print("Ready to test with real CASL backend...")

-- Return the test structures for further testing
return {
    asl_instance = a,
    sequences = {
        simple = simple_seq,
        looped = looped_seq,
        dynamic = dyn_seq,
        lfo = lfo_seq,
        ar = ar_seq,
        adsr = adsr_seq,
        times = times_seq
    }
}
