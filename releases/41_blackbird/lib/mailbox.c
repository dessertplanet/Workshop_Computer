#include "mailbox.h"

// Global mailbox instances
volatile usb_command_mailbox_t g_command_mailbox = {0};
volatile usb_response_mailbox_t g_response_mailbox = {0};

void mailbox_init(void) {
    // Clear mailboxes
    memset((void*)&g_command_mailbox, 0, sizeof(g_command_mailbox));
    memset((void*)&g_response_mailbox, 0, sizeof(g_response_mailbox));
}

// Core1 (USB) functions

bool mailbox_send_command(const char* command) {
    // Check if previous command was processed
    if (g_command_mailbox.ready && !g_command_mailbox.processed) {
        return false; // Mailbox busy
    }
    
    // Send new command
    strncpy((char*)g_command_mailbox.command, command, sizeof(g_command_mailbox.command) - 1);
    g_command_mailbox.command[sizeof(g_command_mailbox.command) - 1] = '\0';
    g_command_mailbox.processed = false;
    g_command_mailbox.ready = true; // Set ready last for memory ordering
    
    return true;
}

bool mailbox_get_response(char* buffer, int buffer_size) {
    if (!g_response_mailbox.ready) {
        return false; // No response available
    }
    
    strncpy(buffer, (const char*)g_response_mailbox.response, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    
    return true;
}

void mailbox_mark_response_sent(void) {
    g_response_mailbox.sent = true;
    g_response_mailbox.ready = false; // Clear ready flag
}

// Core0 (Main) functions

bool mailbox_get_command(char* buffer, int buffer_size) {
    if (!g_command_mailbox.ready) {
        return false; // No command available
    }
    
    strncpy(buffer, (const char*)g_command_mailbox.command, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    
    return true;
}

void mailbox_mark_command_processed(void) {
    g_command_mailbox.processed = true;
    g_command_mailbox.ready = false; // Clear ready flag
}

bool mailbox_send_response(const char* response) {
    // Check if previous response was sent
    if (g_response_mailbox.ready && !g_response_mailbox.sent) {
        return false; // Response mailbox busy
    }
    
    // Send new response
    strncpy((char*)g_response_mailbox.response, response, sizeof(g_response_mailbox.response) - 1);
    g_response_mailbox.response[sizeof(g_response_mailbox.response) - 1] = '\0';
    g_response_mailbox.sent = false;
    g_response_mailbox.ready = true; // Set ready last for memory ordering
    
    return true;
}
