#include "crow_multicore.h"
#include "crow_asl.h"
#include "crow_casl.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "pico/time.h"

// Global shared data
crow_shared_data_t* g_crow_shared = nullptr;

// Initialize the multicore communication system
void crow_multicore_init(void) {
    if (g_crow_shared) {
        return; // Already initialized
    }
    
    printf("Initializing crow multicore communication...\n");
    
    // Allocate shared data structure
    g_crow_shared = (crow_shared_data_t*)malloc(sizeof(crow_shared_data_t));
    if (!g_crow_shared) {
        printf("Failed to allocate multicore shared data!\n");
        return;
    }
    
    // Initialize shared data
    memset((void*)g_crow_shared, 0, sizeof(crow_shared_data_t));
    
    // Initialize message queues
    if (!crow_msg_queue_init(&g_crow_shared->core0_to_core1)) {
        printf("Failed to initialize Core 0->1 message queue\n");
        free(g_crow_shared);
        g_crow_shared = nullptr;
        return;
    }
    
    if (!crow_msg_queue_init(&g_crow_shared->core1_to_core0)) {
        printf("Failed to initialize Core 1->0 message queue\n");
        crow_msg_queue_deinit(&g_crow_shared->core0_to_core1);
        free(g_crow_shared);
        g_crow_shared = nullptr;
        return;
    }
    
    // Initialize block counters
    g_crow_shared->core0_block_counter = 0;
    g_crow_shared->core1_block_counter = 0;
    g_crow_shared->core1_processing = false;
    
    printf("Crow multicore communication initialized\n");
}

void crow_multicore_deinit(void) {
    if (!g_crow_shared) {
        return;
    }
    
    crow_msg_queue_deinit(&g_crow_shared->core0_to_core1);
    crow_msg_queue_deinit(&g_crow_shared->core1_to_core0);
    
    free(g_crow_shared);
    g_crow_shared = nullptr;
}

// Message queue implementation
bool crow_msg_queue_init(crow_msg_queue_t* queue) {
    if (!queue) {
        return false;
    }
    
    queue->write_pos = 0;
    queue->read_pos = 0;
    critical_section_init(&queue->lock);
    
    return true;
}

void crow_msg_queue_deinit(crow_msg_queue_t* queue) {
    if (!queue) {
        return;
    }
    
    critical_section_deinit(&queue->lock);
}

bool crow_msg_queue_send(crow_msg_queue_t* queue, const crow_msg_t* msg) {
    if (!queue || !msg) {
        return false;
    }
    
    critical_section_enter_blocking(&queue->lock);
    
    uint32_t next_write = (queue->write_pos + 1) % CROW_MSG_QUEUE_SIZE;
    
    // Check if queue is full
    if (next_write == queue->read_pos) {
        critical_section_exit(&queue->lock);
        return false; // Queue full
    }
    
    // Copy message
    queue->messages[queue->write_pos] = *msg;
    queue->write_pos = next_write;
    
    critical_section_exit(&queue->lock);
    return true;
}

bool crow_msg_queue_receive(crow_msg_queue_t* queue, crow_msg_t* msg) {
    if (!queue || !msg) {
        return false;
    }
    
    critical_section_enter_blocking(&queue->lock);
    
    // Check if queue is empty
    if (queue->read_pos == queue->write_pos) {
        critical_section_exit(&queue->lock);
        return false; // Queue empty
    }
    
    // Copy message
    *msg = queue->messages[queue->read_pos];
    queue->read_pos = (queue->read_pos + 1) % CROW_MSG_QUEUE_SIZE;
    
    critical_section_exit(&queue->lock);
    return true;
}

bool crow_msg_queue_is_empty(crow_msg_queue_t* queue) {
    if (!queue) {
        return true;
    }
    
    critical_section_enter_blocking(&queue->lock);
    bool empty = (queue->read_pos == queue->write_pos);
    critical_section_exit(&queue->lock);
    
    return empty;
}

// Core 0 functions (called from audio thread)
void crow_multicore_core0_block_start(float* input_blocks[4]) {
    if (!g_crow_shared) {
        return;
    }
    
    // Update input values for Core 1
    // Use the first sample of each block as representative value
    for (int ch = 0; ch < 4; ch++) {
        if (input_blocks[ch]) {
            g_crow_shared->input_values[ch] = input_blocks[ch][0];
            g_crow_shared->input_values_updated[ch] = true;
        }
    }
    
    // Increment block counter
    g_crow_shared->core0_block_counter++;
    
    // Send block sync message to Core 1
    crow_msg_t sync_msg;
    sync_msg.type = CROW_MSG_BLOCK_SYNC;
    sync_msg.channel = 0;
    crow_msg_queue_send(&g_crow_shared->core0_to_core1, &sync_msg);
}

