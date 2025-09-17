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
#include "crow_slopes.h"
#include "crow_asl.h"
#include "crow_casl.h"

// Include wrDsp and wrLib for vector processing and event handling
extern "C" {
#include "wrBlocks.h"
#include "wrEvent.h"
}

// Block processing constants
#define CROW_BLOCK_SIZE 32

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
    
    // Script upload state (Phase 2.2)
    static constexpr size_t MAX_SCRIPT_SIZE = 8192; // 8KB max script size
    bool script_upload_mode;
    // Fixed-size script upload buffer (eliminate dynamic allocation)
    char script_upload_buffer[MAX_SCRIPT_SIZE];
    // Staging buffer for loading scripts from filesystem or flash (replaces dynamic allocations)
    char script_staging_buffer[MAX_SCRIPT_SIZE];
    size_t script_upload_size;
    size_t script_upload_pos;
    
    // Phase 5: Status LED management using ComputerCard methods
    uint32_t status_led_counter;    // Counter for status LED patterns
    uint32_t error_led_counter;     // Counter for error LED flashing
    
    // Block processing buffers for vector operations
    float input_block[4][CROW_BLOCK_SIZE];    // 4 input channels
    float output_block[4][CROW_BLOCK_SIZE];   // 4 output channels
    int block_position;                        // Current position in block (0-31)
    
    // Per-output scale / quantization configuration (only outputs 3 & 4 used)
    struct CrowScale {
        bool    enabled;
        uint8_t mod;          // divisions per octave (e.g. 12)
        float   scaling;      // volts per octave (default 1.0)
        uint8_t count;        // number of degrees
        float   degrees[16];  // degree indices (0..mod-1)
        uint8_t last_midi;
        bool    last_midi_valid;
    };
    CrowScale scale_cfg[4]; // indices 2 & 3 (logical outs 3 & 4) are quantized

    // Output clock / gate generation state
    struct OutputClock {
        bool  enabled;
        float period_s;
        float width_s;
        float phase_s;
        bool  saved_quant_enabled; // prior quantization enabled state for channels 2/3
    };
    OutputClock output_clock[4];
    
    // Direct output voltage storage for immediate hardware control
    float direct_output_volts[4];
    bool direct_output_active[4];
    
public:
    // Static pointer for multicore callback (public for error handling bridge)
    static CrowEmulator* instance;

    // Output full-scale voltage definitions (all outputs currently ±6V)
    static constexpr float CROW_AUDIO_FULLSCALE_VOLTS = 6.0f;
    static constexpr float CROW_CV_FULLSCALE_VOLTS    = 6.0f;
    static constexpr float CROW_FULLSCALE_VOLTS       = 6.0f; // unified alias

    CrowEmulator();
    
    // Audio processing methods (overrides ComputerCard::ProcessSample)
    void ProcessSample() override;
    void ProcessBlock();  // Vector processing method for CROW_BLOCK_SIZE samples
    
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
    
    // Script upload management (Phase 2.2)
    void start_script_upload();
    void end_script_upload(); // legacy volatile finalize wrapper
    void finalize_script_upload(bool persist); // new unified finalize for ^^e (persist=false) and ^^w (persist=true)
    bool process_script_upload_data(const char* data, size_t length);
    
    // Flash command implementations (Phase 7d: Flash Command Integration)
    void handle_print_command();
    void handle_flash_upload_command();
    void handle_flash_clear_command();
    void handle_load_first_command();
    void load_flash_script_at_boot();
    
    // Hardware interface methods (access ComputerCard protected members)
    float computercard_to_crow_volts(int16_t cc_value);
    int16_t crow_to_computercard_value(float crow_volts);
    float crow_get_input(int channel);
    void crow_set_output(int channel, float volts);
    void set_hardware_output(int channel, float volts);  // Internal hardware setting method
    void clear_direct_output(int channel);  // Clear direct output control for slopes
    // Scale / quantization control
    void disable_output_scale(int channel);
    void set_output_scale(int channel, const float* degrees, int count, int mod, float scaling);
    bool output_scale_enabled(int channel) const { return (channel>=0 && channel<4) ? scale_cfg[channel].enabled : false; }
    void crow_hardware_update();

    // Clock mode control
    void set_output_clock(int channel, float period_s, float width_s);
    void clear_output_clock(int channel);
    
    // Phase 5: Status LED and error handling methods
    void update_status_leds();
    void send_error_message(const char* error_msg);
    
    // Main run method that handles ComputerCard initialization
    void RunCrowEmulator();
    
    // ComputerCard unique ID access for lua
    uint64_t get_unique_card_id();
    
    // First.lua loading
    void load_default_first_lua();
};

// Global extern declaration for lua access
extern CrowEmulator* g_crow_emulator;

// C-compatible entry point for core1 (used by flash operations)
extern "C" void CrowEmulator_core1_entry();
