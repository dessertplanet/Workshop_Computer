#include "crow_slopes_integration.h"
#include "pico/time.h"
#include <cstring>
#include <cstdio>
#include <cmath>

// Global configuration
bool crow_slopes_use_optimization = false;  // Start with original for safety
bool crow_slopes_benchmark_mode = false;

slopes_performance_t slopes_perf = {0};
slopes_config_t slopes_config = {
    .enable_lookup_tables = true,
    .enable_fixed_point = true,
    .enable_vector_ops = true,
    .enable_profiling = false,
    .accuracy_threshold = 0.001f  // 0.1% tolerance
};

// Internal state
static bool integration_initialized = false;
static uint32_t benchmark_start_time = 0;
static uint32_t benchmark_blocks = 0;

void crow_slopes_integration_init(void) {
    if (integration_initialized) return;
    
    printf("Initializing slopes integration layer...\n");
    
    // Initialize both systems
    crow_slopes_init();
    crow_slopes_opt_init();
    
    // Reset performance counters
    memset(&slopes_perf, 0, sizeof(slopes_perf));
    slopes_perf.performance_ratio = 1.0f;
    slopes_perf.last_measurement_time = time_us_32();
    
    integration_initialized = true;
    printf("Slopes integration layer initialized\n");
}

void crow_slopes_integration_deinit(void) {
    if (!integration_initialized) return;
    
    crow_slopes_deinit();
    crow_slopes_opt_deinit();
    integration_initialized = false;
}

// Enhanced block processing with performance monitoring and validation
void crow_slopes_process_block_enhanced(float* input_blocks[4], float* output_blocks[4], int block_size) {
    if (!integration_initialized) {
        crow_slopes_integration_init();
    }
    
    uint32_t start_time = time_us_32();
    
    if (crow_slopes_benchmark_mode) {
        // Benchmark mode - compare both implementations
        float original_out[4][block_size];
        float optimized_out[4][block_size];
        float* original_ptrs[4] = {original_out[0], original_out[1], original_out[2], original_out[3]};
        float* optimized_ptrs[4] = {optimized_out[0], optimized_out[1], optimized_out[2], optimized_out[3]};
        
        // Time original implementation
        uint32_t orig_start = time_us_32();
        crow_slopes_process_block(input_blocks, original_ptrs, block_size);
        uint32_t orig_end = time_us_32();
        
        // Time optimized implementation
        uint32_t opt_start = time_us_32();
        slopes_process_block_optimized(input_blocks, optimized_ptrs, block_size);
        uint32_t opt_end = time_us_32();
        
        // Update performance statistics
        slopes_perf.original_cycles += (orig_end - orig_start);
        slopes_perf.optimized_cycles += (opt_end - opt_start);
        slopes_perf.blocks_processed++;
        
        if (slopes_perf.original_cycles > 0) {
            slopes_perf.performance_ratio = (float)slopes_perf.optimized_cycles / (float)slopes_perf.original_cycles;
        }
        
        // Validate accuracy
        bool accurate = crow_slopes_validate_output(original_ptrs, optimized_ptrs, block_size);
        if (!accurate && slopes_config.enable_profiling) {
            printf("WARNING: Optimized slopes output differs from original\n");
        }
        
        // Use the selected output
        if (crow_slopes_use_optimization) {
            for (int ch = 0; ch < 4; ch++) {
                memcpy(output_blocks[ch], optimized_out[ch], block_size * sizeof(float));
            }
        } else {
            for (int ch = 0; ch < 4; ch++) {
                memcpy(output_blocks[ch], original_out[ch], block_size * sizeof(float));
            }
        }
        
    } else {
        // Normal operation mode - use selected implementation
        if (crow_slopes_use_optimization) {
            slopes_process_block_optimized(input_blocks, output_blocks, block_size);
            
            if (slopes_config.enable_profiling) {
                uint32_t end_time = time_us_32();
                slopes_perf.optimized_cycles += (end_time - start_time);
                slopes_perf.blocks_processed++;
            }
        } else {
            crow_slopes_process_block(input_blocks, output_blocks, block_size);
            
            if (slopes_config.enable_profiling) {
                uint32_t end_time = time_us_32();
                slopes_perf.original_cycles += (end_time - start_time);
                slopes_perf.blocks_processed++;
            }
        }
    }
}

