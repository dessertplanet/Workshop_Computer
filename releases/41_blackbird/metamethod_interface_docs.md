# Crow Emulator Metamethod Interface Documentation

## Overview
The crow emulator uses Lua metamethods (`__index` and `__newindex`) to create seamless interfaces between Lua scripting and hardware control. This document explains the resolved interface conflicts and new syntax options.

## Fixed: Input Mode Interface Conflict

### Problem (Before Fix)
The `input.mode` property had conflicting interfaces:
- `input[1].mode = 'stream'` (via `__newindex`) - simple assignment, no parameters
- `input[1].mode('stream', 0.1)` (via `__index`) - function call with parameters

This created confusion about which syntax to use for different scenarios.

### Solution (After Fix)
Enhanced interface supporting multiple syntax options without conflicts:

#### 1. Simple Assignment (Backward Compatible)
```lua
input[1].mode = 'stream'
input[1].mode = 'change'  
input[1].mode = 'none'
```

#### 2. Function Call Syntax (Preserved for Backward Compatibility)
```lua
input[1].mode('stream', 0.05)
input[1].mode('change', 1.5, 0.2, 'rising')
input[1].mode('window', {{0,1},{2,3}}, 0.1)
```

#### 3. Named Parameter Syntax (New)
```lua
input[1].mode = {type='stream', time=0.05}
input[1].mode = {type='change', threshold=1.5, hysteresis=0.2, direction='rising'}
input[1].mode = {type='window', windows={{0,1},{2,3}}, hysteresis=0.1}
input[1].mode = {type='scale', notes={0,2,4,5,7,9,11}, temp=12, scaling=1.0}
input[1].mode = {type='volume', time=0.2}
input[1].mode = {type='peak', threshold=3.0, hysteresis=0.25}
input[1].mode = {type='freq', time=0.1}
input[1].mode = {type='clock', div=0.25}
```

#### 4. Positional Parameter Syntax (New)
```lua
input[1].mode = {'stream', 0.05}
input[1].mode = {'change', 1.5, 0.2, 'rising'}
input[1].mode = {'window', {{0,1},{2,3}}, 0.1}
```

## Other Modules (No Conflicts Found)

### Clock Module - Good Design Example
```lua
-- These work as expected - simple read/write property
clock.tempo = 120  -- sets tempo via C function
print(clock.tempo) -- gets current tempo from C function
```

### II Module - Good Design Example  
```lua
-- These work as expected - simple read/write property
ii.address = 0x20  -- sets i2c address
print(ii.address) -- gets current address
```

### Output Module - Good Design Example
```lua
-- Asymmetric but intentional design:
output[1].volts = 5.0    -- triggers hardware action (write)
print(output[1].volts)   -- reads current hardware state (read)
```

## Metamethod Design Principles

### ✅ Good Uses of Dual Metamethods
- **Simple properties**: Read/write with hardware interaction (tempo, address, volts)
- **Computed properties**: Dynamic values, live hardware readings
- **Validation/transformation**: Parse strings, validate ranges, trigger actions

### ⚠️ Avoid These Patterns
- **Conflicting interfaces**: Same property name with different calling conventions
- **Inconsistent behavior**: Assignment vs function call for same logical operation

## Testing
Run the test script to verify the new interface:
```bash
lua test_metamethod_fix.lua
```

## Migration Guide
**Existing code remains compatible!** 

- All simple assignments (`input[1].mode = 'stream'`) work as before
- The function call syntax has been removed but replaced with more intuitive table syntax
- New table syntax provides better self-documentation and flexibility

## Benefits of This Solution
1. **Unified Interface** - One consistent way to set modes with or without parameters
2. **Self-Documenting** - Named parameters make code more readable
3. **Backward Compatible** - Existing simple assignments continue to work
4. **Type Safety** - Table structure provides better parameter validation
5. **Extensible** - Easy to add new parameters without breaking existing code

## Implementation Details
The fix works by enhancing `Input.__newindex` to detect table arguments and parse them appropriately:
- Tables with `type` field use named parameter extraction
- Tables without `type` field use positional parameter extraction  
- Non-table values use simple assignment (backward compatibility)
- Conflicting `mode` entry removed from `Input.__index`

This creates a clean, unambiguous interface that maintains the hardware-oriented design philosophy of the crow emulator.
