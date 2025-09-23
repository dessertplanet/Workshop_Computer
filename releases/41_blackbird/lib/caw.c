#include "lib/caw.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

void Caw_printf(char* text, ...) {
    if (!text) return;
    
    va_list args;
    va_start(args, text);
    printf("CAW: ");
    vprintf(text, args);
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
