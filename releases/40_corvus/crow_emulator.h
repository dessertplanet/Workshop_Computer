#pragma once

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

// Define this before including ComputerCard.h to prevent implementation inclusion
#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#undef COMPUTERCARD_NOIMPL

#include "crow_lua.h"

// Crow command types (from crow's caw.h)
typedef enum {
    C_none = 0,
    C_repl,
    C_boot,
    C_startupload,
    C_endupload,
    C_flashupload,
    C_restart,
    C_print,
    C_version,
    C_identity,
    C_killlua,
    C_flashclear,
    C_loadFirst
} C_cmd_t;

class CrowEmulator : public ComputerCard
{
private:
    // USB communication buffer
    static const int USB_RX_BUFFER_SIZE = 2048;
    char rx_buffer[USB_RX_BUFFER_SIZE];
    int rx_buffer_pos;
    
    // Communication state
    volatile bool multicore_ready;
    volatile bool usb_connected;
    
    // Crow state variables
    bool multiline_mode;
    
    // Static pointer for multicore callback
    static CrowEmulator* instance;

public:
    CrowEmulator();
    
    // Audio processing method (overrides ComputerCard::ProcessSample)
    void ProcessSample() override;
    
    // USB Communication
    void init_usb_communication();
    void process_usb_data();
    void send_usb_string(const char* str);
    void send_usb_printf(const char* format, ...);
    
    // Crow command processing
    C_cmd_t parse_command(const char* buffer, int length);
    void handle_command(C_cmd_t cmd);
    bool is_multiline_marker(const char* buffer, int length);
    bool is_packet_complete(const char* buffer, int length);
    
    // Core 1 processing (USB/Serial)
    static void core1_entry();
    void core1_main();
    
    // Crow emulation functions
    void crow_init();
    void crow_send_hello();
    void crow_print_version();
    void crow_print_identity();
    
    // Hardware abstraction (Phase 3)
    void crow_set_output(int channel, float volts);
    float crow_get_input(int channel);
    
    // Main run method that handles ComputerCard initialization
    void RunCrowEmulator();
};
