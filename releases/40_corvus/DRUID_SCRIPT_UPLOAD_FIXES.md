# Druid Script Upload - How It Works

## Commands and Expected Behavior

### `r <filename>` (Run/Execute)
- Sends: `^^s` + script content + `^^e`
- **Does NOT save to flash** - script runs temporarily
- `^^print` will show "no script" because nothing is saved
- This is **correct behavior** - matches real crow

### `u <filename>` (Upload)  
- Sends: `^^s` + script content + `^^w`
- **Saves to flash AND runs** the script
- `^^print` will show the saved script content
- This is for persistent scripts

### `^^print` (Print saved script)
- Shows what's currently saved in flash
- Will show "no script" if nothing is saved (even if a script is running from `r` command)

## Implementation Status

### ✅ Working
- `^^s` (start upload) - handled by `C_startupload`
- `^^e` (execute without saving) - handled by `finalize_script_upload(false)`  
- `^^w` (write to flash and execute) - handled by `finalize_script_upload(true)`
- `^^print` - handled by `handle_print_command()` - reads from flash
- Script compilation and execution
- Flash persistence

### ✅ Verified Behavior
- **r command**: Loads and runs script, but doesn't save to flash
- **u command**: Loads, runs, and saves script to flash  
- **^^print**: Only shows scripts saved to flash (not temporary ones from r command)

## Testing

Use `test_run_vs_upload.lua` to verify:

1. **Test r command**: `r test_run_vs_upload.lua`
   - Should show "Test script loaded successfully!" and "init() called"
   - Should respond to input changes
   - `^^print` should show "no script" (correct!)

2. **Test u command**: `u test_run_vs_upload.lua`  
   - Should show "Test script loaded successfully!" and "init() called"
   - Should respond to input changes
   - `^^print` should show the script content (saved to flash)

3. **Test input changes**: Send clock/voltage changes to input 1
   - Should see "Input 1 changed to: X.XX" messages
   - Output 1 should be 2x the input voltage

## Key Insight

The original confusion was thinking `^^print` showing "no script" after `r` command was a bug. 
It's actually **correct behavior** - `r` runs scripts temporarily without saving them.
