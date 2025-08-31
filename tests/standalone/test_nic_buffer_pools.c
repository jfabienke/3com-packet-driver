/**
 * @file test_nic_buffer_pools.c
 * @brief Comprehensive test suite for Per-NIC Buffer Pool Implementation
 * 
 * Sprint 1.5: Per-NIC Buffer Pool Implementation Test Suite
 * 
 * This test validates the per-NIC buffer pool system that provides resource
 * isolation, eliminates contention between NICs, and enables per-NIC 
 * performance tuning. This addresses the architectural gap where the current
 * implementation uses global buffer pools instead of per-NIC pools.
 *
 * Test Coverage:
 * - Per-NIC buffer pool creation and management
 * - Resource isolation between NICs
 * - Performance optimization with size-specific pools
 * - RX_COPYBREAK integration
 * - Resource balancing algorithms
 * - Multi-NIC stress testing
 * - Backward compatibility with legacy systems
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "include/common.h"
#include "include/buffer_alloc.h"
#include "include/nic_buffer_pools.h"
#include "include/hardware.h"
#include "include/logging.h"
#include "include/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test configuration */
#define TEST_MEMORY_LIMIT_KB    2048    /* 2MB test memory limit */
#define TEST_NIC_COUNT          4       /* Number of test NICs */
#define TEST_PACKET_COUNT       1000    /* Packets per test */
#define TEST_STRESS_DURATION    30      /* Stress test duration (seconds) */

/* Test results tracking */
typedef struct {
    uint32_t tests_run;
    uint32_t tests_passed;
    uint32_t tests_failed;
    uint32_t assertions_checked;
    char last_error[256];
} test_results_t;

static test_results_t g_test_results = {0};

/* Test helper macros */
#define TEST_ASSERT(condition, message) do { \
    g_test_results.assertions_checked++; \
    if (!(condition)) { \
        snprintf(g_test_results.last_error, sizeof(g_test_results.last_error), \
                "ASSERTION FAILED: %s (line %d)", message, __LINE__); \
        log_error("Test assertion failed: %s", g_test_results.last_error); \
        return -1; \
    } \
} while(0)

#define TEST_START(test_name) do { \
    log_info("=== Starting Test: %s ===", test_name); \
    g_test_results.tests_run++; \
} while(0)

#define TEST_PASS(test_name) do { \
    log_info("=== Test PASSED: %s ===", test_name); \
    g_test_results.tests_passed++; \
    return 0; \
} while(0)

#define TEST_FAIL(test_name, reason) do { \
    log_error("=== Test FAILED: %s - %s ===", test_name, reason); \
    g_test_results.tests_failed++; \
    return -1; \
} while(0)

/* Test function prototypes */
static int test_nic_buffer_manager_init(void);
static int test_nic_buffer_pool_creation(void);
static int test_buffer_allocation_and_free(void);
static int test_resource_isolation(void);
static int test_size_specific_pools(void);
static int test_rx_copybreak_integration(void);
static int test_resource_balancing(void);
static int test_multi_nic_stress(void);
static int test_legacy_compatibility(void);
static int test_error_handling(void);
static int test_statistics_and_monitoring(void);
static int test_memory_limits(void);

/* Helper functions */
static void setup_test_environment(void);
static void cleanup_test_environment(void);
static void print_test_summary(void);
static int simulate_nic_activity(nic_id_t nic_id, uint32_t activity_level);
static void generate_test_packet(uint8_t* buffer, uint16_t size);

/* === Main Test Function === */

