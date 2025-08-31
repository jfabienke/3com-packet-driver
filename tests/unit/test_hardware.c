/**
 * @file test_hardware.c
 * @brief Comprehensive hardware abstraction layer and multi-NIC management tests
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * 
 * This file implements comprehensive tests for:
 * - Hardware abstraction polymorphic vtable operations
 * - Multi-NIC detection and enumeration
 * - Hardware error recovery mechanisms
 * - Failover between NICs
 * - Resource allocation and deallocation
 * - Hardware capability detection
 * - Resource contention scenarios
 */

#include "../../include/hardware.h"
#include "../../include/hardware_mock.h"
#include "../../include/test_framework.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include "../../include/3c509b.h"
#include "../../include/3c515.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Test configuration constants */
#define HW_TEST_MAX_NICS        4       /* Maximum NICs for testing */
#define HW_TEST_TIMEOUT_MS      5000    /* Test timeout */
#define HW_TEST_STRESS_CYCLES   100     /* Stress test cycles */
#define HW_TEST_PACKET_SIZE     1518    /* Test packet size */

/* Test state tracking */
static bool g_hw_test_initialized = false;
static uint8_t g_mock_device_ids[HW_TEST_MAX_NICS];
static int g_num_mock_devices = 0;

/* Forward declarations */
static int hw_test_setup(void);
static void hw_test_cleanup(void);
static int hw_test_create_mock_nics(int nic_count);
static test_result_t hw_test_vtable_operations(void);
static test_result_t hw_test_multi_nic_detection(void);
static test_result_t hw_test_multi_nic_enumeration(void);
static test_result_t hw_test_error_recovery_mechanisms(void);
static test_result_t hw_test_nic_failover(void);
static test_result_t hw_test_resource_allocation(void);
static test_result_t hw_test_capability_detection(void);
static test_result_t hw_test_resource_contention(void);
static test_result_t hw_test_concurrent_operations(void);
static test_result_t hw_test_load_balancing(void);
static test_result_t hw_test_hardware_failure_injection(void);

/* Test helper functions */
static bool hw_test_validate_nic_info(const nic_info_t *nic, nic_type_t expected_type);
static test_result_t hw_test_validate_vtable(const nic_ops_t *ops, nic_type_t type);
static test_result_t hw_test_simulate_hardware_failure(int nic_index, int failure_type);
static test_result_t hw_test_verify_failover_behavior(int primary_nic, int backup_nic);
static uint32_t hw_test_get_timestamp(void);

/**
 * @brief Initialize hardware testing environment
 * @return 0 on success, negative on error
 */
static int hw_test_setup(void) {
    if (g_hw_test_initialized) {
        return SUCCESS;
    }
    
    LOG_INFO("=== Initializing Hardware Test Environment ===");
    
    /* Initialize mock framework */
    int result = mock_framework_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize mock framework: %d", result);
        return result;
    }
    
    /* Initialize hardware abstraction layer */
    result = hardware_init();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize hardware layer: %d", result);
        return result;
    }
    
    /* Reset test state */
    memset(g_mock_device_ids, 0, sizeof(g_mock_device_ids));
    g_num_mock_devices = 0;
    
    g_hw_test_initialized = true;
    LOG_INFO("Hardware test environment initialized successfully");
    return SUCCESS;
}

/**
 * @brief Cleanup hardware testing environment
 */
static void hw_test_cleanup(void) {
    if (!g_hw_test_initialized) {
        return;
    }
    
    LOG_INFO("=== Cleaning up Hardware Test Environment ===");
    
    /* Cleanup hardware layer */
    hardware_cleanup();
    
    /* Cleanup mock framework */
    mock_framework_cleanup();
    
    g_hw_test_initialized = false;
    LOG_INFO("Hardware test environment cleaned up");
}

/**
 * @brief Create mock NICs for testing
 * @param nic_count Number of NICs to create
 * @return 0 on success, negative on error
 */
