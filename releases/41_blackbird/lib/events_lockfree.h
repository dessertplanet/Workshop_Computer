#pragma once

#include <stdint.h>
#include <stdbool.h>

// Lock-free event queues for timing-critical events in dual-core systems
// Core 1 (audio) = single producer, Core 0 (control) = single consumer

// Default queue size for most queues (metro, input, ASL done)
// NOTE: keeping this at 128 preserves ~7.5KB of BSS for Lua heap on RP2040.
// If you need deeper buffers for stress testing, make this configurable at build time.
#define LOCKFREE_QUEUE_SIZE 128  // Must be power of 2 for efficient wrapping
#define LOCKFREE_QUEUE_MASK (LOCKFREE_QUEUE_SIZE - 1)

// Clock queue can be bursty; allow a larger ring independently if needed
// Keep aligned with LOCKFREE_QUEUE_SIZE to avoid extra BSS cost by default
#define CLOCK_QUEUE_SIZE 128
#define CLOCK_QUEUE_MASK (CLOCK_QUEUE_SIZE - 1)

// Metro event structure for lock-free queue
typedef struct {
    int metro_id;
    int stage;
    uint32_t timestamp_us;  // For debugging timing precision
} metro_event_lockfree_t;

// Clock resume event structure for lock-free queue
typedef struct {
    int coro_id;
    uint32_t timestamp_us;
} clock_event_lockfree_t;

// Input detection event structure for lock-free queue  
typedef struct {
    int channel;        // Input channel (0-based)
    float value;        // Detection value (voltage or state)
    int detection_type; // 0=change, 1=stream, 2=window, 3=scale, 4=volume, 5=peak, 6=freq
    uint32_t timestamp_us;
    // Additional data for modes that need it
    union {
        struct {  // For scale mode
            int index;
            int octave;
            float note;
            float volts;
        } scale;
        struct {  // For window mode (alternative to encoding in value)
            int window;
            bool direction;  // true=up, false=down
        } window;
    } extra;
} input_event_lockfree_t;

// ASL done event structure for lock-free queue
typedef struct {
    int channel;        // ASL channel (0-based)
    uint32_t timestamp_us;
} asl_done_event_lockfree_t;

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

// Complete lock-free clock resume queue
typedef struct {
    lockfree_queue_header_t header;
    volatile clock_event_lockfree_t events[CLOCK_QUEUE_SIZE];
} clock_lockfree_queue_t;

// Complete lock-free input queue
typedef struct {
    lockfree_queue_header_t header;
    volatile input_event_lockfree_t events[LOCKFREE_QUEUE_SIZE];
} input_lockfree_queue_t;

// Complete lock-free ASL done queue
typedef struct {
    lockfree_queue_header_t header;
    volatile asl_done_event_lockfree_t events[LOCKFREE_QUEUE_SIZE];
} asl_done_lockfree_queue_t;

// Clock queue statistics accessors
uint32_t clock_events_posted_count(void);
uint32_t clock_events_processed_count(void);
uint32_t clock_events_dropped_count(void);
// Count of events merged due to queue overflow (overwrote an older pending event).
uint32_t clock_events_coalesced_count(void);
void clock_lockfree_reset_stats(void);

// Reset all queues' stats
void events_lockfree_reset_stats(void);

// Clear all queues (for reset)
void events_lockfree_clear(void);

// Metro queue statistics accessors
uint32_t metro_events_posted_count(void);
uint32_t metro_events_processed_count(void);
uint32_t metro_events_dropped_count(void);
// Count of events merged due to queue overflow (overwrote an older pending event).
uint32_t metro_events_coalesced_count(void);

// Input queue statistics accessors
uint32_t input_events_posted_count(void);
uint32_t input_events_processed_count(void);
uint32_t input_events_dropped_count(void);

// ASL done queue statistics accessors
uint32_t asl_done_events_posted_count(void);
uint32_t asl_done_events_processed_count(void);
uint32_t asl_done_events_dropped_count(void);

// Global lock-free queues
extern metro_lockfree_queue_t g_metro_lockfree_queue;
extern input_lockfree_queue_t g_input_lockfree_queue;
extern clock_lockfree_queue_t g_clock_lockfree_queue;
extern asl_done_lockfree_queue_t g_asl_done_lockfree_queue;

// Lock-free queue operations

// Initialize lock-free event system
void events_lockfree_init(void);

// Metro queue functions (Core 1 = producer, Core 0 = consumer)
bool metro_lockfree_post(int metro_id, int stage);
bool metro_lockfree_get(metro_event_lockfree_t* event);
bool metro_lockfree_peek(metro_event_lockfree_t* event);
uint32_t metro_lockfree_queue_depth(void);

// Clock resume queue functions
bool clock_lockfree_post(int coro_id);
bool clock_lockfree_get(clock_event_lockfree_t* event);
bool clock_lockfree_peek(clock_event_lockfree_t* event);
uint32_t clock_lockfree_queue_depth(void);

// Input queue functions (Core 1 = producer, Core 0 = consumer)  
bool input_lockfree_post(int channel, float value, int detection_type);
bool input_lockfree_post_extended(const input_event_lockfree_t* event);
bool input_lockfree_get(input_event_lockfree_t* event);
uint32_t input_lockfree_queue_depth(void);

// ASL done queue functions (Core 1 = producer, Core 0 = consumer)
bool asl_done_lockfree_post(int channel);
bool asl_done_lockfree_get(asl_done_event_lockfree_t* event);
uint32_t asl_done_lockfree_queue_depth(void);

// Statistics and monitoring
void events_lockfree_print_stats(void);
bool events_lockfree_are_healthy(void);