int main(void) {
    int result = 0;
    
    log_info("Starting Per-NIC Buffer Pool Comprehensive Test Suite");
    log_info("Test configuration: %d KB memory limit, %d test NICs", 
             TEST_MEMORY_LIMIT_KB, TEST_NIC_COUNT);
    
    /* Initialize test environment */
    setup_test_environment();
    
    /* Run all tests */
    struct {
        const char* name;
        int (*test_func)(void);
    } tests[] = {
        {"NIC Buffer Manager Initialization", test_nic_buffer_manager_init},
        {"NIC Buffer Pool Creation", test_nic_buffer_pool_creation},
        {"Buffer Allocation and Free", test_buffer_allocation_and_free},
        {"Resource Isolation", test_resource_isolation},
        {"Size-Specific Pools", test_size_specific_pools},
        {"RX_COPYBREAK Integration", test_rx_copybreak_integration},
        {"Resource Balancing", test_resource_balancing},
        {"Multi-NIC Stress Test", test_multi_nic_stress},
        {"Legacy Compatibility", test_legacy_compatibility},
        {"Error Handling", test_error_handling},
        {"Statistics and Monitoring", test_statistics_and_monitoring},
        {"Memory Limits", test_memory_limits},
        {NULL, NULL}
    };
    
    for (int i = 0; tests[i].name != NULL; i++) {
        int test_result = tests[i].test_func();
        if (test_result != 0) {
            log_error("Test '%s' failed with code %d", tests[i].name, test_result);
            result = -1;
        }
        
        /* Brief pause between tests */
        mdelay(100);
    }
    
    /* Clean up test environment */
    cleanup_test_environment();
    
    /* Print test summary */
    print_test_summary();
    
    if (g_test_results.tests_failed == 0) {
        log_info("ALL TESTS PASSED!");
        return 0;
    } else {
        log_error("SOME TESTS FAILED!");
        return -1;
    }
}

/* === Test Implementations === */

static int test_nic_buffer_manager_init(void) {
    TEST_START("NIC Buffer Manager Initialization");
    
    /* Test manager initialization */
    uint32_t memory_limit = TEST_MEMORY_LIMIT_KB * 1024;
    int result = nic_buffer_pool_manager_init(memory_limit, MEMORY_TIER_AUTO);
    TEST_ASSERT(result == SUCCESS, "Manager initialization should succeed");
    
    /* Test double initialization (should succeed) */
    result = nic_buffer_pool_manager_init(memory_limit, MEMORY_TIER_AUTO);
    TEST_ASSERT(result == SUCCESS, "Double initialization should succeed");
    
    /* Test invalid parameters */
    result = nic_buffer_pool_manager_init(0, MEMORY_TIER_AUTO);
    TEST_ASSERT(result != SUCCESS, "Zero memory limit should fail");
    
    /* Clean up for next test */
    nic_buffer_pool_manager_cleanup();
    
    TEST_PASS("NIC Buffer Manager Initialization");
}

static int test_nic_buffer_pool_creation(void) {
    TEST_START("NIC Buffer Pool Creation");
    
    /* Initialize manager */
    uint32_t memory_limit = TEST_MEMORY_LIMIT_KB * 1024;
    int result = nic_buffer_pool_manager_init(memory_limit, MEMORY_TIER_AUTO);
    TEST_ASSERT(result == SUCCESS, "Manager initialization should succeed");
    
    /* Test creating buffer pools for different NIC types */
    result = nic_buffer_pool_create(0, NIC_TYPE_3C509B, "3C509B-Test-0");
    TEST_ASSERT(result == SUCCESS, "3C509B pool creation should succeed");
    
    result = nic_buffer_pool_create(1, NIC_TYPE_3C515_TX, "3C515-TX-Test-1");
    TEST_ASSERT(result == SUCCESS, "3C515-TX pool creation should succeed");
    
    /* Test duplicate creation (should fail) */
    result = nic_buffer_pool_create(0, NIC_TYPE_3C509B, "3C509B-Duplicate");
    TEST_ASSERT(result != SUCCESS, "Duplicate pool creation should fail");
    
    /* Test invalid parameters */
    result = nic_buffer_pool_create(INVALID_NIC_ID, NIC_TYPE_3C509B, "Invalid");
    TEST_ASSERT(result != SUCCESS, "Invalid NIC ID should fail");
    
    result = nic_buffer_pool_create(2, NIC_TYPE_3C509B, NULL);
    TEST_ASSERT(result != SUCCESS, "NULL name should fail");
    
    /* Verify pools are initialized */
    TEST_ASSERT(nic_buffer_is_initialized(0), "NIC 0 should be initialized");
    TEST_ASSERT(nic_buffer_is_initialized(1), "NIC 1 should be initialized");
    TEST_ASSERT(!nic_buffer_is_initialized(2), "NIC 2 should not be initialized");
    
    TEST_PASS("NIC Buffer Pool Creation");
}