static int hw_test_create_mock_nics(int nic_count) {
    if (nic_count > HW_TEST_MAX_NICS) {
        LOG_ERROR("Too many NICs requested: %d (max %d)", nic_count, HW_TEST_MAX_NICS);
        return ERROR_INVALID_PARAM;
    }
    
    g_num_mock_devices = 0;
    
    for (int i = 0; i < nic_count; i++) {
        mock_device_type_t type = (i % 2 == 0) ? MOCK_DEVICE_3C509B : MOCK_DEVICE_3C515;
        uint16_t io_base = 0x200 + (i * 0x20);  /* Separate I/O ranges */
        uint8_t irq = 10 + i;
        
        int device_id = mock_device_create(type, io_base, irq);
        if (device_id < 0) {
            LOG_ERROR("Failed to create mock device %d: %d", i, device_id);
            return device_id;
        }
        
        g_mock_device_ids[g_num_mock_devices++] = device_id;
        
        /* Enable the device and set link up */
        mock_device_enable(device_id, true);
        mock_device_set_link_status(device_id, true, (type == MOCK_DEVICE_3C515) ? 100 : 10);
        
        LOG_DEBUG("Created mock NIC %d: device_id=%d, type=%d, io_base=0x%X", 
                  i, device_id, type, io_base);
    }
    
    LOG_INFO("Created %d mock NICs for testing", g_num_mock_devices);
    return SUCCESS;
}

/**
 * @brief Test vtable polymorphic operations for both NIC types
 * @return Test result
 */
static test_result_t hw_test_vtable_operations(void) {
    LOG_INFO("Testing vtable polymorphic operations");
    
    /* Test 3C509B operations vtable */
    nic_ops_t *ops_3c509b = get_3c509b_ops();
    if (!ops_3c509b) {
        LOG_ERROR("Failed to get 3C509B operations vtable");
        return TEST_RESULT_FAIL;
    }
    
    test_result_t result = hw_test_validate_vtable(ops_3c509b, NIC_TYPE_3C509B);
    if (result != TEST_RESULT_PASS) {
        LOG_ERROR("3C509B vtable validation failed");
        return result;
    }
    
    /* Test 3C515 operations vtable */
    nic_ops_t *ops_3c515 = get_3c515_ops();
    if (!ops_3c515) {
        LOG_ERROR("Failed to get 3C515 operations vtable");
        return TEST_RESULT_FAIL;
    }
    
    result = hw_test_validate_vtable(ops_3c515, NIC_TYPE_3C515_TX);
    if (result != TEST_RESULT_PASS) {
        LOG_ERROR("3C515 vtable validation failed");
        return result;
    }
    
    /* Test polymorphic operation selection */
    nic_ops_t *ops_by_type_3c509b = get_nic_ops(NIC_TYPE_3C509B);
    nic_ops_t *ops_by_type_3c515 = get_nic_ops(NIC_TYPE_3C515_TX);
    
    if (ops_by_type_3c509b != ops_3c509b) {
        LOG_ERROR("Polymorphic operation selection failed for 3C509B");
        return TEST_RESULT_FAIL;
    }
    
    if (ops_by_type_3c515 != ops_3c515) {
        LOG_ERROR("Polymorphic operation selection failed for 3C515");
        return TEST_RESULT_FAIL;
    }
    
    /* Test invalid type handling */
    nic_ops_t *ops_invalid = get_nic_ops(NIC_TYPE_UNKNOWN);
    if (ops_invalid != NULL) {
        LOG_ERROR("Invalid type should return NULL");
        return TEST_RESULT_FAIL;
    }
    
    LOG_INFO("Vtable polymorphic operations test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test multi-NIC detection capabilities
 * @return Test result
 */
static test_result_t hw_test_multi_nic_detection(void) {
    LOG_INFO("Testing multi-NIC detection");
    
    /* Create multiple mock NICs */
    int result = hw_test_create_mock_nics(3);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to create mock NICs for detection test");
        return TEST_RESULT_FAIL;
    }
    
    /* Test hardware detection */
    result = hardware_detect_all();
    if (result < 0) {
        LOG_ERROR("Hardware detection failed: %d", result);
        return TEST_RESULT_FAIL;
    }
    
    /* Verify NIC count */
    int detected_count = hardware_get_nic_count();
    if (detected_count != g_num_mock_devices) {
        LOG_ERROR("Expected %d NICs, detected %d", g_num_mock_devices, detected_count);
        return TEST_RESULT_FAIL;
    }
    
    /* Validate each detected NIC */
    for (int i = 0; i < detected_count; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) {
            LOG_ERROR("Failed to get NIC %d info", i);
            return TEST_RESULT_FAIL;
        }
        
        /* Verify NIC is present and has valid operations */
        if (!(nic->status & NIC_STATUS_PRESENT)) {
            LOG_ERROR("NIC %d not marked as present", i);
            return TEST_RESULT_FAIL;
        }
        
        if (!nic->ops) {
            LOG_ERROR("NIC %d has no operations vtable", i);
            return TEST_RESULT_FAIL;
        }
        
        /* Validate NIC type and corresponding vtable */
        nic_type_t expected_type = (i % 2 == 0) ? NIC_TYPE_3C509B : NIC_TYPE_3C515_TX;
        if (!hw_test_validate_nic_info(nic, expected_type)) {
            LOG_ERROR("NIC %d validation failed", i);
            return TEST_RESULT_FAIL;
        }
    }
    
    LOG_INFO("Multi-NIC detection test passed - detected %d NICs", detected_count);
    return TEST_RESULT_PASS;
}

