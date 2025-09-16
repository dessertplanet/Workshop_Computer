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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DRUID_STREAM_INTERVAL_MS 100
#define DRUID_INPUT_DELTA_MIN 0.01f

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
    script_upload_size(0),
    script_upload_pos(0),
    status_led_counter(0),
    error_led_counter(0),
    block_position(0)
{
    instance = this;
    memset(rx_buffer, 0, USB_RX_BUFFER_SIZE);
    
    // Script upload buffer now static (no dynamic allocation needed)
    
    // Set global pointer for hardware abstraction
    g_computer_card = this;
    
    // Set global pointer for lua access
    g_crow_emulator = this;
    
    // Initialize error handling system
    crow_error_init();
    
    // Initialize scale / quantization structures
    for (int i = 0; i < 4; i++) {
        scale_cfg[i].enabled = false;
        scale_cfg[i].mod = 12;
        scale_cfg[i].scaling = 1.0f;
        scale_cfg[i].count = 0;
        for (int d = 0; d < 16; d++) scale_cfg[i].degrees[d] = 0.0f;
        scale_cfg[i].last_midi = 60;
        scale_cfg[i].last_midi_valid = false;
    }

    // Initialize clock mode structures
    for (int i = 0; i < 4; i++) {
        output_clock[i].enabled = false;
        output_clock[i].period_s = 0.5f;
        output_clock[i].width_s = 0.01f;
        output_clock[i].phase_s = 0.0f;
        output_clock[i].saved_quant_enabled = false;
    }
    
    // Initialize direct output voltage arrays
    for (int i = 0; i < 4; i++) {
        direct_output_volts[i] = 0.0f;
        direct_output_active[i] = false;
    }
    
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
    
    // Apply outputs using hardware abstraction - single processing chain like real crow
    for (int ch = 0; ch < 4; ch++) {
        // Use processed block output (no override system - matches real crow architecture)
        float output_voltage = output_block[ch][output_index];
        
        // Set hardware output directly
        set_hardware_output(ch, output_voltage);
    }
    
#ifdef CROW_DEBUG_AUDIO_PASSTHRU
    // Debug passthrough: mirror inputs to outputs 1 & 2
    AudioOut1(AudioIn1());
    AudioOut2(AudioIn2());
#endif

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
    // Use stack/static buffers to avoid per-block allocation
    float slopes_output_blocks[4][CROW_BLOCK_SIZE];
    float* slopes_output_blocks_ptrs[4];
    for (int ch = 0; ch < 4; ch++) {
        slopes_output_blocks_ptrs[ch] = slopes_output_blocks[ch];
    }
    
    crow_slopes_process_block(input_blocks, slopes_output_blocks_ptrs, CROW_BLOCK_SIZE);
    
    // Copy slopes output to main output blocks (will be combined with lua outputs later)
    for (int ch = 0; ch < 4; ch++) {
        memcpy(output_block[ch], slopes_output_blocks[ch], CROW_BLOCK_SIZE * sizeof(float));
    }
    
    // Phase 4.3.2: Process detection system with vector operations (stays on Core 0)
    crow_detect_process_block(input_blocks, CROW_BLOCK_SIZE);
    
    // Phase 3: Update hardware abstraction layer
    crow_hardware_update();
    
    // Get Lua output values from Core 1 via multicore communication
    bool volts_changed[4] = {false,false,false,false};
    for (int ch = 0; ch < 4; ch++) {
        float volts;
        bool volts_new, trigger;
        if (crow_multicore_get_lua_output(ch, &volts, &volts_new, &trigger)) {
            if (volts_new) {
                volts_changed[ch] = true;
                // REVERT TO ORIGINAL: Fill entire block with the new voltage value (like before)
                for (int i = 0; i < CROW_BLOCK_SIZE; i++) {
                    output_block[ch][i] = volts;
                }
            }
        }
        // else keep slopes output
    }

    // Auto-cancel clock if user explicitly set volts this block
    for (int ch = 0; ch < 4; ch++) {
        if (volts_changed[ch] && output_clock[ch].enabled) {
            clear_output_clock(ch);
        }
    }

    // Apply clock gating (overrides everything else while active)
    const float sample_time = 1.0f / 48000.0f;
    for (int ch = 0; ch < 4; ch++) {
        if (!output_clock[ch].enabled) continue;
        auto &clk = output_clock[ch];
        for (int i = 0; i < CROW_BLOCK_SIZE; i++) {
            // Gate high if within width window
            float gate = (clk.phase_s < clk.width_s) ? 5.0f : 0.0f;
            output_block[ch][i] = gate;
            clk.phase_s += sample_time;
            if (clk.phase_s >= clk.period_s) {
                clk.phase_s -= clk.period_s;
            }
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
    // Upstream-style greeting + immediate protocol packets
    send_usb_string("hi");
    send_usb_string(""); // blank line
    crow_print_version();
    crow_print_identity();
    send_usb_string("^^ready()");
}

void CrowEmulator::crow_print_version()
{
    // Emit crow-formatted version packet
    send_usb_printf("^^version('%s')", "0.1.0");
}

void CrowEmulator::crow_print_identity()
{
    // Emit crow-formatted identity packet using unique 64-bit ID
    uint64_t uid = get_unique_card_id();
    char buf[64];
    snprintf(buf, sizeof(buf), "^^identity('0x%016llX')", (unsigned long long)uid);
    send_usb_string(buf);
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
            // Check for debug test command
            if (strncmp(rx_buffer, "debug_test", 10) == 0) {
                // Test all print functions
                printf("DEBUG TEST: printf to console\n");
                send_usb_string("DEBUG TEST: send_usb_string");
                send_usb_printf("DEBUG TEST: send_usb_printf with value %d", 42);
                send_usb_printf("[DEBUG] ProcessBlock test message");
                send_usb_printf("[DEBUG] Slopes test message");
                send_usb_printf("[DEBUG] Hardware output test message");
                return;
            }
            
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
        // Scan for upload terminators ^^e (execute) or ^^w (write & execute)
        for (uint32_t i = 0; i < (count >= 2 ? count - 2 : 0); i++) {
            if (temp_buffer[i] == '^' && temp_buffer[i + 1] == '^') {
                char term = temp_buffer[i + 2];
                if (term == 'e' || term == 'w') {
                    // Data before sentinel belongs to script
                    if (i > 0) {
                        process_script_upload_data(temp_buffer, i);
                    }
                    bool persist = (term == 'w');
                    finalize_script_upload(persist);
                    return;
                }
            }
        }
        // No sentinel in this chunk: append all
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
        // Service TinyUSB (required since CFG_TUSB_OS = NONE)
        tud_task();
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
                    if (volts_new) {
                        // Send debug message to druid via USB
                        send_usb_printf("[DEBUG] Output %d changed to %g volts", ch + 1, volts);
                        send_usb_printf("^^output(%d,%g)", ch + 1, volts);
                    }
                }
            }
            
            // Update Lua input values from shared memory
            for (int ch = 0; ch < 2; ch++) {
                float input_value;
                if (crow_multicore_get_input_value(ch, &input_value)) {
                    g_crow_lua->set_input_volts(ch + 1, input_value);
                }
            }

            // Input streaming telemetry (rate & delta limited)
            {
                static uint32_t last_stream_time_ms = 0;
                static float last_stream_val[2] = {0.0f, 0.0f};
                uint32_t now = current_time;
                if (now - last_stream_time_ms >= DRUID_STREAM_INTERVAL_MS) {
                    // Send both channels
                    for (int ch = 0; ch < 2; ch++) {
                        float val;
                        if (crow_multicore_get_input_value(ch, &val)) {
                            send_usb_printf("^^stream(%d,%g)", ch + 1, val);
                            last_stream_val[ch] = val;
                        }
                    }
                    last_stream_time_ms = now;
                } else {
                    for (int ch = 0; ch < 2; ch++) {
                        float val;
                        if (crow_multicore_get_input_value(ch, &val)) {
                            if (fabsf(val - last_stream_val[ch]) >= DRUID_INPUT_DELTA_MIN) {
                                send_usb_printf("^^stream(%d,%g)", ch + 1, val);
                                last_stream_val[ch] = val;
                            }
                        }
                    }
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
        
        // Small delay to prevent busy waiting 
        sleep_us(1000); // 1 millisecond - slower polling to allow voltage changes to be processed
    }
}


// Script upload management (Phase 2.2)
void CrowEmulator::start_script_upload()
{
    script_upload_mode = true;
    script_upload_pos = 0;
    script_upload_size = 0;
    
    send_usb_string("script upload started");
    printf("Script upload started\n");
}

void CrowEmulator::end_script_upload()
{
    // Legacy wrapper: treat as volatile finalize
    finalize_script_upload(false);
}

bool CrowEmulator::process_script_upload_data(const char* data, size_t length)
{
    if (!script_upload_mode) {
        return false;
    }
    
    // Check if we have space for this data (enforce 8kB limit)
    if (script_upload_pos + length >= MAX_SCRIPT_SIZE) {
        send_usb_string("!script too long");
        script_upload_mode = false;
        return false;
    }
    
    memcpy(script_upload_buffer + script_upload_pos, data, length);
    script_upload_pos += length;
    return true;
}

// Unified finalize for ^^e / ^^w
void CrowEmulator::finalize_script_upload(bool persist)
{
    if (!script_upload_mode) {
        send_usb_string("!no upload in progress");
        return;
    }
    script_upload_mode = false;

    if (script_upload_pos == 0) {
        send_usb_string("!empty script");
        script_upload_pos = 0;
        script_upload_size = 0;
        return;
    }

    // Null terminate
    script_upload_buffer[script_upload_pos] = '\0';
    printf("Script upload complete, %zu bytes received\n", script_upload_pos);

    bool compiled = false;
    if (g_crow_lua && g_crow_lua->load_user_script(script_upload_buffer)) {
        compiled = true;
        send_usb_string("script loaded successfully");
        if (g_crow_lua->call_init()) {
            printf("init() called successfully\n");
        }
    } else {
        send_usb_string("!script compilation error");
    }

    if (compiled && persist) {
        uint8_t result = Flash_write_user_script(script_upload_buffer, (uint32_t)script_upload_pos);
        if (result == 0) {
            send_usb_string("script saved to flash");
            printf("Script saved to flash: %zu bytes\n", script_upload_pos);
        } else {
            send_usb_string("!flash write error");
            printf("Flash write failed with error %d\n", result);
        }
    }

    if (compiled) {
        send_usb_string("^^ready()");
    }

    script_upload_pos = 0;
    script_upload_size = 0;
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
    // Map signed 12-bit style range (-2048..2047) to ±CROW_FULLSCALE_VOLTS
    return (cc_value / 2048.0f) * CROW_FULLSCALE_VOLTS;
}

int16_t CrowEmulator::crow_to_computercard_value(float crow_volts)
{
    // Clamp to full-scale range
    if (crow_volts > CROW_FULLSCALE_VOLTS) crow_volts = CROW_FULLSCALE_VOLTS;
    if (crow_volts < -CROW_FULLSCALE_VOLTS) crow_volts = -CROW_FULLSCALE_VOLTS;

    // Normalize and scale to signed 12-bit style range (-2048..2047)
    float normalized = crow_volts / CROW_FULLSCALE_VOLTS; // -1..1
    int16_t result = (int16_t)(normalized * 2048.0f);

    // Clamp to valid bounds
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
    // Real crow architecture: use slopes system with 0ms time for immediate voltage changes
    if (channel >= 0 && channel < 4) {
        // Use slopes system like real crow - 0ms time = immediate
        crow_slopes_toward(channel, volts, 0.0f, CROW_SHAPE_Linear, nullptr);
    }
}

void CrowEmulator::clear_direct_output(int channel)
{
    // Clear direct output control to let slopes system take over
    if (channel >= 0 && channel < 4) {
        direct_output_active[channel] = false;
    }
}

void CrowEmulator::set_hardware_output(int channel, float volts)
{
    // Channels 0 & 1: always continuous audio outputs
    if (channel == 0 || channel == 1) {
        int16_t cc_value = crow_to_computercard_value(volts);
        if (channel == 0) AudioOut1(cc_value);
        else AudioOut2(cc_value);
        return;
    }
    
    // Channels 2 & 3: CV outputs with optional quantization
    if (channel == 2 || channel == 3) {
        CrowScale &cfg = scale_cfg[channel];
        
        // Debug output removed - was causing audio glitches in real-time thread
        
        if (cfg.enabled && cfg.count > 0 && cfg.mod > 0) {
            // Convert volts to MIDI float (0V -> 60)
            float midi_f = 60.0f + (volts * 12.0f / cfg.scaling);
            if (midi_f < 0.0f) midi_f = 0.0f;
            if (midi_f > 127.0f) midi_f = 127.0f;
            
            // Derive octave + degree within mod
            int octave = (int)std::floor(midi_f / cfg.mod);
            float degree_f = midi_f - (float)(octave * cfg.mod);
            
            // Find nearest degree in cfg.degrees
            float best_deg = cfg.degrees[0];
            float best_err = fabsf(degree_f - best_deg);
            for (uint8_t i = 1; i < cfg.count; i++) {
                float err = fabsf(degree_f - cfg.degrees[i]);
                if (err < best_err) {
                    best_err = err;
                    best_deg = cfg.degrees[i];
                }
            }
            float quant_midi_f = (float)(octave * cfg.mod) + best_deg;
            if (quant_midi_f < 0.0f) quant_midi_f = 0.0f;
            if (quant_midi_f > 127.0f) quant_midi_f = 127.0f;
            uint8_t quant_midi = (uint8_t)(quant_midi_f + 0.5f);
            
            // Debug output removed - was causing audio glitches in real-time thread
            
            // Avoid redundant hardware writes
            if (!cfg.last_midi_valid || quant_midi != cfg.last_midi) {
                if (channel == 2) {
                    CVOut1MIDINote(quant_midi);
                } else {
                    CVOut2MIDINote(quant_midi);
                }
                cfg.last_midi = quant_midi;
                cfg.last_midi_valid = true;
            }
            return;
        } else {
            // Pass-through continuous calibrated CV
            int16_t cc_value = crow_to_computercard_value(volts);
            // Debug output removed - was causing audio glitches in real-time thread
            
            if (channel == 2) {
                CVOut1(cc_value);
            } else {
                CVOut2(cc_value);
            }
            return;
        }
    }
}

void CrowEmulator::disable_output_scale(int channel)
{
    if (channel < 0 || channel >= 4) return;
    scale_cfg[channel].enabled = false;
    scale_cfg[channel].count = 0;
    scale_cfg[channel].last_midi_valid = false;
}

void CrowEmulator::set_output_scale(int channel, const float* degrees, int count, int mod, float scaling)
{
    if (channel < 0 || channel >= 4) return;
    // Allow scaling on all outputs, but channels 0,1 will ignore it in set_hardware_output
    CrowScale &cfg = scale_cfg[channel];
    if (count <= 0 || mod <= 0) {
        disable_output_scale(channel);
        return;
    }
    if (count > 16) count = 16;
    cfg.enabled = true;
    cfg.mod = (mod > 0 && mod < 64) ? (uint8_t)mod : 12;
    cfg.scaling = (scaling <= 0.0f) ? 1.0f : scaling;
    cfg.count = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        // Clamp degree into [0, mod)
        float d = degrees[i];
        if (d < 0.0f) d = 0.0f;
        if (d >= cfg.mod) d = (float)(cfg.mod - 1);
        cfg.degrees[i] = d;
    }
    cfg.last_midi_valid = false; // force re-write
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

// Clock mode control --------------------------------------------------------

void CrowEmulator::set_output_clock(int channel, float period_s, float width_s)
{
    if (channel < 0 || channel >= 4) return;
    if (period_s <= 0.0f) period_s = 0.5f;
    if (period_s < 0.001f) period_s = 0.001f; // clamp minimum period (1 kHz)
    if (width_s <= 0.0f) width_s = 0.01f;
    if (width_s >= period_s) width_s = period_s * 0.1f;
    if (width_s > period_s * 0.5f) width_s = period_s * 0.5f;

    OutputClock &clk = output_clock[channel];

    // Save & bypass quantization for calibrated outputs (channels 2 & 3)
    if (!clk.enabled && (channel == 2 || channel == 3)) {
        clk.saved_quant_enabled = scale_cfg[channel].enabled;
        if (scale_cfg[channel].enabled) {
            scale_cfg[channel].enabled = false;      // bypass quant during clock
            scale_cfg[channel].last_midi_valid = false;
        }
    }

    clk.enabled = true;
    clk.period_s = period_s;
    clk.width_s = width_s;
    clk.phase_s = 0.0f;
}

void CrowEmulator::clear_output_clock(int channel)
{
    if (channel < 0 || channel >= 4) return;
    OutputClock &clk = output_clock[channel];
    if (!clk.enabled) return;

    // Restore quantization state for calibrated outputs if previously enabled
    if ((channel == 2 || channel == 3) && clk.saved_quant_enabled) {
        scale_cfg[channel].enabled = true;
        scale_cfg[channel].last_midi_valid = false; // force re-write
    }

    clk.enabled = false;
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
    if (script_upload_pos == 0) {
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
                    send_usb_string("^^ready()");
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
        
        if (file_size > 0 && file_size < USER_SCRIPT_SIZE && file_size < MAX_SCRIPT_SIZE) {
            if (fread(script_staging_buffer, 1, file_size, first_file) == file_size) {
                script_staging_buffer[file_size] = '\0';
                
                // Load script into lua
                if (g_crow_lua->load_user_script(script_staging_buffer)) {
                    send_usb_string("first.lua loaded successfully");
                    g_crow_lua->call_init();
                    send_usb_string("^^ready()");
                    
                    // Store First.lua in flash for future use
                    Flash_write_first_script(script_staging_buffer, file_size);
                    printf("First.lua stored in flash (%ld bytes)\n", file_size);
                } else {
                    send_usb_string("!first.lua compilation error");
                }
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
            
            // Copy the script into staging buffer (null-terminate)
            if (script_len < MAX_SCRIPT_SIZE) {
                memcpy(script_staging_buffer, script_addr, script_len);
                script_staging_buffer[script_len] = '\0';
                
                // Load and execute the script
                if (g_crow_lua->load_user_script(script_staging_buffer)) {
                    printf("Flash script loaded successfully\n");
                    
                    // Call init() function after successful script load
                    if (g_crow_lua->call_init()) {
                        printf("Flash script init() called successfully\n");
            send_usb_string("^^ready()");
                    }
                } else {
                    printf("Flash script compilation failed\n");
                }
            } else {
                printf("Flash script too large (%d bytes)\n", script_len);
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