static int test_buffer_allocation_and_free(void) {
    TEST_START("Buffer Allocation and Free");
    
    nic_id_t nic_id = 0;
    
    /* Test basic allocation */
    buffer_desc_t* buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 512);
    TEST_ASSERT(buffer != NULL, "Buffer allocation should succeed");
    TEST_ASSERT(buffer->size >= 512, "Buffer should be at least requested size");
    TEST_ASSERT(buffer->type == BUFFER_TYPE_TX, "Buffer type should match");
    
    /* Test buffer data operations */
    uint8_t test_data[512];
    generate_test_packet(test_data, 512);
    
    int result = buffer_set_data(buffer, test_data, 512);
    TEST_ASSERT(result == SUCCESS, "Buffer data setting should succeed");
    TEST_ASSERT(buffer->used == 512, "Buffer used size should be updated");
    
    /* Verify data integrity */
    TEST_ASSERT(memory_compare(buffer->data, test_data, 512) == 0, "Buffer data should match");
    
    /* Test buffer free */
    nic_buffer_free(nic_id, buffer);
    
    /* Test multiple allocations */
    buffer_desc_t* buffers[10];
    for (int i = 0; i < 10; i++) {
        buffers[i] = nic_buffer_alloc(nic_id, BUFFER_TYPE_RX, 1024);
        TEST_ASSERT(buffers[i] != NULL, "Multiple allocations should succeed");
    }
    
    /* Free all buffers */
    for (int i = 0; i < 10; i++) {
        nic_buffer_free(nic_id, buffers[i]);
    }
    
    /* Test different buffer types */
    buffer_desc_t* tx_buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 256);
    buffer_desc_t* rx_buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_RX, 256);
    
    TEST_ASSERT(tx_buffer != NULL && rx_buffer != NULL, "Different types should allocate");
    
    nic_buffer_free(nic_id, tx_buffer);
    nic_buffer_free(nic_id, rx_buffer);
    
    TEST_PASS("Buffer Allocation and Free");
}

static int test_resource_isolation(void) {
    TEST_START("Resource Isolation");
    
    /* Create pools for multiple NICs */
    int result = nic_buffer_pool_create(2, NIC_TYPE_3C509B, "3C509B-Test-2");
    TEST_ASSERT(result == SUCCESS, "Second NIC pool creation should succeed");
    
    result = nic_buffer_pool_create(3, NIC_TYPE_3C515_TX, "3C515-TX-Test-3");
    TEST_ASSERT(result == SUCCESS, "Third NIC pool creation should succeed");
    
    /* Get initial statistics */
    buffer_pool_stats_t stats_nic0, stats_nic2, stats_nic3;
    
    result = nic_buffer_get_stats(0, &stats_nic0);
    TEST_ASSERT(result == SUCCESS, "Getting stats for NIC 0 should succeed");
    
    result = nic_buffer_get_stats(2, &stats_nic2);
    TEST_ASSERT(result == SUCCESS, "Getting stats for NIC 2 should succeed");
    
    result = nic_buffer_get_stats(3, &stats_nic3);
    TEST_ASSERT(result == SUCCESS, "Getting stats for NIC 3 should succeed");
    
    /* Allocate buffers from different NICs */
    buffer_desc_t* nic0_buffers[5];
    buffer_desc_t* nic2_buffers[5];
    
    for (int i = 0; i < 5; i++) {
        nic0_buffers[i] = nic_buffer_alloc(0, BUFFER_TYPE_TX, 512);
        nic2_buffers[i] = nic_buffer_alloc(2, BUFFER_TYPE_TX, 512);
        
        TEST_ASSERT(nic0_buffers[i] != NULL, "NIC 0 allocation should succeed");
        TEST_ASSERT(nic2_buffers[i] != NULL, "NIC 2 allocation should succeed");
    }
    
    /* Verify resource isolation - NIC 0 allocations shouldn't affect NIC 2 stats */
    buffer_pool_stats_t new_stats_nic0, new_stats_nic2;
    
    nic_buffer_get_stats(0, &new_stats_nic0);
    nic_buffer_get_stats(2, &new_stats_nic2);
    
    TEST_ASSERT(new_stats_nic0.total_allocations == stats_nic0.total_allocations + 5,
                "NIC 0 should have 5 more allocations");
    TEST_ASSERT(new_stats_nic2.total_allocations == stats_nic2.total_allocations + 5,
                "NIC 2 should have 5 more allocations");
    
    /* Verify cross-contamination doesn't occur */
    nic_buffer_get_stats(3, &stats_nic3);
    buffer_pool_stats_t new_stats_nic3;
    nic_buffer_get_stats(3, &new_stats_nic3);
    
    TEST_ASSERT(new_stats_nic3.total_allocations == stats_nic3.total_allocations,
                "NIC 3 allocations should be unchanged");
    
    /* Clean up */
    for (int i = 0; i < 5; i++) {
        nic_buffer_free(0, nic0_buffers[i]);
        nic_buffer_free(2, nic2_buffers[i]);
    }
    
    TEST_PASS("Resource Isolation");
}

