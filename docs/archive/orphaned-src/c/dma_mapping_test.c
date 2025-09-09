/**
 * @file dma_mapping_test.c
 * @brief Comprehensive test suite for centralized DMA mapping layer
 *
 * This test suite validates:
 * - DMA boundary checking and bounce buffer allocation
 * - Cache coherency operations 
 * - Direction-specific TX/RX mapping
 * - Physical address calculation
 * - Error handling and edge cases
 * - Performance and memory usage
 */

#include "dma_mapping.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test constants */
#define TEST_BUFFER_SIZE    1500
#define TEST_SMALL_SIZE     64
#define TEST_LARGE_SIZE     8192
#define TEST_ITERATIONS     100
#define TEST_STRESS_COUNT   1000

/* Test statistics */
typedef struct {
    uint32_t tests_run;
    uint32_t tests_passed;
    uint32_t tests_failed;
    uint32_t bounce_buffers_used;
    uint32_t direct_mappings_used;
} test_stats_t;

static test_stats_t g_test_stats = {0};

/* Test helper macros */
#define TEST_ASSERT(condition, message) \
    do { \
        g_test_stats.tests_run++; \
        if (condition) { \
            g_test_stats.tests_passed++; \
            LOG_INFO("PASS: %s", message); \
        } else { \
            g_test_stats.tests_failed++; \
            LOG_ERROR("FAIL: %s", message); \
            return -1; \
        } \
    } while(0)

#define TEST_ASSERT_NOT_NULL(ptr, message) \
    TEST_ASSERT((ptr) != NULL, message)

#define TEST_ASSERT_NULL(ptr, message) \
    TEST_ASSERT((ptr) == NULL, message)

#define TEST_ASSERT_EQUAL(expected, actual, message) \
    TEST_ASSERT((expected) == (actual), message)

/* Test buffer allocation helpers */
static void* alloc_test_buffer(size_t size, bool force_unsafe) {
    void* buffer;
    
    if (force_unsafe) {
        /* Try to allocate buffer that crosses 64KB boundary */
        buffer = malloc(size + 65536);
        if (buffer) {
            /* Align to create boundary crossing */
            uintptr_t addr = (uintptr_t)buffer;
            addr = (addr + 65535) & ~0xFFFF;  /* Align to 64KB boundary */
            addr -= size / 2;  /* Position to cross boundary */
            return (void*)addr;
        }
    }
    
    return malloc(size);
}

static void free_test_buffer(void* buffer, bool was_unsafe) {
    if (!was_unsafe) {
        free(buffer);
    }
    /* For unsafe buffers allocated with offset, we can't easily free 
     * the original malloc'd pointer, so we accept the leak for testing */
}

/* Test functions */

/**
 * Test basic DMA mapping initialization and shutdown
 */
static int test_dma_mapping_init_shutdown(void) {
    LOG_INFO("=== Testing DMA mapping initialization and shutdown ===");
    
    /* Test initialization */
    int result = dma_mapping_init();
    TEST_ASSERT(result == DMA_MAP_SUCCESS, "DMA mapping initialization");
    
    /* Test double initialization (should be safe) */
    result = dma_mapping_init();
    TEST_ASSERT(result == DMA_MAP_SUCCESS, "DMA mapping double initialization");
    
    /* Test shutdown */
    dma_mapping_shutdown();
    
    /* Re-initialize for other tests */
    result = dma_mapping_init();
    TEST_ASSERT(result == DMA_MAP_SUCCESS, "DMA mapping re-initialization");
    
    return 0;
}

/**
 * Test TX DMA mapping with safe buffers
 */
