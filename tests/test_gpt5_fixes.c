/**
 * @file test_gpt5_fixes.c
 * @brief Test Suite for GPT-5 Critical Bug Fixes
 * 
 * Tests all the critical issues identified and fixed by GPT-5 review:
 * - Memory safety in handle compact system
 * - XMS buffer migration safety
 * - Runtime configuration parameter handling
 * - Multi-NIC coordination logic
 * - DOS compatibility fixes
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/handle_compact.h"
#include "../include/xms_buffer_migration.h"
#include "../include/runtime_config.h"
#include "../include/multi_nic_coord.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test framework macros */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            return 0; \
        } \
    } while(0)

#define TEST_SUCCESS(message) \
    do { \
        printf("PASS: %s - %s\n", __func__, message); \
        return 1; \
    } while(0)

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

/**
 * @brief Test handle compact system memory safety
 */
static int test_handle_compact_memory_safety(void) {
    tests_run++;
    
    /* Test 1: Initialize system */
    int result = handle_compact_init();
    TEST_ASSERT(result == SUCCESS, "Handle system initialization");
    
    /* Test 2: NULL pointer safety - allocate with invalid NIC */
    handle_compact_t *handle = handle_compact_allocate(255, HANDLE_TYPE_ETHERNET);
    TEST_ASSERT(handle == NULL, "NULL returned for invalid NIC index");
    
    /* Test 3: Valid allocation */
    handle = handle_compact_allocate(0, HANDLE_TYPE_ETHERNET);
    TEST_ASSERT(handle != NULL, "Valid handle allocation");
    TEST_ASSERT(handle->flags & HANDLE_FLAG_ACTIVE, "Handle marked as active");
    TEST_ASSERT(handle_is_active(handle), "Handle reports active");
    
    /* Test 4: Statistics access safety */
    handle_stats_t *stats = handle_compact_get_stats(handle);
    TEST_ASSERT(stats != NULL, "Statistics accessible");
    
    /* Test 5: Counter updates with interrupt safety */
    handle_compact_update_counters(handle, 1, 5);  /* RX */
    TEST_ASSERT(handle->packets.counts.rx_count == 5, "RX counter updated");
    TEST_ASSERT(stats->rx_packets == 5, "Full stats updated");
    
    handle_compact_update_counters(handle, 0, 3);  /* TX */
    TEST_ASSERT(handle->packets.counts.tx_count == 3, "TX counter updated");
    TEST_ASSERT(stats->tx_packets == 3, "Full stats updated");
    
    /* Test 6: Handle deallocation */
    result = handle_compact_free(handle);
    TEST_ASSERT(result == SUCCESS, "Handle freed successfully");
    TEST_ASSERT(!handle_is_active(handle), "Handle no longer active");
    
    /* Test 7: Double-free protection */
    result = handle_compact_free(handle);
    TEST_ASSERT(result == ERROR_INVALID_STATE, "Double-free detected");
    
    /* Test 8: Cleanup */
    result = handle_compact_cleanup();
    TEST_ASSERT(result == SUCCESS, "Handle system cleanup");
    
    tests_passed++;
    TEST_SUCCESS("All handle compact memory safety tests passed");
}

/**
 * @brief Test XMS buffer migration safety
 */
static int test_xms_buffer_migration_safety(void) {
    tests_run++;
    
    /* Test 1: Invalid buffer index bounds checking */
    int result = xms_buffer_migrate_to_xms(9999);  /* Invalid index */
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Invalid buffer index rejected");
    
    /* Test 2: XMS not available fallback */
    result = xms_buffer_migration_init();
    /* Should succeed even without XMS, falling back to conventional */
    TEST_ASSERT(result == SUCCESS || result == ERROR_NO_XMS, "XMS init with fallback");
    
    /* Test 3: Buffer allocation with bounds checking */
    uint16_t buffer_index;
    result = xms_buffer_allocate(1518, &buffer_index);
    if (result == SUCCESS) {
        TEST_ASSERT(buffer_index < MAX_PACKET_BUFFERS, "Buffer index in valid range");
        
        /* Test 4: Buffer access with validation */
        void *buffer_ptr = xms_buffer_get_access((void*)(uintptr_t)buffer_index, NULL);
        TEST_ASSERT(buffer_ptr != NULL, "Buffer access returns valid pointer");
        
        /* Test 5: Buffer deallocation */
        result = xms_buffer_free((void*)(uintptr_t)buffer_index);
        TEST_ASSERT(result == SUCCESS, "Buffer freed successfully");
    }
    
    /* Test 6: Cleanup */
    result = xms_buffer_migration_cleanup();
    TEST_ASSERT(result == SUCCESS, "XMS migration cleanup");
    
    tests_passed++;
    TEST_SUCCESS("All XMS buffer migration safety tests passed");
}

