#include "flash_storage.h"
#include <cstring>
#include "pico/multicore.h"
#include "tusb.h"

// External flag from main.cpp to signal core1
extern volatile bool g_flash_operation_pending;

void FlashStorage::init() {
    // Nothing special needed - XIP is already configured by SDK
    // Silent init - no output needed
}

USERSCRIPT_t FlashStorage::which_user_script() {
    const uint32_t* status_word = (const uint32_t*)USER_SCRIPT_LOCATION;
    uint8_t magic = (*status_word) & 0xF;
    
    switch(magic) {
        case USER_MAGIC: return USERSCRIPT_User;
        case USER_CLEAR: return USERSCRIPT_Clear;
        default:         return USERSCRIPT_Default;
    }
}

// Static buffer for flash operations - must not use dynamic allocation during flash write
static uint8_t flash_write_buffer[USER_SCRIPT_SIZE + 256] __attribute__((aligned(256)));

// Helper function in RAM that does the actual flash write
// Must be in RAM because XIP is disabled during flash operations
static void __not_in_flash_func(do_flash_write)(uint32_t offset, const uint8_t* data, size_t size) {
    // Disable interrupts during flash operations
    uint32_t ints = save_and_disable_interrupts();
    
    // Erase the required sectors
    flash_range_erase(offset, USER_SCRIPT_SECTORS * 4096);
    
    // Program the flash
    flash_range_program(offset, data, size);
    
    restore_interrupts(ints);
}

bool FlashStorage::write_user_script(const char* script, uint32_t length) {
    // Call the extended version with no name
    return write_user_script_with_name(script, length, "");
}

bool FlashStorage::write_user_script_with_name(const char* script, uint32_t length, const char* name) {
    if (length > USER_SCRIPT_SIZE) {
        return false;
    }
    
    // Don't use tud_cdc_write here - it might interfere with USB in some way
    // Just do the work silently
    
    // Prepare aligned buffer for flash programming (use static buffer to avoid heap allocation)
    // Layout: [status_word:4][script_name:32][script_data:N]
    const size_t total_size = 4 + MAX_SCRIPT_NAME_LEN + length;
    const size_t aligned_size = (total_size + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);  // Round up to page
    
    memset(flash_write_buffer, 0xFF, aligned_size);
    
    // Build status word: [magic:4][version:12][length:16]
    uint32_t status_word = USER_MAGIC 
                         | (get_version_word() << 4) 
                         | (length << 16);
    memcpy(flash_write_buffer, &status_word, 4);
    
    // Copy script name (up to 31 chars + null terminator)
    if (name && name[0]) {
        size_t name_len = strlen(name);
        if (name_len > MAX_SCRIPT_NAME_LEN - 1) {
            name_len = MAX_SCRIPT_NAME_LEN - 1;
        }
        memcpy(flash_write_buffer + 4, name, name_len);
        flash_write_buffer[4 + name_len] = '\0';  // Ensure null termination
    } else {
        flash_write_buffer[4] = '\0';  // Empty name
    }
    
    // Copy script data after name field
    memcpy(flash_write_buffer + 4 + MAX_SCRIPT_NAME_LEN, script, length);
    
    // CRITICAL: On RP2040 multicore, we MUST stop core1 before flash operations
    // because flash_range_erase/program disable XIP which would crash core1
    // We'll reset core1, do the flash write, then restart it
    // This will cause audio to stop temporarily (~1 second) but is unavoidable
    
    // Store core1 entry point so we can restart it
    extern void core1_entry();
    
    // Reset core1 (this stops it completely)
    multicore_reset_core1();
    
    // Now it's safe to write to flash
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(USER_SCRIPT_OFFSET, USER_SCRIPT_SECTORS * 4096);
    flash_range_program(USER_SCRIPT_OFFSET, flash_write_buffer, aligned_size);
    restore_interrupts(ints);
    
    // Restart core1
    multicore_launch_core1(core1_entry);
    
    return true;
}

bool FlashStorage::read_user_script(char* buffer, uint32_t* length) {
    if (which_user_script() != USERSCRIPT_User) {
        return false;
    }
    
    const uint32_t* flash_addr = (const uint32_t*)USER_SCRIPT_LOCATION;
    *length = (*flash_addr >> 16) & 0xFFFF;
    
    // Read directly from flash (XIP makes this easy!)
    // Script data starts after: [status_word:4][script_name:32]
    const char* script_start = (const char*)(USER_SCRIPT_LOCATION + 4 + MAX_SCRIPT_NAME_LEN);
    memcpy(buffer, script_start, *length);
    
    return true;
}

uint16_t FlashStorage::get_user_script_length() {
    if (which_user_script() != USERSCRIPT_User) {
        return 0;
    }
    const uint32_t* status_word = (const uint32_t*)USER_SCRIPT_LOCATION;
    return (*status_word >> 16) & 0xFFFF;
}

const char* FlashStorage::get_user_script_addr() {
    if (which_user_script() != USERSCRIPT_User) {
        return nullptr;
    }
    // Script data starts after: [status_word:4][script_name:32]
    return (const char*)(USER_SCRIPT_LOCATION + 4 + MAX_SCRIPT_NAME_LEN);
}

const char* FlashStorage::get_script_name() {
    if (which_user_script() != USERSCRIPT_User) {
        return "";
    }
    // Script name is stored right after status word
    const char* name = (const char*)(USER_SCRIPT_LOCATION + 4);
    // Return empty string if first char is 0xFF (erased flash) or 0x00 (empty name)
    if (name[0] == (char)0xFF || name[0] == '\0') {
        return "";
    }
    return name;
}

void FlashStorage::clear_user_script() {
    uint8_t buffer[FLASH_PAGE_SIZE];
    memset(buffer, 0xFF, FLASH_PAGE_SIZE);
    
    // Write USER_CLEAR magic to indicate intentional clearing
    uint32_t status_word = USER_CLEAR | (get_version_word() << 4);
    memcpy(buffer, &status_word, 4);
    
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(USER_SCRIPT_OFFSET, USER_SCRIPT_SECTORS * 4096);
    flash_range_program(USER_SCRIPT_OFFSET, buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    
    tud_cdc_write_str("User script cleared\n\r");
    tud_cdc_write_flush();
}

// Set flash to use default First.lua script on boot
// Returns true on success
bool FlashStorage::set_default_script_mode() {
    uint8_t buffer[FLASH_PAGE_SIZE];
    memset(buffer, 0xFF, FLASH_PAGE_SIZE);
    
    // Write neither USER_MAGIC nor USER_CLEAR - triggers default
    uint32_t status_word = 0x0 | (get_version_word() << 4);
    memcpy(buffer, &status_word, 4);
    
    // CRITICAL: On RP2040 multicore, we MUST stop core1 before flash operations
    // Store core1 entry point so we can restart it
    extern void core1_entry();
    
    // Reset core1 (this stops it completely)
    multicore_reset_core1();
    
    // Now it's safe to write to flash
    uint32_t ints = save_and_disable_interrupts();
    // Erase all user script sectors to fully clear any stored script
    flash_range_erase(USER_SCRIPT_OFFSET, USER_SCRIPT_SECTORS * 4096);
    flash_range_program(USER_SCRIPT_OFFSET, buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    
    // Restart core1
    multicore_launch_core1(core1_entry);
    
    return true;
}

uint32_t FlashStorage::get_version_word() {
    // Return firmware version as 12-bit value (0x040 = v0.4)
    return 0x040;
}
