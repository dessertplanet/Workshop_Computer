#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// Error reporting system for crow emulator
// Provides consistent error handling across all systems

typedef enum {
    CROW_ERROR_NONE = 0,
    CROW_ERROR_LUA_SYNTAX,
    CROW_ERROR_LUA_RUNTIME,
    CROW_ERROR_LUA_MEMORY,
    CROW_ERROR_USB_BUFFER_OVERFLOW,
    CROW_ERROR_SCRIPT_TOO_LARGE,
    CROW_ERROR_NO_SCRIPT,
    CROW_ERROR_HARDWARE_FAULT,
    CROW_ERROR_INIT_FAILED,
    CROW_ERROR_INVALID_PARAM,
    CROW_ERROR_SYSTEM_FAULT
} crow_error_t;

typedef struct {
    crow_error_t type;
    char message[128];
    uint32_t timestamp_us;
    const char* function;
    int line;
} crow_error_info_t;

#define CROW_ERROR_RING_SIZE 8

// Ring buffer accessors
size_t crow_error_ring_count(void);
const crow_error_info_t* crow_error_ring_get(size_t index);
void crow_error_dump_all(void);

// Error reporting functions
void crow_error_init(void);
void crow_error_report(crow_error_t type, const char* message, const char* function, int line);
void crow_error_clear(void);
bool crow_error_has_error(void);
const crow_error_info_t* crow_error_get_last(void);
void crow_error_print_last(void);

// Lua-specific error handling
void crow_error_lua_handler(const char* error_msg);
void crow_error_format_lua_traceback(const char* lua_error, char* output, size_t output_size);

// USB error reporting integration
void crow_error_send_to_usb(const crow_error_info_t* error);

// Macros for convenient error reporting
#define CROW_ERROR(type, msg) crow_error_report(type, msg, __FUNCTION__, __LINE__)
#define CROW_ERROR_LUA(msg) crow_error_report(CROW_ERROR_LUA_RUNTIME, msg, __FUNCTION__, __LINE__)

#ifdef __cplusplus
}
#endif
