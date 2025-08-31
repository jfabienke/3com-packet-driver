/**
 * @file test_memory.c
 * @brief Test and validation for three-tier memory management system
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "memory.h"
#include "xms_detect.h"
#include "buffer_alloc.h"
#include "cpu_detect.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>

/* Test result structure */
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    char last_error[256];
} test_results_t;

static test_results_t g_test_results = {0};

/* Test helper macros */
#define TEST_ASSERT(condition, message) \
    do { \
        g_test_results.tests_run++; \
        if (!(condition)) { \
            g_test_results.tests_failed++; \
            snprintf(g_test_results.last_error, sizeof(g_test_results.last_error), \
                     "FAIL: %s", message); \
            log_error("TEST FAILED: %s", message); \
            return -1; \
        } else { \
            g_test_results.tests_passed++; \
            log_info("PASS: %s", message); \
        } \
    } while(0)

#define TEST_START(name) \
    log_info("=== Starting test: %s ===", name)

#define TEST_END(name) \
    log_info("=== Completed test: %s ===", name)

/**
 * @brief Test XMS detection and basic allocation
 * @return 0 on success, negative on failure
 */
static int test_xms_basic(void) {
    TEST_START("XMS Basic Functionality");
    
    /* Test XMS detection */
    int result = xms_detect_and_init();
    if (result == 0) {
        log_info("XMS detected and initialized successfully");
        
        /* Test XMS allocation */
        uint16_t handle;
        result = xms_allocate(4, &handle);  /* 4KB */
        TEST_ASSERT(result == 0, "XMS allocation of 4KB");
        
        if (result == 0) {
            /* Test XMS locking */
            uint32_t linear_addr;
            result = xms_lock(handle, &linear_addr);
            TEST_ASSERT(result == 0, "XMS block locking");
            TEST_ASSERT(linear_addr != 0, "XMS linear address valid");
            
            /* Test XMS unlocking */
            result = xms_unlock(handle);
            TEST_ASSERT(result == 0, "XMS block unlocking");
            
            /* Test XMS deallocation */
            result = xms_free(handle);
            TEST_ASSERT(result == 0, "XMS deallocation");
        }
    } else {
        log_info("XMS not available - skipping XMS tests");
    }
    
    TEST_END("XMS Basic Functionality");
    return 0;
}

/**
 * @brief Test three-tier memory allocation strategy
 * @return 0 on success, negative on failure
 */
static int test_memory_tiers(void) {
    TEST_START("Three-Tier Memory Allocation");
    
    /* Initialize memory system */
    int result = memory_init();
    TEST_ASSERT(result == 0, "Memory system initialization");
    
    /* Test small allocation (should use conventional memory) */
    void *small_ptr = memory_alloc(64, MEM_TYPE_GENERAL, 0);
    TEST_ASSERT(small_ptr != NULL, "Small allocation (64 bytes)");
    
    /* Test medium allocation (may use UMB if available) */
    void *medium_ptr = memory_alloc(2048, MEM_TYPE_PACKET_BUFFER, MEM_FLAG_ALIGNED);
    TEST_ASSERT(medium_ptr != NULL, "Medium allocation (2048 bytes)");
    
    /* Test large allocation (should prefer XMS if available) */
    void *large_ptr = memory_alloc(8192, MEM_TYPE_PACKET_BUFFER, MEM_FLAG_DMA_CAPABLE);
    if (memory_xms_available()) {
        TEST_ASSERT(large_ptr != NULL, "Large allocation (8192 bytes) with XMS");
    } else {
        log_info("XMS not available - large allocation may fail or use conventional");
    }
    
    /* Test memory statistics */
    const mem_stats_t *stats = memory_get_stats();
    TEST_ASSERT(stats != NULL, "Memory statistics available");
    TEST_ASSERT(stats->total_allocations >= 2, "Allocation count tracking");
    
    /* Free allocations */
    if (small_ptr) memory_free(small_ptr);
    if (medium_ptr) memory_free(medium_ptr);
    if (large_ptr) memory_free(large_ptr);
    
    TEST_END("Three-Tier Memory Allocation");
    return 0;
}

/**
 * @brief Test CPU-optimized memory operations
 * @return 0 on success, negative on failure
 */