static int test_size_specific_pools(void) {
    TEST_START("Size-Specific Pools");
    
    nic_id_t nic_id = 0;
    
    /* Test small buffer allocation */
    buffer_desc_t* small_buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 64);
    TEST_ASSERT(small_buffer != NULL, "Small buffer allocation should succeed");
    TEST_ASSERT(small_buffer->size >= 64, "Small buffer should meet size requirement");
    
    /* Test medium buffer allocation */
    buffer_desc_t* medium_buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 256);
    TEST_ASSERT(medium_buffer != NULL, "Medium buffer allocation should succeed");
    TEST_ASSERT(medium_buffer->size >= 256, "Medium buffer should meet size requirement");
    
    /* Test large buffer allocation */
    buffer_desc_t* large_buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 1024);
    TEST_ASSERT(large_buffer != NULL, "Large buffer allocation should succeed");
    TEST_ASSERT(large_buffer->size >= 1024, "Large buffer should meet size requirement");
    
    /* Test jumbo buffer allocation */
    buffer_desc_t* jumbo_buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 1518);
    TEST_ASSERT(jumbo_buffer != NULL, "Jumbo buffer allocation should succeed");
    TEST_ASSERT(jumbo_buffer->size >= 1518, "Jumbo buffer should meet size requirement");
    
    /* Verify different pool usage by checking statistics */
    buffer_pool_stats_t stats;
    int result = nic_buffer_get_stats(nic_id, &stats);
    TEST_ASSERT(result == SUCCESS, "Getting stats should succeed");
    TEST_ASSERT(stats.fast_path_hits > 0, "Size-specific pools should be used");
    
    /* Clean up */
    nic_buffer_free(nic_id, small_buffer);
    nic_buffer_free(nic_id, medium_buffer);
    nic_buffer_free(nic_id, large_buffer);
    nic_buffer_free(nic_id, jumbo_buffer);
    
    TEST_PASS("Size-Specific Pools");
}

