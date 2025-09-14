#include "crow_slopes_integration.h"
#include "pico/stdlib.h"
#include <cstdio>

// Test program to demonstrate slopes optimization
// This can be called from your main crow emulator or as a standalone test

void test_slopes_basic_functionality(void) {
    printf("\n=== Testing Basic Slopes Functionality ===\n");
    
    crow_slopes_integration_init();
    
    const int block_size = 32;
    float input_blocks[4][block_size] = {0};  // Zero input
    float output_blocks[4][block_size];
    float* input_ptrs[4] = {input_blocks[0], input_blocks[1], input_blocks[2], input_blocks[3]};
    float* output_ptrs[4] = {output_blocks[0], output_blocks[1], output_blocks[2], output_blocks[3]};
    
    // Test 1: Simple linear slope
    printf("Test 1: Linear slope 0V -> 5V over 10ms\n");
    crow_slopes_toward(0, 5.0f, 10.0f, CROW_SHAPE_Linear, nullptr);
    
    crow_slopes_enable_optimization(false);  // Test original first
    crow_slopes_process_block_enhanced(input_ptrs, output_ptrs, block_size);
    printf("Original - First sample: %.3f, Last sample: %.3f\n", output_blocks[0][0], output_blocks[0][block_size-1]);
    
    crow_slopes_enable_optimization(true);   // Test optimized
    crow_slopes_toward(0, 5.0f, 10.0f, CROW_SHAPE_Linear, nullptr); // Reset slope
    crow_slopes_process_block_enhanced(input_ptrs, output_ptrs, block_size);
    printf("Optimized - First sample: %.3f, Last sample: %.3f\n", output_blocks[0][0], output_blocks[0][block_size-1]);
    
    // Test 2: Exponential curve
    printf("\nTest 2: Exponential curve 0V -> 3V over 5ms\n");
    crow_slopes_toward(1, 3.0f, 5.0f, CROW_SHAPE_Expo, nullptr);
    crow_slopes_process_block_enhanced(input_ptrs, output_ptrs, block_size);
    printf("Exponential - First sample: %.3f, Last sample: %.3f\n", output_blocks[1][0], output_blocks[1][block_size-1]);
    
    printf("Basic functionality test completed\n");
}

void test_slopes_performance_comparison(void) {
    printf("\n=== Performance Comparison Test ===\n");
    
    // Enable profiling
    slopes_config_t config;
    crow_slopes_get_config(&config);
    config.enable_profiling = true;
    crow_slopes_set_config(&config);
    
    // Start benchmark mode
    crow_slopes_start_benchmark();
    
    const int block_size = 32;
    const int num_blocks = 1000;  // Process 1000 blocks for meaningful statistics
    float input_blocks[4][block_size] = {0};
    float output_blocks[4][block_size];
    float* input_ptrs[4] = {input_blocks[0], input_blocks[1], input_blocks[2], input_blocks[3]};
    float* output_ptrs[4] = {output_blocks[0], output_blocks[1], output_blocks[2], output_blocks[3]};
    
    printf("Processing %d blocks of %d samples each...\n", num_blocks, block_size);
    
    for (int block = 0; block < num_blocks; block++) {
        // Set up different slopes for variety
        if (block % 100 == 0) {
            for (int ch = 0; ch < 4; ch++) {
                float dest = -5.0f + (float)(block % 20);
                float time_ms = 1.0f + (float)(block % 50);
                crow_shape_t shape = (crow_shape_t)(block % (CROW_SHAPE_Rebound + 1));
                crow_slopes_toward(ch, dest, time_ms, shape, nullptr);
            }
        }
        
        crow_slopes_process_block_enhanced(input_ptrs, output_ptrs, block_size);
    }
    
    crow_slopes_stop_benchmark();
}

void test_slopes_accuracy(void) {
    printf("\n=== Accuracy Validation Test ===\n");
    
    // Enable detailed profiling for accuracy test
    slopes_config_t config;
    crow_slopes_get_config(&config);
    config.enable_profiling = true;
    config.accuracy_threshold = 0.001f;  // 0.1% tolerance
    crow_slopes_set_config(&config);
    
    crow_slopes_run_accuracy_test();
}

