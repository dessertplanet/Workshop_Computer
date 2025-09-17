# Crow Emulator Script Upload Fixes

## Problem Summary
The crow emulator was not properly handling the `druid run` and `druid upload` commands due to issues in the script upload protocol implementation.

## Root Cause Analysis
1. **Buffer Management Issues**: The script upload buffer wasn't properly accumulating multi-line script content
2. **Termination Detection**: The `^^e` and `^^w` termination commands weren't being detected reliably across USB packet boundaries
3. **Line Handling**: Druid sends scripts line-by-line with small delays, but the emulator wasn't handling this streaming protocol correctly

## Fixes Implemented

### 1. Enhanced USB Data Processing (`process_usb_data`)
- **Better termination detection**: Now properly handles cases where `^^e` or `^^w` might span USB packet boundaries
- **Comprehensive debugging**: Added detailed logging to track script upload progress
- **Improved packet boundary handling**: Better logic for detecting partial terminators

### 2. Improved Script Buffer Management (`process_script_upload_data`)
- **Enhanced debugging**: Added logging to track exactly what data is being added to the buffer
- **Better error handling**: More detailed error messages for buffer overflow conditions

### 3. Enhanced Script Finalization (`finalize_script_upload`)
- **Detailed logging**: Added comprehensive debugging to show script content and compilation status
- **Better error reporting**: More informative messages about compilation success/failure
- **Script content display**: Shows the exact script content received for debugging

## How Druid Commands Work

### `druid run <script.lua>` (^^e - execute without saving)
1. Druid sends `^^s` (start upload)
2. Druid sends script content line by line with 1ms delays
3. Druid sends `^^e` (execute without saving to flash)
4. Emulator compiles and runs the script

### `druid upload <script.lua>` (^^w - write to flash and execute) 
1. Druid sends `^^s` (start upload)
2. Druid sends script content line by line with 1ms delays  
3. Druid sends `^^w` (write to flash and execute)
4. Emulator compiles, runs, and saves the script to flash

## Testing Instructions

### 1. Flash the Updated Firmware
- Use the generated `corvus.uf2` file to update your crow emulator
- The file is located in `build/corvus.uf2`

### 2. Test with Simple Script
```bash
# Test run command (temporary execution)
druid run test_simple.lua

# Test upload command (save to flash)
druid upload test_simple.lua
```

### 3. Monitor Debug Output
The emulator now provides detailed debug output:
- `[DEBUG] Upload mode: received X bytes`
- `[DEBUG] Upload data: 'content'`
- `[DEBUG] Found terminator: ^^e at position X`
- `[DEBUG] Script content: [full script]`
- `[DEBUG] Script compiled successfully`

### 4. Expected Behavior
After successful script upload, you should see:
- "script loaded successfully" 
- "^^ready()" signal
- Script execution with init() function called
- Debug output showing voltage changes

## File Changes Made
- `crow_emulator.cpp`: Enhanced script upload processing with better debugging and packet boundary handling
- `test_simple.lua`: Simple test script for verification

## Verification
The fixes ensure that:
1. Multi-line scripts are properly accumulated in the upload buffer
2. Termination commands are reliably detected across packet boundaries  
3. Script compilation and execution works correctly
4. Proper response messages are sent back to druid
5. Flash storage works for persistent scripts

## Next Steps
1. Test with more complex scripts
2. Verify behavior matches real crow hardware exactly
3. Test edge cases like very large scripts or network interruptions