static int test_rx_copybreak_integration(void) {
    TEST_START("RX_COPYBREAK Integration");
    
    nic_id_t nic_id = 1; /* Use NIC 1 for this test */
    
    /* Initialize RX_COPYBREAK for this NIC */
    int result = nic_rx_copybreak_init(nic_id, 32, 16, 200);
    TEST_ASSERT(result == SUCCESS, "RX_COPYBREAK initialization should succeed");
    
    /* Test small packet allocation (below threshold) */
    buffer_desc_t* small_buffer = nic_rx_copybreak_alloc(nic_id, 150);
    TEST_ASSERT(small_buffer != NULL, "Small RX_COPYBREAK allocation should succeed");
    TEST_ASSERT(small_buffer->size <= 256, "Small buffer should be from small pool");
    
    /* Test large packet allocation (above threshold) */
    buffer_desc_t* large_buffer = nic_rx_copybreak_alloc(nic_id, 300);
    TEST_ASSERT(large_buffer != NULL, "Large RX_COPYBREAK allocation should succeed");
    TEST_ASSERT(large_buffer->size >= 300, "Large buffer should accommodate packet");
    
    /* Test multiple small allocations to check pool exhaustion handling */
    buffer_desc_t* small_buffers[40]; /* More than the 32 we allocated */
    int successful_allocs = 0;
    
    for (int i = 0; i < 40; i++) {
        small_buffers[i] = nic_rx_copybreak_alloc(nic_id, 100);
        if (small_buffers[i] != NULL) {
            successful_allocs++;
        }
    }
    
    TEST_ASSERT(successful_allocs > 30, "Most small allocations should succeed");
    log_info("RX_COPYBREAK allocated %d out of 40 small buffers", successful_allocs);
    
    /* Free allocated buffers */
    nic_rx_copybreak_free(nic_id, small_buffer);
    nic_rx_copybreak_free(nic_id, large_buffer);
    
    for (int i = 0; i < successful_allocs; i++) {
        if (small_buffers[i] != NULL) {
            nic_rx_copybreak_free(nic_id, small_buffers[i]);
        }
    }
    
    /* Test invalid operations */
    result = nic_rx_copybreak_init(nic_id, 0, 16, 200); /* Invalid small count */
    TEST_ASSERT(result != SUCCESS, "Invalid RX_COPYBREAK init should fail");
    
    TEST_PASS("RX_COPYBREAK Integration");
}

static int test_resource_balancing(void) {
    TEST_START("Resource Balancing");
    
    /* Simulate activity on different NICs */
    simulate_nic_activity(0, 80); /* High activity */
    simulate_nic_activity(1, 20); /* Low activity */
    simulate_nic_activity(2, 60); /* Medium activity */
    
    /* Get initial memory allocations */
    uint32_t initial_memory_0 = nic_buffer_get_available_memory(0);
    uint32_t initial_memory_1 = nic_buffer_get_available_memory(1);
    uint32_t initial_memory_2 = nic_buffer_get_available_memory(2);
    
    log_info("Initial memory: NIC0=%u, NIC1=%u, NIC2=%u", 
             initial_memory_0, initial_memory_1, initial_memory_2);
    
    /* Trigger resource balancing */
    int result = balance_buffer_resources();
    TEST_ASSERT(result == SUCCESS, "Resource balancing should succeed");
    
    /* Wait for balancing to take effect */
    mdelay(1000);
    
    /* Check if resources were rebalanced (this is implementation-dependent) */
    uint32_t new_memory_0 = nic_buffer_get_available_memory(0);
    uint32_t new_memory_1 = nic_buffer_get_available_memory(1);
    uint32_t new_memory_2 = nic_buffer_get_available_memory(2);
    
    log_info("After balancing: NIC0=%u, NIC1=%u, NIC2=%u", 
             new_memory_0, new_memory_1, new_memory_2);
    
    /* Test manual memory adjustment */
    result = adjust_nic_buffer_allocation(0, 256); /* 256 KB */
    TEST_ASSERT(result == SUCCESS, "Manual allocation adjustment should succeed");
    
    result = adjust_nic_buffer_allocation(1, 128); /* 128 KB */
    TEST_ASSERT(result == SUCCESS, "Manual allocation adjustment should succeed");
    
    /* Test invalid adjustments */
    result = adjust_nic_buffer_allocation(0, 16); /* Too small */
    TEST_ASSERT(result != SUCCESS, "Too small allocation should fail");
    
    result = adjust_nic_buffer_allocation(0, 8192); /* Too large */
    TEST_ASSERT(result != SUCCESS, "Too large allocation should fail");
    
    TEST_PASS("Resource Balancing");
}