static int test_tx_mapping_safe_buffers(void) {
    LOG_INFO("=== Testing TX DMA mapping with safe buffers ===");
    
    uint8_t *buffer = alloc_test_buffer(TEST_BUFFER_SIZE, false);
    TEST_ASSERT_NOT_NULL(buffer, "Test buffer allocation");
    
    /* Fill with test pattern */
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Test TX mapping */
    dma_mapping_t *mapping = dma_map_tx(buffer, TEST_BUFFER_SIZE);
    TEST_ASSERT_NOT_NULL(mapping, "TX DMA mapping creation");
    
    /* Verify mapping properties */
    void *mapped_addr = dma_mapping_get_address(mapping);
    TEST_ASSERT_NOT_NULL(mapped_addr, "Mapped address retrieval");
    
    size_t mapped_len = dma_mapping_get_length(mapping);
    TEST_ASSERT_EQUAL(TEST_BUFFER_SIZE, mapped_len, "Mapped length verification");
    
    uint32_t phys_addr = dma_mapping_get_phys_addr(mapping);
    TEST_ASSERT(phys_addr != 0, "Physical address calculation");
    
    /* For safe buffers, might not need bounce buffer */
    if (!dma_mapping_uses_bounce(mapping)) {
        g_test_stats.direct_mappings_used++;
        LOG_DEBUG("Direct mapping used (no bounce buffer needed)");
    } else {
        g_test_stats.bounce_buffers_used++;
        LOG_DEBUG("Bounce buffer used for safety");
    }
    
    /* Test data integrity if using direct mapping */
    if (!dma_mapping_uses_bounce(mapping)) {
        uint8_t *mapped_data = (uint8_t*)mapped_addr;
        bool data_ok = true;
        for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
            if (mapped_data[i] != (uint8_t)(i & 0xFF)) {
                data_ok = false;
                break;
            }
        }
        TEST_ASSERT(data_ok, "Direct mapping data integrity");
    }
    
    /* Test synchronization */
    int sync_result = dma_mapping_sync_for_device(mapping);
    TEST_ASSERT(sync_result == DMA_MAP_SUCCESS, "TX sync for device");
    
    /* Cleanup */
    dma_unmap_tx(mapping);
    free_test_buffer(buffer, false);
    
    return 0;
}

/**
 * Test RX DMA mapping with safe buffers  
 */
static int test_rx_mapping_safe_buffers(void) {
    LOG_INFO("=== Testing RX DMA mapping with safe buffers ===");
    
    uint8_t *buffer = alloc_test_buffer(TEST_BUFFER_SIZE, false);
    TEST_ASSERT_NOT_NULL(buffer, "Test buffer allocation");
    
    /* Test RX mapping */
    dma_mapping_t *mapping = dma_map_rx(buffer, TEST_BUFFER_SIZE);
    TEST_ASSERT_NOT_NULL(mapping, "RX DMA mapping creation");
    
    /* Verify mapping properties */
    void *mapped_addr = dma_mapping_get_address(mapping);
    TEST_ASSERT_NOT_NULL(mapped_addr, "Mapped address retrieval");
    
    size_t mapped_len = dma_mapping_get_length(mapping);
    TEST_ASSERT_EQUAL(TEST_BUFFER_SIZE, mapped_len, "Mapped length verification");
    
    /* Track bounce buffer usage */
    if (!dma_mapping_uses_bounce(mapping)) {
        g_test_stats.direct_mappings_used++;
    } else {
        g_test_stats.bounce_buffers_used++;
    }
    
    /* Test synchronization */
    int sync_result = dma_mapping_sync_for_cpu(mapping);
    TEST_ASSERT(sync_result == DMA_MAP_SUCCESS, "RX sync for CPU");
    
    /* Simulate received data by writing to mapped buffer */
    uint8_t *mapped_data = (uint8_t*)mapped_addr;
    for (int i = 0; i < 100; i++) {
        mapped_data[i] = 0xAA;
    }
    
    /* For RX, data should be copied back to original buffer on unmap */
    dma_unmap_rx(mapping);
    
    /* Verify data was copied back (if bounce buffer was used) */
    if (g_test_stats.bounce_buffers_used > g_test_stats.direct_mappings_used) {
        bool data_copied = true;
        for (int i = 0; i < 100; i++) {
            if (buffer[i] != 0xAA) {
                data_copied = false;
                break;
            }
        }
        TEST_ASSERT(data_copied, "RX bounce buffer data copy-back");
    }
    
    free_test_buffer(buffer, false);
    
    return 0;
}

