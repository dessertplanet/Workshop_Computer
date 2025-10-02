#pragma once

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Flash storage configuration for user scripts
// Reserve last 16KB of flash for user script storage (matches crow v3.0+)
#define USER_SCRIPT_SIZE     (16 * 1024 - 4)  // 16KB minus status word
#define USER_SCRIPT_SECTORS  4                 // 4 sectors @ 4KB each = 16KB
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#define USER_SCRIPT_OFFSET    (PICO_FLASH_SIZE_BYTES - (USER_SCRIPT_SECTORS * 4096))
#define USER_SCRIPT_LOCATION  (XIP_BASE + USER_SCRIPT_OFFSET)

// Magic numbers (matches crow)
#define USER_MAGIC 0x0A  // User script present
#define USER_CLEAR 0x0C  // Flash cleared, no script

// Script type
typedef enum {
    USERSCRIPT_Default,  // Use default First.lua
    USERSCRIPT_User,     // User script stored in flash
    USERSCRIPT_Clear     // No script loaded
} USERSCRIPT_t;

class FlashStorage {
public:
    // Initialize flash storage system
    static void init();
    
    // Check what type of script is stored
    static USERSCRIPT_t which_user_script();
    
    // Write a script to flash (returns true on success)
    static bool write_user_script(const char* script, uint32_t length);
    
    // Read script from flash into buffer (returns true on success)
    static bool read_user_script(char* buffer, uint32_t* length);
    
    // Get script length without reading
    static uint16_t get_user_script_length();
    
    // Get direct pointer to script in flash (read-only, XIP)
    static const char* get_user_script_addr();
    
    // Clear user script (write clear marker)
    static void clear_user_script();
    
    // Reset to default script mode
    static void load_default_script();
    
private:
    // Get firmware version word for status
    static uint32_t get_version_word();
};
