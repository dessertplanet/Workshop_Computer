#include "tusb.h"
#include "pico/multicore.h"
#include <cstdarg>
#include <cstring>

// Include our header (ComputerCard.h is included via computer_card_impl.cpp)
#include "crow_emulator.h"

// Static instance pointer for multicore callback
CrowEmulator* CrowEmulator::instance = nullptr;

CrowEmulator::CrowEmulator() : 
    rx_buffer_pos(0),
    multicore_ready(false),
    usb_connected(false),
    multiline_mode(false)
{
    instance = this;
    memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
    
    // Initialize crow emulation
    crow_init();
}

void CrowEmulator::ProcessSample()
{
    // 48kHz audio processing callback
    
    // Phase 2.1: Basic lua processing on Core 0
    crow_lua_process_events();
    
    // Update input values for lua
    if (g_crow_lua) {
        g_crow_lua->set_input_volts(1, crow_get_input(1));
        g_crow_lua->set_input_volts(2, crow_get_input(2));
    }
    
    // Get lua output values and apply them
    for (int i = 1; i <= 4; i++) {
        float volts;
        bool volts_new, trigger;
        if (g_crow_lua && g_crow_lua->get_output_volts_and_trigger(i, &volts, &volts_new, &trigger)) {
            if (volts_new) {
                crow_set_output(i, volts);
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
        case C_repl:
            // Handle lua REPL command
            if (g_crow_lua && crow_lua_eval_repl(rx_buffer, rx_buffer_pos)) {
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

// Hardware abstraction functions (Phase 3 placeholders)
void CrowEmulator::crow_set_output(int channel, float volts)
{
    // TODO: Phase 3 - Implement hardware mapping
    // For now, just clamp and route to appropriate output
    volts = (volts < -5.0f) ? -5.0f : ((volts > 10.0f) ? 10.0f : volts);
    
    switch (channel) {
        case 1:
        case 2:
            // Audio outputs (convert volts to audio samples)
            AudioOut(channel - 1, (int16_t)(volts * 204.7f)); // rough conversion
            break;
        case 3:
        case 4:
            // CV outputs
            CVOut(channel - 3, (int16_t)(volts * 204.7f)); // rough conversion
            break;
        default:
            break;
    }
}

float CrowEmulator::crow_get_input(int channel)
{
    // TODO: Phase 3 - Implement hardware mapping
    switch (channel) {
        case 1:
            return AudioIn1() / 204.7f; // rough conversion
        case 2:
            return AudioIn2() / 204.7f; // rough conversion
        default:
            return 0.0f;
    }
}

void CrowEmulator::RunCrowEmulator()
{
    // This method wraps ComputerCard::Run() to avoid exposing it in main.cpp
    // ComputerCard::Run() calls AudioWorker() which sets up audio processing
    // and calls ProcessSample() at 48kHz in an infinite loop
    Run();
}
