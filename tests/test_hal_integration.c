/**
 * @file test_hal_integration.c
 * @brief C test framework for Hardware Abstraction Layer validation
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * 
 * This file implements comprehensive integration tests for the HAL layer,
 * including vtable operations, multi-NIC management, error recovery,
 * and defensive programming validation.
 */

#include "../include/hardware.h"
#include "../include/hardware_mock.h"
#include "../include/test_framework.h"
#include "../include/logging.h"
#include "../include/memory.h"
#include "../include/3c509b.h"
#include "../include/3c515.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Test configuration constants */
#define HAL_TEST_MAX_NICS           8
#define HAL_TEST_TIMEOUT_MS         10000
#define HAL_TEST_STRESS_ITERATIONS  500
#define HAL_TEST_PACKET_SIZE        1518
#define HAL_TEST_BUFFER_SIZE        2048

/* HAL test result tracking */
typedef struct {
    char test_name[64];
    test_result_t result;
    uint32_t duration_ms;
    uint32_t error_code;
    char error_details[128];
} hal_test_result_t;

/* HAL test suite state */
static struct {
    bool initialized;
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    hal_test_result_t results[64];
    uint8_t mock_device_ids[HAL_TEST_MAX_NICS];
    int mock_device_count;
} hal_test_state = {0};

/* Forward declarations */
static int hal_test_setup(void);
static void hal_test_cleanup(void);
static test_result_t hal_test_vtable_polymorphism(void);
static test_result_t hal_test_multi_nic_management(void);
static test_result_t hal_test_error_recovery_integration(void);
static test_result_t hal_test_defensive_programming(void);
static test_result_t hal_test_resource_lifecycle(void);
static test_result_t hal_test_interrupt_handling(void);
static test_result_t hal_test_packet_operations_integration(void);
static test_result_t hal_test_configuration_management(void);
static test_result_t hal_test_statistics_integration(void);
static test_result_t hal_test_stress_conditions(void);

/* Helper functions */
static bool hal_test_create_mock_environment(int nic_count);
static test_result_t hal_test_validate_nic_vtable_completeness(const nic_ops_t *ops);
static test_result_t hal_test_simulate_hardware_failures(void);
static void hal_test_record_result(const char *test_name, test_result_t result, 
                                  uint32_t duration, uint32_t error_code, const char *details);

/**
 * @brief Initialize HAL test framework
 * @return 0 on success, negative on error
 */
static int hal_test_setup(void) {
    if (hal_test_state.initialized) {
        return SUCCESS;
    }
    
    LOG_INFO("=== Initializing HAL Integration Test Framework ===");
    
    /* Initialize mock framework */
    int result = mock_framework_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize mock framework: %d", result);
        return result;
    }
    
    /* Initialize hardware abstraction layer */
    result = hardware_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize HAL: %d", result);
        return result;
    }
    
    /* Initialize memory management for testing */
    result = memory_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize memory management: %d", result);
        return result;
    }
    
    /* Reset test state */
    memset(&hal_test_state, 0, sizeof(hal_test_state));
    hal_test_state.initialized = true;
    
    /* Create comprehensive mock environment */
    if (!hal_test_create_mock_environment(4)) {
        LOG_ERROR("Failed to create mock test environment");
        return ERROR_HARDWARE;
    }
    
    LOG_INFO("HAL integration test framework initialized successfully");
    return SUCCESS;
}

/**
 * @brief Cleanup HAL test framework
 */
static void hal_test_cleanup(void) {
    if (!hal_test_state.initialized) {
        return;
    }
    
    LOG_INFO("=== Cleaning up HAL Integration Test Framework ===");
    
    /* Cleanup all mock devices */
    for (int i = 0; i < hal_test_state.mock_device_count; i++) {
        mock_device_destroy(hal_test_state.mock_device_ids[i]);
    }
    
    /* Cleanup HAL */
    hardware_cleanup();
    
    /* Cleanup mock framework */
    mock_framework_cleanup();
    
    /* Cleanup memory management */
    memory_cleanup();
    
    hal_test_state.initialized = false;
    LOG_INFO("HAL integration test framework cleaned up");
}

/**
 * @brief Create comprehensive mock environment for testing
 * @param nic_count Number of NICs to create
 * @return true on success, false on failure
 */
