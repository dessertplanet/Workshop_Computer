#pragma once

#include <stdint.h>
#include <stdbool.h>

// Forward declaration for wrQueue
struct wrQueue;

// Forward declaration for crow slope callback
typedef void (*crow_slope_callback_t)(int channel);

// Event data union (matching real crow's design)
union crow_event_data {
    void* p;
    int i;
    float f;
    uint8_t u8s[4];
};

// Event structure (matching real crow's design)
typedef struct crow_event {
    void (*handler)(struct crow_event* e);
    union crow_event_data index;
    union crow_event_data data;
    uint32_t timestamp;  // For future precise timing
} crow_event_t;

// Event queue configuration
#define CROW_MAX_EVENTS 40  // Same as real crow

// Global event system
extern void crow_events_init(void);
extern void crow_events_deinit(void);
extern void crow_events_clear(void);
extern bool crow_event_post(crow_event_t* e);
extern void crow_event_process_next(void);
extern void crow_events_process_all(void);
extern uint32_t crow_events_get_queue_size(void);
extern bool crow_events_is_queue_full(void);

// Convenience functions for common event types
extern void crow_event_post_lua_callback(void (*callback)(int), int param);
extern void crow_event_post_slope_complete(int channel, crow_slope_callback_t callback);
extern void crow_event_post_detect_trigger(int channel, float value);
extern void crow_event_post_metro_tick(int metro_id, int stage);

// Event handlers (implemented by various subsystems)
extern void crow_event_handle_lua_callback(crow_event_t* e);
extern void crow_event_handle_slope_complete(crow_event_t* e);
extern void crow_event_handle_detect_trigger(crow_event_t* e);
extern void crow_event_handle_metro_tick(crow_event_t* e);