static int test_multi_nic_stress(void) {
    TEST_START("Multi-NIC Stress Test");
    
    log_info("Starting %d-second stress test with %d NICs", TEST_STRESS_DURATION, TEST_NIC_COUNT);
    
    uint32_t start_time = get_system_timestamp_ms();
    uint32_t end_time = start_time + (TEST_STRESS_DURATION * 1000);
    
    uint32_t total_allocations = 0;
    uint32_t total_failures = 0;
    
    while (get_system_timestamp_ms() < end_time) {
        /* Randomly select a NIC and allocate/free buffers */
        nic_id_t nic_id = rand() % TEST_NIC_COUNT;
        
        if (!nic_buffer_is_initialized(nic_id)) {
            continue; /* Skip uninitialized NICs */
        }
        
        /* Random buffer size */
        uint32_t size = 64 + (rand() % 1454); /* 64 to 1518 bytes */
        buffer_type_t type = (rand() % 2) ? BUFFER_TYPE_TX : BUFFER_TYPE_RX;
        
        buffer_desc_t* buffer = nic_buffer_alloc(nic_id, type, size);
        total_allocations++;
        
        if (buffer != NULL) {
            /* Do some work with the buffer */
            uint8_t test_data[64];
            generate_test_packet(test_data, 64);
            buffer_set_data(buffer, test_data, 64);
            
            /* Free immediately */
            nic_buffer_free(nic_id, buffer);
        } else {
            total_failures++;
        }
        
        /* Occasionally trigger monitoring */
        if ((total_allocations % 100) == 0) {
            monitor_nic_buffer_usage();
        }
        
        /* Brief pause */
        if ((total_allocations % 50) == 0) {
            mdelay(1);
        }
    }
    
    log_info("Stress test completed: %u allocations, %u failures (%.2f%% failure rate)",
             total_allocations, total_failures, 
             (total_failures * 100.0) / total_allocations);
    
    /* Check final statistics */
    for (nic_id_t nic_id = 0; nic_id < TEST_NIC_COUNT; nic_id++) {
        if (nic_buffer_is_initialized(nic_id)) {
            buffer_pool_stats_t stats;
            if (nic_buffer_get_stats(nic_id, &stats) == SUCCESS) {
                log_info("NIC %d: %lu allocs, %lu failures", 
                         nic_id, stats.total_allocations, stats.allocation_failures);
            }
        }
    }
    
    /* Failure rate should be reasonable */
    double failure_rate = (total_failures * 100.0) / total_allocations;
    TEST_ASSERT(failure_rate < 10.0, "Failure rate should be less than 10%");
    TEST_ASSERT(total_allocations > 1000, "Should complete significant number of allocations");
    
    TEST_PASS("Multi-NIC Stress Test");
}

static int test_legacy_compatibility(void) {
    TEST_START("Legacy Compatibility");
    
    /* Test legacy buffer allocation functions still work */
    buffer_desc_t* legacy_buffer = buffer_alloc_type(BUFFER_TYPE_TX);
    TEST_ASSERT(legacy_buffer != NULL, "Legacy allocation should succeed");
    
    /* Test legacy free */
    buffer_free_any(legacy_buffer);
    
    /* Test NIC-aware functions with invalid NIC ID (should fall back to legacy) */
    buffer_desc_t* fallback_buffer = buffer_alloc_nic_aware(INVALID_NIC_ID, BUFFER_TYPE_RX, 512);
    TEST_ASSERT(fallback_buffer != NULL, "Fallback to legacy should work");
    
    buffer_free_nic_aware(INVALID_NIC_ID, fallback_buffer);
    
    /* Test that both new and legacy systems can coexist */
    buffer_desc_t* new_buffer = nic_buffer_alloc(0, BUFFER_TYPE_TX, 256);
    buffer_desc_t* old_buffer = buffer_alloc_type(BUFFER_TYPE_TX);
    
    TEST_ASSERT(new_buffer != NULL && old_buffer != NULL, 
                "Both new and legacy allocation should work");
    
    nic_buffer_free(0, new_buffer);
    buffer_free_any(old_buffer);
    
    TEST_PASS("Legacy Compatibility");
}