/**
 * @brief Test multi-NIC enumeration functionality
 * @return Test result
 */
static test_result_t hw_test_multi_nic_enumeration(void) {
    LOG_INFO("Testing multi-NIC enumeration");
    
    nic_info_t nics[MAX_NICS];
    int enumerated_count = hardware_enumerate_nics(nics, MAX_NICS);
    
    if (enumerated_count < 0) {
        LOG_ERROR("NIC enumeration failed: %d", enumerated_count);
        return TEST_RESULT_FAIL;
    }
    
    int expected_count = hardware_get_nic_count();
    if (enumerated_count != expected_count) {
        LOG_ERROR("Enumeration count mismatch: expected %d, got %d", 
                  expected_count, enumerated_count);
        return TEST_RESULT_FAIL;
    }
    
    /* Test NIC search functions */
    for (int i = 0; i < enumerated_count; i++) {
        nic_info_t *nic = &nics[i];
        
        /* Test find by type */
        nic_info_t *found_by_type = hardware_find_nic_by_type(nic->type);
        if (!found_by_type) {
            LOG_ERROR("Failed to find NIC by type %d", nic->type);
            return TEST_RESULT_FAIL;
        }
        
        /* Test find by MAC address */
        nic_info_t *found_by_mac = hardware_find_nic_by_mac(nic->mac);
        if (!found_by_mac) {
            LOG_ERROR("Failed to find NIC by MAC address");
            return TEST_RESULT_FAIL;
        }
        
        /* Verify presence/active status functions */
        if (!hardware_is_nic_present(i)) {
            LOG_ERROR("NIC %d should be present", i);
            return TEST_RESULT_FAIL;
        }
        
        /* Initially NICs should be present but not necessarily active */
        LOG_DEBUG("NIC %d: present=%d, active=%d", 
                  i, hardware_is_nic_present(i), hardware_is_nic_active(i));
    }
    
    LOG_INFO("Multi-NIC enumeration test passed - enumerated %d NICs", enumerated_count);
    return TEST_RESULT_PASS;
}

/**
 * @brief Test hardware error recovery mechanisms
 * @return Test result
 */
