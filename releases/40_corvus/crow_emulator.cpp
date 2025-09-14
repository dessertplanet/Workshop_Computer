#include "tusb.h"
#include "pico/multicore.h"
#include <cstdarg>
#include <cstring>

// Include our header (ComputerCard.h is included via computer_card_impl.cpp)
#include "crow_emulator.h"
#include "crow_metro.h"

// Static instance pointer for multicore callback
CrowEmulator* CrowEmulator::instance = nullptr;

// Global ComputerCard pointer for hardware abstraction
ComputerCard* g_computer_card = nullptr;

CrowEmulator::CrowEmulator() : 
    rx_buffer_pos(0),
    multicore_ready(false),
    usb_connected(false),
    multiline_mode(false),
    script_upload_mode(false),
    script_upload_buffer(nullptr),
    script_upload_size(0),
    script_upload_pos(0)
{
    instance = this;
    memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
    
    // Allocate script upload buffer
    script_upload_buffer = new char[MAX_SCRIPT_SIZE];
    if (!script_upload_buffer) {
        printf("Failed to allocate script upload buffer\n");
    }
    
    // Set global pointer for hardware abstraction
    g_computer_card = this;
    
    // Initialize crow emulation
    crow_init();
}

void CrowEmulator::ProcessSample()
{
    // 48kHz audio processing callback
    
    // Phase 2.4: Process metro events (real-time event integration)
    metro_process_events();
    
    // Phase 2.1: Basic lua processing on Core 0
    crow_lua_process_events();
    
    // Phase 3: Update hardware abstraction layer
    crow_hardware_update();
    
    // Update input values for lua using hardware abstraction
    if (g_crow_lua) {
        g_crow_lua->set_input_volts(1, crow_get_input(0));  // Convert to 0-based indexing
        g_crow_lua->set_input_volts(2, crow_get_input(1));
    }
    
    // Get lua output values and apply them using hardware abstraction
    for (int i = 1; i <= 4; i++) {
        float volts;
        bool volts_new, trigger;
        if (g_crow_lua && g_crow_lua->get_output_volts_and_trigger(i, &volts, &volts_new, &trigger)) {
            if (volts_new) {
                crow_set_output(i - 1, volts);  // Convert to 0-based indexing
            }
        }
    }
    
    // For now, also pass through audio inputs to outputs for testing
    AudioOut1(AudioIn1());
    AudioOut2(AudioIn2());
    
    // Sign of life LED
    LedBrightness(0, 4095); // LED 0 full brightness
}

void CrowEmulator::crow_init()
{
    printf("Initializing Crow Emulator...\n");
    
    // Initialize lua system on Core 0
    if (!crow_lua_init()) {
        printf("Failed to initialize Lua system\n");
        return;
    }
    
    // Initialize metro system
    metro_init();
    
    // Initialize USB communication
    init_usb_communication();
    
    // Start second core for USB processing
    multicore_launch_core1(core1_entry);
    
    // Send hello message
    sleep_ms(100); // Give USB time to enumerate
    crow_send_hello();
}

void CrowEmulator::init_usb_communication()
{
    // TinyUSB device is initialized by tusb_init() in main
    // Just reset our buffer state
    rx_buffer_pos = 0;
    multiline_mode = false;
}

void CrowEmulator::send_usb_string(const char* str)
{
    if (tud_cdc_connected()) {
        tud_cdc_write_str(str);
        tud_cdc_write_str("\n\r");
        tud_cdc_write_flush();
    }
}

void CrowEmulator::send_usb_printf(const char* format, ...)
{
    if (tud_cdc_connected()) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        tud_cdc_write_str(buffer);
        tud_cdc_write_str("\n\r");
        tud_cdc_write_flush();
    }
}

void CrowEmulator::crow_send_hello()
{
    send_usb_string("hi from crow!");
    send_usb_string("");  // Send empty line like real crow
}

void CrowEmulator::crow_print_version()
{
    send_usb_string("crow workshop emulator v0.1.0");
    send_usb_string("build: workshop-computer");
}

void CrowEmulator::crow_print_identity()
{
    send_usb_string("monome crow");
    send_usb_string("workshop computer emulation");
}

C_cmd_t CrowEmulator::parse_command(const char* buffer, int length)
{
    // Look for crow system commands (^^x format)
    for (int i = 0; i < length - 2; i++) {
        if (buffer[i] == '^' && buffer[i + 1] == '^') {
            char cmd = buffer[i + 2];
            switch (cmd) {
                case 'b': return C_boot;
                case 's': return C_startupload;
                case 'e': return C_endupload;
                case 'w': return C_flashupload;
                case 'r': return C_restart;
                case 'p': return C_print;
                case 'v': return C_version;
                case 'i': return C_identity;
                case 'k': return C_killlua;
                case 'c': return C_flashclear;
                case 'f':
                case 'F': return C_loadFirst;
                default: break;
            }
        }
    }
    return C_none;
}

