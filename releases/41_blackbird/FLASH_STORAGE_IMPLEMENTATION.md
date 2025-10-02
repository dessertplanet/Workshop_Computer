# Flash Storage Implementation for Blackbird Crow Emulator

## Overview

This document describes the implementation of user script flash storage for the Blackbird crow emulator, matching the behavior of the real monome crow hardware.

## Implementation Summary

### 1. Flash Storage Architecture

**Flash Layout (RP2040 - 2MB Flash):**
- **Last 16KB reserved for user scripts** (matches crow v3.0+)
- Location: `0x1FFC000` to `0x1FFFFFF`
- 4 sectors × 4KB each
- Uses RP2040's XIP (Execute-In-Place) for direct execution from flash

**Storage Format:**
```
Offset 0x0: Status Word (32-bit)
  bits [0:3]   - Magic number (0xA=user script, 0xC=cleared, other=default)
  bits [4:15]  - Firmware version (12-bit)
  bits [16:31] - Script length in bytes (16-bit)

Offset 0x4: Script Data
  - Raw Lua source code
  - Maximum: 16,380 bytes (16KB - 4 bytes)
```

### 2. Upload State Machine

The implementation adds a **REPL mode state machine** matching crow's behavior:

```c
typedef enum {
    REPL_normal = 0,    // Normal REPL - evaluate immediately
    REPL_reception,     // Script upload mode - accumulate data
    REPL_discard        // Error state - discard input
} repl_mode_t;
```

**State Transitions:**
```
Normal Mode:
  ^^s → Reception Mode → accumulate script data
  ^^e → Execute from RAM, return to Normal
  ^^w → Execute AND save to flash, return to Normal
  ESC → Return to Normal (abort)
```

### 3. Upload Command Sequence

**From druid:**
```
1. Send: ^^s
   → Emulator enters REPL_reception mode
   → All incoming data is buffered, NOT executed

2. Stream script lines
   → Each line appended to g_new_script buffer
   → Buffer overflow protection (16KB limit)

3a. Send: ^^e (run in RAM only)
    → Execute script
    → Temporary - lost on reboot
    
3b. Send: ^^w (save to flash)
    → Execute script
    → Write to flash
    → Persistent across reboots
```

### 4. Boot Sequence

On startup, the emulator checks flash and loads the appropriate script:

```cpp
switch(FlashStorage::which_user_script()) {
    case USERSCRIPT_Default:
        // Load First.lua (built-in default)
        break;
        
    case USERSCRIPT_User:
        // Load and execute user script from flash
        // Uses XIP - no copy to RAM needed!
        break;
        
    case USERSCRIPT_Clear:
        // No script loaded
        break;
}
```

### 5. Key Features

✅ **Druid Compatible** - Implements crow's upload protocol exactly  
✅ **XIP Execution** - Scripts execute directly from flash (no RAM copy)  
✅ **State Machine** - Proper separation of REPL vs reception mode  
✅ **Error Handling** - Buffer overflow protection, upload abort (ESC)  
✅ **Persistent Storage** - 16KB user script survives reboots  
✅ **Flash Commands** - `^^w` (write), `^^c` (clear), `^^f` (default)

## Files Modified/Created

### New Files:
- `lib/flash_storage.h` - Flash storage API
- `lib/flash_storage.cpp` - Flash storage implementation

### Modified Files:
- `main.cpp` - Added:
  - Upload state machine variables
  - `receive_script_data()` function
  - Updated command handlers
  - Boot sequence flash loading
  - ESC key resets reception mode
  
- `CMakeLists.txt` - Added:
  - `lib/flash_storage.cpp` to sources
  - `hardware_flash` to link libraries

## API Reference

### FlashStorage Class Methods

```cpp
// Initialize flash storage system
static void init();

// Check what type of script is stored
static USERSCRIPT_t which_user_script();

// Write a script to flash (returns true on success)
static bool write_user_script(const char* script, uint32_t length);

// Read script from flash into buffer
static bool read_user_script(char* buffer, uint32_t* length);

// Get script length without reading
static uint16_t get_user_script_length();

// Get direct pointer to script in flash (XIP, read-only)
static const char* get_user_script_addr();

// Clear user script (write clear marker)
static void clear_user_script();

// Reset to default script mode
static void load_default_script();
```

## Command Reference

### Crow Protocol Commands

| Command | Description | State Change |
|---------|-------------|--------------|
| `^^s` | Start upload | → REPL_reception |
| `^^e` | End upload (RAM) | → REPL_normal |
| `^^w` | Flash upload | → REPL_normal |
| `^^c` | Clear flash | - |
| `^^f` | Load First.lua | - |
| `ESC` | Abort upload | → REPL_normal |

## Usage Example with druid

```bash
# Connect to emulator
druid connect

# Upload and save script to flash
druid upload myscript.lua

# This internally does:
# 1. Send ^^s (start upload)
# 2. Stream script lines
# 3. Send ^^w (flash upload)

# Clear user script
druid clear

# This sends: ^^c
```

## Technical Notes

### RP2040-Specific Considerations

1. **Flash Erase Granularity**: 4KB sectors (vs STM32F7's variable sectors)
2. **Flash Program Granularity**: 256 bytes
3. **XIP Advantage**: Can execute directly from flash without copying to RAM
4. **Critical Section**: Flash operations require interrupts disabled
5. **Multi-core**: Currently single-core during flash operations (safe)

### Memory Safety

- Buffer overflow protection in `receive_script_data()`
- Maximum script size: 16,380 bytes
- Overflow switches to `REPL_discard` mode
- ESC key provides emergency abort

### Flash Wear

- RP2040 flash: ~100K erase cycles
- User scripts updated infrequently (not a concern)
- No wear leveling needed for this use case

## Testing

### Basic Upload Test
```lua
-- test.lua
print("Hello from flash!")

function init()
    print("Script initialized")
end
```

```bash
# Upload to flash
druid upload test.lua

# Should see:
# "User script updated."

# Reboot emulator
# Should see at boot:
# "Loaded: user script from flash (XX bytes)"
# "Hello from flash!"
# "Script initialized"
```

### Clear Test
```bash
druid clear

# Should see:
# "User script cleared."

# Reboot - should load First.lua instead
```

## Comparison with Real Crow

| Feature | Real Crow | Blackbird Emulator |
|---------|-----------|-------------------|
| Flash size | 16KB | 16KB ✅ |
| Upload protocol | `^^s`, `^^e`, `^^w` | Same ✅ |
| Reception mode | Yes | Yes ✅ |
| XIP execution | No (STM32 copies) | Yes (RP2040 XIP) ✅ |
| Boot detection | 3 modes | 3 modes ✅ |
| druid compatible | Yes | Yes ✅ |

## Future Enhancements

Potential improvements (not yet implemented):

1. **Script versioning** - Track script version in status word
2. **Checksum validation** - CRC check on boot
3. **Multi-script support** - Store multiple scripts, switch between them
4. **Compression** - LZ4 compression for larger scripts
5. **Script metadata** - Store name, author, timestamp

## Troubleshooting

### Script upload fails
- Check script size < 16KB
- Try ESC to abort, then retry
- Check USB connection stable

### Script doesn't persist after reboot
- Verify `^^w` used (not `^^e`)
- Check flash write success message
- Try `^^c` then re-upload

### Boot hangs after flash write
- Script may have syntax error
- Flash may be corrupted
- Use `^^c` to clear, then `^^f` to load default

---

**Implementation Date**: October 2, 2025  
**Version**: 0.4  
**Author**: Blackbird Crow Emulator Team
