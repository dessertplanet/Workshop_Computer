#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pico/critical_section.h"
#include "pico/multicore.h"

// Inter-core message types
typedef enum {
    CROW_MSG_ASL_ACTION,
    CROW_MSG_CASL_ACTION,
    CROW_MSG_LUA_OUTPUT,
    CROW_MSG_SLOPE_REQUEST,
    CROW_MSG_BLOCK_SYNC
} crow_msg_type_t;

// Inter-core message structure
typedef struct {
    crow_msg_type_t type;
    uint8_t channel;
    union {
        struct {
            int action;
        } asl;
        struct {
            int action;
        } casl;
        struct {
            float volts;
            bool volts_changed;
            bool trigger;
        } lua_output;
        struct {
            float dest;
            float time_ms;
            uint8_t shape;
        } slope;
    } data;
} crow_msg_t;

// Lock-free ring buffer for inter-core communication
#define CROW_MSG_QUEUE_SIZE 64
typedef struct {
    crow_msg_t messages[CROW_MSG_QUEUE_SIZE];
    volatile uint32_t write_pos;
    volatile uint32_t read_pos;
    critical_section_t lock;
} crow_msg_queue_t;

// Shared data structures for Core 0 <-> Core 1 communication
typedef struct {
    // Lua output values (Core 1 -> Core 0)
    volatile float lua_outputs[4];
    volatile bool lua_outputs_changed[4];
    volatile bool lua_triggers[4];
    
    // Input values (Core 0 -> Core 1)
    volatile float input_values[4];
    volatile bool input_values_updated[4];
    
    // Block synchronization
    volatile uint32_t core0_block_counter;
    volatile uint32_t core1_block_counter;
    volatile bool core1_processing;
    
    // Message queues
    crow_msg_queue_t core0_to_core1;  // Commands from Core 0 to Core 1
    crow_msg_queue_t core1_to_core0;  // Responses from Core 1 to Core 0
    
} crow_shared_data_t;

// Global multicore interface
void crow_multicore_init(void);
void crow_multicore_deinit(void);

// Core 0 functions (called from audio thread)
void crow_multicore_core0_block_start(float* input_blocks[4]);
void crow_multicore_core0_block_complete(void);
bool crow_multicore_get_lua_output(int channel, float* volts, bool* volts_changed, bool* trigger);
void crow_multicore_send_asl_action(int channel, int action);
void crow_multicore_send_casl_action(int channel, int action);

// Core 1 functions (called from background thread)
void crow_multicore_core1_process_block(void);
void crow_multicore_set_lua_output(int channel, float volts, bool changed, bool trigger);
bool crow_multicore_get_input_value(int channel, float* value);

// Message queue functions
bool crow_msg_queue_init(crow_msg_queue_t* queue);
void crow_msg_queue_deinit(crow_msg_queue_t* queue);
bool crow_msg_queue_send(crow_msg_queue_t* queue, const crow_msg_t* msg);
bool crow_msg_queue_receive(crow_msg_queue_t* queue, crow_msg_t* msg);
bool crow_msg_queue_is_empty(crow_msg_queue_t* queue);

// Timing synchronization
void crow_multicore_wait_for_core1_sync(void);
bool crow_multicore_is_core1_ready(void);

extern crow_shared_data_t* g_crow_shared;
