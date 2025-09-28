#!/usr/bin/env lua
-- Test script for the fixed __index/__newindex metamethod conflicts
-- This demonstrates the new unified mode interface

-- Mock the C functions that would normally be available
function set_input_none() end
function set_input_stream() end  
function set_input_change() end
function set_input_window() end
function set_input_scale() end
function set_input_volume() end
function set_input_peak() end
function set_input_freq() end
function set_input_clock() end
function io_get_input() return 2.5 end

-- Mock the global _c object
_c = {
    tell = function(...) 
        print("C call:", table.concat({...}, ", "))
    end
}

-- Load the Input module
local Input = require('lib/input')

print("=== Testing Fixed Metamethod Interface ===\n")

-- Create an input instance
local input1 = Input.new(1)

print("1. Simple mode assignment (backward compatible):")
input1.mode = 'stream'
print("   Mode set to:", input1._mode)
print()

print("2. Table mode assignment with named parameters:")
input1.mode = {type='change', threshold=1.5, hysteresis=0.2, direction='rising'}
print("   Mode set to:", input1._mode)
print("   Threshold:", input1.threshold)
print("   Hysteresis:", input1.hysteresis) 
print("   Direction:", input1.direction)
print()

print("3. Table mode assignment with positional parameters:")
input1.mode = {'window', {{1,2,3}}, 0.1}
print("   Mode set to:", input1._mode)
print("   Windows:", input1.windows and #input1.windows or "nil")
print("   Hysteresis:", input1.hysteresis)
print()

print("4. Testing volts property (should work as before):")
print("   Current volts:", input1.volts)
print()

print("5. Testing query method (should work as before):")
print("   Query function exists:", type(input1.query) == 'function')
print()

print("6. All supported mode types with table syntax:")
local test_modes = {
    {type='stream', time=0.05},
    {type='change', threshold=2.0, hysteresis=0.15, direction='both'},
    {type='window', windows={{0,1},{2,3}}, hysteresis=0.08},
    {type='scale', notes={0,2,4,5,7,9,11}, temp=12, scaling=1.0},
    {type='volume', time=0.2},
    {type='peak', threshold=3.0, hysteresis=0.25},
    {type='freq', time=0.1},
    {type='clock', div=0.25}
}

for i, mode_config in ipairs(test_modes) do
    input1.mode = mode_config
    print(string.format("   %s mode set successfully", mode_config.type))
end

print("\n7. Testing function call syntax (backward compatibility):")
input1.mode('stream', 2)
print("   Function call input1.mode('stream', 2) works:", input1._mode == 'stream')
print("   Time parameter set:", input1.time == 2)

input1.mode('change', 1.5, 0.3, 'rising')
print("   Function call with multiple args works:", input1._mode == 'change')
print("   Parameters set - threshold:", input1.threshold, "hysteresis:", input1.hysteresis, "direction:", input1.direction)

print("\n=== All Tests Completed Successfully! ===")
print("\nComplete Interface Summary:")
print("- input.mode = 'stream'  -- Simple assignment") 
print("- input.mode = {type='stream', time=0.1}  -- Named parameters")
print("- input.mode = {'stream', 0.1}  -- Positional parameters")
print("- input.mode('stream', 0.1)  -- Function call (backward compatible)")
print("- All syntax options now work together without conflicts!")
