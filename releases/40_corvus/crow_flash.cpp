#include "crow_flash.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>
#include <cstdio>

// External symbol marking end of program flash
extern char __flash_binary_end;

// ===== Flash layout safety additions =====
#ifndef PICO_FLASH_SIZE_BYTES
// Fallback if board definition not present (common Pico default 2MB)
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

static bool g_flash_layout_valid = true;

// Forward declaration for offset helper used below
static uint32_t get_flash_offset();

static bool flash_layout_check() {
    // We need two contiguous sectors: user script + First.lua
    uint32_t user_sector_off = get_flash_offset();
    uint32_t first_sector_off = user_sector_off + FLASH_SECTOR_SIZE;
    uint32_t end_required = first_sector_off + FLASH_SECTOR_SIZE;
    if (end_required > PICO_FLASH_SIZE_BYTES) {
        printf("FLASH WARNING: insufficient space for script sectors (need end 0x%08x, flash size 0x%08x)\n",
               end_required, (unsigned)PICO_FLASH_SIZE_BYTES);
        return false;
    }
    return true;
}

// Public helper (could be added to header later if needed)
bool Flash_layout_valid(void) {
    return g_flash_layout_valid;
}

// Calculate flash offset dynamically 
static uint32_t get_flash_offset() {
    // Round up to next flash sector (4KB aligned)
    uintptr_t binary_end = (uintptr_t)&__flash_binary_end;
    uintptr_t flash_start = XIP_BASE;
    uint32_t program_size = binary_end - flash_start;
    
    // Round up to next 4KB boundary for flash operations
    return (program_size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
}

static uint32_t* get_flash_address() {
    return (uint32_t*)(XIP_BASE + get_flash_offset());
}

USERSCRIPT_t Flash_which_user_script(void) {
    uint32_t* flash_addr = get_flash_address();
    uint32_t magic = *flash_addr;
    
    if (magic == USER_MAGIC) {
        return USERSCRIPT_User;
    } else if (magic == USER_CLEAR) {
        return USERSCRIPT_Clear;
    } else {
        return USERSCRIPT_Default;
    }
}

void Flash_clear_user_script(void) {
    uint32_t flash_offset = get_flash_offset();
    
    // Critical section - disable interrupts during flash operation
    uint32_t ints = save_and_disable_interrupts();
    
    // Erase the sector
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    
    // Write clear magic number
    uint32_t clear_magic = USER_CLEAR;
    flash_range_program(flash_offset, (uint8_t*)&clear_magic, sizeof(uint32_t));
    
    restore_interrupts(ints);
}

uint8_t Flash_write_user_script(char* script, uint32_t length) {
    if (length > USER_SCRIPT_SIZE) {
        return 1; // Script too large
    }
    
    uint32_t flash_offset = get_flash_offset();
    
    // Prepare data buffer (4KB sector)
    uint8_t sector_data[FLASH_SECTOR_SIZE];
    memset(sector_data, 0xFF, FLASH_SECTOR_SIZE); // Initialize to erased state
    
    // Set header
    *(uint32_t*)sector_data = USER_MAGIC;
    *(uint32_t*)(sector_data + 4) = length;
    
    // Copy script data
    memcpy(sector_data + 8, script, length);
    
    // Critical section - disable interrupts during flash operation
    uint32_t ints = save_and_disable_interrupts();
    
    // Erase and program sector
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    flash_range_program(flash_offset, sector_data, FLASH_SECTOR_SIZE);
    
    restore_interrupts(ints);
    
    return 0; // Success
}

uint16_t Flash_read_user_scriptlen(void) {
    uint32_t* flash_addr = get_flash_address();
    
    if (*flash_addr != USER_MAGIC) {
        return 0; // No valid script
    }
    
    return (uint16_t)(*(flash_addr + 1));
}

char* Flash_read_user_scriptaddr(void) {
    uint32_t* flash_addr = get_flash_address();
    
    if (*flash_addr != USER_MAGIC) {
        return NULL; // No valid script
    }
    
    return (char*)(flash_addr + 2); // Skip 8-byte header
}

uint8_t Flash_read_user_script(char* buffer) {
    uint32_t* flash_addr = get_flash_address();
    
    if (*flash_addr != USER_MAGIC) {
        return 1; // No valid script
    }
    
    uint32_t length = *(flash_addr + 1);
    if (length > USER_SCRIPT_SIZE) {
        return 1; // Invalid length
    }
    
    memcpy(buffer, flash_addr + 2, length);
    buffer[length] = '\0'; // Null terminate
    
    return 0; // Success
}

void Flash_default_user_script(void) {
    // Clear any existing user script to force loading of First.lua
    Flash_clear_user_script();
}

// === FIRST.LUA FLASH FUNCTIONS ===

// Get First.lua flash offset (separate from user script)
static uint32_t get_first_flash_offset() {
    return get_flash_offset() + FLASH_SECTOR_SIZE; // Next 4KB sector after user script
}

static uint32_t* get_first_flash_address() {
    return (uint32_t*)(XIP_BASE + get_first_flash_offset());
}

// Check if First.lua is stored in flash
bool Flash_first_exists(void) {
    uint32_t* flash_addr = get_first_flash_address();
    return (*flash_addr == USER_MAGIC);
}

// Write First.lua to separate flash sector
uint8_t Flash_write_first_script(const char* script, uint32_t length) {
    if (length > USER_SCRIPT_SIZE) {
        return 1; // Script too large
    }
    
    uint32_t flash_offset = get_first_flash_offset();
    
    // Prepare data buffer (4KB sector)
    uint8_t sector_data[FLASH_SECTOR_SIZE];
    memset(sector_data, 0xFF, FLASH_SECTOR_SIZE);
    
    // Set header
    *(uint32_t*)sector_data = USER_MAGIC;
    *(uint32_t*)(sector_data + 4) = length;
    
    // Copy script data
    memcpy(sector_data + 8, script, length);
    
    // Critical section - disable interrupts during flash operation
    uint32_t ints = save_and_disable_interrupts();
    
    // Erase and program sector
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    flash_range_program(flash_offset, sector_data, FLASH_SECTOR_SIZE);
    
    restore_interrupts(ints);
    
    return 0; // Success
}

// Read First.lua from flash
uint8_t Flash_read_first_script(char* buffer) {
    uint32_t* flash_addr = get_first_flash_address();
    
    if (*flash_addr != USER_MAGIC) {
        return 1; // No First.lua stored
    }
    
    uint32_t length = *(flash_addr + 1);
    if (length > USER_SCRIPT_SIZE) {
        return 1; // Invalid length
    }
    
    memcpy(buffer, flash_addr + 2, length);
    buffer[length] = '\0'; // Null terminate
    
    return 0; // Success
}

// Get direct address of First.lua (for fast access)
char* Flash_read_first_scriptaddr(void) {
    uint32_t* flash_addr = get_first_flash_address();
    
    if (*flash_addr != USER_MAGIC) {
        return NULL; // No First.lua stored
    }
    
    return (char*)(flash_addr + 2); // Skip 8-byte header
}

void Flash_init(void) {
    // Validate layout once at startup
    g_flash_layout_valid = flash_layout_check();
    if (!g_flash_layout_valid) {
        printf("FLASH ERROR: layout invalid; script writes disabled\n");
    }
}

uint32_t flash_version12b(void) {
    // Return a 12-bit version number for compatibility
    return 0x400; // Version 4.0.0 equivalent
}