void test_shapes_lookup_tables(void) {
    printf("\n=== Shape Functions Lookup Table Test ===\n");
    
    // Test all shape functions against originals
    const int num_samples = 100;
    float max_error = 0.0f;
    
    printf("Testing shape functions accuracy:\n");
    
    for (int i = 0; i < num_samples; i++) {
        float x = (float)i / (float)(num_samples - 1);
        
        // Test sine
        float orig_sine = crow_shape_sine(x);
        float fast_sine = crow_shape_sine_fast(x);
        float sine_error = fabsf(orig_sine - fast_sine);
        if (sine_error > max_error) max_error = sine_error;
        
        // Test exponential
        float orig_exp = crow_shape_exp(x);
        float fast_exp = crow_shape_exp_fast(x);
        float exp_error = fabsf(orig_exp - fast_exp);
        if (exp_error > max_error) max_error = exp_error;
        
        // Test logarithmic
        float orig_log = crow_shape_log(x);
        float fast_log = crow_shape_log_fast(x);
        float log_error = fabsf(orig_log - fast_log);
        if (log_error > max_error) max_error = log_error;
    }
    
    printf("Maximum error across all shape functions: %.6f\n", max_error);
    
    if (max_error < 0.01f) {
        printf("✓ Shape function accuracy test PASSED (< 1%% error)\n");
    } else {
        printf("✗ Shape function accuracy test FAILED (> 1%% error)\n");
    }
}

void test_memory_usage(void) {
    printf("\n=== Memory Usage Comparison ===\n");
    
    uint32_t original_memory = crow_slopes_get_memory_usage();
    uint32_t optimized_memory = crow_slopes_get_optimized_memory_usage();
    
    printf("Original slopes memory usage: %u bytes\n", original_memory);
    printf("Optimized slopes memory usage: %u bytes\n", optimized_memory);
    printf("Memory overhead: %d bytes (%.1f%%)\n", 
           (int)(optimized_memory - original_memory),
           100.0f * (float)(optimized_memory - original_memory) / original_memory);
    
    // Memory breakdown
    uint32_t lut_memory = 6 * 256 * sizeof(int16_t);  // 6 lookup tables * 256 entries * 2 bytes
    printf("Lookup tables memory: %u bytes\n", lut_memory);
    printf("Additional overhead: %u bytes\n", optimized_memory - original_memory - lut_memory);
}

// Main test function - call this from your crow emulator
void run_slopes_optimization_tests(void) {
    printf("\n\n========================================\n");
    printf("    CROW SLOPES OPTIMIZATION TESTS\n");
    printf("========================================\n");
    
    test_slopes_basic_functionality();
    test_shapes_lookup_tables();
    test_memory_usage();
    test_slopes_accuracy();
    test_slopes_performance_comparison();
    
    printf("\n========================================\n");
    printf("    ALL TESTS COMPLETED\n");
    printf("========================================\n\n");
    
    // Leave optimization enabled for regular use
    crow_slopes_enable_optimization(true);
    printf("Slopes optimization is now ENABLED for regular operation\n");
}

// Quick benchmark function for integration into audio loop
void quick_slopes_benchmark(int num_blocks) {
    if (num_blocks <= 0) num_blocks = 100;
    
    printf("Running quick benchmark (%d blocks)...\n", num_blocks);
    
    crow_slopes_start_benchmark();
    
    const int block_size = 32;
    float input_blocks[4][block_size] = {0};
    float output_blocks[4][block_size];
    float* input_ptrs[4] = {input_blocks[0], input_blocks[1], input_blocks[2], input_blocks[3]};
    float* output_ptrs[4] = {output_blocks[0], output_blocks[1], output_blocks[2], output_blocks[3]};
    
    // Set up some test slopes
    crow_slopes_toward(0, 5.0f, 10.0f, CROW_SHAPE_Expo, nullptr);
    crow_slopes_toward(1, -3.0f, 8.0f, CROW_SHAPE_Sine, nullptr);
    crow_slopes_toward(2, 7.0f, 15.0f, CROW_SHAPE_Log, nullptr);
    crow_slopes_toward(3, 2.0f, 5.0f, CROW_SHAPE_Linear, nullptr);
    
    for (int i = 0; i < num_blocks; i++) {
        crow_slopes_process_block_enhanced(input_ptrs, output_ptrs, block_size);
    }
    
    crow_slopes_stop_benchmark();
}