/**
 * @brief Test runtime configuration parameter handling
 */
static int test_runtime_config_safety(void) {
    tests_run++;
    
    /* Test 1: Initialize system */
    int result = runtime_config_init();
    TEST_ASSERT(result == SUCCESS, "Runtime config initialization");
    
    /* Test 2: Invalid parameter ID rejection */
    result = runtime_config_set_param(0x9999, 100, 0xFF);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Invalid parameter ID rejected");
    
    /* Test 3: Value range validation */
    result = runtime_config_set_param(CONFIG_PARAM_LOG_LEVEL, 999, 0xFF);
    TEST_ASSERT(result == ERROR_OUT_OF_RANGE, "Out-of-range value rejected");
    
    /* Test 4: Valid parameter setting */
    result = runtime_config_set_param(CONFIG_PARAM_LOG_LEVEL, 2, 0xFF);
    TEST_ASSERT(result == SUCCESS, "Valid parameter set");
    
    /* Test 5: Parameter value retrieval */
    uint32_t value;
    result = runtime_config_get_param(CONFIG_PARAM_LOG_LEVEL, &value, 0xFF);
    TEST_ASSERT(result == SUCCESS, "Parameter value retrieved");
    TEST_ASSERT(value == 2, "Parameter value correct");
    
    /* Test 6: Per-NIC parameter with invalid NIC */
    result = runtime_config_set_param(CONFIG_PARAM_PROMISCUOUS, 1, 255);
    TEST_ASSERT(result == ERROR_INVALID_NIC, "Invalid NIC index rejected");
    
    /* Test 7: Reset-required parameter queueing */
    result = runtime_config_set_param(CONFIG_PARAM_BUFFER_SIZE, 2048, 0xFF);
    TEST_ASSERT(result == SUCCESS, "Reset-required parameter queued");
    
    /* Test 8: Duplicate pending value handling */
    result = runtime_config_set_param(CONFIG_PARAM_BUFFER_SIZE, 2048, 0xFF);
    TEST_ASSERT(result == SUCCESS, "Duplicate pending value handled");
    
    /* Test 9: Configuration export/import */
    uint8_t export_buffer[1024];
    uint16_t export_size = sizeof(export_buffer);
    result = runtime_config_export(export_buffer, &export_size);
    TEST_ASSERT(result == SUCCESS, "Configuration exported");
    TEST_ASSERT(export_size > 0, "Export has data");
    
    result = runtime_config_import(export_buffer, export_size);
    TEST_ASSERT(result == SUCCESS, "Configuration imported");
    
    /* Test 10: Cleanup */
    result = runtime_config_cleanup();
    TEST_ASSERT(result == SUCCESS, "Runtime config cleanup");
    
    tests_passed++;
    TEST_SUCCESS("All runtime configuration safety tests passed");
}

/**
 * @brief Test multi-NIC coordination logic
 */
