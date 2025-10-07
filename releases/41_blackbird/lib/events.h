#pragma once

#include <stdint.h>

// Event types for better debugging and monitoring
typedef enum {
    EVENT_TYPE_CHANGE = 0,   // Input change detection
    EVENT_TYPE_STREAM,       // Input stream data
    EVENT_TYPE_LUA_CALL,     // Safe Lua execution
    EVENT_TYPE_OUTPUT,       // Output voltage change
    EVENT_TYPE_SYSTEM,       // System events
    EVENT_TYPE_DEBUG,        // Debug/monitoring events
    EVENT_TYPE_COUNT         // Total number of event types
} event_type_t;

union Data{
    void* p;
    int i;
    float f;
    uint8_t u8s[4];
};

typedef struct event{
    void (*handler)( struct event* e );
    union Data   index;
    union Data   data;
    event_type_t type;       // Event type for monitoring
    uint32_t     timestamp;  // Timestamp for debugging
} event_t;

// Event system statistics for monitoring
typedef struct {
    uint32_t events_posted[EVENT_TYPE_COUNT];
    uint32_t events_processed[EVENT_TYPE_COUNT];
    uint32_t events_dropped;
    uint32_t queue_overflows;
    uint32_t max_queue_depth;
    uint8_t  current_queue_depth;
} event_stats_t;

extern void events_init(void);
extern void events_clear(void);
extern uint8_t event_post(event_t *e);
extern void event_next( void );

// Enhanced monitoring and safety functions
extern event_stats_t* events_get_stats(void);
extern void events_reset_stats(void);
extern uint8_t events_get_queue_depth(void);
extern uint8_t events_is_queue_healthy(void);
extern void events_print_stats(void);
