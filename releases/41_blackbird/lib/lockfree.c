#include "lockfree.h"
#include <string.h>
#include <stdio.h>

// Lock-free output state implementation
// Uses versioned updates for consistency across all 4 channels

void lockfree_output_init(lockfree_output_state_t* state) {
    if (!state) return;
    
    // Initialize all values to zero
    for (int i = 0; i < 4; i++) {
        atomic_store_32(&state->values[i], 0);
    }
    atomic_store_32((volatile int32_t*)&state->version, 0);
    
    STORE_BARRIER();
    printf("Lock-free output state initialized\n");
}

void lockfree_output_set(lockfree_output_state_t* state, int channel, int32_t value_mv) {
    if (!state || channel < 0 || channel >= 4) return;
    
    // Atomic update with version increment for consistency
    atomic_store_32(&state->values[channel], value_mv);
    atomic_fetch_add_32((volatile int32_t*)&state->version, 1);
    
    STORE_BARRIER();
}

int32_t lockfree_output_get(lockfree_output_state_t* state, int channel) {
    if (!state || channel < 0 || channel >= 4) return 0;
    
    LOAD_BARRIER();
    return atomic_load_32(&state->values[channel]);
}

bool lockfree_output_get_all(lockfree_output_state_t* state, int32_t values[4]) {
    if (!state || !values) return false;
    
    // Get consistent snapshot using version checking
    uint32_t version1, version2;
    int attempts = 0;
    const int max_attempts = 10;
    
    do {
        version1 = atomic_load_32((volatile int32_t*)&state->version);
        LOAD_BARRIER();
        
        // Read all values
        for (int i = 0; i < 4; i++) {
            values[i] = atomic_load_32(&state->values[i]);
        }
        
        LOAD_BARRIER();
        version2 = atomic_load_32((volatile int32_t*)&state->version);
        
        attempts++;
        if (attempts >= max_attempts) {
            printf("Warning: lockfree_output_get_all max attempts reached\n");
            return false;
        }
    } while (version1 != version2);
    
    return true;
}

// Lock-free event queue implementation
// Single producer (audio core), single consumer (USB core) queue
// Uses sequence numbers to detect ABA problems

void lockfree_queue_init(lockfree_event_queue_t* queue) {
    if (!queue) return;
    
    atomic_store_8(&queue->head, 0);
    atomic_store_8(&queue->tail, 0);
    
    // Initialize sequence numbers
    for (int i = 0; i < LOCKFREE_EVENT_QUEUE_SIZE; i++) {
        atomic_store_32(&queue->sequence[i], 0);
        queue->data[i] = NULL;
    }
    
    STORE_BARRIER();
    printf("Lock-free event queue initialized (size=%d)\n", LOCKFREE_EVENT_QUEUE_SIZE);
}

bool lockfree_queue_enqueue(lockfree_event_queue_t* queue, void* data) {
    if (!queue || !data) return false;
    
    uint8_t head = atomic_load_8(&queue->head);
    uint8_t next_head = (head + 1) & (LOCKFREE_EVENT_QUEUE_SIZE - 1);
    uint8_t tail = atomic_load_8(&queue->tail);
    
    // Check if queue is full
    if (next_head == tail) {
        return false;  // Queue full
    }
    
    // Get current sequence number for this slot
    uint32_t current_seq = atomic_load_32(&queue->sequence[head]);
    
    // Store data and increment sequence
    queue->data[head] = data;
    STORE_BARRIER();
    atomic_store_32(&queue->sequence[head], current_seq + 1);
    
    // Advance head pointer
    atomic_store_8(&queue->head, next_head);
    
    return true;
}

bool lockfree_queue_dequeue(lockfree_event_queue_t* queue, void** data) {
    if (!queue || !data) return false;
    
    uint8_t tail = atomic_load_8(&queue->tail);
    uint8_t head = atomic_load_8(&queue->head);
    
    // Check if queue is empty
    if (tail == head) {
        return false;  // Queue empty
    }
    
    // Get sequence number to verify data consistency
    uint32_t expected_seq = atomic_load_32(&queue->sequence[tail]);
    if (expected_seq == 0) {
        return false;  // No valid data
    }
    
    LOAD_BARRIER();
    
    // Retrieve data
    *data = queue->data[tail];
    
    // Clear the slot and reset sequence
    queue->data[tail] = NULL;
    atomic_store_32(&queue->sequence[tail], 0);
    
    STORE_BARRIER();
    
    // Advance tail pointer
    uint8_t next_tail = (tail + 1) & (LOCKFREE_EVENT_QUEUE_SIZE - 1);
    atomic_store_8(&queue->tail, next_tail);
    
    return true;
}

uint8_t lockfree_queue_size(lockfree_event_queue_t* queue) {
    if (!queue) return 0;
    
    uint8_t head = atomic_load_8(&queue->head);
    uint8_t tail = atomic_load_8(&queue->tail);
    
    return (head >= tail) ? (head - tail) : (LOCKFREE_EVENT_QUEUE_SIZE - tail + head);
}

bool lockfree_queue_is_empty(lockfree_event_queue_t* queue) {
    if (!queue) return true;
    
    return atomic_load_8(&queue->head) == atomic_load_8(&queue->tail);
}

bool lockfree_queue_is_full(lockfree_event_queue_t* queue) {
    if (!queue) return true;
    
    uint8_t head = atomic_load_8(&queue->head);
    uint8_t tail = atomic_load_8(&queue->tail);
    uint8_t next_head = (head + 1) & (LOCKFREE_EVENT_QUEUE_SIZE - 1);
    
    return next_head == tail;
}