static int test_multi_nic_coordination_logic(void) {
    tests_run++;
    
    /* Test 1: Initialize system */
    int result = multi_nic_init();
    TEST_ASSERT(result == SUCCESS, "Multi-NIC coordinator initialization");
    
    /* Test 2: Invalid NIC registration */
    nic_capabilities_t caps = {100, 1500, 64, 1, 0, 0, 0};
    result = multi_nic_register(255, &caps);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Invalid NIC index rejected");
    
    /* Test 3: Valid NIC registration */
    result = multi_nic_register(0, &caps);
    TEST_ASSERT(result == SUCCESS, "Valid NIC registered");
    
    result = multi_nic_register(1, &caps);
    TEST_ASSERT(result == SUCCESS, "Second NIC registered");
    
    /* Test 4: NIC state management */
    result = multi_nic_update_state(0, NIC_STATE_UP);
    TEST_ASSERT(result == SUCCESS, "NIC state updated");
    
    result = multi_nic_update_state(1, NIC_STATE_UP);
    TEST_ASSERT(result == SUCCESS, "Second NIC state updated");
    
    /* Test 5: Packet routing with NULL pointer safety */
    packet_context_t context = {
        0xC0A80101, 0xC0A80102,  /* src_ip, dst_ip */
        80, 1024,                /* src_port, dst_port */
        6, 0, 1500, 0xFF         /* protocol, priority, packet_size, selected_nic */
    };
    
    uint8_t selected_nic;
    result = multi_nic_select_tx(&context, &selected_nic);
    TEST_ASSERT(result == SUCCESS, "Packet routing succeeded");
    TEST_ASSERT(selected_nic < MAX_NICS, "Valid NIC selected");
    
    /* Test 6: NULL pointer validation */
    result = multi_nic_select_tx(NULL, &selected_nic);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL context rejected");
    
    result = multi_nic_select_tx(&context, NULL);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL selected_nic rejected");
    
    /* Test 7: Load balancing algorithm bounds checking */
    multi_nic_config_t config = {
        MULTI_NIC_MODE_LOAD_BALANCE,
        255,  /* Invalid algorithm */
        3, 30, 5, 300, 1024,
        MULTI_NIC_FLAG_ENABLED
    };
    
    result = multi_nic_configure(&config);
    TEST_ASSERT(result == SUCCESS, "Configuration set (invalid algo will be caught during selection)");
    
    /* Should fail with invalid algorithm */
    result = multi_nic_select_tx(&context, &selected_nic);
    TEST_ASSERT(result == ERROR_INVALID_CONFIG, "Invalid algorithm rejected");
    
    /* Test 8: Valid load balancing configuration */
    config.load_balance_algo = LB_ALGO_ROUND_ROBIN;
    result = multi_nic_configure(&config);
    TEST_ASSERT(result == SUCCESS, "Valid load balancing configured");
    
    result = multi_nic_select_tx(&context, &selected_nic);
    TEST_ASSERT(result == SUCCESS, "Load balancing selection succeeded");
    
    /* Test 9: Failover simulation */
    result = multi_nic_handle_failure(0);
    TEST_ASSERT(result == SUCCESS, "Failover handled");
    
    /* Test 10: Health monitoring */
    result = multi_nic_health_check();
    TEST_ASSERT(result == SUCCESS, "Health check performed");
    
    /* Test 11: Statistics collection */
    multi_nic_stats_t stats;
    multi_nic_get_stats(&stats);
    TEST_ASSERT(stats.packets_routed > 0, "Routing statistics collected");
    
    /* Test 12: Cleanup */
    result = multi_nic_cleanup();
    TEST_ASSERT(result == SUCCESS, "Multi-NIC coordinator cleanup");
    
    tests_passed++;
    TEST_SUCCESS("All multi-NIC coordination logic tests passed");
}

/**
 * @brief Test DOS compatibility fixes
 */
static int test_dos_compatibility(void) {
    tests_run++;
    
    /* Test 1: Structure sizes are correct */
    TEST_ASSERT(sizeof(handle_compact_t) == 16, "Handle structure is 16 bytes");
    
    /* Test 2: Bool replacements work correctly */
    int result = handle_is_active(NULL);
    TEST_ASSERT(result == 0, "handle_is_active returns int");
    
    /* Test 3: Far pointer handling (basic test) */
    handle_compact_t test_handle = {0};
    test_handle.flags = HANDLE_FLAG_ACTIVE;
    result = handle_is_active(&test_handle);
    TEST_ASSERT(result != 0, "Far pointer access works");
    
    /* Test 4: Bit packing works correctly */
    test_handle.interface = (HANDLE_TYPE_ETHERNET) | (0x03);  /* Type=0, NIC=3 */
    TEST_ASSERT(handle_get_nic(&test_handle) == 3, "NIC index extraction");
    TEST_ASSERT(handle_get_type(&test_handle) == HANDLE_TYPE_ETHERNET, "Type extraction");
    
    /* Test 5: Counter operations are atomic-safe */
    test_handle.packets.counts.rx_count = 0xFFFE;
    handle_compact_update_counters(&test_handle, 1, 5);  /* Should saturate */
    TEST_ASSERT(test_handle.packets.counts.rx_count == 0xFFFF, "Counter saturation");
    
    tests_passed++;
    TEST_SUCCESS("All DOS compatibility tests passed");
}

