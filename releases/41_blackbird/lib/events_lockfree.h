#pragma once

#include <stdint.h>
#include <stdbool.h>

// Lock-free event queues for timing-critical events in dual-core systems
// Core 1 (audio) = single producer, Core 0 (control) = single consumer

#define LOCKFREE_QUEUE_SIZE 64  // Must be power of 2 for efficient wrapping
#define LOCKFREE_QUEUE_MASK (LOCKFREE_QUEUE_SIZE - 1)

// Metro event structure for lock-free queue
typedef struct {
    int metro_id;
    int stage;
    uint32_t timestamp_us;  // For debugging timing precision
} metro_event_lockfree_t;

// Input detection event structure for lock-free queue  
typedef struct {
    int channel;        // Input channel (0-based)
    float value;        // Detection value (voltage or state)
    int detection_type; // 0=change, 1=stream, 2=other
    uint32_t timestamp_us;
} input_event_lockfree_t;

// Lock-free SPSC (Single Producer Single Consumer) ring buffer
typedef struct {
    volatile uint32_t write_idx;  // Only Core 1 writes this
    volatile uint32_t read_idx;   // Only Core 0 reads this
    uint32_t size;               // Queue size (must be power of 2)
    uint32_t mask;               // Size - 1 for efficient wrapping
} lockfree_queue_header_t;

// Complete lock-free metro queue
typedef struct {
    lockfree_queue_header_t header;
    volatile metro_event_lockfree_t events[LOCKFREE_QUEUE_SIZE];
} metro_lockfree_queue_t;

// Complete lock-free input queue
typedef struct {
    lockfree_queue_header_t header;
    volatile input_event_lockfree_t events[LOCKFREE_QUEUE_SIZE];
} input_lockfree_queue_t;

// Global lock-free queues
extern metro_lockfree_queue_t g_metro_lockfree_queue;
extern input_lockfree_queue_t g_input_lockfree_queue;

// Lock-free queue operations

// Initialize lock-free event system
void events_lockfree_init(void);

// Metro queue functions (Core 1 = producer, Core 0 = consumer)
bool metro_lockfree_post(int metro_id, int stage);
bool metro_lockfree_get(metro_event_lockfree_t* event);
uint32_t metro_lockfree_queue_depth(void);

// Input queue functions (Core 1 = producer, Core 0 = consumer)  
bool input_lockfree_post(int channel, float value, int detection_type);
bool input_lockfree_get(input_event_lockfree_t* event);
uint32_t input_lockfree_queue_depth(void);

// Statistics and monitoring
void events_lockfree_print_stats(void);
bool events_lockfree_are_healthy(void);
