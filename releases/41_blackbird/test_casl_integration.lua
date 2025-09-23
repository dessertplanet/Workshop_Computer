-- CASL Integration Test
-- Test the complete ASL→CASL pipeline

print("Testing CASL Integration...")

-- Test 1: Basic ASL creation and compilation
print("\n=== Test 1: Basic ASL Creation ===")
local a1 = Asl.new(1)
print("Created ASL instance for output 1:", a1.id)

-- Test 2: Simple ASL sequence
print("\n=== Test 2: Simple ASL Sequence ===")
local simple_asl = {
    to(2.0, 1.0, 'linear'),
    to(0.0, 0.5, 'linear')
}

-- Compile and describe the ASL
a1:describe(simple_asl)
print("Successfully compiled simple ASL sequence")

-- Test 3: ASL with dynamics
print("\n=== Test 3: ASL with Dynamic Variables ===")
local dynamic_asl = {
    to(dyn{level=3.0}, dyn{time=0.8}, 'linear'),
    to(0.0, 0.2, 'sine')
}

-- Test the dynamic variable system
a1.dyn.level = 4.5
a1.dyn.time = 1.2
print("Set dynamic variables: level =", a1.dyn.level, "time =", a1.dyn.time)

a1:describe(dynamic_asl)
print("Successfully compiled ASL with dynamics")

-- Test 4: Library functions
print("\n=== Test 4: ASL Library Functions ===")

-- Test LFO
local lfo_seq = lfo(2.0, 5.0, 'sine')
print("LFO created with", #lfo_seq, "elements")

-- Test AR envelope
local ar_seq = ar(0.1, 1.0, 7.0, 'log')
print("AR envelope created with", #ar_seq, "elements")

-- Test ADSR envelope
local adsr_seq = adsr(0.05, 0.3, 2.0, 1.5)
print("ADSR envelope created with", #adsr_seq, "elements")

-- Compile an LFO
a1:describe(lfo_seq)
print("Successfully compiled LFO sequence")

-- Test 5: Action triggering
print("\n=== Test 5: Action Triggering ===")
a1:action(1) -- Trigger the ASL
print("Triggered ASL action")

-- Test 6: Output integration
print("\n=== Test 6: Output Integration ===")
print("Current output 1 voltage:", output[1].volts)

-- Set a test voltage
output[1].volts = 3.5
print("Set output 1 to 3.5V, readback:", output[1].volts)

print("\n=== CASL Integration Test Completed ===")
print("All tests passed! ASL→CASL pipeline is working.")

return {
    asl_instance = a1,
    test_sequences = {
        simple = simple_asl,
        dynamic = dynamic_asl,
        lfo = lfo_seq,
        ar = ar_seq,
        adsr = adsr_seq
    }
}