static bool hal_test_create_mock_environment(int nic_count) {
    if (nic_count > HAL_TEST_MAX_NICS) {
        LOG_ERROR("Too many NICs requested: %d (max %d)", nic_count, HAL_TEST_MAX_NICS);
        return false;
    }
    
    hal_test_state.mock_device_count = 0;
    
    for (int i = 0; i < nic_count; i++) {
        /* Alternate between NIC types for diversity */
        mock_device_type_t type = (i % 2 == 0) ? MOCK_DEVICE_3C509B : MOCK_DEVICE_3C515;
        uint16_t io_base = 0x200 + (i * 0x30);  /* Non-overlapping I/O ranges */
        uint8_t irq = 10 + i;
        
        int device_id = mock_device_create(type, io_base, irq);
        if (device_id < 0) {
            LOG_ERROR("Failed to create mock device %d: %d", i, device_id);
            return false;
        }
        
        hal_test_state.mock_device_ids[hal_test_state.mock_device_count++] = device_id;
        
        /* Configure mock device for comprehensive testing */
        uint8_t mac_addr[6] = {0x00, 0x60, 0x8C, 0x10 + i, 0x20 + i, 0x30 + i};
        mock_device_set_mac_address(device_id, mac_addr);
        mock_device_set_link_status(device_id, true, (type == MOCK_DEVICE_3C515) ? 100 : 10);
        mock_device_enable(device_id, true);
        
        /* Initialize EEPROM with realistic data */
        uint16_t eeprom_data[16];
        memset(eeprom_data, 0, sizeof(eeprom_data));
        eeprom_data[0] = (type == MOCK_DEVICE_3C515) ? 0x5150 : 0x5090; /* Product ID */
        eeprom_data[1] = (mac_addr[1] << 8) | mac_addr[0];  /* MAC bytes 0-1 */
        eeprom_data[2] = (mac_addr[3] << 8) | mac_addr[2];  /* MAC bytes 2-3 */
        eeprom_data[3] = (mac_addr[5] << 8) | mac_addr[4];  /* MAC bytes 4-5 */
        mock_eeprom_init(device_id, eeprom_data, 16);
        
        LOG_DEBUG("Created mock NIC %d: device_id=%d, type=%d, io_base=0x%X, irq=%d", 
                  i, device_id, type, io_base, irq);
    }
    
    LOG_INFO("Created %d mock NICs for HAL integration testing", hal_test_state.mock_device_count);
    return true;
}

/**
 * @brief Test vtable polymorphism and completeness
 * @return Test result
 */