static test_result_t hw_test_error_recovery_mechanisms(void) {
    LOG_INFO("Testing hardware error recovery mechanisms");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for error recovery testing");
        return TEST_RESULT_SKIP;
    }
    
    nic_info_t *nic = hardware_get_nic(0);
    if (!nic) {
        LOG_ERROR("Failed to get NIC 0 for error recovery test");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 1: Link lost recovery */
    LOG_DEBUG("Testing link lost recovery");
    uint8_t mock_device_id = g_mock_device_ids[0];
    
    /* Simulate link down */
    mock_device_set_link_status(mock_device_id, false, 0);
    
    /* Check if hardware layer detects link loss */
    int link_status = hardware_get_link_status(nic);
    if (link_status > 0) {
        LOG_ERROR("Link should be down but hardware reports up");
        return TEST_RESULT_FAIL;
    }
    
    /* Restore link and verify recovery */
    mock_device_set_link_status(mock_device_id, true, 10);
    link_status = hardware_get_link_status(nic);
    if (link_status <= 0) {
        LOG_ERROR("Link should be up after recovery");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 2: Hardware reset recovery */
    LOG_DEBUG("Testing hardware reset recovery");
    
    int reset_result = hardware_reset_nic(nic);
    if (reset_result != SUCCESS) {
        LOG_ERROR("Hardware reset failed: %d", reset_result);
        return TEST_RESULT_FAIL;
    }
    
    /* Verify NIC is still functional after reset */
    if (!(nic->status & NIC_STATUS_PRESENT)) {
        LOG_ERROR("NIC should be present after reset");
        return TEST_RESULT_FAIL;
    }
    
    /* Test 3: Self-test recovery */
    LOG_DEBUG("Testing self-test recovery");
    
    int self_test_result = hardware_test_nic(nic);
    if (self_test_result != SUCCESS) {
        LOG_ERROR("Self-test failed: %d", self_test_result);
        return TEST_RESULT_FAIL;
    }
    
    LOG_INFO("Hardware error recovery mechanisms test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test NIC failover functionality
 * @return Test result
 */
static test_result_t hw_test_nic_failover(void) {
    LOG_INFO("Testing NIC failover functionality");
    
    if (hardware_get_nic_count() < 2) {
        LOG_WARNING("Need at least 2 NICs for failover testing");
        return TEST_RESULT_SKIP;
    }
    
    nic_info_t *primary_nic = hardware_get_nic(0);
    nic_info_t *backup_nic = hardware_get_nic(1);
    
    if (!primary_nic || !backup_nic) {
        LOG_ERROR("Failed to get NICs for failover test");
        return TEST_RESULT_FAIL;
    }
    
    LOG_DEBUG("Testing failover from NIC 0 to NIC 1");
    
    /* Test hardware failover function */
    int failover_result = hardware_test_failover(0);
    if (failover_result != SUCCESS) {
        LOG_ERROR("Hardware failover test failed: %d", failover_result);
        return TEST_RESULT_FAIL;
    }
    
    /* Simulate primary NIC failure */
    test_result_t sim_result = hw_test_simulate_hardware_failure(0, HW_FAILURE_CRITICAL);
    if (sim_result != TEST_RESULT_PASS) {
        LOG_ERROR("Failed to simulate hardware failure");
        return sim_result;
    }
    
    /* Verify failover behavior */
    test_result_t verify_result = hw_test_verify_failover_behavior(0, 1);
    if (verify_result != TEST_RESULT_PASS) {
        LOG_ERROR("Failover verification failed");
        return verify_result;
    }
    
    /* Restore primary NIC */
    uint8_t primary_device_id = g_mock_device_ids[0];
    mock_device_enable(primary_device_id, true);
    mock_device_set_link_status(primary_device_id, true, 10);
    
    LOG_INFO("NIC failover test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test resource allocation and deallocation
 * @return Test result
 */
static test_result_t hw_test_resource_allocation(void) {
    LOG_INFO("Testing resource allocation and deallocation");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for resource allocation testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test NIC initialization (resource allocation) */
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) continue;
        
        /* Test resource allocation via NIC initialization */
        int init_result = hardware_init_nic(nic);
        if (init_result != SUCCESS) {
            LOG_ERROR("Failed to initialize NIC %d: %d", i, init_result);
            return TEST_RESULT_FAIL;
        }
        
        /* Verify NIC is initialized */
        if (!(nic->status & NIC_STATUS_INITIALIZED)) {
            LOG_ERROR("NIC %d should be marked as initialized", i);
            return TEST_RESULT_FAIL;
        }
        
        /* Test resource deallocation via NIC cleanup */
        int cleanup_result = hardware_cleanup_nic(nic);
        if (cleanup_result != SUCCESS) {
            LOG_ERROR("Failed to cleanup NIC %d: %d", i, cleanup_result);
            return TEST_RESULT_FAIL;
        }
        
        /* Re-initialize for other tests */
        hardware_init_nic(nic);
    }
    
    /* Test memory resource tracking */
    const mem_stats_t *mem_stats_before = memory_get_stats();
    uint32_t memory_before = mem_stats_before->used_memory;
    
    /* Allocate resources for packet buffers */
    void *test_buffers[10];
    for (int i = 0; i < 10; i++) {
        test_buffers[i] = memory_alloc(HW_TEST_PACKET_SIZE, MEM_TYPE_PACKET_BUFFER, 0);
        if (!test_buffers[i]) {
            LOG_ERROR("Failed to allocate test buffer %d", i);
            return TEST_RESULT_FAIL;
        }
    }
    
    const mem_stats_t *mem_stats_after = memory_get_stats();
    uint32_t memory_after = mem_stats_after->used_memory;
    
    if (memory_after <= memory_before) {
        LOG_ERROR("Memory usage should have increased after allocation");
        return TEST_RESULT_FAIL;
    }
    
    /* Free allocated resources */
    for (int i = 0; i < 10; i++) {
        if (test_buffers[i]) {
            memory_free(test_buffers[i]);
        }
    }
    
    const mem_stats_t *mem_stats_final = memory_get_stats();
    uint32_t memory_final = mem_stats_final->used_memory;
    
    if (memory_final > memory_before + 512) {  /* Allow some tolerance */
        LOG_ERROR("Memory leak detected: before=%lu, final=%lu", 
                  memory_before, memory_final);
        return TEST_RESULT_FAIL;
    }
    
    LOG_INFO("Resource allocation and deallocation test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test hardware capability detection
 * @return Test result
 */
static test_result_t hw_test_capability_detection(void) {
    LOG_INFO("Testing hardware capability detection");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for capability testing");
        return TEST_RESULT_SKIP;
    }
    
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) continue;
        
        LOG_DEBUG("Testing capabilities for NIC %d (type=%d)", i, nic->type);
        
        /* Test capability flags based on NIC type */
        switch (nic->type) {
            case NIC_TYPE_3C509B:
                /* 3C509B expected capabilities */
                if (nic->capabilities & HW_CAP_DMA) {
                    LOG_ERROR("3C509B should not have DMA capability");
                    return TEST_RESULT_FAIL;
                }
                
                if (nic->capabilities & HW_CAP_BUS_MASTER) {
                    LOG_ERROR("3C509B should not have bus mastering capability");
                    return TEST_RESULT_FAIL;
                }
                
                if (!(nic->capabilities & HW_CAP_PROMISCUOUS)) {
                    LOG_WARNING("3C509B should support promiscuous mode");
                }
                break;
                
            case NIC_TYPE_3C515_TX:
                /* 3C515 expected capabilities */
                if (!(nic->capabilities & HW_CAP_DMA)) {
                    LOG_ERROR("3C515 should have DMA capability");
                    return TEST_RESULT_FAIL;
                }
                
                if (!(nic->capabilities & HW_CAP_BUS_MASTER)) {
                    LOG_ERROR("3C515 should have bus mastering capability");
                    return TEST_RESULT_FAIL;
                }
                
                if (!(nic->capabilities & HW_CAP_FULL_DUPLEX)) {
                    LOG_WARNING("3C515 should support full duplex");
                }
                
                if (!(nic->capabilities & HW_CAP_AUTO_SPEED)) {
                    LOG_WARNING("3C515 should support auto speed detection");
                }
                break;
                
            default:
                LOG_WARNING("Unknown NIC type %d", nic->type);
                break;
        }
        
        /* Test capability-based feature detection */
        if (nic->capabilities & HW_CAP_PROMISCUOUS) {
            int result = hardware_set_promiscuous_mode(nic, true);
            if (result != SUCCESS) {
                LOG_ERROR("Failed to enable promiscuous mode on capable NIC");
                return TEST_RESULT_FAIL;
            }
            hardware_set_promiscuous_mode(nic, false);
        }
        
        if (nic->capabilities & HW_CAP_FULL_DUPLEX) {
            int speed = 0;
            bool full_duplex = false;
            int result = hardware_get_speed_duplex(nic, &speed, &full_duplex);
            if (result != SUCCESS) {
                LOG_ERROR("Failed to get speed/duplex on capable NIC");
                return TEST_RESULT_FAIL;
            }
        }
    }
    
    LOG_INFO("Hardware capability detection test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test resource contention scenarios
 * @return Test result
 */
static test_result_t hw_test_resource_contention(void) {
    LOG_INFO("Testing resource contention scenarios");
    
    if (hardware_get_nic_count() < 2) {
        LOG_WARNING("Need at least 2 NICs for contention testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test concurrent packet operations */
    uint8_t test_packet[HW_TEST_PACKET_SIZE];
    memset(test_packet, 0xAA, sizeof(test_packet));
    
    /* Test simultaneous transmission on multiple NICs */
    for (int cycle = 0; cycle < 10; cycle++) {
        for (int i = 0; i < hardware_get_nic_count() && i < 2; i++) {
            nic_info_t *nic = hardware_get_nic(i);
            if (!nic) continue;
            
            /* Modify packet to be unique per NIC */
            test_packet[0] = 0x50 + i;
            test_packet[1] = cycle;
            
            int result = hardware_send_packet(nic, test_packet, sizeof(test_packet));
            if (result != SUCCESS && result != ERROR_BUSY) {
                LOG_ERROR("Packet send failed on NIC %d, cycle %d: %d", i, cycle, result);
                return TEST_RESULT_FAIL;
            }
        }
        
        /* Small delay between cycles */
        for (volatile int delay = 0; delay < 1000; delay++);
    }
    
    /* Test hardware resource contention function */
    int contention_result = hardware_test_resource_contention(50);
    if (contention_result != SUCCESS) {
        LOG_ERROR("Hardware resource contention test failed: %d", contention_result);
        return TEST_RESULT_FAIL;
    }
    
    LOG_INFO("Resource contention test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test concurrent operations across multiple NICs
 * @return Test result
 */
static test_result_t hw_test_concurrent_operations(void) {
    LOG_INFO("Testing concurrent operations");
    
    if (hardware_get_nic_count() < 2) {
        LOG_WARNING("Need at least 2 NICs for concurrent operations testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test concurrent operations using hardware function */
    int concurrent_result = hardware_test_concurrent_operations(3000);  /* 3 seconds */
    if (concurrent_result != SUCCESS) {
        LOG_ERROR("Concurrent operations test failed: %d", concurrent_result);
        return TEST_RESULT_FAIL;
    }
    
    LOG_INFO("Concurrent operations test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test load balancing across multiple NICs
 * @return Test result
 */
static test_result_t hw_test_load_balancing(void) {
    LOG_INFO("Testing load balancing");
    
    if (hardware_get_nic_count() < 2) {
        LOG_WARNING("Need at least 2 NICs for load balancing testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test load balancing using hardware function */
    int lb_result = hardware_test_load_balancing(100);  /* 100 packets */
    if (lb_result != SUCCESS) {
        LOG_ERROR("Load balancing test failed: %d", lb_result);
        return TEST_RESULT_FAIL;
    }
    
    LOG_INFO("Load balancing test passed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Test hardware failure injection
 * @return Test result
 */
static test_result_t hw_test_hardware_failure_injection(void) {
    LOG_INFO("Testing hardware failure injection");
    
    if (hardware_get_nic_count() < 1) {
        LOG_WARNING("No NICs available for failure injection testing");
        return TEST_RESULT_SKIP;
    }
    
    /* Test various failure types */
    int failure_types[] = {
        MOCK_ERROR_TX_TIMEOUT,
        MOCK_ERROR_RX_OVERRUN,
        MOCK_ERROR_CRC_ERROR,
        MOCK_ERROR_DMA_ERROR
    };
    
    for (int i = 0; i < sizeof(failure_types) / sizeof(failure_types[0]); i++) {
        uint8_t device_id = g_mock_device_ids[0];
        
        /* Inject error */
        int inject_result = mock_error_inject(device_id, failure_types[i], 1);
        if (inject_result != SUCCESS) {
            LOG_ERROR("Failed to inject error type %d", failure_types[i]);
            return TEST_RESULT_FAIL;
        }
        
        /* Perform operation that should trigger the error */
        nic_info_t *nic = hardware_get_nic(0);
        uint8_t test_data[] = "ERROR_INJECTION_TEST";
        
        /* This might fail due to injected error - that's expected */
        hardware_send_packet(nic, test_data, sizeof(test_data));
        
        /* Clear the error */
        mock_error_clear(device_id);
        
        /* Verify normal operation resumes */
        int normal_result = hardware_send_packet(nic, test_data, sizeof(test_data));
        if (normal_result != SUCCESS && normal_result != ERROR_BUSY) {
            LOG_ERROR("Normal operation should resume after error clear");
            return TEST_RESULT_FAIL;
        }
    }
    
    LOG_INFO("Hardware failure injection test passed");
    return TEST_RESULT_PASS;
}

/* Helper function implementations */

/**
 * @brief Validate NIC info structure
 * @param nic NIC info to validate
 * @param expected_type Expected NIC type
 * @return true if valid, false otherwise
 */
static bool hw_test_validate_nic_info(const nic_info_t *nic, nic_type_t expected_type) {
    if (!nic) return false;
    
    if (nic->type != expected_type) {
        LOG_ERROR("NIC type mismatch: expected %d, got %d", expected_type, nic->type);
        return false;
    }
    
    if (!nic->ops) {
        LOG_ERROR("NIC operations vtable is NULL");
        return false;
    }
    
    if (nic->io_base == 0) {
        LOG_ERROR("NIC I/O base address is 0");
        return false;
    }
    
    return true;
}

/**
 * @brief Validate vtable operations for specific NIC type
 * @param ops Operations vtable
 * @param type NIC type
 * @return Test result
 */
static test_result_t hw_test_validate_vtable(const nic_ops_t *ops, nic_type_t type) {
    if (!ops) {
        LOG_ERROR("Operations vtable is NULL");
        return TEST_RESULT_FAIL;
    }
    
    /* Check required function pointers */
    if (!ops->init) {
        LOG_ERROR("Missing init function in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->cleanup) {
        LOG_ERROR("Missing cleanup function in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->send_packet) {
        LOG_ERROR("Missing send_packet function in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->receive_packet) {
        LOG_ERROR("Missing receive_packet function in vtable");
        return TEST_RESULT_FAIL;
    }
    
    if (!ops->get_mac_address) {
        LOG_ERROR("Missing get_mac_address function in vtable");
        return TEST_RESULT_FAIL;
    }
    
    /* Type-specific validations */
    switch (type) {
        case NIC_TYPE_3C515_TX:
            /* 3C515 should have DMA-specific functions */
            if (!ops->handle_interrupt) {
                LOG_WARNING("3C515 missing interrupt handler");
            }
            break;
            
        case NIC_TYPE_3C509B:
            /* 3C509B uses PIO, different requirements */
            break;
            
        default:
            LOG_WARNING("Unknown NIC type for vtable validation: %d", type);
            break;
    }
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Simulate hardware failure on specified NIC
 * @param nic_index NIC index
 * @param failure_type Failure type to simulate
 * @return Test result
 */
static test_result_t hw_test_simulate_hardware_failure(int nic_index, int failure_type) {
    if (nic_index >= g_num_mock_devices) {
        return TEST_RESULT_FAIL;
    }
    
    uint8_t device_id = g_mock_device_ids[nic_index];
    mock_error_type_t mock_error = MOCK_ERROR_ADAPTER_FAILURE;
    
    /* Map hardware failure types to mock error types */
    switch (failure_type) {
        case HW_FAILURE_LINK_LOST:
            mock_device_set_link_status(device_id, false, 0);
            return TEST_RESULT_PASS;
            
        case HW_FAILURE_CRITICAL:
            mock_device_enable(device_id, false);
            return TEST_RESULT_PASS;
            
        default:
            mock_error = MOCK_ERROR_ADAPTER_FAILURE;
            break;
    }
    
    int result = mock_error_inject(device_id, mock_error, 1);
    return (result == SUCCESS) ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Verify failover behavior between NICs
 * @param primary_nic Primary NIC index
 * @param backup_nic Backup NIC index
 * @return Test result
 */
static test_result_t hw_test_verify_failover_behavior(int primary_nic, int backup_nic) {
    nic_info_t *primary = hardware_get_nic(primary_nic);
    nic_info_t *backup = hardware_get_nic(backup_nic);
    
    if (!primary || !backup) {
        return TEST_RESULT_FAIL;
    }
    
    /* Verify backup NIC is available and functional */
    if (!(backup->status & NIC_STATUS_PRESENT)) {
        LOG_ERROR("Backup NIC is not present");
        return TEST_RESULT_FAIL;
    }
    
    /* Test packet transmission on backup NIC */
    uint8_t test_packet[] = "FAILOVER_TEST_PACKET";
    int result = hardware_send_packet(backup, test_packet, sizeof(test_packet));
    if (result != SUCCESS && result != ERROR_BUSY) {
        LOG_ERROR("Backup NIC packet transmission failed: %d", result);
        return TEST_RESULT_FAIL;
    }
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Get current timestamp (simple implementation for testing)
 * @return Timestamp
 */
static uint32_t hw_test_get_timestamp(void) {
    static uint32_t counter = 0;
    return ++counter;
}

/**
 * @brief Run all hardware abstraction layer tests
 * @return 0 on success, negative on error
 */
int run_hardware_tests(void) {
    int result = hw_test_setup();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to setup hardware test environment");
        return result;
    }
    
    LOG_INFO("=== Starting Hardware Abstraction Layer Tests ===");
    
    /* Define test cases */
    struct {
        const char *name;
        test_result_t (*test_func)(void);
        bool required;
    } test_cases[] = {
        {"Vtable Operations", hw_test_vtable_operations, true},
        {"Multi-NIC Detection", hw_test_multi_nic_detection, true},
        {"Multi-NIC Enumeration", hw_test_multi_nic_enumeration, true},
        {"Error Recovery Mechanisms", hw_test_error_recovery_mechanisms, true},
        {"NIC Failover", hw_test_nic_failover, false},
        {"Resource Allocation", hw_test_resource_allocation, true},
        {"Capability Detection", hw_test_capability_detection, true},
        {"Resource Contention", hw_test_resource_contention, false},
        {"Concurrent Operations", hw_test_concurrent_operations, false},
        {"Load Balancing", hw_test_load_balancing, false},
        {"Hardware Failure Injection", hw_test_hardware_failure_injection, true}
    };
    
    int total_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    int passed_tests = 0;
    int failed_tests = 0;
    int skipped_tests = 0;
    
    /* Run each test */
    for (int i = 0; i < total_tests; i++) {
        LOG_INFO("Running test: %s", test_cases[i].name);
        
        uint32_t start_time = hw_test_get_timestamp();
        test_result_t test_result = test_cases[i].test_func();
        uint32_t duration = hw_test_get_timestamp() - start_time;
        
        switch (test_result) {
            case TEST_RESULT_PASS:
                LOG_INFO("PASS: %s (duration: %lu)", test_cases[i].name, duration);
                passed_tests++;
                break;
                
            case TEST_RESULT_FAIL:
                LOG_ERROR("FAIL: %s (duration: %lu)", test_cases[i].name, duration);
                failed_tests++;
                if (test_cases[i].required) {
                    LOG_ERROR("Required test failed, stopping");
                    hw_test_cleanup();
                    return ERROR_HARDWARE;
                }
                break;
                
            case TEST_RESULT_SKIP:
                LOG_INFO("SKIP: %s (duration: %lu)", test_cases[i].name, duration);
                skipped_tests++;
                break;
                
            default:
                LOG_ERROR("ERROR: %s returned invalid result %d", test_cases[i].name, test_result);
                failed_tests++;
                break;
        }
    }
    
    /* Generate test summary */
    LOG_INFO("=== Hardware Test Summary ===");
    LOG_INFO("Total tests: %d", total_tests);
    LOG_INFO("Passed: %d", passed_tests);
    LOG_INFO("Failed: %d", failed_tests);
    LOG_INFO("Skipped: %d", skipped_tests);
    
    if (failed_tests == 0) {
        LOG_INFO("=== ALL HARDWARE TESTS PASSED ===");
        result = SUCCESS;
    } else {
        LOG_ERROR("=== %d HARDWARE TESTS FAILED ===", failed_tests);
        result = ERROR_HARDWARE;
    }
    
    hw_test_cleanup();
    return result;
}

/**
 * @brief Main function for standalone hardware test execution
 * @param argc Argument count
 * @param argv Argument values
 * @return 0 on success, non-zero on failure
 */
int main(int argc, char *argv[]) {
    printf("3Com Packet Driver - Hardware Abstraction Layer Test Suite\n");
    printf("==========================================================\n");
    
    /* Initialize logging for test output */
    logging_init(LOG_LEVEL_INFO);
    
    /* Run comprehensive hardware tests */
    int result = run_hardware_tests();
    
    if (result == SUCCESS) {
        printf("\n=== ALL HARDWARE TESTS PASSED ===\n");
        return 0;
    } else {
        printf("\n=== HARDWARE TESTS FAILED ===\n");
        return 1;
    }
}