bool CrowEmulator::is_multiline_marker(const char* buffer, int length)
{
    // Check for ``` (three backticks)
    if (length >= 3) {
        return (buffer[0] == '`' && buffer[1] == '`' && buffer[2] == '`');
    }
    return false;
}

bool CrowEmulator::is_packet_complete(const char* buffer, int length)
{
    if (length == 0) return false;
    char last_char = buffer[length - 1];
    return (last_char == '\0' || last_char == '\n' || last_char == '\r');
}

void CrowEmulator::handle_command(C_cmd_t cmd)
{
    switch (cmd) {
        case C_version:
            crow_print_version();
            break;
        case C_identity:
            crow_print_identity();
            break;
        case C_boot:
            send_usb_string("bootloader not implemented");
            break;
        case C_restart:
            send_usb_string("restarting...");
            // TODO: Implement restart
            break;
        case C_print:
            send_usb_string("no script loaded");
            break;
        case C_killlua:
            send_usb_string("lua killed");
            // TODO: Phase 2 - Kill lua interpreter
            break;
        case C_startupload:
            start_script_upload();
            break;
        case C_endupload:
            end_script_upload();
            break;
        case C_flashupload:
            send_usb_string("flash upload not implemented yet");
            break;
        case C_repl:
            // Handle lua REPL command
            if (g_crow_lua && g_crow_lua->eval_script(rx_buffer, rx_buffer_pos, "repl")) {
                // Lua command executed successfully - response already sent via printf
            } else {
                send_usb_string("lua error");
            }
            break;
        default:
            // Unknown command, ignore
            break;
    }
}

void CrowEmulator::process_usb_data()
{
    if (!tud_cdc_available()) return;
    
    char temp_buffer[64];
    uint32_t count = tud_cdc_read(temp_buffer, sizeof(temp_buffer) - 1);
    
    // If in script upload mode, handle differently
    if (script_upload_mode) {
        // Check for end upload command in the data first
        for (uint32_t i = 0; i < count - 2; i++) {
            if (temp_buffer[i] == '^' && temp_buffer[i + 1] == '^' && temp_buffer[i + 2] == 'e') {
                // Found ^^e - end upload command
                // Process any data before the command
                if (i > 0) {
                    process_script_upload_data(temp_buffer, i);
                }
                end_script_upload();
                return;
            }
        }
        
        // No end command found, add all data to script buffer
        process_script_upload_data(temp_buffer, count);
        return;
    }
    
    // Normal command processing mode
    for (uint32_t i = 0; i < count; i++) {
        char ch = temp_buffer[i];
        
        // Handle escape key - clear buffer
        if (ch == '\e') {
            rx_buffer_pos = 0;
            continue;
        }
        
        // Handle backspace
        if (ch == '\b' && rx_buffer_pos > 0) {
            rx_buffer_pos--;
            continue;
        }
        
        // Add character to buffer (with overflow protection)
        if (rx_buffer_pos < USB_RX_BUFFER_SIZE - 1) {
            rx_buffer[rx_buffer_pos++] = ch;
        } else {
            // Buffer overflow - reset and send error
            rx_buffer_pos = 0;
            send_usb_string("!chunk too long!");
            continue;
        }
        
        // Check for multiline markers
        if (is_multiline_marker(rx_buffer, rx_buffer_pos)) {
            multiline_mode = !multiline_mode;
            if (!multiline_mode) {
                // End of multiline - process as REPL command
                handle_command(C_repl);
                rx_buffer_pos = 0;
            }
            continue;
        }
        
        // Check for command completion
        if (!multiline_mode && is_packet_complete(rx_buffer, rx_buffer_pos)) {
            // Null terminate
            rx_buffer[rx_buffer_pos] = '\0';
            
            // Check for system command
            C_cmd_t cmd = parse_command(rx_buffer, rx_buffer_pos);
            if (cmd != C_none) {
                handle_command(cmd);
            } else {
                // Regular REPL command
                handle_command(C_repl);
            }
            
            // Reset buffer
            rx_buffer_pos = 0;
        }
    }
}

// Static entry point for core 1
void CrowEmulator::core1_entry()
{
    if (instance) {
        instance->core1_main();
    }
}

void CrowEmulator::core1_main()
{
    multicore_ready = true;
    
    printf("Core 1: USB processing started\n");
    
    while (true) {
        // Process incoming USB data
        process_usb_data();
        
        // Check USB connection status
        bool connected = tud_cdc_connected();
        if (connected != usb_connected) {
            usb_connected = connected;
            if (connected) {
                printf("USB connected\n");
                crow_send_hello();
            } else {
                printf("USB disconnected\n");
            }
        }
        
        // Small delay to prevent busy waiting
        sleep_ms(1);
    }
}