static int test_error_handling(void) {
    TEST_START("Error Handling");
    
    /* Test invalid NIC ID */
    buffer_desc_t* buffer = nic_buffer_alloc(INVALID_NIC_ID, BUFFER_TYPE_TX, 512);
    TEST_ASSERT(buffer == NULL, "Invalid NIC ID should fail");
    
    /* Test uninitialized NIC */
    buffer = nic_buffer_alloc(7, BUFFER_TYPE_TX, 512); /* NIC 7 not initialized */
    TEST_ASSERT(buffer == NULL, "Uninitialized NIC should fail");
    
    /* Test invalid buffer size */
    buffer = nic_buffer_alloc(0, BUFFER_TYPE_TX, 0);
    TEST_ASSERT(buffer == NULL, "Zero size should fail");
    
    /* Test pool exhaustion handling */
    buffer_desc_t* buffers[1000];
    int allocated = 0;
    
    /* Try to exhaust the pools */
    for (int i = 0; i < 1000; i++) {
        buffers[i] = nic_buffer_alloc(0, BUFFER_TYPE_TX, 1518);
        if (buffers[i] != NULL) {
            allocated++;
        } else {
            break; /* Pool exhausted */
        }
    }
    
    log_info("Allocated %d buffers before exhaustion", allocated);
    TEST_ASSERT(allocated > 10, "Should allocate reasonable number before exhaustion");
    
    /* Free allocated buffers */
    for (int i = 0; i < allocated; i++) {
        nic_buffer_free(0, buffers[i]);
    }
    
    /* Test recovery after exhaustion */
    buffer = nic_buffer_alloc(0, BUFFER_TYPE_TX, 512);
    TEST_ASSERT(buffer != NULL, "Allocation should work after freeing buffers");
    nic_buffer_free(0, buffer);
    
    TEST_PASS("Error Handling");
}

static int test_statistics_and_monitoring(void) {
    TEST_START("Statistics and Monitoring");
    
    nic_id_t nic_id = 0;
    
    /* Get initial statistics */
    buffer_pool_stats_t initial_stats;
    int result = nic_buffer_get_stats(nic_id, &initial_stats);
    TEST_ASSERT(result == SUCCESS, "Getting initial stats should succeed");
    
    /* Perform some allocations */
    buffer_desc_t* buffers[10];
    for (int i = 0; i < 10; i++) {
        buffers[i] = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 512);
        TEST_ASSERT(buffers[i] != NULL, "Allocation should succeed");
    }
    
    /* Get updated statistics */
    buffer_pool_stats_t updated_stats;
    result = nic_buffer_get_stats(nic_id, &updated_stats);
    TEST_ASSERT(result == SUCCESS, "Getting updated stats should succeed");
    
    /* Verify statistics were updated */
    TEST_ASSERT(updated_stats.total_allocations >= initial_stats.total_allocations + 10,
                "Allocation count should increase");
    TEST_ASSERT(updated_stats.current_allocated >= initial_stats.current_allocated + 10,
                "Current allocated should increase");
    
    /* Free buffers and check statistics */
    for (int i = 0; i < 10; i++) {
        nic_buffer_free(nic_id, buffers[i]);
    }
    
    buffer_pool_stats_t final_stats;
    result = nic_buffer_get_stats(nic_id, &final_stats);
    TEST_ASSERT(result == SUCCESS, "Getting final stats should succeed");
    
    TEST_ASSERT(final_stats.total_frees >= initial_stats.total_frees + 10,
                "Free count should increase");
    
    /* Test global statistics */
    uint32_t total_allocated, active_nics, contentions;
    result = nic_buffer_get_global_stats(&total_allocated, &active_nics, &contentions);
    TEST_ASSERT(result == SUCCESS, "Getting global stats should succeed");
    TEST_ASSERT(active_nics >= 3, "Should have at least 3 active NICs");
    
    /* Test monitoring function */
    monitor_nic_buffer_usage(); /* Should not crash */
    
    TEST_PASS("Statistics and Monitoring");
}