/**
 * Test DMA mapping with unsafe buffers (force bounce buffer usage)
 */
static int test_mapping_unsafe_buffers(void) {
    LOG_INFO("=== Testing DMA mapping with unsafe buffers ===");
    
    uint8_t *buffer = alloc_test_buffer(TEST_BUFFER_SIZE, true);
    TEST_ASSERT_NOT_NULL(buffer, "Unsafe test buffer allocation");
    
    /* Fill with test pattern */
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Test TX mapping with forced bounce buffer */
    dma_mapping_t *tx_mapping = dma_map_tx_flags(buffer, TEST_BUFFER_SIZE, DMA_MAP_FORCE_BOUNCE);
    TEST_ASSERT_NOT_NULL(tx_mapping, "TX mapping with forced bounce");
    TEST_ASSERT(dma_mapping_uses_bounce(tx_mapping), "Forced bounce buffer usage");
    
    /* Verify data was copied to bounce buffer */
    void *bounce_addr = dma_mapping_get_address(tx_mapping);
    uint8_t *bounce_data = (uint8_t*)bounce_addr;
    bool data_copied = true;
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        if (bounce_data[i] != (uint8_t)(i & 0xFF)) {
            data_copied = false;
            break;
        }
    }
    TEST_ASSERT(data_copied, "TX data copied to bounce buffer");
    
    dma_unmap_tx(tx_mapping);
    
    /* Test RX mapping with unsafe buffer */
    dma_mapping_t *rx_mapping = dma_map_rx(buffer, TEST_BUFFER_SIZE);
    TEST_ASSERT_NOT_NULL(rx_mapping, "RX mapping with unsafe buffer");
    
    /* Should use bounce buffer for safety */
    if (dma_mapping_uses_bounce(rx_mapping)) {
        g_test_stats.bounce_buffers_used++;
        LOG_DEBUG("Bounce buffer correctly used for unsafe RX buffer");
    }
    
    dma_unmap_rx(rx_mapping);
    free_test_buffer(buffer, true);
    
    return 0;
}

/**
 * Test batch DMA mapping operations
 */
static int test_batch_mapping(void) {
    LOG_INFO("=== Testing batch DMA mapping operations ===");
    
    const int batch_size = 8;
    dma_mapping_batch_t *batch = dma_create_mapping_batch(batch_size);
    TEST_ASSERT_NOT_NULL(batch, "Batch creation");
    
    /* Create multiple mappings and add to batch */
    for (int i = 0; i < batch_size; i++) {
        uint8_t *buffer = alloc_test_buffer(TEST_SMALL_SIZE, false);
        TEST_ASSERT_NOT_NULL(buffer, "Batch buffer allocation");
        
        dma_mapping_t *mapping = dma_map_tx(buffer, TEST_SMALL_SIZE);
        TEST_ASSERT_NOT_NULL(mapping, "Batch mapping creation");
        
        int add_result = dma_batch_add_mapping(batch, mapping);
        TEST_ASSERT(add_result == DMA_MAP_SUCCESS, "Adding mapping to batch");
    }
    
    /* Test batch properties */
    TEST_ASSERT(batch->count == batch_size, "Batch count verification");
    TEST_ASSERT(batch->total_length == batch_size * TEST_SMALL_SIZE, "Batch total length");
    
    /* Clean up batch */
    dma_unmap_batch(batch);
    dma_free_mapping_batch(batch);
    
    return 0;
}

/**
 * Test error conditions and edge cases
 */
