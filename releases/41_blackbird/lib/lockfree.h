#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef PICO_BUILD
#include "pico/sync.h"
#include "hardware/sync.h"
#endif

// Lock-free atomic operations for RP2040
// Uses ARM Cortex-M0+ LDREX/STREX instructions for true atomics

#ifdef PICO_BUILD

// 32-bit atomic load
static inline int32_t atomic_load_32(volatile int32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

// 32-bit atomic store
static inline void atomic_store_32(volatile int32_t* ptr, int32_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

// 32-bit atomic compare-and-swap
static inline bool atomic_cas_32(volatile int32_t* ptr, int32_t expected, int32_t desired) {
    return __atomic_compare_exchange_n(ptr, &expected, desired, false, 
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// 32-bit atomic fetch-and-add
static inline int32_t atomic_fetch_add_32(volatile int32_t* ptr, int32_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_ACQ_REL);
}

// 8-bit atomic operations for queue indices
static inline uint8_t atomic_load_8(volatile uint8_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static inline void atomic_store_8(volatile uint8_t* ptr, uint8_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

static inline bool atomic_cas_8(volatile uint8_t* ptr, uint8_t expected, uint8_t desired) {
    return __atomic_compare_exchange_n(ptr, &expected, desired, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// Memory barriers
#define MEMORY_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define LOAD_BARRIER() __atomic_thread_fence(__ATOMIC_ACQUIRE)  
#define STORE_BARRIER() __atomic_thread_fence(__ATOMIC_RELEASE)

#else
// Non-RP2040 fallback (development/testing)
#define atomic_load_32(ptr) (*(ptr))
#define atomic_store_32(ptr, val) (*(ptr) = (val))
#define atomic_cas_32(ptr, exp, des) (*(ptr) == (exp) ? (*(ptr) = (des), true) : false)
#define atomic_fetch_add_32(ptr, val) (*(ptr) += (val), *(ptr) - (val))
#define atomic_load_8(ptr) (*(ptr))
#define atomic_store_8(ptr, val) (*(ptr) = (val))
#define atomic_cas_8(ptr, exp, des) (*(ptr) == (exp) ? (*(ptr) = (des), true) : false)
#define MEMORY_BARRIER() ((void)0)
#define LOAD_BARRIER() ((void)0)
#define STORE_BARRIER() ((void)0)
#endif

// Lock-free output state structure
typedef struct {
    volatile int32_t values[4];  // Output voltages in millivolts
    volatile uint32_t version;   // Version counter for consistency
} lockfree_output_state_t;

// Lock-free circular buffer for events (single producer, single consumer)
#define LOCKFREE_EVENT_QUEUE_SIZE 64  // Power of 2 for efficient modulo

typedef struct {
    volatile uint8_t head;      // Producer index
    volatile uint8_t tail;      // Consumer index  
    volatile uint32_t sequence[LOCKFREE_EVENT_QUEUE_SIZE]; // Sequence numbers
    void* data[LOCKFREE_EVENT_QUEUE_SIZE];  // Event data
} lockfree_event_queue_t;

// Lock-free output state functions
void lockfree_output_init(lockfree_output_state_t* state);
void lockfree_output_set(lockfree_output_state_t* state, int channel, int32_t value_mv);
int32_t lockfree_output_get(lockfree_output_state_t* state, int channel);
bool lockfree_output_get_all(lockfree_output_state_t* state, int32_t values[4]);

// Lock-free event queue functions  
void lockfree_queue_init(lockfree_event_queue_t* queue);
bool lockfree_queue_enqueue(lockfree_event_queue_t* queue, void* data);
bool lockfree_queue_dequeue(lockfree_event_queue_t* queue, void** data);
uint8_t lockfree_queue_size(lockfree_event_queue_t* queue);
bool lockfree_queue_is_empty(lockfree_event_queue_t* queue);
bool lockfree_queue_is_full(lockfree_event_queue_t* queue);
