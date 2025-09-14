#include "crow_error.h"
#include "pico/time.h"
#include "tusb.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

// Global error state
static crow_error_info_t g_last_error;
static bool g_error_initialized = false;

// Forward declarations for USB communication
extern "C" {
    // These will be linked from CrowEmulator
    void crow_send_error_to_usb(const char* message);
}

void crow_error_init(void)
{
    memset(&g_last_error, 0, sizeof(g_last_error));
    g_last_error.type = CROW_ERROR_NONE;
    g_error_initialized = true;
}

void crow_error_report(crow_error_t type, const char* message, const char* function, int line)
{
    if (!g_error_initialized) {
        crow_error_init();
    }
    
    // Update error info
    g_last_error.type = type;
    g_last_error.timestamp_us = time_us_64();
    g_last_error.function = function;
    g_last_error.line = line;
    
    // Copy message safely
    strncpy(g_last_error.message, message, sizeof(g_last_error.message) - 1);
    g_last_error.message[sizeof(g_last_error.message) - 1] = '\0';
    
    // Print to debug console
    printf("CROW ERROR [%llu]: %s in %s:%d\n", 
           g_last_error.timestamp_us, message, function, line);
    
    // Send to USB if available
    crow_error_send_to_usb(&g_last_error);
}

void crow_error_clear(void)
{
    if (g_error_initialized) {
        g_last_error.type = CROW_ERROR_NONE;
        memset(g_last_error.message, 0, sizeof(g_last_error.message));
    }
}

bool crow_error_has_error(void)
{
    return g_error_initialized && (g_last_error.type != CROW_ERROR_NONE);
}

const crow_error_info_t* crow_error_get_last(void)
{
    return g_error_initialized ? &g_last_error : nullptr;
}

void crow_error_print_last(void)
{
    if (crow_error_has_error()) {
        printf("Last error: %s (type %d) at %s:%d\n",
               g_last_error.message,
               g_last_error.type,
               g_last_error.function,
               g_last_error.line);
    } else {
        printf("No error recorded\n");
    }
}

// Lua-specific error handling
void crow_error_lua_handler(const char* error_msg)
{
    char formatted_msg[256];
    crow_error_format_lua_traceback(error_msg, formatted_msg, sizeof(formatted_msg));
    crow_error_report(CROW_ERROR_LUA_RUNTIME, formatted_msg, "lua_handler", 0);
}

void crow_error_format_lua_traceback(const char* lua_error, char* output, size_t output_size)
{
    if (!lua_error || !output || output_size == 0) {
        return;
    }
    
    // Format lua error for crow-style output
    // Real crow formats lua errors with line numbers and context
    const char* line_start = strstr(lua_error, ":");
    if (line_start) {
        // Extract line number if present
        char* endptr;
        int line_num = (int)strtol(line_start + 1, &endptr, 10);
        if (endptr != line_start + 1) {
            snprintf(output, output_size, "lua error line %d: %s", line_num, endptr + 2);
        } else {
            snprintf(output, output_size, "lua error: %s", lua_error);
        }
    } else {
        snprintf(output, output_size, "lua error: %s", lua_error);
    }
    
    // Ensure null termination
    output[output_size - 1] = '\0';
}

void crow_error_send_to_usb(const crow_error_info_t* error)
{
    if (!error || error->type == CROW_ERROR_NONE) {
        return;
    }
    
    // Format error message for USB transmission
    char usb_message[256];
    const char* error_prefix = "!";
    
    switch (error->type) {
        case CROW_ERROR_LUA_SYNTAX:
        case CROW_ERROR_LUA_RUNTIME:
        case CROW_ERROR_LUA_MEMORY:
            snprintf(usb_message, sizeof(usb_message), "%s%s", error_prefix, error->message);
            break;
        case CROW_ERROR_USB_BUFFER_OVERFLOW:
            snprintf(usb_message, sizeof(usb_message), "%schunk too long!", error_prefix);
            break;
        case CROW_ERROR_SCRIPT_TOO_LARGE:
            snprintf(usb_message, sizeof(usb_message), "%sscript too large", error_prefix);
            break;
        case CROW_ERROR_NO_SCRIPT:
            snprintf(usb_message, sizeof(usb_message), "%sno script loaded", error_prefix);
            break;
        default:
            snprintf(usb_message, sizeof(usb_message), "%ssystem error: %s", error_prefix, error->message);
            break;
    }
    
    // Send via USB CDC if connected
    if (tud_cdc_connected()) {
        tud_cdc_write_str(usb_message);
        tud_cdc_write_str("\n\r");
        tud_cdc_write_flush();
    }
}