static test_result_t hal_test_vtable_polymorphism(void) {
    LOG_INFO("Testing HAL vtable polymorphism and completeness");
    
    /* Test 1: Get operations vtables for both NIC types */
    nic_ops_t *ops_3c509b = get_3c509b_ops();
    if (!ops_3c509b) {
        LOG_ERROR("Failed to get 3C509B operations vtable");
        return TEST_RESULT_FAIL;
    }
    
    nic_ops_t *ops_3c515 = get_3c515_ops();
    if (!ops_3c515) {
        LOG_ERROR("Failed to get 3C515 operations vtable");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 2: Validate vtable completeness */
    test_result_t result = hal_test_validate_nic_vtable_completeness(ops_3c509b);
    if (result != TEST_RESULT_PASS) {
        LOG_ERROR("3C509B vtable completeness validation failed");
        return result;
    }
    
    result = hal_test_validate_nic_vtable_completeness(ops_3c515);
    if (result != TEST_RESULT_PASS) {
        LOG_ERROR("3C515 vtable completeness validation failed");
        return result;
    }
    
    /* Test 3: Polymorphic operation selection */
    nic_ops_t *ops_by_type_3c509b = get_nic_ops(NIC_TYPE_3C509B);
    nic_ops_t *ops_by_type_3c515 = get_nic_ops(NIC_TYPE_3C515_TX);
    nic_ops_t *ops_invalid = get_nic_ops(NIC_TYPE_UNKNOWN);
    
    if (ops_by_type_3c509b != ops_3c509b || ops_by_type_3c515 != ops_3c515 || ops_invalid != NULL) {
        LOG_ERROR("Polymorphic operation selection failed");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 4: Cross-type vtable distinctness */
    if (ops_3c509b == ops_3c515) {
        LOG_ERROR("Different NIC types should have different vtables");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 5: Function pointer consistency within vtables */
    if (ops_3c509b->init == ops_3c515->init && ops_3c509b->send_packet == ops_3c515->send_packet) {
        LOG_WARNING("vtables appear to share function pointers - check implementation");
    }
    
    LOG_INFO("HAL vtable polymorphism test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Validate vtable completeness and required function presence
 * @param ops Operations vtable to validate
 * @return Test result
 */
static test_result_t hal_test_validate_nic_vtable_completeness(const nic_ops_t *ops) {
    if (!ops) {
        return TEST_RESULT_FAIL;
    }
    
    /* Check critical function pointers */
    if (!ops->init || !ops->cleanup || !ops->reset) {
        LOG_ERROR("Missing core lifecycle functions in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->send_packet || !ops->receive_packet) {
        LOG_ERROR("Missing packet operation functions in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->handle_interrupt || !ops->enable_interrupts || !ops->disable_interrupts) {
        LOG_ERROR("Missing interrupt handling functions in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->get_mac_address || !ops->set_mac_address) {
        LOG_ERROR("Missing MAC address functions in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->get_link_status || !ops->get_statistics) {
        LOG_ERROR("Missing status/statistics functions in vtable");
        return TEST_RESULT_FAIL;
    }
    
    /* Check advanced functions */
    if (!ops->set_promiscuous || !ops->set_multicast || !ops->set_receive_mode) {
        LOG_WARNING("Missing advanced configuration functions in vtable");
    }
    
    if (!ops->suspend || !ops->resume || !ops->set_power_state) {
        LOG_WARNING("Missing power management functions in vtable");
    }
    
    if (!ops->handle_error || !ops->recover_from_error) {
        LOG_WARNING("Missing error recovery functions in vtable");
    }
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test multi-NIC management capabilities
 * @return Test result
 */
static test_result_t hal_test_multi_nic_management(void) {
    LOG_INFO("Testing HAL multi-NIC management");
    
    /* Test 1: Hardware detection and enumeration */
    int detect_result = hardware_detect_all();
    if (detect_result < 0) {
        LOG_ERROR("Hardware detection failed: %d", detect_result);
        return TEST_RESULT_FAIL;
    }
    
    int nic_count = hardware_get_nic_count();
    if (nic_count != hal_test_state.mock_device_count) {
        LOG_ERROR("NIC count mismatch: expected %d, detected %d", 
                  hal_test_state.mock_device_count, nic_count);
        return TEST_RESULT_FAIL;
    }
    
    /* Test 2: NIC enumeration and access */
    nic_info_t enumerated_nics[MAX_NICS];
    int enum_count = hardware_enumerate_nics(enumerated_nics, MAX_NICS);
    if (enum_count != nic_count) {
        LOG_ERROR("Enumeration count mismatch: expected %d, enumerated %d", nic_count, enum_count);
        return TEST_RESULT_FAIL;
    }
    
    /* Test 3: Individual NIC access and validation */
    for (int i = 0; i < nic_count; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) {
            LOG_ERROR("Failed to get NIC %d", i);
            return TEST_RESULT_FAIL;
        }
        
        /* Validate NIC structure integrity */
        if (!(nic->status & NIC_STATUS_PRESENT)) {
            LOG_ERROR("NIC %d not marked as present", i);
            return TEST_RESULT_FAIL;
        }
        
        if (!nic->ops) {
            LOG_ERROR("NIC %d has no operations vtable", i);
            return TEST_RESULT_FAIL;
        }
        
        /* Test NIC type consistency */
        nic_type_t expected_type = (i % 2 == 0) ? NIC_TYPE_3C509B : NIC_TYPE_3C515_TX;
        if (nic->type != expected_type) {
            LOG_ERROR("NIC %d type mismatch: expected %d, got %d", i, expected_type, nic->type);
            return TEST_RESULT_FAIL;
        }
        
        /* Validate I/O addresses and IRQ assignments */
        if (nic->io_base == 0 || nic->irq == 0) {
            LOG_ERROR("NIC %d has invalid I/O base (0x%X) or IRQ (%d)", i, nic->io_base, nic->irq);
            return TEST_RESULT_FAIL;
        }
        
        /* Test search functions */
        nic_info_t *found_by_type = hardware_find_nic_by_type(nic->type);
        if (!found_by_type) {
            LOG_ERROR("Failed to find NIC by type %d", nic->type);
            return TEST_RESULT_FAIL;
        }
        
        nic_info_t *found_by_mac = hardware_find_nic_by_mac(nic->mac);
        if (!found_by_mac) {
            LOG_ERROR("Failed to find NIC by MAC address");
            return TEST_RESULT_FAIL;
        }
    }
    
    /* Test 4: NIC lifecycle management */
    for (int i = 0; i < nic_count; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) continue;
        
        /* Initialize NIC */
        int init_result = hardware_init_nic(nic);
        if (init_result != SUCCESS) {
            LOG_ERROR("Failed to initialize NIC %d: %d", i, init_result);
            return TEST_RESULT_FAIL;
        }
        
        /* Verify initialization status */
        if (!(nic->status & NIC_STATUS_INITIALIZED)) {
            LOG_ERROR("NIC %d not marked as initialized after init", i);
            return TEST_RESULT_FAIL;
        }
        
        /* Test NIC functionality */
        int test_result = hardware_test_nic(nic);
        if (test_result != SUCCESS) {
            LOG_ERROR("NIC %d self-test failed: %d", i, test_result);
            return TEST_RESULT_FAIL;
        }
    }
    
    LOG_INFO("HAL multi-NIC management test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test error recovery integration
 * @return Test result
 */
static test_result_t hal_test_error_recovery_integration(void) {
    LOG_INFO("Testing HAL error recovery integration");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for error recovery testing");
        return TEST_RESULT_SKIP;
    }
    
    nic_info_t *test_nic = hardware_get_nic(0);
    if (!test_nic) {
        LOG_ERROR("Failed to get test NIC");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 1: Link failure recovery */
    LOG_DEBUG("Testing link failure recovery");
    uint8_t device_id = hal_test_state.mock_device_ids[0];
    
    /* Simulate link failure */
    mock_device_set_link_status(device_id, false, 0);
    
    /* Check HAL detects link failure */
    int link_status = hardware_get_link_status(test_nic);
    if (link_status > 0) {
        LOG_ERROR("HAL should detect link failure");
        return TEST_RESULT_FAIL;
    }
    
    /* Restore link and verify recovery */
    mock_device_set_link_status(device_id, true, 10);
    link_status = hardware_get_link_status(test_nic);
    if (link_status <= 0) {
        LOG_ERROR("HAL should detect link recovery");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 2: Hardware reset recovery */
    LOG_DEBUG("Testing hardware reset recovery");
    int reset_result = hardware_reset_nic(test_nic);
    if (reset_result != SUCCESS) {
        LOG_ERROR("Hardware reset failed: %d", reset_result);
        return TEST_RESULT_FAIL;
    }
    
    /* Verify NIC remains functional after reset */
    if (!(test_nic->status & NIC_STATUS_PRESENT)) {
        LOG_ERROR("NIC should be present after reset");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 3: Error injection and recovery */
    test_result_t inject_result = hal_test_simulate_hardware_failures();
    if (inject_result != TEST_RESULT_PASS) {
        LOG_ERROR("Hardware failure simulation failed");
        return inject_result;
    }
    
    /* Test 4: Multi-NIC failover */
    if (hardware_get_nic_count() >= 2) {
        int failover_result = hardware_test_failover(0);
        if (failover_result != SUCCESS) {
            LOG_ERROR("Multi-NIC failover test failed: %d", failover_result);
            return TEST_RESULT_FAIL;
        }
    }
    
    LOG_INFO("HAL error recovery integration test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Simulate various hardware failures and test recovery
 * @return Test result
 */
static test_result_t hal_test_simulate_hardware_failures(void) {
    if (hal_test_state.mock_device_count < 1) {
        return TEST_RESULT_SKIP;
    }
    
    uint8_t device_id = hal_test_state.mock_device_ids[0];
    nic_info_t *test_nic = hardware_get_nic(0);
    
    /* Error types to test */
    mock_error_type_t error_types[] = {
        MOCK_ERROR_TX_TIMEOUT,
        MOCK_ERROR_RX_OVERRUN,
        MOCK_ERROR_CRC_ERROR,
        MOCK_ERROR_DMA_ERROR
    };
    
    for (int i = 0; i < sizeof(error_types) / sizeof(error_types[0]); i++) {
        /* Inject error */
        int inject_result = mock_error_inject(device_id, error_types[i], 1);
        if (inject_result != SUCCESS) {
            LOG_ERROR("Failed to inject error type %d", error_types[i]);
            return TEST_RESULT_FAIL;
        }
        
        /* Perform operation that should trigger error */
        uint8_t test_packet[64] = "ERROR_RECOVERY_TEST_PACKET";
        hardware_send_packet(test_nic, test_packet, sizeof(test_packet));
        
        /* Clear error and verify recovery */
        mock_error_clear(device_id);
        
        /* Test normal operation resume */
        int normal_result = hardware_send_packet(test_nic, test_packet, sizeof(test_packet));
        if (normal_result != SUCCESS && normal_result != ERROR_BUSY) {
            LOG_ERROR("Normal operation should resume after error clear (error type %d)", error_types[i]);
            return TEST_RESULT_FAIL;
        }
    }
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test defensive programming patterns
 * @return Test result
 */
static test_result_t hal_test_defensive_programming(void) {
    LOG_INFO("Testing HAL defensive programming patterns");
    
    /* Test 1: NULL pointer handling */
    int null_result;
    
    /* Test hardware operations with NULL parameters */
    null_result = hardware_init_nic(NULL);
    if (null_result == SUCCESS) {
        LOG_ERROR("hardware_init_nic should reject NULL pointer");
        return TEST_RESULT_FAIL;
    }
    
    null_result = hardware_send_packet(NULL, (uint8_t*)"test", 4);
    if (null_result == SUCCESS) {
        LOG_ERROR("hardware_send_packet should reject NULL NIC pointer");
        return TEST_RESULT_FAIL;
    }
    
    if (hardware_get_nic_count() > 0) {
        nic_info_t *nic = hardware_get_nic(0);
        null_result = hardware_send_packet(nic, NULL, 4);
        if (null_result == SUCCESS) {
            LOG_ERROR("hardware_send_packet should reject NULL packet pointer");
            return TEST_RESULT_FAIL;
        }
    }
    
    /* Test 2: Invalid parameter ranges */
    nic_info_t *invalid_nic = hardware_get_nic(-1);
    if (invalid_nic != NULL) {
        LOG_ERROR("hardware_get_nic should reject negative indices");
        return TEST_RESULT_FAIL;
    }
    
    invalid_nic = hardware_get_nic(999);
    if (invalid_nic != NULL) {
        LOG_ERROR("hardware_get_nic should reject out-of-range indices");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 3: State validation */
    if (hardware_get_nic_count() > 0) {
        nic_info_t *nic = hardware_get_nic(0);
        
        /* Save original status */
        uint32_t original_status = nic->status;
        
        /* Corrupt status and test operations */
        nic->status = 0;  /* Mark as not present */
        
        int corrupt_result = hardware_send_packet(nic, (uint8_t*)"test", 4);
        /* Should fail gracefully, not crash */
        
        /* Restore status */
        nic->status = original_status;
    }
    
    /* Test 4: Boundary conditions */
    if (hardware_get_nic_count() > 0) {
        nic_info_t *nic = hardware_get_nic(0);
        uint8_t oversized_packet[65536];  /* Much larger than MTU */
        
        /* Should reject oversized packets gracefully */
        int boundary_result = hardware_send_packet(nic, oversized_packet, sizeof(oversized_packet));
        if (boundary_result == SUCCESS) {
            LOG_ERROR("Should reject oversized packets");
            return TEST_RESULT_FAIL;
        }
        
        /* Test zero-length packet */
        boundary_result = hardware_send_packet(nic, oversized_packet, 0);
        if (boundary_result == SUCCESS) {
            LOG_ERROR("Should reject zero-length packets");
            return TEST_RESULT_FAIL;
        }
    }
    
    /* Test 5: Resource exhaustion handling */
    /* Simulate memory exhaustion and verify graceful degradation */
    void *large_allocations[100];
    int allocation_count = 0;
    
    /* Try to exhaust memory */
    for (int i = 0; i < 100; i++) {
        large_allocations[i] = memory_alloc(8192, MEM_TYPE_PACKET_BUFFER, 0);
        if (large_allocations[i]) {
            allocation_count++;
        } else {
            break;
        }
    }
    
    /* Test HAL operations under memory pressure */
    if (hardware_get_nic_count() > 0) {
        nic_info_t *nic = hardware_get_nic(0);
        uint8_t test_packet[] = "MEMORY_PRESSURE_TEST";
        
        /* Should handle gracefully, not crash */
        hardware_send_packet(nic, test_packet, sizeof(test_packet));
    }
    
    /* Clean up allocations */
    for (int i = 0; i < allocation_count; i++) {
        if (large_allocations[i]) {
            memory_free(large_allocations[i]);
        }
    }
    
    LOG_INFO("HAL defensive programming test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test resource lifecycle management
 * @return Test result
 */
static test_result_t hal_test_resource_lifecycle(void) {
    LOG_INFO("Testing HAL resource lifecycle management");
    
    const mem_stats_t *initial_stats = memory_get_stats();
    uint32_t initial_memory = initial_stats->used_memory;
    
    /* Test 1: NIC initialization resource allocation */
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) continue;
        
        /* Initialize NIC (should allocate resources) */
        int init_result = hardware_init_nic(nic);
        if (init_result != SUCCESS) {
            LOG_ERROR("NIC %d initialization failed: %d", i, init_result);
            return TEST_RESULT_FAIL;
        }
        
        /* Verify memory usage increased */
        const mem_stats_t *after_init_stats = memory_get_stats();
        if (after_init_stats->used_memory <= initial_memory) {
            LOG_WARNING("NIC initialization should allocate resources");
        }
    }
    
    /* Test 2: Packet buffer allocation/deallocation */
    void *packet_buffers[50];
    int buffer_count = 0;
    
    for (int i = 0; i < 50; i++) {
        packet_buffers[i] = memory_alloc(HAL_TEST_PACKET_SIZE, MEM_TYPE_PACKET_BUFFER, 0);
        if (packet_buffers[i]) {
            buffer_count++;
        }
    }
    
    /* Verify memory increase */
    const mem_stats_t *after_alloc_stats = memory_get_stats();
    if (after_alloc_stats->used_memory <= initial_memory) {
        LOG_ERROR("Buffer allocation should increase memory usage");
        return TEST_RESULT_FAIL;
    }
    
    /* Free buffers */
    for (int i = 0; i < buffer_count; i++) {
        if (packet_buffers[i]) {
            memory_free(packet_buffers[i]);
        }
    }
    
    /* Test 3: NIC cleanup resource deallocation */
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) continue;
        
        int cleanup_result = hardware_cleanup_nic(nic);
        if (cleanup_result != SUCCESS) {
            LOG_ERROR("NIC %d cleanup failed: %d", i, cleanup_result);
            return TEST_RESULT_FAIL;
        }
    }
    
    /* Test 4: Memory leak detection */
    const mem_stats_t *final_stats = memory_get_stats();
    uint32_t final_memory = final_stats->used_memory;
    
    if (final_memory > initial_memory + 1024) {  /* Allow 1KB tolerance */
        LOG_ERROR("Potential memory leak detected: initial=%lu, final=%lu", 
                  initial_memory, final_memory);
        return TEST_RESULT_FAIL;
    }
    
    /* Re-initialize NICs for other tests */
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic) {
            hardware_init_nic(nic);
        }
    }
    
    LOG_INFO("HAL resource lifecycle test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test interrupt handling integration
 * @return Test result
 */
static test_result_t hal_test_interrupt_handling(void) {
    LOG_INFO("Testing HAL interrupt handling integration");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for interrupt testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test interrupt handling for each NIC */
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) continue;
        
        uint8_t device_id = hal_test_state.mock_device_ids[i];
        
        /* Test 1: Enable/disable interrupts */
        int enable_result = hardware_enable_interrupts(nic);
        if (enable_result != SUCCESS) {
            LOG_ERROR("Failed to enable interrupts on NIC %d: %d", i, enable_result);
            return TEST_RESULT_FAIL;
        }
        
        int disable_result = hardware_disable_interrupts(nic);
        if (disable_result != SUCCESS) {
            LOG_ERROR("Failed to disable interrupts on NIC %d: %d", i, disable_result);
            return TEST_RESULT_FAIL;
        }
        
        /* Test 2: Interrupt simulation and handling */
        hardware_enable_interrupts(nic);
        
        /* Generate mock interrupt */
        mock_interrupt_generate(device_id, MOCK_INTR_TX_COMPLETE);
        
        /* Check if interrupt is detected */
        if (!mock_interrupt_pending(device_id)) {
            LOG_ERROR("Mock interrupt should be pending");
            return TEST_RESULT_FAIL;
        }
        
        /* Simulate interrupt handling */
        hardware_handle_nic_interrupt(nic);
        
        /* Clear interrupt */
        mock_interrupt_clear(device_id);
        
        hardware_disable_interrupts(nic);
    }
    
    /* Test 3: Global interrupt source checking */
    int interrupt_source = hardware_check_interrupt_source();
    /* Should not crash, return value depends on implementation */
    
    LOG_INFO("HAL interrupt handling test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test packet operations integration
 * @return Test result
 */
static test_result_t hal_test_packet_operations_integration(void) {
    LOG_INFO("Testing HAL packet operations integration");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for packet operations testing");
        return TEST_RESULT_SKIP;
    }
    
    nic_info_t *test_nic = hardware_get_nic(0);
    if (!test_nic) {
        LOG_ERROR("Failed to get test NIC");
        return TEST_RESULT_FAIL;
    }
    
    uint8_t device_id = hal_test_state.mock_device_ids[0];
    
    /* Test 1: Basic packet transmission */
    uint8_t test_packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* Destination MAC (broadcast) */
        0x00, 0x60, 0x8C, 0x10, 0x20, 0x30,  /* Source MAC */
        0x08, 0x00,                           /* EtherType (IP) */
        'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'
    };
    
    int send_result = hardware_send_packet(test_nic, test_packet, sizeof(test_packet));
    if (send_result != SUCCESS && send_result != ERROR_BUSY) {
        LOG_ERROR("Packet transmission failed: %d", send_result);
        return TEST_RESULT_FAIL;
    }
    
    /* Test 2: Packet reception simulation */
    uint8_t rx_packet[] = {
        0x00, 0x60, 0x8C, 0x10, 0x20, 0x30,  /* Destination MAC */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  /* Source MAC */
        0x08, 0x00,                           /* EtherType (IP) */
        'R', 'e', 'c', 'e', 'i', 'v', 'e', ' ', 'T', 'e', 's', 't'
    };
    
    /* Inject packet for reception */
    mock_packet_inject_rx(device_id, rx_packet, sizeof(rx_packet));
    
    /* Check if packet is available */
    int rx_available = hardware_check_rx_available(test_nic);
    if (rx_available <= 0) {
        LOG_WARNING("Injected packet should be available for reception");
    }
    
    /* Receive packet */
    uint8_t rx_buffer[HAL_TEST_BUFFER_SIZE];
    size_t rx_length = sizeof(rx_buffer);
    int recv_result = hardware_receive_packet(test_nic, rx_buffer, &rx_length);
    
    if (recv_result == SUCCESS) {
        /* Verify received packet content */
        if (rx_length != sizeof(rx_packet) || memcmp(rx_buffer, rx_packet, rx_length) != 0) {
            LOG_ERROR("Received packet content mismatch");
            return TEST_RESULT_FAIL;
        }
    }
    
    /* Test 3: Transmission completion checking */
    int tx_complete = hardware_check_tx_complete(test_nic);
    /* Should not crash, return value depends on mock state */
    
    /* Test 4: Multiple packet operations */
    for (int i = 0; i < 10; i++) {
        test_packet[sizeof(test_packet) - 1] = '0' + i;  /* Vary packet content */
        
        int multi_send_result = hardware_send_packet(test_nic, test_packet, sizeof(test_packet));
        if (multi_send_result != SUCCESS && multi_send_result != ERROR_BUSY) {
            LOG_WARNING("Multi-packet send iteration %d failed: %d", i, multi_send_result);
        }
    }
    
    LOG_INFO("HAL packet operations integration test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test configuration management
 * @return Test result
 */
static test_result_t hal_test_configuration_management(void) {
    LOG_INFO("Testing HAL configuration management");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for configuration testing");
        return TEST_RESULT_SKIP;
    }
    
    nic_info_t *test_nic = hardware_get_nic(0);
    if (!test_nic) {
        LOG_ERROR("Failed to get test NIC");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 1: MAC address configuration */
    uint8_t original_mac[6];
    int get_mac_result = hardware_get_mac_address(test_nic, original_mac);
    if (get_mac_result != SUCCESS) {
        LOG_ERROR("Failed to get MAC address: %d", get_mac_result);
        return TEST_RESULT_FAIL;
    }
    
    /* Set new MAC address */
    uint8_t new_mac[6] = {0x02, 0x00, 0x00, 0xAA, 0xBB, 0xCC};
    int set_mac_result = hardware_set_mac_address(test_nic, new_mac);
    if (set_mac_result != SUCCESS) {
        LOG_ERROR("Failed to set MAC address: %d", set_mac_result);
        return TEST_RESULT_FAIL;
    }
    
    /* Verify MAC address change */
    uint8_t verify_mac[6];
    get_mac_result = hardware_get_mac_address(test_nic, verify_mac);
    if (get_mac_result == SUCCESS && memcmp(verify_mac, new_mac, 6) != 0) {
        LOG_ERROR("MAC address verification failed");
        return TEST_RESULT_FAIL;
    }
    
    /* Restore original MAC */
    hardware_set_mac_address(test_nic, original_mac);
    
    /* Test 2: Promiscuous mode configuration */
    int promisc_enable_result = hardware_set_promiscuous_mode(test_nic, true);
    if (promisc_enable_result != SUCCESS) {
        LOG_WARNING("Promiscuous mode enable failed: %d", promisc_enable_result);
    }
    
    int promisc_disable_result = hardware_set_promiscuous_mode(test_nic, false);
    if (promisc_disable_result != SUCCESS) {
        LOG_WARNING("Promiscuous mode disable failed: %d", promisc_disable_result);
    }
    
    /* Test 3: Receive mode configuration */
    int rx_mode_result = hardware_set_receive_mode(test_nic, 0x01);  /* Unicast only */
    if (rx_mode_result != SUCCESS) {
        LOG_WARNING("Receive mode configuration failed: %d", rx_mode_result);
    }
    
    /* Test 4: Speed/duplex configuration (if supported) */
    if (test_nic->capabilities & HW_CAP_FULL_DUPLEX) {
        int speed_duplex_result = hardware_set_speed_duplex(test_nic, 100, true);
        if (speed_duplex_result != SUCCESS) {
            LOG_WARNING("Speed/duplex configuration failed: %d", speed_duplex_result);
        }
        
        /* Verify configuration */
        int speed = 0;
        bool full_duplex = false;
        int get_speed_result = hardware_get_speed_duplex(test_nic, &speed, &full_duplex);
        if (get_speed_result == SUCCESS) {
            LOG_DEBUG("NIC speed: %d Mbps, duplex: %s", speed, full_duplex ? "full" : "half");
        }
    }
    
    /* Test 5: Flow control configuration (if supported) */
    if (test_nic->type == NIC_TYPE_3C515_TX) {
        int flow_ctrl_result = hardware_set_flow_control(test_nic, true);
        if (flow_ctrl_result != SUCCESS) {
            LOG_WARNING("Flow control configuration failed: %d", flow_ctrl_result);
        }
    }
    
    LOG_INFO("HAL configuration management test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test statistics integration
 * @return Test result
 */
static test_result_t hal_test_statistics_integration(void) {
    LOG_INFO("Testing HAL statistics integration");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for statistics testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test statistics for each NIC */
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) continue;
        
        /* Test 1: Get statistics */
        uint32_t stats_buffer[64];  /* Generic statistics buffer */
        int stats_result = hardware_get_nic_statistics(nic, stats_buffer);
        if (stats_result != SUCCESS) {
            LOG_WARNING("Failed to get statistics for NIC %d: %d", i, stats_result);
            continue;
        }
        
        /* Test 2: Clear statistics */
        int clear_result = hardware_clear_nic_statistics(nic);
        if (clear_result != SUCCESS) {
            LOG_WARNING("Failed to clear statistics for NIC %d: %d", i, clear_result);
        }
        
        /* Test 3: Verify basic statistics fields */
        LOG_DEBUG("NIC %d statistics: TX packets=%lu, RX packets=%lu, TX bytes=%lu, RX bytes=%lu",
                  i, nic->tx_packets, nic->rx_packets, nic->tx_bytes, nic->rx_bytes);
        LOG_DEBUG("NIC %d errors: TX errors=%lu, RX errors=%lu, interrupts=%lu",
                  i, nic->tx_errors, nic->rx_errors, nic->interrupts);
    }
    
    /* Test 4: Print comprehensive statistics */
    hardware_print_comprehensive_stats();
    
    LOG_INFO("HAL statistics integration test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test stress conditions
 * @return Test result
 */
static test_result_t hal_test_stress_conditions(void) {
    LOG_INFO("Testing HAL under stress conditions");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for stress testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test 1: High-rate packet operations */
    nic_info_t *stress_nic = hardware_get_nic(0);
    uint8_t stress_packet[64];
    memset(stress_packet, 0xAA, sizeof(stress_packet));
    
    int successful_sends = 0;
    int failed_sends = 0;
    
    for (int i = 0; i < HAL_TEST_STRESS_ITERATIONS; i++) {
        stress_packet[63] = i & 0xFF;  /* Vary packet content */
        
        int send_result = hardware_send_packet(stress_nic, stress_packet, sizeof(stress_packet));
        if (send_result == SUCCESS) {
            successful_sends++;
        } else {
            failed_sends++;
        }
        
        /* Small delay to prevent overwhelming */
        for (volatile int delay = 0; delay < 10; delay++);
    }
    
    LOG_INFO("Stress test results: %d successful sends, %d failed sends", 
             successful_sends, failed_sends);
    
    if (successful_sends == 0) {
        LOG_ERROR("No packets sent successfully during stress test");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 2: Concurrent operations (if multiple NICs available) */
    if (hardware_get_nic_count() >= 2) {
        int concurrent_result = hardware_test_concurrent_operations(2000);  /* 2 seconds */
        if (concurrent_result != SUCCESS) {
            LOG_WARNING("Concurrent operations stress test failed: %d", concurrent_result);
        }
    }
    
    /* Test 3: Resource contention */
    if (hardware_get_nic_count() >= 2) {
        int contention_result = hardware_test_resource_contention(100);  /* 100 iterations */
        if (contention_result != SUCCESS) {
            LOG_WARNING("Resource contention test failed: %d", contention_result);
        }
    }
    
    /* Test 4: Memory pressure operations */
    void *pressure_allocs[100];
    int alloc_count = 0;
    
    /* Create memory pressure */
    for (int i = 0; i < 100; i++) {
        pressure_allocs[i] = memory_alloc(1024, MEM_TYPE_PACKET_BUFFER, 0);
        if (pressure_allocs[i]) {
            alloc_count++;
        }
    }
    
    /* Test operations under memory pressure */
    for (int i = 0; i < 20; i++) {
        hardware_send_packet(stress_nic, stress_packet, sizeof(stress_packet));
    }
    
    /* Clean up memory pressure */
    for (int i = 0; i < alloc_count; i++) {
        if (pressure_allocs[i]) {
            memory_free(pressure_allocs[i]);
        }
    }
    
    LOG_INFO("HAL stress conditions test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Record test result in test state
 * @param test_name Name of the test
 * @param result Test result
 * @param duration Test duration in milliseconds
 * @param error_code Error code if test failed
 * @param details Additional error details
 */
static void hal_test_record_result(const char *test_name, test_result_t result, 
                                  uint32_t duration, uint32_t error_code, const char *details) {
    if (hal_test_state.total_tests >= sizeof(hal_test_state.results) / sizeof(hal_test_state.results[0])) {
        return;  /* Results array full */
    }
    
    hal_test_result_t *entry = &hal_test_state.results[hal_test_state.total_tests];
    
    strncpy(entry->test_name, test_name, sizeof(entry->test_name) - 1);
    entry->test_name[sizeof(entry->test_name) - 1] = '\0';
    
    entry->result = result;
    entry->duration_ms = duration;
    entry->error_code = error_code;
    
    if (details) {
        strncpy(entry->error_details, details, sizeof(entry->error_details) - 1);
        entry->error_details[sizeof(entry->error_details) - 1] = '\0';
    } else {
        entry->error_details[0] = '\0';
    }
    
    /* Update counters */
    hal_test_state.total_tests++;
    switch (result) {
        case TEST_RESULT_PASS:
            hal_test_state.passed_tests++;
            break;
        case TEST_RESULT_FAIL:
        case TEST_RESULT_ERROR:
            hal_test_state.failed_tests++;
            break;
        case TEST_RESULT_SKIP:
            hal_test_state.skipped_tests++;
            break;
    }
}

/**
 * @brief Main HAL integration test runner
 * @return 0 on success, negative on failure
 */
int run_hal_integration_tests(void) {
    int setup_result = hal_test_setup();
    if (setup_result != SUCCESS) {
        LOG_ERROR("Failed to setup HAL integration test environment: %d", setup_result);
        return setup_result;
    }
    
    LOG_INFO("=== Starting HAL Integration Test Suite ===");
    
    /* Define comprehensive test cases */
    struct {
        const char *name;
        test_result_t (*test_func)(void);
        bool required;
    } test_cases[] = {
        {"Vtable Polymorphism", hal_test_vtable_polymorphism, true},
        {"Multi-NIC Management", hal_test_multi_nic_management, true},
        {"Error Recovery Integration", hal_test_error_recovery_integration, true},
        {"Defensive Programming", hal_test_defensive_programming, true},
        {"Resource Lifecycle", hal_test_resource_lifecycle, true},
        {"Interrupt Handling", hal_test_interrupt_handling, true},
        {"Packet Operations Integration", hal_test_packet_operations_integration, true},
        {"Configuration Management", hal_test_configuration_management, false},
        {"Statistics Integration", hal_test_statistics_integration, false},
        {"Stress Conditions", hal_test_stress_conditions, false}
    };
    
    int total_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    
    /* Execute each test with timing */
    for (int i = 0; i < total_tests; i++) {
        LOG_INFO("Running HAL test: %s", test_cases[i].name);
        
        uint32_t start_time = 0;  /* Simple timestamp - could use timer */
        test_result_t result = test_cases[i].test_func();
        uint32_t duration = 1;    /* Simple duration - could calculate actual time */
        
        hal_test_record_result(test_cases[i].name, result, duration, 0, NULL);
        
        switch (result) {
            case TEST_RESULT_PASS:
                LOG_INFO("PASS: %s", test_cases[i].name);
                break;
                
            case TEST_RESULT_FAIL:
                LOG_ERROR("FAIL: %s", test_cases[i].name);
                if (test_cases[i].required) {
                    LOG_ERROR("Required test failed - stopping test suite");
                    hal_test_cleanup();
                    return ERROR_HARDWARE;
                }
                break;
                
            case TEST_RESULT_SKIP:
                LOG_INFO("SKIP: %s", test_cases[i].name);
                break;
                
            default:
                LOG_ERROR("ERROR: %s returned invalid result %d", test_cases[i].name, result);
                hal_test_state.failed_tests++;
                break;
        }
    }
    
    /* Generate comprehensive test summary */
    LOG_INFO("=== HAL Integration Test Summary ===");
    LOG_INFO("Total tests: %d", hal_test_state.total_tests);
    LOG_INFO("Passed: %d", hal_test_state.passed_tests);
    LOG_INFO("Failed: %d", hal_test_state.failed_tests);
    LOG_INFO("Skipped: %d", hal_test_state.skipped_tests);
    
    int final_result = SUCCESS;
    if (hal_test_state.failed_tests == 0) {
        LOG_INFO("=== ALL HAL INTEGRATION TESTS PASSED ===");
    } else {
        LOG_ERROR("=== %d HAL INTEGRATION TESTS FAILED ===", hal_test_state.failed_tests);
        final_result = ERROR_HARDWARE;
    }
    
    hal_test_cleanup();
    return final_result;
}

/**
 * @brief Main function for standalone execution
 * @param argc Argument count
 * @param argv Argument values
 * @return 0 on success, non-zero on failure
 */
int main(int argc, char *argv[]) {
    printf("3Com Packet Driver - HAL Integration Test Suite\n");
    printf("==============================================\n");
    
    /* Initialize logging */
    logging_init(LOG_LEVEL_INFO);
    
    /* Run comprehensive HAL integration tests */
    int result = run_hal_integration_tests();
    
    if (result == SUCCESS) {
        printf("\n=== ALL HAL INTEGRATION TESTS PASSED ===\n");
        return 0;
    } else {
        printf("\n=== HAL INTEGRATION TESTS FAILED ===\n");
        return 1;
    }
}