void crow_multicore_core0_block_complete(void) {
    if (!g_crow_shared) {
        return;
    }
    
    // Clear input update flags
    for (int ch = 0; ch < 4; ch++) {
        g_crow_shared->input_values_updated[ch] = false;
    }
}

bool crow_multicore_get_lua_output(int channel, float* volts, bool* volts_changed, bool* trigger) {
    if (!g_crow_shared || channel < 0 || channel >= 4) {
        return false;
    }
    
    if (volts) {
        *volts = g_crow_shared->lua_outputs[channel];
    }
    if (volts_changed) {
        *volts_changed = g_crow_shared->lua_outputs_changed[channel];
        g_crow_shared->lua_outputs_changed[channel] = false; // Clear flag after reading
    }
    if (trigger) {
        *trigger = g_crow_shared->lua_triggers[channel];
        g_crow_shared->lua_triggers[channel] = false; // Clear trigger after reading
    }
    
    return true;
}

void crow_multicore_send_asl_action(int channel, int action) {
    if (!g_crow_shared) {
        return;
    }
    
    crow_msg_t msg;
    msg.type = CROW_MSG_ASL_ACTION;
    msg.channel = channel;
    msg.data.asl.action = action;
    
    if (!crow_msg_queue_send(&g_crow_shared->core0_to_core1, &msg)) {
        printf("Warning: ASL action message queue full\n");
    }
}

void crow_multicore_send_casl_action(int channel, int action) {
    if (!g_crow_shared) {
        return;
    }
    
    crow_msg_t msg;
    msg.type = CROW_MSG_CASL_ACTION;
    msg.channel = channel;
    msg.data.casl.action = action;
    
    if (!crow_msg_queue_send(&g_crow_shared->core0_to_core1, &msg)) {
        printf("Warning: CASL action message queue full\n");
    }
}

// Core 1 functions (called from background thread)
void crow_multicore_core1_process_block(void) {
    if (!g_crow_shared) {
        return;
    }
    
    g_crow_shared->core1_processing = true;
    
    // Process all pending messages from Core 0
    crow_msg_t msg;
    while (crow_msg_queue_receive(&g_crow_shared->core0_to_core1, &msg)) {
        switch (msg.type) {
            case CROW_MSG_ASL_ACTION:
                // Forward to ASL system on Core 1
                crow_asl_action(msg.channel, msg.data.asl.action);
                printf("Core 1: ASL action ch=%d action=%d\n", msg.channel, msg.data.asl.action);
                break;
                
            case CROW_MSG_CASL_ACTION:
                // Forward to CASL system on Core 1
                crow_casl_action(msg.channel, msg.data.casl.action);
                printf("Core 1: CASL action ch=%d action=%d\n", msg.channel, msg.data.casl.action);
                break;
                
            case CROW_MSG_BLOCK_SYNC:
                // Sync with Core 0's block processing
                g_crow_shared->core1_block_counter = g_crow_shared->core0_block_counter;
                break;
                
            default:
                printf("Core 1: Unknown message type %d\n", msg.type);
                break;
        }
    }
    
    g_crow_shared->core1_processing = false;
}

void crow_multicore_set_lua_output(int channel, float volts, bool changed, bool trigger) {
    if (!g_crow_shared || channel < 0 || channel >= 4) {
        return;
    }
    
    g_crow_shared->lua_outputs[channel] = volts;
    g_crow_shared->lua_outputs_changed[channel] = changed;
    g_crow_shared->lua_triggers[channel] = trigger;
}

bool crow_multicore_get_input_value(int channel, float* value) {
    if (!g_crow_shared || channel < 0 || channel >= 4 || !value) {
        return false;
    }
    
    *value = g_crow_shared->input_values[channel];
    return g_crow_shared->input_values_updated[channel];
}

// Timing synchronization
void crow_multicore_wait_for_core1_sync(void) {
    if (!g_crow_shared) {
        return;
    }
    
    // Wait for Core 1 to catch up with block processing
    uint32_t core0_counter = g_crow_shared->core0_block_counter;
    uint32_t timeout = 1000; // Timeout in microseconds
    
    while (g_crow_shared->core1_block_counter < core0_counter && timeout > 0) {
        sleep_us(1);
        timeout--;
    }
    
    if (timeout == 0) {
        printf("Warning: Core 1 sync timeout\n");
    }
}

bool crow_multicore_is_core1_ready(void) {
    if (!g_crow_shared) {
        return false;
    }
    
    return !g_crow_shared->core1_processing;
}
