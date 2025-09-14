#include "tusb.h"
#include "pico/multicore.h"
#include <cstdarg>
#include <cstring>
#include <cmath>

// Include our header (ComputerCard.h is included via computer_card_impl.cpp)
#include "crow_emulator.h"
#include "crow_metro.h"
#include "crow_detect.h"
#include "crow_error.h"
#include "crow_flash.h"
#include "crow_events.h"
#include "crow_multicore.h"

// Static instance pointer for multicore callback
CrowEmulator* CrowEmulator::instance = nullptr;

// Global ComputerCard pointer for hardware abstraction
ComputerCard* g_computer_card = nullptr;

// Global CrowEmulator pointer for lua access
CrowEmulator* g_crow_emulator = nullptr;

CrowEmulator::CrowEmulator() : 
    rx_buffer_pos(0),
    multicore_ready(false),
    usb_connected(false),
    multiline_mode(false),
    script_upload_mode(false),
    script_upload_buffer(nullptr),
    script_upload_size(0),
    script_upload_pos(0),
    status_led_counter(0),
    error_led_counter(0),
    block_position(0)
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
    
    // Set global pointer for lua access
    g_crow_emulator = this;
    
    // Initialize error handling system
    crow_error_init();
    
    // Initialize crow emulation
    crow_init();
}

void CrowEmulator::ProcessSample()
{
    // Accumulate input samples into block buffers
    input_block[0][block_position] = crow_get_input(0);  // Input 1
    input_block[1][block_position] = crow_get_input(1);  // Input 2
    input_block[2][block_position] = 0.0f;               // Input 3 (not connected)
    input_block[3][block_position] = 0.0f;               // Input 4 (not connected)
    
    // Increment block position
    block_position++;
    
    // When block is full, process it with vector operations
    if (block_position >= CROW_BLOCK_SIZE) {
        ProcessBlock();
        block_position = 0;
    }
    
    // Output the current sample from the output block
    int output_index = (block_position == 0) ? CROW_BLOCK_SIZE - 1 : block_position - 1;
    
    // Apply outputs using hardware abstraction
    crow_set_output(0, output_block[0][output_index]);  // Output 1 → AudioOut1
    crow_set_output(1, output_block[1][output_index]);  // Output 2 → AudioOut2
    crow_set_output(2, output_block[2][output_index]);  // Output 3 → CVOut1
    crow_set_output(3, output_block[3][output_index]);  // Output 4 → CVOut2
    
    // For now, also pass through audio inputs to outputs for testing
    AudioOut1(AudioIn1());
    AudioOut2(AudioIn2());
    
    // Phase 5: Simple status LED management using ComputerCard methods
    update_status_leds();
}