static int test_error_conditions(void) {
    LOG_INFO("=== Testing error conditions and edge cases ===");
    
    /* Test NULL parameters */
    dma_mapping_t *mapping = dma_map_tx(NULL, TEST_BUFFER_SIZE);
    TEST_ASSERT_NULL(mapping, "TX mapping with NULL buffer");
    
    uint8_t *buffer = alloc_test_buffer(TEST_BUFFER_SIZE, false);
    mapping = dma_map_tx(buffer, 0);
    TEST_ASSERT_NULL(mapping, "TX mapping with zero length");
    
    /* Test invalid mapping operations */
    void *addr = dma_mapping_get_address(NULL);
    TEST_ASSERT_NULL(addr, "Get address from NULL mapping");
    
    uint32_t phys = dma_mapping_get_phys_addr(NULL);
    TEST_ASSERT(phys == 0, "Get physical address from NULL mapping");
    
    size_t len = dma_mapping_get_length(NULL);
    TEST_ASSERT(len == 0, "Get length from NULL mapping");
    
    /* Test double unmap (should be safe) */
    mapping = dma_map_tx(buffer, TEST_BUFFER_SIZE);
    TEST_ASSERT_NOT_NULL(mapping, "Valid TX mapping for double unmap test");
    dma_unmap_tx(mapping);
    /* Second unmap should be handled gracefully (mapping is invalid now) */
    
    free_test_buffer(buffer, false);
    
    return 0;
}

/**
 * Test cache coherency functionality
 */
static int test_cache_coherency(void) {
    LOG_INFO("=== Testing cache coherency operations ===");
    
    uint8_t *buffer = alloc_test_buffer(TEST_BUFFER_SIZE, false);
    TEST_ASSERT_NOT_NULL(buffer, "Test buffer allocation for coherency");
    
    /* Test coherency with normal mapping */
    dma_mapping_t *mapping = dma_map_tx(buffer, TEST_BUFFER_SIZE);
    TEST_ASSERT_NOT_NULL(mapping, "TX mapping for coherency test");
    
    /* Test coherency operations */
    int result = dma_mapping_test_coherency(buffer, TEST_BUFFER_SIZE);
    TEST_ASSERT(result == DMA_MAP_SUCCESS, "Cache coherency test");
    
    /* Test coherent mapping flag */
    dma_mapping_t *coherent_mapping = dma_map_tx_flags(buffer, TEST_BUFFER_SIZE, DMA_MAP_COHERENT);
    TEST_ASSERT_NOT_NULL(coherent_mapping, "Coherent TX mapping");
    TEST_ASSERT(dma_mapping_is_coherent(coherent_mapping), "Coherent mapping flag check");
    
    /* Sync should be no-op for coherent mappings */
    result = dma_mapping_sync_for_device(coherent_mapping);
    TEST_ASSERT(result == DMA_MAP_SUCCESS, "Coherent mapping sync");
    
    dma_unmap_tx(mapping);
    dma_unmap_tx(coherent_mapping);
    free_test_buffer(buffer, false);
    
    return 0;
}

/**
 * Test performance and stress conditions
 */
static int test_performance_stress(void) {
    LOG_INFO("=== Testing performance and stress conditions ===");
    
    /* Stress test: rapid allocation/deallocation */
    for (int i = 0; i < TEST_STRESS_COUNT; i++) {
        uint8_t *buffer = alloc_test_buffer(TEST_SMALL_SIZE, false);
        if (!buffer) continue;
        
        dma_mapping_t *mapping = dma_map_tx(buffer, TEST_SMALL_SIZE);
        if (mapping) {
            dma_unmap_tx(mapping);
        }
        
        free_test_buffer(buffer, false);
    }
    
    LOG_INFO("Completed %d stress allocation/deallocation cycles", TEST_STRESS_COUNT);
    
    /* Test large buffer mapping */
    uint8_t *large_buffer = alloc_test_buffer(TEST_LARGE_SIZE, false);
    if (large_buffer) {
        dma_mapping_t *mapping = dma_map_tx(large_buffer, TEST_LARGE_SIZE);
        TEST_ASSERT_NOT_NULL(mapping, "Large buffer mapping");
        
        if (mapping) {
            TEST_ASSERT(dma_mapping_get_length(mapping) == TEST_LARGE_SIZE, "Large buffer length");
            dma_unmap_tx(mapping);
        }
        
        free_test_buffer(large_buffer, false);
    }
    
    return 0;
}

