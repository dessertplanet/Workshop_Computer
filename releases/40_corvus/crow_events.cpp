#include "crow_events.h"
#include "crow_lua.h"
#include <cstdio>
#include <cstring>

extern "C" {
    #include "wrQueue.h"
}

// Event storage array and index-based queue
static crow_event_t event_storage[CROW_MAX_EVENTS];
static queue_t* event_queue = nullptr;
static bool events_initialized = false;

// Statistics
static uint32_t events_posted = 0;
static uint32_t events_processed = 0;
static uint32_t events_dropped = 0;

// Initialize the event system
void crow_events_init(void) {
    if (events_initialized) {
        return;
    }
    
    printf("Initializing crow event system (queue size: %d)...\n", CROW_MAX_EVENTS);
    
    // Create event queue using wrQueue (index-based)
    event_queue = queue_init(CROW_MAX_EVENTS);
    if (!event_queue) {
        printf("ERROR: Failed to create event queue\n");
        return;
    }
    
    // Clear event storage
    memset(event_storage, 0, sizeof(event_storage));
    
    events_initialized = true;
    events_posted = 0;
    events_processed = 0;
    events_dropped = 0;
    
    printf("Crow event system initialized\n");
}

void crow_events_deinit(void) {
    if (!events_initialized) {
        return;
    }
    
    if (event_queue) {
        queue_deinit(event_queue);
        event_queue = nullptr;
    }
    
    printf("Event system stats - Posted: %u, Processed: %u, Dropped: %u\n",
           events_posted, events_processed, events_dropped);
    
    events_initialized = false;
}

void crow_events_clear(void) {
    if (!events_initialized || !event_queue) {
        return;
    }
    
    // Dequeue all events (just indices)
    while (queue_front(event_queue) != -1) {
        queue_dequeue(event_queue);
    }
    
    printf("Event queue cleared\n");
}

// Post an event to the queue
bool crow_event_post(crow_event_t* e) {
    if (!events_initialized || !event_queue || !e) {
        return false;
    }
    
    // Get an index for storing the event
    int index = queue_enqueue(event_queue);
    if (index == -1) {
        // Queue is full
        events_dropped++;
        printf("WARNING: Event queue full! Dropping event (total dropped: %u)\n", events_dropped);
        return false;
    }
    
    // Add timestamp if not set
    if (e->timestamp == 0) {
        // For now, use a simple counter. Future: use precise timing
        static uint32_t event_counter = 0;
        e->timestamp = ++event_counter;
    }
    
    // Store event data at the allocated index
    event_storage[index] = *e;
    events_posted++;
    return true;
}

// Process the next event in the queue
void crow_event_process_next(void) {
    if (!events_initialized || !event_queue) {
        return;
    }
    
    // Get next event index
    int index = queue_front(event_queue);
    if (index == -1) {
        // No events in queue
        return;
    }
    
    // Remove from queue
    queue_dequeue(event_queue);
    events_processed++;
    
    // Get the event and call its handler
    crow_event_t* event = &event_storage[index];
    if (event->handler) {
        event->handler(event);
    } else {
        printf("WARNING: Event with null handler processed\n");
    }
}

// Process all pending events in the queue
void crow_events_process_all(void) {
    if (!events_initialized || !event_queue) {
        return;
    }
    
    // Process all events currently in queue
    // Use a safety counter to prevent infinite loops
    int safety_counter = CROW_MAX_EVENTS * 2;  // Allow double the queue size
    
    while (queue_front(event_queue) != -1 && safety_counter > 0) {
        crow_event_process_next();
        safety_counter--;
    }
    
    if (safety_counter <= 0) {
        printf("WARNING: Event processing safety limit reached!\n");
    }
}

// Get current queue size
uint32_t crow_events_get_queue_size(void) {
    if (!events_initialized || !event_queue) {
        return 0;
    }
    
    return event_queue->count;
}

// Check if queue is full
bool crow_events_is_queue_full(void) {
    if (!events_initialized || !event_queue) {
        return true;  // Treat uninitialized as full
    }
    
    return event_queue->count >= event_queue->length;
}

// Convenience function: Post lua callback event
void crow_event_post_lua_callback(void (*callback)(int), int param) {
    crow_event_t event;
    event.handler = crow_event_handle_lua_callback;
    event.index.p = (void*)callback;
    event.data.i = param;
    event.timestamp = 0;  // Will be set automatically
    
    crow_event_post(&event);
}

// Convenience function: Post slope complete event
void crow_event_post_slope_complete(int channel, crow_slope_callback_t callback) {
    crow_event_t event;
    event.handler = crow_event_handle_slope_complete;
    event.index.i = channel;
    event.data.p = (void*)callback;
    event.timestamp = 0;
    
    crow_event_post(&event);
}

// Convenience function: Post detect trigger event
void crow_event_post_detect_trigger(int channel, float value) {
    crow_event_t event;
    event.handler = crow_event_handle_detect_trigger;
    event.index.i = channel;
    event.data.f = value;
    event.timestamp = 0;
    
    crow_event_post(&event);
}

// Convenience function: Post metro tick event
void crow_event_post_metro_tick(int metro_id, int stage) {
    crow_event_t event;
    event.handler = crow_event_handle_metro_tick;
    event.index.i = metro_id;
    event.data.i = stage;
    event.timestamp = 0;
    
    crow_event_post(&event);
}

// Event handlers - these will be implemented by the respective subsystems

void crow_event_handle_lua_callback(crow_event_t* e) {
    if (!e || !e->index.p) {
        printf("WARNING: Invalid lua callback event\n");
        return;
    }
    
    // Cast back to function pointer and call it
    void (*callback)(int) = (void (*)(int))e->index.p;
    callback(e->data.i);
}

void crow_event_handle_slope_complete(crow_event_t* e) {
    if (!e) {
        printf("WARNING: Invalid slope complete event\n");
        return;
    }
    
    int channel = e->index.i;
    crow_slope_callback_t callback = (crow_slope_callback_t)e->data.p;
    
    printf("Event: Slope complete on channel %d\n", channel);
    
    // Call the original callback if present
    if (callback) {
        callback(channel);
    }
}

void crow_event_handle_detect_trigger(crow_event_t* e) {
    if (!e) {
        printf("WARNING: Invalid detect trigger event\n");
        return;
    }
    
    int channel = e->index.i;
    float value = e->data.f;
    printf("Event: Detect trigger on channel %d, value %.3f\n", channel, value);
    
    // Future: Forward to lua system for input[n].change callbacks
    // For now, just log the event
}

void crow_event_handle_metro_tick(crow_event_t* e) {
    if (!e) {
        printf("WARNING: Invalid metro tick event\n");
        return;
    }
    
    int metro_id = e->index.i;
    int stage = e->data.i;
    
    printf("Event: Metro %d tick at stage %d\n", metro_id, stage);
    
    // Forward to lua system for metro_handler callback
    if (g_crow_lua) {
        g_crow_lua->call_metro_handler(metro_id, stage);
    }
}