// Benchmarking utilities
void crow_slopes_start_benchmark(void) {
    printf("Starting slopes benchmark...\n");
    crow_slopes_benchmark_mode = true;
    benchmark_start_time = time_us_32();
    benchmark_blocks = 0;
    
    // Reset performance counters
    memset(&slopes_perf, 0, sizeof(slopes_perf));
    slopes_perf.performance_ratio = 1.0f;
    slopes_perf.last_measurement_time = benchmark_start_time;
}

void crow_slopes_stop_benchmark(void) {
    crow_slopes_benchmark_mode = false;
    uint32_t end_time = time_us_32();
    uint32_t total_time = end_time - benchmark_start_time;
    
    printf("Benchmark completed after %u microseconds\n", total_time);
    crow_slopes_print_performance_stats();
}

void crow_slopes_print_performance_stats(void) {
    printf("\n=== Slopes Performance Statistics ===\n");
    printf("Blocks processed: %u\n", slopes_perf.blocks_processed);
    printf("Original cycles: %u us\n", slopes_perf.original_cycles);
    printf("Optimized cycles: %u us\n", slopes_perf.optimized_cycles);
    
    if (slopes_perf.original_cycles > 0) {
        float speedup = (float)slopes_perf.original_cycles / (float)slopes_perf.optimized_cycles;
        float cpu_saved = 100.0f * (1.0f - slopes_perf.performance_ratio);
        
        printf("Performance ratio: %.3f (optimized/original)\n", slopes_perf.performance_ratio);
        printf("Speedup: %.2fx\n", speedup);
        printf("CPU usage reduced by: %.1f%%\n", cpu_saved);
        
        if (slopes_perf.blocks_processed > 0) {
            float orig_per_block = (float)slopes_perf.original_cycles / slopes_perf.blocks_processed;
            float opt_per_block = (float)slopes_perf.optimized_cycles / slopes_perf.blocks_processed;
            printf("Original: %.1f us/block\n", orig_per_block);
            printf("Optimized: %.1f us/block\n", opt_per_block);
        }
    }
    printf("=====================================\n\n");
}

// Hot-swapping between implementations
void crow_slopes_enable_optimization(bool enable) {
    crow_slopes_use_optimization = enable;
    printf("Slopes optimization %s\n", enable ? "ENABLED" : "DISABLED");
}

bool crow_slopes_is_optimization_enabled(void) {
    return crow_slopes_use_optimization;
}

// Validate output accuracy between implementations
bool crow_slopes_validate_output(float* original_out[4], float* optimized_out[4], int block_size) {
    float max_error = 0.0f;
    int error_samples = 0;
    
    for (int ch = 0; ch < 4; ch++) {
        for (int i = 0; i < block_size; i++) {
            float orig = original_out[ch][i];
            float opt = optimized_out[ch][i];
            float error = fabsf(orig - opt);
            float relative_error = (orig != 0.0f) ? (error / fabsf(orig)) : error;
            
            if (relative_error > slopes_config.accuracy_threshold) {
                error_samples++;
                if (error > max_error) {
                    max_error = error;
                }
            }
        }
    }
    
    if (slopes_config.enable_profiling && error_samples > 0) {
        printf("Validation: %d samples exceed threshold, max error: %.6f\n", error_samples, max_error);
    }
    
    // Consider output valid if less than 1% of samples exceed threshold
    return (error_samples < (block_size * 4) / 100);
}

