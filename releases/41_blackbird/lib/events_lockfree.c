#include "events_lockfree.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "debug.h"

// ARM Cortex-M0+ memory barriers for RP2040
#define DMB() __asm volatile ("dmb" ::: "memory")
#define DSB() __asm volatile ("dsb" ::: "memory")

// Global lock-free queues
metro_lockfree_queue_t g_metro_lockfree_queue;
input_lockfree_queue_t g_input_lockfree_queue;

// Statistics
static volatile uint32_t metro_events_posted = 0;
static volatile uint32_t metro_events_processed = 0;
static volatile uint32_t metro_events_dropped = 0;
static volatile uint32_t input_events_posted = 0;
static volatile uint32_t input_events_processed = 0;
static volatile uint32_t input_events_dropped = 0;

// Initialize lock-free event system
void events_lockfree_init(void) {
    // Initialize metro queue
    g_metro_lockfree_queue.header.write_idx = 0;
    g_metro_lockfree_queue.header.read_idx = 0;
    g_metro_lockfree_queue.header.size = LOCKFREE_QUEUE_SIZE;
    g_metro_lockfree_queue.header.mask = LOCKFREE_QUEUE_MASK;
    
    // Initialize input queue  
    g_input_lockfree_queue.header.write_idx = 0;
    g_input_lockfree_queue.header.read_idx = 0;
    g_input_lockfree_queue.header.size = LOCKFREE_QUEUE_SIZE;
    g_input_lockfree_queue.header.mask = LOCKFREE_QUEUE_MASK;
    
    // Reset statistics
    metro_events_posted = 0;
    metro_events_processed = 0;
    metro_events_dropped = 0;
    input_events_posted = 0;
    input_events_processed = 0;
    input_events_dropped = 0;
    
    DEBUG_LF_PRINT("Lock-free event queues initialized (metro=%d, input=%d slots)\n", 
                   LOCKFREE_QUEUE_SIZE, LOCKFREE_QUEUE_SIZE);
}

// Metro queue functions

// Post metro event from Core 1 (audio) - NEVER BLOCKS!
bool metro_lockfree_post(int metro_id, int stage) {
    metro_lockfree_queue_t* queue = &g_metro_lockfree_queue;
    
    // Load current write index
    uint32_t current_write = queue->header.write_idx;
    uint32_t next_write = (current_write + 1) & queue->header.mask;
    
    // Check if queue has space (leave one slot empty to distinguish full from empty)
    if (next_write == queue->header.read_idx) {
        // Queue full - drop event (don't block audio core!)
        metro_events_dropped++;
        return false;
    }
    
    // Store event data
    queue->events[current_write].metro_id = metro_id;
    queue->events[current_write].stage = stage;
    queue->events[current_write].timestamp_us = time_us_32();
    
    // Memory barrier - ensure event data is written before updating index
    DMB();
    
    // Commit the write (this makes the event visible to consumer)
    queue->header.write_idx = next_write;
    
    metro_events_posted++;
    return true;
}

// Get metro event from Core 0 (control) - NEVER BLOCKS!
bool metro_lockfree_get(metro_event_lockfree_t* event) {
    metro_lockfree_queue_t* queue = &g_metro_lockfree_queue;
    
    // Load current read index  
    uint32_t current_read = queue->header.read_idx;
    
    // Check if queue has data
    if (current_read == queue->header.write_idx) {
        // Queue empty
        return false;
    }
    
    // Copy event data
    *event = queue->events[current_read];
    
    // Memory barrier - ensure event data is read before updating index
    DMB();
    
    // Commit the read (this frees the slot for producer)
    uint32_t next_read = (current_read + 1) & queue->header.mask;
    queue->header.read_idx = next_read;
    
    metro_events_processed++;
    return true;
}

// Get metro queue depth (for monitoring)
uint32_t metro_lockfree_queue_depth(void) {
    metro_lockfree_queue_t* queue = &g_metro_lockfree_queue;
    uint32_t write_idx = queue->header.write_idx;
    uint32_t read_idx = queue->header.read_idx;
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (queue->header.size - read_idx) + write_idx;
    }
}

// Input queue functions

// Post input detection event from Core 1 (audio) - NEVER BLOCKS!
bool input_lockfree_post(int channel, float value, int detection_type) {
    input_lockfree_queue_t* queue = &g_input_lockfree_queue;
    
    // Load current write index
    uint32_t current_write = queue->header.write_idx;
    uint32_t next_write = (current_write + 1) & queue->header.mask;
    
    // Check if queue has space
    if (next_write == queue->header.read_idx) {
        // Queue full - drop event (don't block audio core!)
        input_events_dropped++;
        return false;
    }
    
    // Store event data
    queue->events[current_write].channel = channel;
    queue->events[current_write].value = value;
    queue->events[current_write].detection_type = detection_type;
    queue->events[current_write].timestamp_us = time_us_32();
    
    // Memory barrier - ensure event data is written before updating index
    DMB();
    
    // Commit the write
    queue->header.write_idx = next_write;
    
    input_events_posted++;
    return true;
}

// Get input detection event from Core 0 (control) - NEVER BLOCKS!
bool input_lockfree_get(input_event_lockfree_t* event) {
    input_lockfree_queue_t* queue = &g_input_lockfree_queue;
    
    // Load current read index
    uint32_t current_read = queue->header.read_idx;
    
    // Check if queue has data
    if (current_read == queue->header.write_idx) {
        // Queue empty
        return false;
    }
    
    // Copy event data
    *event = queue->events[current_read];
    
    // Memory barrier - ensure event data is read before updating index
    DMB();
    
    // Commit the read
    uint32_t next_read = (current_read + 1) & queue->header.mask;
    queue->header.read_idx = next_read;
    
    input_events_processed++;
    return true;
}

// Get input queue depth (for monitoring)
uint32_t input_lockfree_queue_depth(void) {
    input_lockfree_queue_t* queue = &g_input_lockfree_queue;
    uint32_t write_idx = queue->header.write_idx;
    uint32_t read_idx = queue->header.read_idx;
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (queue->header.size - read_idx) + write_idx;
    }
}

// Statistics and monitoring

void events_lockfree_print_stats(void) {
    DEBUG_LF_PRINT("=== LOCK-FREE EVENT QUEUE STATISTICS ===\n");
    DEBUG_LF_PRINT("Metro Queue: depth=%lu/%d\n", metro_lockfree_queue_depth(), LOCKFREE_QUEUE_SIZE);
    DEBUG_LF_PRINT("  Posted: %lu, Processed: %lu, Dropped: %lu\n", 
                   metro_events_posted, metro_events_processed, metro_events_dropped);
    
    DEBUG_LF_PRINT("Input Queue: depth=%lu/%d\n", input_lockfree_queue_depth(), LOCKFREE_QUEUE_SIZE);
    DEBUG_LF_PRINT("  Posted: %lu, Processed: %lu, Dropped: %lu\n", 
                   input_events_posted, input_events_processed, input_events_dropped);
    
    DEBUG_LF_PRINT("Health: Metro=%s, Input=%s\n", 
                   (metro_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE/2) ? "OK" : "OVERLOADED",
                   (input_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE/2) ? "OK" : "OVERLOADED");
    DEBUG_LF_PRINT("=======================================\n");
}

bool events_lockfree_are_healthy(void) {
    return (metro_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE * 3 / 4) &&
           (input_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE * 3 / 4) &&
           (metro_events_dropped == 0) &&
           (input_events_dropped == 0);
}
