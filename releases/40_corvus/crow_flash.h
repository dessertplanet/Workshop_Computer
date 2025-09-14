#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"

// Flash layout constants (adapted from pico-lfs-test dynamic approach)
extern uint32_t flash_start;
extern uint32_t flash_user_script_start;
extern uint32_t flash_user_script_offset;
extern uint32_t flash_calibration_start;
extern uint32_t flash_calibration_offset;

// Script size limits (matching crow v3.0+)
#define USER_SCRIPT_SIZE    (0x4000 - 4)  // 16KB - 4 bytes for header (crow v3.0+)
#define CALIBRATION_SIZE    (0x4000 - 4)  // 16KB - 4 bytes for header

// Magic numbers (from crow submodule)
#define USER_MAGIC 0xA  // Script present
#define USER_CLEAR 0xC  // Script cleared

// Script state enumeration (matches crow exactly)
typedef enum { 
    USERSCRIPT_Default,  // No user script, load First.lua
    USERSCRIPT_User,     // User script present
    USERSCRIPT_Clear     // Script explicitly cleared
} USERSCRIPT_t;

// Flash status for internal state tracking
typedef enum { 
    FLASH_Status_Init  = 0,
    FLASH_Status_Saved = 1,
    FLASH_Status_Dirty = 2
} FLASH_Status_t;

typedef struct FLASH_Store {
    FLASH_Status_t status;
    size_t         size;
    void*          address;
} FLASH_Store_t;

// === USER SCRIPT FUNCTIONS (exact crow API) ===

// Check what type of script is stored
USERSCRIPT_t Flash_which_user_script(void);

// Clear user script (^^c command)
void Flash_clear_user_script(void);

// Load default First.lua script (^^f command)
void Flash_default_user_script(void);

// Write user script to flash (^^w command)
// Returns 0 on success, 1 if script too long
uint8_t Flash_write_user_script(char* script, uint32_t length);

// Get length of stored script
uint16_t Flash_read_user_scriptlen(void);

// Get direct address of stored script (for fast access)
char* Flash_read_user_scriptaddr(void);

// Read script into buffer (^^p command)
// Returns 0 on success, 1 if no script present
uint8_t Flash_read_user_script(char* buffer);

// === CALIBRATION FUNCTIONS (for future use) ===

// Check if calibration data present
uint8_t Flash_is_calibrated(void);

// Clear calibration data
void Flash_clear_calibration(void);

// Write calibration data
uint8_t Flash_write_calibration(uint8_t* data, uint32_t length);

// Read calibration data
uint8_t Flash_read_calibration(uint8_t* data, uint32_t length);

// === FIRST.LUA FUNCTIONS ===

// Check if First.lua exists in flash
bool Flash_first_exists(void);

// Write First.lua to separate flash sector
uint8_t Flash_write_first_script(const char* script, uint32_t length);

// Read First.lua from flash
uint8_t Flash_read_first_script(char* buffer);

// Get direct address of First.lua (for fast access)
char* Flash_read_first_scriptaddr(void);

// === INITIALIZATION ===

// Initialize flash addresses (call once at startup)
void Flash_init(void);

// === INTERNAL HELPERS ===

// Get version as 12-bit value (for compatibility)
uint32_t flash_version12b(void);
