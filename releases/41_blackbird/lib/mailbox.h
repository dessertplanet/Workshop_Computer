#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Simple mailbox communication between cores
// Replaces complex lock-free and mutex-based systems

// Command mailbox: Core1 (USB) → Core0 (Main)
typedef struct {
    char command[128];
    volatile bool ready;
    volatile bool processed;
} usb_command_mailbox_t;

// Response mailbox: Core0 (Main) → Core1 (USB)
typedef struct {
    char response[256];
    volatile bool ready;
    volatile bool sent;
} usb_response_mailbox_t;

// Global mailbox instances
extern volatile usb_command_mailbox_t g_command_mailbox;
extern volatile usb_response_mailbox_t g_response_mailbox;

// Core1 (USB) functions
bool mailbox_send_command(const char* command);
bool mailbox_get_response(char* buffer, int buffer_size);
void mailbox_mark_response_sent(void);

// Core0 (Main) functions  
bool mailbox_get_command(char* buffer, int buffer_size);
void mailbox_mark_command_processed(void);
bool mailbox_send_response(const char* response);

// Initialize mailbox system
void mailbox_init(void);

#ifdef __cplusplus
}
#endif

#endif // MAILBOX_H