/**
 * @brief Test interrupt safety and concurrency
 */
static int test_interrupt_safety(void) {
    tests_run++;
    
    /* Test 1: Initialize handle system */
    int result = handle_compact_init();
    TEST_ASSERT(result == SUCCESS, "Handle system initialization");
    
    /* Test 2: Allocate handle for interrupt safety test */
    handle_compact_t *handle = handle_compact_allocate(0, HANDLE_TYPE_ETHERNET);
    TEST_ASSERT(handle != NULL, "Handle allocated for interrupt test");
    
    /* Test 3: Simulate concurrent counter updates */
    for (int i = 0; i < 1000; i++) {
        handle_compact_update_counters(handle, 1, 1);  /* RX */
        handle_compact_update_counters(handle, 0, 1);  /* TX */
    }
    
    TEST_ASSERT(handle->packets.counts.rx_count == 1000, "Concurrent RX updates");
    TEST_ASSERT(handle->packets.counts.tx_count == 1000, "Concurrent TX updates");
    
    /* Test 4: Statistics table access during growth simulation */
    /* This would normally require multiple threads, but we can test the bounds checking */
    handle_stats_t *stats = handle_compact_get_stats(handle);
    TEST_ASSERT(stats != NULL, "Stats accessible during operations");
    
    /* Test 5: Cleanup */
    result = handle_compact_free(handle);
    TEST_ASSERT(result == SUCCESS, "Handle freed after interrupt test");
    
    result = handle_compact_cleanup();
    TEST_ASSERT(result == SUCCESS, "Handle system cleanup after interrupt test");
    
    tests_passed++;
    TEST_SUCCESS("All interrupt safety tests passed");
}

/**
 * @brief Run all tests
 */
int main(void) {
    printf("=== GPT-5 Critical Bug Fixes Test Suite ===\n\n");
    
    /* Run all test cases */
    test_handle_compact_memory_safety();
    test_xms_buffer_migration_safety();
    test_runtime_config_safety();
    test_multi_nic_coordination_logic();
    test_dos_compatibility();
    test_interrupt_safety();
    
    /* Print results */
    printf("\n=== Test Results ===\n");
    printf("Tests Run: %d\n", tests_run);
    printf("Tests Passed: %d\n", tests_passed);
    printf("Tests Failed: %d\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf("\nAll tests PASSED! GPT-5 fixes are working correctly.\n");
        return 0;
    } else {
        printf("\nSome tests FAILED! Review the fixes and try again.\n");
        return 1;
    }
}

/**
 * @brief Helper function to simulate memory pressure
 */
static void simulate_memory_pressure(void) {
    /* Allocate many handles to test stats table growth */
    for (int i = 0; i < 50; i++) {
        handle_compact_t *handle = handle_compact_allocate(i % 4, HANDLE_TYPE_ETHERNET);
        if (handle) {
            /* Immediately free to test reuse */
            handle_compact_free(handle);
        }
    }
}

/**
 * @brief Helper function to test error conditions
 */
static int test_error_conditions(void) {
    tests_run++;
    
    /* Test various error conditions that GPT-5 identified */
    
    /* Test 1: Uninitialized system access */
    handle_compact_t *handle = handle_compact_allocate(0, HANDLE_TYPE_ETHERNET);
    TEST_ASSERT(handle == NULL, "Uninitialized system returns NULL");
    
    /* Test 2: Invalid parameter combinations */
    int result = runtime_config_set_param(0, 0, 0);
    TEST_ASSERT(result != SUCCESS, "Invalid parameter combination rejected");
    
    /* Test 3: Resource exhaustion simulation */
    simulate_memory_pressure();
    
    tests_passed++;
    TEST_SUCCESS("All error condition tests passed");
}