static int test_cpu_optimized_memory(void) {
    TEST_START("CPU-Optimized Memory Operations");
    
    extern cpu_info_t g_cpu_info;
    
    /* Initialize CPU-optimized memory */
    int result = memory_init_cpu_optimized();
    TEST_ASSERT(result == 0, "CPU-optimized memory initialization");
    
    /* Test CPU-optimized copy */
    uint8_t src_data[1024];
    uint8_t dest_data[1024];
    
    /* Fill source with test pattern */
    for (int i = 0; i < 1024; i++) {
        src_data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Test optimized copy */
    memory_copy_optimized(dest_data, src_data, 1024);
    TEST_ASSERT(memcmp(src_data, dest_data, 1024) == 0, "CPU-optimized memory copy");
    
    /* Test CPU-optimized set */
    memory_set_optimized(dest_data, 0xAA, 512);
    int all_aa = 1;
    for (int i = 0; i < 512; i++) {
        if (dest_data[i] != 0xAA) {
            all_aa = 0;
            break;
        }
    }
    TEST_ASSERT(all_aa, "CPU-optimized memory set");
    
    /* Test aligned allocation based on CPU */
    void *aligned_ptr = memory_alloc_aligned(256, 0, MEM_TYPE_PACKET_BUFFER);
    TEST_ASSERT(aligned_ptr != NULL, "CPU-optimized aligned allocation");
    
    uint32_t expected_alignment = (g_cpu_info.type >= CPU_TYPE_80386) ? 4 : 2;
    TEST_ASSERT(IS_ALIGNED((uint32_t)aligned_ptr, expected_alignment), 
                "Memory aligned to CPU requirements");
    
    if (aligned_ptr) memory_free(aligned_ptr);
    
    TEST_END("CPU-Optimized Memory Operations");
    return 0;
}

/**
 * @brief Test buffer allocation system
 * @return 0 on success, negative on failure
 */
static int test_buffer_system(void) {
    TEST_START("Buffer Allocation System");
    
    /* Initialize buffer system */
    int result = buffer_system_init_optimized();
    TEST_ASSERT(result == 0, "Buffer system initialization");
    
    /* Test Ethernet frame buffer allocation */
    buffer_desc_t *small_frame = buffer_alloc_ethernet_frame(64, BUFFER_TYPE_TX);
    TEST_ASSERT(small_frame != NULL, "Small Ethernet frame buffer allocation");
    TEST_ASSERT(buffer_is_valid(small_frame), "Small frame buffer validation");
    
    buffer_desc_t *large_frame = buffer_alloc_ethernet_frame(1518, BUFFER_TYPE_RX);
    TEST_ASSERT(large_frame != NULL, "Large Ethernet frame buffer allocation");
    TEST_ASSERT(buffer_is_valid(large_frame), "Large frame buffer validation");
    
    /* Test DMA buffer allocation */
    buffer_desc_t *dma_buffer = buffer_alloc_dma(1024, 16);
    if (dma_buffer) {
        TEST_ASSERT(buffer_is_valid(dma_buffer), "DMA buffer validation");
        TEST_ASSERT(dma_buffer->flags & BUFFER_FLAG_DMA_CAPABLE, "DMA buffer capability flag");
    } else {
        log_info("DMA buffer allocation failed - may be normal if no DMA memory available");
    }
    
    /* Test buffer data operations */
    if (small_frame) {
        uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
        result = buffer_set_data(small_frame, test_data, sizeof(test_data));
        TEST_ASSERT(result == SUCCESS, "Buffer data setting");
        TEST_ASSERT(small_frame->used == sizeof(test_data), "Buffer used size tracking");
        
        /* Test buffer copy */
        if (large_frame) {
            result = buffer_copy_packet_data(large_frame, small_frame);
            TEST_ASSERT(result == SUCCESS, "Buffer packet data copy");
            TEST_ASSERT(large_frame->used == small_frame->used, "Buffer copy size consistency");
        }
    }
    
    /* Test buffer pool statistics */
    const buffer_stats_t *buf_stats = buffer_get_stats();
    TEST_ASSERT(buf_stats != NULL, "Buffer statistics available");
    TEST_ASSERT(buf_stats->total_allocations > 0, "Buffer allocation tracking");
    
    /* Free buffers */
    if (small_frame) buffer_free_any(small_frame);
    if (large_frame) buffer_free_any(large_frame);
    if (dma_buffer) buffer_free_any(dma_buffer);
    
    TEST_END("Buffer Allocation System");
    return 0;
}

/**
 * @brief Test memory fallback scenarios
 * @return 0 on success, negative on failure
 */
static int test_memory_fallback(void) {
    TEST_START("Memory Fallback Scenarios");
    
    /* Test allocation when preferred tier is unavailable */
    void *fallback_ptr = NULL;
    
    /* Try to allocate a large buffer that would prefer XMS */
    for (int i = 0; i < 10; i++) {
        fallback_ptr = memory_alloc(4096, MEM_TYPE_PACKET_BUFFER, MEM_FLAG_ALIGNED);
        if (!fallback_ptr) {
            break;
        }
        /* Don't free immediately to stress the system */
    }
    
    /* System should gracefully handle memory pressure */
    TEST_ASSERT(memory_get_last_error() != MEM_ERROR_CORRUPTION, "No memory corruption under pressure");
    
    /* Test error handling */
    void *invalid_ptr = memory_alloc(0, MEM_TYPE_GENERAL, 0);
    TEST_ASSERT(invalid_ptr == NULL, "Invalid size allocation rejection");
    TEST_ASSERT(memory_get_last_error() == MEM_ERROR_INVALID_SIZE, "Proper error code setting");
    
    /* Test large allocation that should fail gracefully */
    void *huge_ptr = memory_alloc(0xFFFFFFFF, MEM_TYPE_GENERAL, 0);
    TEST_ASSERT(huge_ptr == NULL, "Huge allocation rejection");
    
    if (fallback_ptr) {
        memory_free(fallback_ptr);
    }
    
    TEST_END("Memory Fallback Scenarios");
    return 0;
}

/**
 * @brief Test memory statistics and reporting
 * @return 0 on success, negative on failure
 */
static int test_memory_statistics(void) {
    TEST_START("Memory Statistics and Reporting");
    
    /* Clear existing statistics */
    memory_stats_init(&g_mem_stats);
    
    /* Perform some allocations to generate statistics */
    void *ptrs[5];
    uint32_t sizes[] = {64, 128, 256, 512, 1024};
    
    for (int i = 0; i < 5; i++) {
        ptrs[i] = memory_alloc(sizes[i], MEM_TYPE_PACKET_BUFFER, 0);
    }
    
    const mem_stats_t *stats = memory_get_stats();
    TEST_ASSERT(stats->total_allocations == 5, "Allocation count accuracy");
    TEST_ASSERT(stats->used_memory > 0, "Used memory tracking");
    TEST_ASSERT(stats->largest_allocation == 1024, "Largest allocation tracking");
    TEST_ASSERT(stats->smallest_allocation == 64, "Smallest allocation tracking");
    
    /* Free allocations and check statistics */
    for (int i = 0; i < 5; i++) {
        if (ptrs[i]) {
            memory_free(ptrs[i]);
        }
    }
    
    TEST_ASSERT(stats->total_frees <= 5, "Free count tracking");
    
    /* Test statistics printing (should not crash) */
    memory_print_stats();
    
    TEST_END("Memory Statistics and Reporting");
    return 0;
}

/**
 * @brief Run comprehensive memory system tests
 * @return 0 on success, negative on failure
 */
int memory_run_comprehensive_tests(void) {
    int result = 0;
    
    log_info("=== Starting Comprehensive Memory System Tests ===");
    
    /* Initialize test results */
    memset(&g_test_results, 0, sizeof(g_test_results));
    
    /* Run all test suites */
    if (test_xms_basic() < 0) result = -1;
    if (test_memory_tiers() < 0) result = -1;
    if (test_cpu_optimized_memory() < 0) result = -1;
    if (test_buffer_system() < 0) result = -1;
    if (test_memory_fallback() < 0) result = -1;
    if (test_memory_statistics() < 0) result = -1;
    
    /* Print test summary */
    log_info("=== Test Summary ===");
    log_info("Tests run: %d", g_test_results.tests_run);
    log_info("Tests passed: %d", g_test_results.tests_passed);
    log_info("Tests failed: %d", g_test_results.tests_failed);
    
    if (g_test_results.tests_failed > 0) {
        log_error("Last error: %s", g_test_results.last_error);
        result = -1;
    }
    
    if (result == 0) {
        log_info("=== ALL TESTS PASSED ===");
    } else {
        log_error("=== SOME TESTS FAILED ===");
    }
    
    return result;
}

/**
 * @brief Test memory system under stress conditions
 * @return 0 on success, negative on failure
 */
int memory_stress_test(void) {
    TEST_START("Memory System Stress Test");
    
    void *ptrs[100];
    int allocated_count = 0;
    
    /* Allocate many small buffers */
    for (int i = 0; i < 100; i++) {
        uint32_t size = 64 + (i * 16); /* Varying sizes */
        ptrs[i] = memory_alloc(size, MEM_TYPE_PACKET_BUFFER, 0);
        if (ptrs[i]) {
            allocated_count++;
        }
    }
    
    log_info("Stress test allocated %d out of 100 buffers", allocated_count);
    TEST_ASSERT(allocated_count > 0, "At least some allocations succeeded under stress");
    
    /* Free every other buffer */
    for (int i = 0; i < 100; i += 2) {
        if (ptrs[i]) {
            memory_free(ptrs[i]);
            ptrs[i] = NULL;
        }
    }
    
    /* Try to allocate again in the freed slots */
    int realloc_count = 0;
    for (int i = 0; i < 100; i += 2) {
        ptrs[i] = memory_alloc(128, MEM_TYPE_PACKET_BUFFER, 0);
        if (ptrs[i]) {
            realloc_count++;
        }
    }
    
    log_info("Stress test reallocated %d buffers", realloc_count);
    
    /* Free all remaining buffers */
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) {
            memory_free(ptrs[i]);
        }
    }
    
    /* Check system integrity after stress */
    const mem_stats_t *stats = memory_get_stats();
    TEST_ASSERT(memory_get_last_error() != MEM_ERROR_CORRUPTION, 
                "No corruption detected after stress test");
    
    TEST_END("Memory System Stress Test");
    return 0;
}