// Run comprehensive accuracy test
void crow_slopes_run_accuracy_test(void) {
    printf("Running slopes accuracy test...\n");
    
    if (!integration_initialized) {
        crow_slopes_integration_init();
    }
    
    const int test_block_size = 32;
    const int num_test_blocks = 100;
    float input_blocks[4][test_block_size];
    float original_out[4][test_block_size];
    float optimized_out[4][test_block_size];
    float* original_ptrs[4] = {original_out[0], original_out[1], original_out[2], original_out[3]};
    float* optimized_ptrs[4] = {optimized_out[0], optimized_out[1], optimized_out[2], optimized_out[3]};
    float* input_ptrs[4] = {input_blocks[0], input_blocks[1], input_blocks[2], input_blocks[3]};
    
    // Initialize test input (zeros for this test)
    memset(input_blocks, 0, sizeof(input_blocks));
    
    int passed_blocks = 0;
    float total_error = 0.0f;
    
    // Test various slope configurations
    for (int test = 0; test < num_test_blocks; test++) {
        // Set up test slopes with different parameters
        for (int ch = 0; ch < 4; ch++) {
            float dest = -5.0f + (float)(test % 20);  // -5V to +15V range
            float time_ms = 1.0f + (float)(test % 50);  // 1ms to 50ms
            crow_shape_t shape = (crow_shape_t)(test % (CROW_SHAPE_Rebound + 1));
            
            crow_slopes_toward(ch, dest, time_ms, shape, nullptr);
        }
        
        // Process one block with each implementation
        crow_slopes_process_block(input_ptrs, original_ptrs, test_block_size);
        slopes_process_block_optimized(input_ptrs, optimized_ptrs, test_block_size);
        
        // Validate accuracy
        bool accurate = crow_slopes_validate_output(original_ptrs, optimized_ptrs, test_block_size);
        if (accurate) {
            passed_blocks++;
        }
        
        // Calculate RMS error for this block
        float block_error = 0.0f;
        for (int ch = 0; ch < 4; ch++) {
            for (int i = 0; i < test_block_size; i++) {
                float error = original_out[ch][i] - optimized_out[ch][i];
                block_error += error * error;
            }
        }
        total_error += sqrtf(block_error / (4 * test_block_size));
    }
    
    float pass_rate = (float)passed_blocks / num_test_blocks * 100.0f;
    float avg_rms_error = total_error / num_test_blocks;
    
    printf("Accuracy test results:\n");
    printf("- Blocks passed: %d/%d (%.1f%%)\n", passed_blocks, num_test_blocks, pass_rate);
    printf("- Average RMS error: %.6f\n", avg_rms_error);
    printf("- Accuracy threshold: %.6f\n", slopes_config.accuracy_threshold);
    
    if (pass_rate > 95.0f) {
        printf("✓ Accuracy test PASSED\n");
    } else {
        printf("✗ Accuracy test FAILED\n");
    }
    printf("\n");
}

// Memory usage estimation
uint32_t crow_slopes_get_memory_usage(void) {
    // Estimate memory usage of original slopes system
    return sizeof(crow_slope_t) * CROW_SLOPE_CHANNELS + 1024;  // slopes + overhead
}

uint32_t crow_slopes_get_optimized_memory_usage(void) {
    // Estimate memory usage of optimized slopes system
    uint32_t lut_memory = 6 * SHAPE_LUT_SIZE * sizeof(int16_t);  // 6 lookup tables
    uint32_t slopes_memory = sizeof(optimized_slope_t) * CROW_SLOPE_CHANNELS;
    return lut_memory + slopes_memory + 512;  // LUTs + slopes + overhead
}

// Configuration management
void crow_slopes_set_config(const slopes_config_t* config) {
    if (config) {
        slopes_config = *config;
        printf("Slopes configuration updated\n");
    }
}

void crow_slopes_get_config(slopes_config_t* config) {
    if (config) {
        *config = slopes_config;
    }
}
