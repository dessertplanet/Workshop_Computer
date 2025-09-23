// events.c adapted from github.com/monome/libavr32
// Enhanced with proper multicore synchronization for RP2040

#include <stdio.h>
#include "events.h"
#include "caw.h" // Caw_send_luachunk

// RP2040 multicore protection - CRITICAL for thread safety!
#ifdef PICO_BUILD
#include "pico/sync.h"
#include "pico/mutex.h"

// Global mutex for event queue protection
static mutex_t event_queue_mutex;
static bool mutex_initialized = false;

#define BLOCK_IRQS(code) do { \
    uint32_t irq_state = save_and_disable_interrupts(); \
    code \
    restore_interrupts(irq_state); \
} while(0)

// Multicore-safe critical sections
#define MULTICORE_SAFE(code) do { \
    if (mutex_initialized) { \
        mutex_enter_blocking(&event_queue_mutex); \
        code \
        mutex_exit(&event_queue_mutex); \
    } else { \
        BLOCK_IRQS(code); \
    } \
} while(0)

#else
// Original implementation would have proper IRQ blocking here
#define BLOCK_IRQS(code) do { code } while(0)
#define MULTICORE_SAFE(code) BLOCK_IRQS(code)
#endif


/// NOTE: if we are ever over-filling the event queue, we have problems.
/// making the event queue bigger not likely to solve the problems.
#define MAX_EVENTS   40

// macro for incrementing an index into a circular buffer.
#define INCR_EVENT_INDEX( x )  { if ( ++x == MAX_EVENTS ) x = 0; }

// get/put indexes into sysEvents[] array
volatile static int putIdx = 0;
volatile static int getIdx = 0;

// the system event queue is a circular array of event records
// NOTE be aware of event_t and MAX_EVENTS for RAM usage
volatile static event_t sysEvents[ MAX_EVENTS ];

// Enhanced monitoring and statistics
static event_stats_t event_statistics = {0};
static uint32_t event_counter = 0;

// initialize event handler
void events_init() {
    printf("\ninitializing event handler with multicore safety\n");

#ifdef PICO_BUILD
    // Initialize mutex for multicore protection
    mutex_init(&event_queue_mutex);
    mutex_initialized = true;
    printf("Event queue mutex initialized\n");
#endif

    events_clear();
}

void events_clear(void)
{
    // set queue (circular list) to empty
    putIdx = 0;
    getIdx = 0;

    // zero out the event records
    for ( int k = 0; k < MAX_EVENTS; k++ ) {
        sysEvents[ k ].data.i  = 0;
        sysEvents[ k ].handler = NULL;
    }
}

// get next event
// returns non-zero if an event was available
void event_next( void ){
    event_t* e = NULL;
    int processedIdx = -1;

    MULTICORE_SAFE(
        // if pointers are equal, the queue is empty... don't allow idx's to wrap!
        if ( getIdx != putIdx ) {
            // CORRECT: Get event at current position, THEN increment
            e = (event_t*)&sysEvents[ getIdx ];
            processedIdx = getIdx;  // Save for debug output
            INCR_EVENT_INDEX( getIdx );
        }
    );

    if( e != NULL ){ 
        printf("EVENT NEXT: processing idx=%d, getIdx now=%d, putIdx=%d\n", 
               processedIdx, getIdx, putIdx);
        (*e->handler)(e); // call the event handler after enabling IRQs
    }
}


// add event to queue, return success status
uint8_t event_post( event_t *e ) {
    uint8_t status = 0;

    MULTICORE_SAFE(
        // Store event at CURRENT putIdx position, then increment
        int nextIdx = putIdx;
        INCR_EVENT_INDEX( nextIdx );
        
        if ( nextIdx != getIdx  ) {
            // Store event at the CURRENT putIdx position (before increment)
            sysEvents[ putIdx ].handler = e->handler;
            sysEvents[ putIdx ].index   = e->index;
            sysEvents[ putIdx ].data    = e->data;
            
            // Now advance putIdx for next event
            putIdx = nextIdx;
            status = 1;
            
            // Debug output to track queue operations
            printf("EVENT POST: stored at idx=%d, putIdx now=%d, getIdx=%d\n", 
                   (nextIdx == 0) ? MAX_EVENTS - 1 : nextIdx - 1, putIdx, getIdx);
        }
        // If queue is full, nextIdx == getIdx, so putIdx stays unchanged
    );

    if( !status ){
        printf("event queue full! putIdx=%d, getIdx=%d\n", putIdx, getIdx);
        Caw_send_luachunk("event queue full!");
        event_statistics.events_dropped++;
        event_statistics.queue_overflows++;
    } else {
        // Update statistics for successful post
        if (e->type < EVENT_TYPE_COUNT) {
            event_statistics.events_posted[e->type]++;
        }
        
        // Update queue depth tracking
        int current_depth = (putIdx >= getIdx) ? (putIdx - getIdx) : (MAX_EVENTS - getIdx + putIdx);
        event_statistics.current_queue_depth = current_depth;
        if (current_depth > event_statistics.max_queue_depth) {
            event_statistics.max_queue_depth = current_depth;
        }
    }

    return status;
}

// Enhanced monitoring and safety functions
event_stats_t* events_get_stats(void) {
    return &event_statistics;
}

void events_reset_stats(void) {
    MULTICORE_SAFE(
        for (int i = 0; i < EVENT_TYPE_COUNT; i++) {
            event_statistics.events_posted[i] = 0;
            event_statistics.events_processed[i] = 0;
        }
        event_statistics.events_dropped = 0;
        event_statistics.queue_overflows = 0;
        event_statistics.max_queue_depth = 0;
        event_counter = 0;
    );
}

uint8_t events_get_queue_depth(void) {
    uint8_t depth = 0;
    MULTICORE_SAFE(
        depth = (putIdx >= getIdx) ? (putIdx - getIdx) : (MAX_EVENTS - getIdx + putIdx);
    );
    return depth;
}

uint8_t events_is_queue_healthy(void) {
    uint8_t depth = events_get_queue_depth();
    // Consider queue healthy if less than 75% full
    return (depth < (MAX_EVENTS * 3 / 4)) ? 1 : 0;
}

void events_print_stats(void) {
    printf("=== EVENT SYSTEM STATISTICS ===\n");
    printf("Queue: %d/%d (max: %d)\n", 
           event_statistics.current_queue_depth, MAX_EVENTS, event_statistics.max_queue_depth);
    printf("Overflows: %d, Dropped: %d\n", 
           event_statistics.queue_overflows, event_statistics.events_dropped);
    printf("Events by type:\n");
    
    const char* type_names[] = {
        "CHANGE", "STREAM", "LUA_CALL", "OUTPUT", "SYSTEM", "DEBUG"
    };
    
    for (int i = 0; i < EVENT_TYPE_COUNT; i++) {
        printf("  %s: posted=%d, processed=%d\n", 
               type_names[i], event_statistics.events_posted[i], event_statistics.events_processed[i]);
    }
    printf("Health: %s\n", events_is_queue_healthy() ? "HEALTHY" : "OVERLOADED");
    printf("==============================\n");
}
