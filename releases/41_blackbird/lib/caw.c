#include "lib/caw.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tusb.h"  // For TinyUSB CDC functions

// Stub implementations for RP2040 build

static char read_buffer[1024];
static uint32_t read_len = 0;

void Caw_Init(int timer_index) {
    // Stub implementation - no initialization needed for RP2040
    (void)timer_index;
}

void Caw_DeInit(void) {
    // Stub implementation - no deinitialization needed for RP2040
}

void Caw_send_raw(uint8_t* buf, uint32_t len) {
    // Stub implementation - could send to USB serial
    if (buf && len > 0) {
        printf("CAW_RAW: ");
        for (uint32_t i = 0; i < len; i++) {
            printf("%02x ", buf[i]);
        }
        printf("\n");
    }
}

void Caw_printf(const char* text, ...) {
    if (!text) return;
    
    // Use direct TinyUSB CDC write like ^^pubview messages
    // This ensures proper formatting without prefixes
    va_list args;
    va_start(args, text);
    
    // First get the length
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, text, args_copy);
    va_end(args_copy);
    
    if (len > 0) {
        // Allocate buffer (+3 for \r\n\0)
        char buf[len + 3];
        
        // Format the message
        vsnprintf(buf, len + 1, text, args);
        
        // Add crow-style line ending
        buf[len] = '\r';
        buf[len + 1] = '\n';
        buf[len + 2] = '\0';
        
        // Send directly via TinyUSB CDC (same method as ^^pubview)
        if (tud_cdc_connected()) {
            tud_cdc_write(buf, len + 2);
            tud_cdc_write_flush();
        }
    }
    
    va_end(args);
}

void Caw_send_luachunk(char* text) {
    if (text) {
        printf("CAW_LUACHUNK: %s\n", text);
    }
}

void Caw_send_luaerror(char* error_msg) {
    if (error_msg) {
        printf("CAW_LUAERROR: %s\n", error_msg);
    }
}

void Caw_send_value(uint8_t type, float value) {
    printf("CAW_VALUE: type=%d, value=%f\n", type, value);
}

void Caw_stream_constchar(const char* stream) {
    if (stream) {
        printf("CAW_STREAM: %s\n", stream);
    }
}

void Caw_send_queued(void) {
    // Stub implementation - nothing queued in this simple version
}

C_cmd_t Caw_try_receive(void) {
    // Stub implementation - always return no command
    return C_none;
}

char* Caw_get_read(void) {
    return read_buffer;
}

uint32_t Caw_get_read_len(void) {
    return read_len;
}