void CrowEmulator::ProcessBlock()
{
    // Multicore Block Processing - Core 0 handles real-time audio
    
    // Create input pointers array for wrDsp compatibility
    float* input_blocks[4];
    for (int ch = 0; ch < 4; ch++) {
        input_blocks[ch] = input_block[ch];
    }
    
    // Notify Core 1 of block start and send input values
    crow_multicore_core0_block_start(input_blocks);
    
    // Priority Event Queue Processing (like real crow)
    // Process all pending events in proper order
    crow_events_process_all();
    
    // Phase 4.1: Process slopes system with vector operations (stays on Core 0)
    // Vector processing implementation using wrDsp functions
    float* slopes_output_blocks[4];
    for (int ch = 0; ch < 4; ch++) {
        slopes_output_blocks[ch] = new float[CROW_BLOCK_SIZE];
    }
    
    crow_slopes_process_block(input_blocks, slopes_output_blocks, CROW_BLOCK_SIZE);
    
    // Copy slopes output to main output blocks (will be combined with lua outputs later)
    for (int ch = 0; ch < 4; ch++) {
        memcpy(output_block[ch], slopes_output_blocks[ch], CROW_BLOCK_SIZE * sizeof(float));
        delete[] slopes_output_blocks[ch];
    }
    
    // Phase 4.3.2: Process detection system with vector operations (stays on Core 0)
    crow_detect_process_block(input_blocks, CROW_BLOCK_SIZE);
    
    // Phase 3: Update hardware abstraction layer
    crow_hardware_update();
    
    // Get Lua output values from Core 1 via multicore communication
    for (int ch = 0; ch < 4; ch++) {
        float volts;
        bool volts_new, trigger;
        if (crow_multicore_get_lua_output(ch, &volts, &volts_new, &trigger)) {
            if (volts_new) {
                // Fill entire block with the new voltage value
                for (int i = 0; i < CROW_BLOCK_SIZE; i++) {
                    output_block[ch][i] = volts;
                }
            }
        } else {
            // No new value, keep slopes output
            // output_block already contains slopes output from above
        }
    }
    
    // Complete block processing
    crow_multicore_core0_block_complete();
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
    
    // Initialize slopes system (Phase 4.1)
    crow_slopes_init();
    
    // Initialize ASL system (Phase 4.2)
    crow_asl_init();
    
    // Initialize CASL system (Phase 4.3.1)
    crow_casl_init();
    
    // Initialize detection system (Phase 4.3.2)
    crow_detect_init(CROW_DETECT_CHANNELS);
    
    // Initialize event system (Priority Queue)
    crow_events_init();
    
    // Initialize multicore communication
    crow_multicore_init();
    
    // Initialize USB communication
    init_usb_communication();
    
    // Start second core for background processing (USB + ASL + CASL + Lua)
    multicore_launch_core1(core1_entry);
    
    // Send hello message
    sleep_ms(100); // Give USB time to enumerate
    crow_send_hello();
    
    // Phase 7e: Load script from flash at boot time (after USB is ready)
    load_flash_script_at_boot();
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
            handle_print_command();
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
            handle_flash_upload_command();
            break;
        case C_flashclear:
            handle_flash_clear_command();
            break;
        case C_loadFirst:
            handle_load_first_command();
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
    
    printf("Core 1: Background processing started (USB + ASL + CASL + Lua)\n");
    
    while (true) {
        // Process multicore communication and run background tasks
        crow_multicore_core1_process_block();
        
        // Process Lua events and periodic tasks (moved to Core 1)
        if (g_crow_lua) {
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            g_crow_lua->process_periodic_tasks(current_time);
            
            // Call step function periodically
            g_crow_lua->call_step();
            
            // Update Lua output values in shared memory
            for (int ch = 0; ch < 4; ch++) {
                float volts;
                bool volts_new, trigger;
                if (g_crow_lua->get_output_volts_and_trigger(ch + 1, &volts, &volts_new, &trigger)) {
                    crow_multicore_set_lua_output(ch, volts, volts_new, trigger);
                }
            }
            
            // Update Lua input values from shared memory
            for (int ch = 0; ch < 2; ch++) {
                float input_value;
                if (crow_multicore_get_input_value(ch, &input_value)) {
                    g_crow_lua->set_input_volts(ch + 1, input_value);
                }
            }
        }
        
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
        
        // Small delay to prevent busy waiting (reduced for better responsiveness)
        sleep_us(100); // 100 microseconds instead of 1 millisecond
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

// Phase 5: Simple status LED management using ComputerCard methods
void CrowEmulator::update_status_leds()
{
    status_led_counter++;
    
    // Status LED (LED 0): Normal operation heartbeat
    // Slow breathing pattern every 2 seconds (96,000 samples at 48kHz)
    uint32_t heartbeat_period = 96000;
    float phase = (float)(status_led_counter % heartbeat_period) / heartbeat_period;
    float breath = (sin(phase * 2.0f * M_PI) + 1.0f) * 0.5f; // 0.0 to 1.0
    uint16_t heartbeat_brightness = (uint16_t)(1024 * breath); // Max 25% brightness
    
    // Error LED (LED 1): Flash on errors
    uint16_t error_brightness = 0;
    if (crow_error_has_error()) {
        error_led_counter++;
        // Fast flash every 0.25 seconds (12,000 samples at 48kHz)
        uint32_t error_period = 12000;
        bool error_on = (error_led_counter % error_period) < (error_period / 2);
        error_brightness = error_on ? 4095 : 0; // Full brightness flash
    } else {
        error_led_counter = 0;
    }
    
    // USB connection LED (LED 2): Shows USB connection status
    uint16_t usb_brightness = usb_connected ? 2048 : 0; // 50% brightness when connected
    
    // Script status LED (LED 3): Shows script activity
    uint16_t script_brightness = 0;
    if (script_upload_mode) {
        // Fast blink during script upload
        uint32_t upload_period = 4800; // 0.1 seconds at 48kHz
        bool upload_on = (status_led_counter % upload_period) < (upload_period / 2);
        script_brightness = upload_on ? 3072 : 0; // 75% brightness flash
    } else {
        // Dim glow when not uploading (indicates ready for scripts)
        script_brightness = 256; // 6.25% brightness
    }
    
    // Apply LED settings using ComputerCard methods
    LedBrightness(0, heartbeat_brightness);
    LedBrightness(1, error_brightness);
    LedBrightness(2, usb_brightness);
    LedBrightness(3, script_brightness);
    
    // LEDs 4 and 5 can be used for additional status or user control
    LedBrightness(4, 0);
    LedBrightness(5, 0);
}

// Enhanced error handling with USB integration
void CrowEmulator::send_error_message(const char* error_msg)
{
    if (error_msg && tud_cdc_connected()) {
        tud_cdc_write_str("!");
        tud_cdc_write_str(error_msg);
        tud_cdc_write_str("\n\r");
        tud_cdc_write_flush();
    }
    
    // Also trigger error LED
    error_led_counter = 0; // Reset counter to start flashing immediately
}

// Flash command implementations (Phase 7d: Flash Command Integration)
void CrowEmulator::handle_print_command()
{
    USERSCRIPT_t flash_status = Flash_which_user_script();
    
    switch (flash_status) {
        case USERSCRIPT_User:
            {
                char* script_addr = Flash_read_user_scriptaddr();
                uint16_t script_len = Flash_read_user_scriptlen();
                
                if (script_addr && script_len > 0) {
                    // Send script content directly (no extra newlines)
                    if (tud_cdc_connected()) {
                        tud_cdc_write(script_addr, script_len);
                        tud_cdc_write_flush();
                    }
                } else {
                    send_usb_string("!flash read error");
                }
            }
            break;
        case USERSCRIPT_Clear:
            send_usb_string("-- script cleared --");
            break;
        case USERSCRIPT_Default:
        default:
            send_usb_string("-- no script --");
            break;
    }
}

void CrowEmulator::handle_flash_upload_command()
{
    if (!script_upload_buffer || script_upload_pos == 0) {
        send_usb_string("!no script to upload to flash");
        return;
    }
    
    // Write current script buffer to flash
    uint8_t result = Flash_write_user_script(script_upload_buffer, script_upload_pos);
    
    if (result == 0) {
        send_usb_string("script saved to flash");
        printf("Script saved to flash: %zu bytes\n", script_upload_pos);
    } else {
        send_usb_string("!flash write error");
        printf("Flash write failed with error %d\n", result);
    }
}

void CrowEmulator::handle_flash_clear_command()
{
    Flash_clear_user_script();
    send_usb_string("flash cleared");
    printf("Flash script cleared\n");
}

void CrowEmulator::handle_load_first_command()
{
    // Load default First.lua script (like real crow)
    if (g_crow_lua) {
        send_usb_string("loading first.lua...");
        printf("Loading first.lua\n");
        
        // Try to load First.lua from flash first, if not available load default
        if (Flash_first_exists()) {
            // Load First.lua from flash
            char script_buffer[USER_SCRIPT_SIZE + 1];
            if (Flash_read_first_script(script_buffer) == 0) {
                if (g_crow_lua->load_user_script(script_buffer)) {
                    send_usb_string("first.lua loaded from flash");
                    g_crow_lua->call_init();
                    return;
                }
            }
        }
        
        // Load default First.lua (embedded or from file system)
        load_default_first_lua();
    } else {
        send_usb_string("!lua system not initialized");
    }
}

// Load default First.lua and store it in flash
void CrowEmulator::load_default_first_lua()
{
    // Try to read First.lua from file system first (for development)
    FILE* first_file = fopen("First.lua", "r");
    if (first_file) {
        // Get file size
        fseek(first_file, 0, SEEK_END);
        long file_size = ftell(first_file);
        fseek(first_file, 0, SEEK_SET);
        
        if (file_size > 0 && file_size < USER_SCRIPT_SIZE) {
            char* script_buffer = new char[file_size + 1];
            if (script_buffer && fread(script_buffer, 1, file_size, first_file) == file_size) {
                script_buffer[file_size] = '\0';
                
                // Load script into lua
                if (g_crow_lua->load_user_script(script_buffer)) {
                    send_usb_string("first.lua loaded successfully");
                    g_crow_lua->call_init();
                    
                    // Store First.lua in flash for future use
                    Flash_write_first_script(script_buffer, file_size);
                    printf("First.lua stored in flash (%ld bytes)\n", file_size);
                } else {
                    send_usb_string("!first.lua compilation error");
                }
                
                delete[] script_buffer;
            }
        }
        fclose(first_file);
    } else {
        send_usb_string("!first.lua not found");
        printf("Could not open First.lua file\n");
    }
}

// Boot-time flash script loading (Phase 7e: Boot-time Script Loading)
void CrowEmulator::load_flash_script_at_boot()
{
    USERSCRIPT_t flash_status = Flash_which_user_script();
    
    if (flash_status == USERSCRIPT_User && g_crow_lua) {
        char* script_addr = Flash_read_user_scriptaddr();
        uint16_t script_len = Flash_read_user_scriptlen();
        
        if (script_addr && script_len > 0) {
            printf("Loading script from flash (%d bytes)\n", script_len);
            
            // Create a temporary buffer and copy the script (null-terminate)
            char* temp_script = new char[script_len + 1];
            if (temp_script) {
                memcpy(temp_script, script_addr, script_len);
                temp_script[script_len] = '\0';
                
                // Load and execute the script
                if (g_crow_lua->load_user_script(temp_script)) {
                    printf("Flash script loaded successfully\n");
                    
                    // Call init() function after successful script load
                    if (g_crow_lua->call_init()) {
                        printf("Flash script init() called successfully\n");
                    }
                } else {
                    printf("Flash script compilation failed\n");
                }
                
                delete[] temp_script;
            } else {
                printf("Failed to allocate memory for flash script\n");
            }
        } else {
            printf("Invalid flash script data\n");
        }
    } else {
        printf("No valid script in flash\n");
    }
}

// Get unique card ID for First.lua script generation
uint64_t CrowEmulator::get_unique_card_id()
{
    return UniqueCardID();  // Access ComputerCard's unique ID method
}

// Implementation for crow_error.cpp bridge function
extern "C" void crow_send_error_to_usb(const char* message)
{
    if (CrowEmulator::instance) {
        CrowEmulator::instance->send_error_message(message);
    }
}
