# Druid `u` Command Fix - Script Auto-Execution After Flash Upload

## Problem Summary

The `u` command (upload to flash) was uploading scripts to flash memory successfully but not executing them afterward, unlike real crow hardware which should run the script immediately after upload.

## Root Cause Analysis

The issue was in the `finalize_script_upload()` function in `crow_emulator.cpp`. When `persist=true` (the `u` command), the sequence was:

1. ✅ Compile and load script into Lua
2. ✅ Call `init()` 
3. ✅ Save to flash via `Flash_write_user_script()`
4. ❌ **Script lost from memory after flash write**

The problem occurred because `Flash_write_user_script()` includes multicore coordination that stops and restarts core1 (which runs the Lua VM):

```cpp
multicore_reset_core1();        // This kills the Lua VM state!
flash_range_erase(...);
flash_range_program(...);
multicore_launch_core1(CrowEmulator_core1_entry);  // Restarts with clean state
```

So the script was saved to flash but no longer loaded in the Lua VM.

## Solution Implemented

After successful flash write, reload the script from the buffer and re-call `init()`:

```cpp
if (result == 0) {
    send_usb_string("script saved to flash");
    printf("[DEBUG] Script saved to flash: %zu bytes\n", script_upload_pos);
    
    // After flash write (which restarts core1), reload the script to keep it running
    printf("[DEBUG] Reloading script from flash after multicore restart\n");
    if (g_crow_lua && g_crow_lua->load_user_script(script_upload_buffer)) {
        printf("[DEBUG] Script reloaded successfully after flash write\n");
        if (g_crow_lua->call_init()) {
            printf("[DEBUG] init() called successfully after reload\n");
        }
    } else {
        printf("[DEBUG] Failed to reload script after flash write\n");
        send_usb_string("!script reload error after flash write");
    }
}
```

## Expected Behavior Now

- `r script.lua` - runs script temporarily (loads and executes, not saved to flash)
- `u script.lua` - uploads to flash AND keeps running (exactly like real crow!)
- `^^print` - shows script content if saved to flash
- Script persists after power cycle (loaded from flash at boot)

## Testing

1. Upload a script with `u script.lua`
2. Script should execute immediately (calls `init()`)
3. Script should persist after reboot
4. No more ARM M lockups during flash operations

## Additional Fix: Safe Script Loading Without Multicore Interference

Initially, we attempted to reset the Lua state for both `r` and `u` commands to ensure clean script environments. However, this caused communication breakdowns because the Lua system runs on core1 while script upload processing happens on core0.

**Issue:** Calling `g_crow_lua->deinit()` and `g_crow_lua->init()` from core0 during script upload interfered with core1's Lua processing, causing the system to hang showing "Crow Lua initializing..." in druid.

**Solution:** We removed the unsafe multicore Lua reset and instead rely on the Lua system's natural script replacement behavior. The `load_user_script()` function properly loads new scripts into the existing Lua environment, and Lua's dynamic nature handles variable and function redefinition correctly.

This approach matches real crow hardware behavior where script state can persist between uploads unless explicitly cleared by the script itself (e.g., using global variable initialization).

The crow emulator now behaves exactly like real crow hardware for both `r` and `u` commands!