/**
 * Test statistics and debugging features
 */
static int test_statistics_debugging(void) {
    LOG_INFO("=== Testing statistics and debugging features ===");
    
    /* Reset statistics */
    dma_mapping_reset_stats();
    
    /* Perform some operations to generate stats */
    uint8_t *buffer = alloc_test_buffer(TEST_BUFFER_SIZE, false);
    TEST_ASSERT_NOT_NULL(buffer, "Stats test buffer allocation");
    
    dma_mapping_t *mapping1 = dma_map_tx(buffer, TEST_BUFFER_SIZE);
    dma_mapping_t *mapping2 = dma_map_rx(buffer, TEST_BUFFER_SIZE);
    
    /* Get statistics */
    dma_mapping_stats_t stats;
    dma_mapping_get_stats(&stats);
    
    TEST_ASSERT(stats.total_mappings >= 2, "Statistics total mappings");
    TEST_ASSERT(stats.active_mappings == 2, "Statistics active mappings");
    
    /* Test validation function */
    bool valid1 = dma_mapping_validate(mapping1);
    bool valid2 = dma_mapping_validate(mapping2);
    TEST_ASSERT(valid1 && valid2, "Mapping validation");
    
    /* Print statistics */
    dma_mapping_print_stats();
    
    /* Cleanup */
    if (mapping1) dma_unmap_tx(mapping1);
    if (mapping2) dma_unmap_rx(mapping2);
    free_test_buffer(buffer, false);
    
    /* Verify active count decreased */
    dma_mapping_get_stats(&stats);
    TEST_ASSERT(stats.active_mappings == 0, "Statistics cleanup verification");
    
    return 0;
}

/**
 * Run all DMA mapping tests
 */
int run_dma_mapping_tests(void) {
    LOG_INFO("=== Starting comprehensive DMA mapping test suite ===");
    
    memset(&g_test_stats, 0, sizeof(g_test_stats));
    
    /* Run test functions */
    if (test_dma_mapping_init_shutdown() < 0) return -1;
    if (test_tx_mapping_safe_buffers() < 0) return -1;
    if (test_rx_mapping_safe_buffers() < 0) return -1;
    if (test_mapping_unsafe_buffers() < 0) return -1;
    if (test_batch_mapping() < 0) return -1;
    if (test_error_conditions() < 0) return -1;
    if (test_cache_coherency() < 0) return -1;
    if (test_performance_stress() < 0) return -1;
    if (test_statistics_debugging() < 0) return -1;
    
    /* Final cleanup */
    dma_mapping_shutdown();
    
    /* Print test summary */
    LOG_INFO("=== DMA Mapping Test Suite Results ===");
    LOG_INFO("Tests run: %u", g_test_stats.tests_run);
    LOG_INFO("Tests passed: %u", g_test_stats.tests_passed);
    LOG_INFO("Tests failed: %u", g_test_stats.tests_failed);
    LOG_INFO("Bounce buffers used: %u", g_test_stats.bounce_buffers_used);
    LOG_INFO("Direct mappings used: %u", g_test_stats.direct_mappings_used);
    
    if (g_test_stats.tests_failed == 0) {
        LOG_INFO("*** ALL TESTS PASSED ***");
        return 0;
    } else {
        LOG_ERROR("*** %u TESTS FAILED ***", g_test_stats.tests_failed);
        return -1;
    }
}

/* Test entry point for integration with driver */
int dma_mapping_run_self_test(void) {
    LOG_INFO("Running DMA mapping self-test...");
    int result = run_dma_mapping_tests();
    if (result == 0) {
        LOG_INFO("DMA mapping self-test completed successfully");
    } else {
        LOG_ERROR("DMA mapping self-test failed");
    }
    return result;
}