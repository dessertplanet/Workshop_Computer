#pragma once

#include "crow_slopes.h"
#include "crow_slopes_optimized.h"

// Integration layer for testing optimized slopes
// This allows switching between original and optimized processing

// Configuration flags
extern bool crow_slopes_use_optimization;
extern bool crow_slopes_benchmark_mode;

// Timing and performance tracking
typedef struct {
    uint32_t original_cycles;
    uint32_t optimized_cycles;
    uint32_t blocks_processed;
    float performance_ratio;  // optimized/original (lower is better)
    uint32_t last_measurement_time;
} slopes_performance_t;

extern slopes_performance_t slopes_perf;

// Integration functions
void crow_slopes_integration_init(void);
void crow_slopes_integration_deinit(void);

// Enhanced block processing with performance monitoring
void crow_slopes_process_block_enhanced(float* input_blocks[4], float* output_blocks[4], int block_size);

// Benchmarking utilities
void crow_slopes_start_benchmark(void);
void crow_slopes_stop_benchmark(void);
void crow_slopes_print_performance_stats(void);

// Hot-swapping between implementations
void crow_slopes_enable_optimization(bool enable);
bool crow_slopes_is_optimization_enabled(void);

// Performance validation
bool crow_slopes_validate_output(float* original_out[4], float* optimized_out[4], int block_size);
void crow_slopes_run_accuracy_test(void);

// Memory usage comparison
uint32_t crow_slopes_get_memory_usage(void);
uint32_t crow_slopes_get_optimized_memory_usage(void);

// Configuration
typedef struct {
    bool enable_lookup_tables;
    bool enable_fixed_point;
    bool enable_vector_ops;
    bool enable_profiling;
    float accuracy_threshold;  // For validation (e.g., 0.001f for 0.1% tolerance)
} slopes_config_t;

extern slopes_config_t slopes_config;

void crow_slopes_set_config(const slopes_config_t* config);
void crow_slopes_get_config(slopes_config_t* config);
