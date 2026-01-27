/*
 * Lock-free ring buffer for audio samples between cores
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define AUDIO_BUFFER_SIZE 4096  // Power of 2 for efficiency

typedef struct {
    uint32_t buffer[AUDIO_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} audio_ring_buffer_t;

static inline void rb_init(audio_ring_buffer_t *rb) {
    rb->head = 0;
    rb->tail = 0;
}

static inline void rb_clear(audio_ring_buffer_t *rb) {
    rb->head = 0;
    rb->tail = 0;
}

static inline bool rb_is_empty(audio_ring_buffer_t *rb) {
    return rb->head == rb->tail;
}

static inline bool rb_is_full(audio_ring_buffer_t *rb) {
    return ((rb->head + 1) % AUDIO_BUFFER_SIZE) == rb->tail;
}

static inline bool rb_push(audio_ring_buffer_t *rb, uint32_t data) {
    uint32_t next = (rb->head + 1) % AUDIO_BUFFER_SIZE;
    if (next == rb->tail) {
        return false; // Buffer full
    }
    rb->buffer[rb->head] = data;
    __sync_synchronize(); // Memory barrier
    rb->head = next;
    return true;
}

static inline bool rb_pop(audio_ring_buffer_t *rb, uint32_t *data) {
    if (rb->head == rb->tail) {
        return false; // Buffer empty
    }
    *data = rb->buffer[rb->tail];
    __sync_synchronize(); // Memory barrier
    rb->tail = (rb->tail + 1) % AUDIO_BUFFER_SIZE;
    return true;
}

static inline uint32_t rb_count(audio_ring_buffer_t *rb) {
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    } else {
        return AUDIO_BUFFER_SIZE - (rb->tail - rb->head);
    }
}

#endif // RING_BUFFER_H
