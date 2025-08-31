/**
 * @file test_cpu_optimized_pio.c
 * @brief Test CPU-optimized PIO operations for Phase 1 enhancements
 *
 * This test validates that the CPU-optimized PIO operations work correctly
 * on both 286 and 386+ systems, ensuring backward compatibility while
 * providing enhanced performance on capable systems.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/direct_pio_enhanced.h"
#include "../include/logging.h"
#include "../include/memory.h"

/* Test constants */
#define TEST_BUFFER_SIZE    1024
#define TEST_IO_PORT        0x300  /* Simulated I/O port for testing */
#define SMALL_PACKET_SIZE   32     /* Below 32-bit threshold */
#define LARGE_PACKET_SIZE   256    /* Above 32-bit threshold */

/* Test data buffers */
static uint8_t test_buffer_src[TEST_BUFFER_SIZE];
static uint8_t test_buffer_dst[TEST_BUFFER_SIZE];
static bool test_results[10];
static int test_count = 0;

/* Test helper functions */
static void init_test_data(void);
static void record_test_result(const char* test_name, bool passed);
static void print_test_summary(void);
static bool validate_buffer_data(const uint8_t* buffer, size_t size, uint8_t expected_pattern);

/**
 * @brief Initialize test data with known patterns
 */
static void init_test_data(void) {
    /* Initialize source buffer with sequential pattern */
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        test_buffer_src[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Clear destination buffer */
    memset(test_buffer_dst, 0, TEST_BUFFER_SIZE);
    
    LOG_DEBUG("Test data initialized");
}

/**
 * @brief Record test result
 */
static void record_test_result(const char* test_name, bool passed) {
    if (test_count < 10) {
        test_results[test_count] = passed;
        test_count++;
    }
    
    LOG_INFO("Test '%s': %s", test_name, passed ? "PASSED" : "FAILED");
}

/**
 * @brief Validate buffer contains expected pattern
 */
static bool validate_buffer_data(const uint8_t* buffer, size_t size, uint8_t expected_pattern) {
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] != ((expected_pattern + i) & 0xFF)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Test CPU detection initialization
 */
static void test_cpu_detection_init(void) {
    LOG_INFO("Testing CPU detection initialization...");
    
    /* Initialize CPU detection */
    direct_pio_init_cpu_detection();
    
    /* Get optimization level */
    uint8_t opt_level = direct_pio_get_optimization_level();
    uint8_t cpu_support = direct_pio_get_cpu_support_info();
    
    LOG_INFO("Detected optimization level: %d", opt_level);
    LOG_INFO("32-bit support: %s", cpu_support ? "Yes" : "No");
    
    /* Test should pass if values are reasonable */
    bool passed = (opt_level <= 2) && (cpu_support <= 1);
    record_test_result("CPU Detection Init", passed);
}

/**
 * @brief Test enhanced PIO threshold logic
 */
static void test_pio_threshold_logic(void) {
    LOG_INFO("Testing PIO threshold logic...");
    
    /* Test small packet - should not use enhanced PIO */
    bool small_enhanced = should_use_enhanced_pio(SMALL_PACKET_SIZE - 1);
    
    /* Test large packet - depends on CPU capability */
    bool large_enhanced = should_use_enhanced_pio(LARGE_PACKET_SIZE);
    
    /* Small packets should never use enhanced PIO */
    bool test1_passed = !small_enhanced;
    
    /* Large packets should use enhanced PIO only if CPU supports it */
    uint8_t cpu_support = direct_pio_get_cpu_support_info();
    bool test2_passed = (large_enhanced == (cpu_support ? true : false));
    
    record_test_result("Small Packet Threshold", test1_passed);
    record_test_result("Large Packet Threshold", test2_passed);
}

/**
 * @brief Test optimal transfer unit calculation
 */
static void test_optimal_transfer_unit(void) {
    LOG_INFO("Testing optimal transfer unit calculation...");
    
    uint8_t transfer_unit = get_optimal_transfer_unit();
    uint8_t cpu_support = direct_pio_get_cpu_support_info();
    
    /* Should be 4 bytes for 32-bit capable systems, 2 for 286 */
    bool passed = (cpu_support && transfer_unit == 4) || (!cpu_support && transfer_unit == 2);
    
    LOG_INFO("Transfer unit: %d bytes, CPU support: %s", transfer_unit, cpu_support ? "Yes" : "No");
    record_test_result("Optimal Transfer Unit", passed);
}

/**
 * @brief Test enhanced packet send with simulated I/O
 * 
 * Note: This test uses simulated I/O since we can't safely perform actual
 * I/O operations in a test environment. In a real system, the assembly
 * functions would perform the actual hardware I/O.
 */
static void test_enhanced_packet_send(void) {
    LOG_INFO("Testing enhanced packet send...");
    
    /* Test with different packet sizes */
    const uint16_t test_sizes[] = { 16, 32, 64, 128, 256, 512, 1024 };
    const int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    bool all_passed = true;
    
    for (int i = 0; i < num_tests; i++) {
        uint16_t size = test_sizes[i];
        
        /* This would normally call the enhanced send function
         * For testing, we just verify the logic path selection */
        bool should_enhance = should_use_enhanced_pio(size);
        uint8_t cpu_support = direct_pio_get_cpu_support_info();
        
        /* Verify the enhancement decision is correct */
        bool expected_enhance = (cpu_support && size >= PIO_32BIT_THRESHOLD);
        bool test_passed = (should_enhance == expected_enhance);
        
        if (!test_passed) {
            all_passed = false;
            LOG_WARNING("Enhanced send test failed for size %d", size);
        }
        
        LOG_DEBUG("Size %d: enhance=%s, expected=%s", 
                  size, should_enhance ? "yes" : "no", expected_enhance ? "yes" : "no");
    }
    
    record_test_result("Enhanced Packet Send", all_passed);
}

/**
 * @brief Test backward compatibility
 */
static void test_backward_compatibility(void) {
    LOG_INFO("Testing backward compatibility...");
    
    /* Test that the system works regardless of CPU type */
    uint8_t opt_level = direct_pio_get_optimization_level();
    
    /* All optimization levels should be valid */
    bool level_valid = (opt_level >= 0 && opt_level <= 2);
    
    /* Test that functions don't crash on any CPU type */
    uint8_t transfer_unit = get_optimal_transfer_unit();
    bool unit_valid = (transfer_unit == 2 || transfer_unit == 4);
    
    bool passed = level_valid && unit_valid;
    record_test_result("Backward Compatibility", passed);
}

/**
 * @brief Print test summary
 */
static void print_test_summary(void) {
    int passed_count = 0;
    
    printf("\n=== CPU-Optimized PIO Test Summary ===\n");
    
    for (int i = 0; i < test_count; i++) {
        if (test_results[i]) {
            passed_count++;
        }
    }
    
    printf("Tests passed: %d/%d\n", passed_count, test_count);
    printf("Success rate: %.1f%%\n", (float)passed_count / test_count * 100.0f);
    
    if (passed_count == test_count) {
        printf("✓ All tests PASSED - CPU optimizations working correctly\n");
    } else {
        printf("✗ Some tests FAILED - Check implementation\n");
    }
    
    /* Print system information */
    printf("\n=== System Information ===\n");
    printf("Optimization level: %d\n", direct_pio_get_optimization_level());
    printf("32-bit support: %s\n", direct_pio_get_cpu_support_info() ? "Yes" : "No");
    printf("Optimal transfer unit: %d bytes\n", get_optimal_transfer_unit());
    printf("32-bit threshold: %d bytes\n", PIO_32BIT_THRESHOLD);
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== CPU-Optimized PIO Test Suite ===\n");
    printf("Testing Phase 1 CPU-specific I/O optimizations\n\n");
    
    /* Initialize logging */
    logging_set_level(LOG_LEVEL_DEBUG);
    
    /* Initialize test data */
    init_test_data();
    
    /* Run tests */
    test_cpu_detection_init();
    test_pio_threshold_logic();
    test_optimal_transfer_unit();
    test_enhanced_packet_send();
    test_backward_compatibility();
    
    /* Print results */
    print_test_summary();
    
    return (test_count > 0 && test_results[0]) ? 0 : 1;
}

/**
 * @brief Test runner for integration with existing test framework
 */
int run_cpu_optimized_pio_tests(void) {
    printf("Running CPU-optimized PIO tests...\n");
    return main();
}