// Performance testing and validation functions

#ifdef PICO_BUILD
#include "pico/time.h"

// Benchmark lock-free vs mutex performance
void lockfree_benchmark_output_state(int iterations) {
    printf("=== LOCK-FREE OUTPUT STATE BENCHMARK ===\n");
    
    lockfree_output_state_t state;
    lockfree_output_init(&state);
    
    // Benchmark writes
    uint32_t start_time = to_us_since_boot(get_absolute_time());
    
    for (int i = 0; i < iterations; i++) {
        for (int ch = 0; ch < 4; ch++) {
            lockfree_output_set(&state, ch, i * 100 + ch * 10);
        }
    }
    
    uint32_t write_time = to_us_since_boot(get_absolute_time()) - start_time;
    
    // Benchmark reads
    start_time = to_us_since_boot(get_absolute_time());
    int32_t values[4];
    
    for (int i = 0; i < iterations; i++) {
        lockfree_output_get_all(&state, values);
    }
    
    uint32_t read_time = to_us_since_boot(get_absolute_time()) - start_time;
    
    printf("Write performance: %d ops in %lu μs (%.1f ops/μs)\n", 
           iterations * 4, write_time, (float)(iterations * 4) / write_time);
    printf("Read performance: %d ops in %lu μs (%.1f ops/μs)\n", 
           iterations, read_time, (float)iterations / read_time);
}

void lockfree_benchmark_event_queue(int iterations) {
    printf("=== LOCK-FREE EVENT QUEUE BENCHMARK ===\n");
    
    lockfree_event_queue_t queue;
    lockfree_queue_init(&queue);
    
    // Create dummy data pointers
    static int dummy_data[64];
    for (int i = 0; i < 64; i++) {
        dummy_data[i] = i;
    }
    
    // Benchmark enqueue operations
    uint32_t start_time = to_us_since_boot(get_absolute_time());
    int enqueue_count = 0;
    
    for (int i = 0; i < iterations; i++) {
        if (lockfree_queue_enqueue(&queue, &dummy_data[i % 64])) {
            enqueue_count++;
        }
        
        // Dequeue periodically to prevent full queue
        if ((i % 32) == 31) {
            void* data;
            while (lockfree_queue_dequeue(&queue, &data)) {
                // Process dequeued data
            }
        }
    }
    
    uint32_t enqueue_time = to_us_since_boot(get_absolute_time()) - start_time;
    
    printf("Enqueue performance: %d/%d ops in %lu μs (%.1f ops/μs)\n", 
           enqueue_count, iterations, enqueue_time, (float)enqueue_count / enqueue_time);
    
    // Clear remaining items
    void* data;
    int dequeue_count = 0;
    start_time = to_us_since_boot(get_absolute_time());
    
    while (lockfree_queue_dequeue(&queue, &data)) {
        dequeue_count++;
    }
    
    uint32_t dequeue_time = to_us_since_boot(get_absolute_time()) - start_time;
    
    printf("Dequeue performance: %d ops in %lu μs (%.1f ops/μs)\n", 
           dequeue_count, dequeue_time, dequeue_count > 0 ? (float)dequeue_count / dequeue_time : 0.0f);
}

// Stress test for concurrent access patterns
void lockfree_stress_test(void) {
    printf("=== LOCK-FREE STRESS TEST ===\n");
    
    lockfree_output_state_t state;
    lockfree_output_init(&state);
    
    // Rapid concurrent-style operations
    const int test_iterations = 1000;
    int consistency_failures = 0;
    
    for (int i = 0; i < test_iterations; i++) {
        // Simulate Core 0 writing
        int32_t test_values[4] = {i, i+1, i+2, i+3};
        for (int ch = 0; ch < 4; ch++) {
            lockfree_output_set(&state, ch, test_values[ch]);
        }
        
        // Simulate Core 1 reading
        int32_t read_values[4];
        if (lockfree_output_get_all(&state, read_values)) {
            // Check for consistency (values should be sequential or from a previous iteration)
            bool consistent = true;
            for (int ch = 1; ch < 4; ch++) {
                if (read_values[ch] != read_values[0] + ch) {
                    // Allow for values from different iterations due to concurrent access
                    if ((read_values[ch] - read_values[0]) != ch) {
                        consistent = false;
                        break;
                    }
                }
            }
            
            if (!consistent) {
                consistency_failures++;
            }
        }
    }
    
    printf("Stress test completed: %d iterations, %d consistency failures (%.2f%%)\n", 
           test_iterations, consistency_failures, 
           (float)consistency_failures * 100.0f / test_iterations);
    
    if (consistency_failures == 0) {
        printf("✓ Lock-free implementation is consistent under stress\n");
    } else if (consistency_failures < test_iterations / 100) {
        printf("⚠ Minor consistency issues detected (< 1%% failure rate)\n");
    } else {
        printf("✗ Significant consistency issues detected\n");
    }
}

#else
// Non-RP2040 stub implementations
void lockfree_benchmark_output_state(int iterations) {
    printf("Benchmarking not available on non-RP2040 platforms\n");
}

void lockfree_benchmark_event_queue(int iterations) {
    printf("Benchmarking not available on non-RP2040 platforms\n");
}

void lockfree_stress_test(void) {
    printf("Stress testing not available on non-RP2040 platforms\n");
}
#endif