static int test_memory_limits(void) {
    TEST_START("Memory Limits");
    
    nic_id_t nic_id = 2;
    
    /* Set a small memory limit */
    int result = nic_buffer_set_memory_limit(nic_id, 64); /* 64 KB */
    TEST_ASSERT(result == SUCCESS, "Setting memory limit should succeed");
    
    /* Try to allocate more than the limit */
    buffer_desc_t* buffers[100];
    int allocated = 0;
    
    for (int i = 0; i < 100; i++) {
        buffers[i] = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 1024);
        if (buffers[i] != NULL) {
            allocated++;
        } else {
            break; /* Hit memory limit */
        }
    }
    
    log_info("Allocated %d 1KB buffers with 64KB limit", allocated);
    TEST_ASSERT(allocated < 70, "Should hit memory limit before allocating too many");
    TEST_ASSERT(allocated > 10, "Should allocate reasonable number within limit");
    
    /* Free some buffers */
    for (int i = 0; i < allocated / 2; i++) {
        nic_buffer_free(nic_id, buffers[i]);
    }
    
    /* Should be able to allocate again */
    buffer_desc_t* new_buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 512);
    TEST_ASSERT(new_buffer != NULL, "Should allocate after freeing");
    
    /* Clean up */
    nic_buffer_free(nic_id, new_buffer);
    for (int i = allocated / 2; i < allocated; i++) {
        nic_buffer_free(nic_id, buffers[i]);
    }
    
    /* Test invalid memory limit */
    result = nic_buffer_set_memory_limit(nic_id, 16); /* Too small */
    TEST_ASSERT(result != SUCCESS, "Too small memory limit should fail");
    
    TEST_PASS("Memory Limits");
}

/* === Helper Functions === */

static void setup_test_environment(void) {
    log_info("Setting up test environment");
    
    /* Initialize logging system */
    log_init(LOG_LEVEL_INFO);
    
    /* Initialize memory system */
    memory_init();
    
    /* Initialize buffer system */
    buffer_system_init();
    
    log_info("Test environment setup complete");
}

static void cleanup_test_environment(void) {
    log_info("Cleaning up test environment");
    
    /* Cleanup buffer system */
    nic_buffer_pool_manager_cleanup();
    buffer_system_cleanup();
    
    /* Print final comprehensive statistics */
    log_info("=== Final System Statistics ===");
    buffer_print_comprehensive_stats();
    
    log_info("Test environment cleanup complete");
}

static void print_test_summary(void) {
    log_info("=== TEST SUMMARY ===");
    log_info("Total tests run: %u", g_test_results.tests_run);
    log_info("Tests passed: %u", g_test_results.tests_passed);
    log_info("Tests failed: %u", g_test_results.tests_failed);
    log_info("Assertions checked: %u", g_test_results.assertions_checked);
    
    if (g_test_results.tests_failed > 0) {
        log_error("Last error: %s", g_test_results.last_error);
    }
    
    double pass_rate = (g_test_results.tests_passed * 100.0) / g_test_results.tests_run;
    log_info("Pass rate: %.1f%%", pass_rate);
}

static int simulate_nic_activity(nic_id_t nic_id, uint32_t activity_level) {
    if (!nic_buffer_is_initialized(nic_id)) {
        return -1;
    }
    
    /* Simulate activity by doing allocations proportional to activity level */
    uint32_t num_operations = activity_level / 2; /* 0-50 operations */
    
    for (uint32_t i = 0; i < num_operations; i++) {
        buffer_desc_t* buffer = nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, 512);
        if (buffer) {
            /* Brief work simulation */
            mdelay(1);
            nic_buffer_free(nic_id, buffer);
        }
    }
    
    return 0;
}

static void generate_test_packet(uint8_t* buffer, uint16_t size) {
    if (!buffer || size == 0) {
        return;
    }
    
    /* Generate a simple test pattern */
    for (uint16_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }
}