// Script upload management (Phase 2.2)
void CrowEmulator::start_script_upload()
{
    if (!script_upload_buffer) {
        send_usb_string("!script upload buffer not available");
        return;
    }
    
    script_upload_mode = true;
    script_upload_pos = 0;
    script_upload_size = 0;
    
    send_usb_string("script upload started");
    printf("Script upload started\n");
}

void CrowEmulator::end_script_upload()
{
    if (!script_upload_mode) {
        send_usb_string("!no upload in progress");
        return;
    }
    
    script_upload_mode = false;
    
    if (script_upload_pos > 0) {
        // Null terminate the script
        script_upload_buffer[script_upload_pos] = '\0';
        
        printf("Script upload complete, %zu bytes received\n", script_upload_pos);
        
        // Send script to lua system for compilation and execution
        if (g_crow_lua && g_crow_lua->load_user_script(script_upload_buffer)) {
            send_usb_string("script loaded successfully");
            // Call init() function after successful script load (crow behavior)
            if (g_crow_lua->call_init()) {
                printf("init() called successfully\n");
            }
        } else {
            send_usb_string("!script compilation error");
        }
    } else {
        send_usb_string("!empty script");
    }
    
    // Reset upload state
    script_upload_pos = 0;
    script_upload_size = 0;
}

bool CrowEmulator::process_script_upload_data(const char* data, size_t length)
{
    if (!script_upload_mode || !script_upload_buffer) {
        return false;
    }
    
    // Check if we have space for this data
    if (script_upload_pos + length >= MAX_SCRIPT_SIZE) {
        send_usb_string("!script too large");
        script_upload_mode = false;
        return false;
    }
    
    // Copy data to upload buffer
    memcpy(script_upload_buffer + script_upload_pos, data, length);
    script_upload_pos += length;
    
    return true;
}

void CrowEmulator::RunCrowEmulator()
{
    // This method wraps ComputerCard::Run() to avoid exposing it in main.cpp
    // ComputerCard::Run() calls AudioWorker() which sets up audio processing
    // and calls ProcessSample() at 48kHz in an infinite loop
    Run();
}

// Hardware interface implementations (Phase 3: Hardware Abstraction Layer)
// These methods can access protected ComputerCard members since CrowEmulator inherits from ComputerCard

float CrowEmulator::computercard_to_crow_volts(int16_t cc_value)
{
    // ComputerCard uses ±2048 range for ±6V
    static constexpr float CC_RANGE_VOLTS = 6.0f;      // ±6V range
    static constexpr float CC_RANGE_VALUES = 4096.0f;  // ±2048 = 4096 total range
    static constexpr float VOLTS_PER_VALUE = CC_RANGE_VOLTS / CC_RANGE_VALUES;
    
    return cc_value * VOLTS_PER_VALUE;
}

int16_t CrowEmulator::crow_to_computercard_value(float crow_volts)
{
    // ComputerCard uses ±2048 range for ±6V
    static constexpr float CC_RANGE_VOLTS = 6.0f;      // ±6V range
    static constexpr float CC_RANGE_VALUES = 4096.0f;  // ±2048 = 4096 total range
    static constexpr float VALUES_PER_VOLT = CC_RANGE_VALUES / CC_RANGE_VOLTS;
    
    // Clamp to valid range
    if (crow_volts > 6.0f) crow_volts = 6.0f;
    if (crow_volts < -6.0f) crow_volts = -6.0f;
    
    int16_t result = (int16_t)(crow_volts * VALUES_PER_VOLT);
    
    // Clamp to ±2047 (12-bit signed range)
    if (result > 2047) result = 2047;
    if (result < -2048) result = -2048;
    
    return result;
}

float CrowEmulator::crow_get_input(int channel)
{
    // Map crow input channels to ComputerCard audio inputs
    // channel 0 → AudioIn1, channel 1 → AudioIn2
    int16_t cc_value;
    switch (channel) {
        case 0: cc_value = AudioIn1(); break;
        case 1: cc_value = AudioIn2(); break;
        default: return 0.0f; // Invalid channel
    }
    
    return computercard_to_crow_volts(cc_value);
}

void CrowEmulator::crow_set_output(int channel, float volts)
{
    // Map crow output channels to ComputerCard outputs
    // channel 0 → AudioOut1, channel 1 → AudioOut2
    // channel 2 → CVOut1, channel 3 → CVOut2
    int16_t cc_value = crow_to_computercard_value(volts);
    
    switch (channel) {
        case 0: AudioOut1(cc_value); break;
        case 1: AudioOut2(cc_value); break;
        case 2: CVOut1(cc_value); break;
        case 3: CVOut2(cc_value); break;
        default: break; // Invalid channel
    }
}

void CrowEmulator::crow_hardware_update()
{
    // This is called from ProcessSample() at 48kHz
    // For now, no additional processing needed beyond the direct I/O mapping
    // Future enhancements could include:
    // - Input change detection
    // - Output ramping/smoothing
    // - Hardware calibration
}
