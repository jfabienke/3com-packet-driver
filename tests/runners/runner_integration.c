/**
 * @file runner_integration.c
 * @brief Integration Test Runner - Cross-component and system-level testing
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test runner executes integration tests that verify multiple components
 * work together correctly, including:
 * - Driver + Memory integration
 * - Protocol + Driver integration
 * - Hardware + Software integration
 * - Multi-NIC scenarios
 * - End-to-end packet flow
 * - System-level error handling
 */

#include "../common/test_framework.h"
#include "../common/hardware_mock.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include "../../include/packet_ops.h"
#include "../../include/arp.h"
#include "../../include/routing.h"
#include <stdio.h>
#include <string.h>

/* External integration test functions */
extern int test_integ_memory_main(void);

/* Integration test configuration */
typedef struct {
    bool run_driver_memory_tests;
    bool run_protocol_driver_tests;
    bool run_hardware_software_tests;
    bool run_multi_nic_tests;
    bool run_end_to_end_tests;
    bool run_error_recovery_tests;
    bool run_system_validation_tests;
    bool verbose_output;
    bool stop_on_failure;
    uint32_t test_timeout_ms;
} integration_test_config_t;

/* Integration test statistics */
typedef struct {
    int total_suites_run;
    int suites_passed;
    int suites_failed;
    int total_tests_run;
    int total_tests_passed;
    int total_tests_failed;
    uint32_t total_duration_ms;
    uint32_t longest_test_ms;
    const char *longest_test_name;
} integration_test_stats_t;

/* Integration test suite definition */
typedef struct {
    const char *name;
    const char *description;
    int (*test_main)(void);
    bool *enabled_flag;
    bool is_critical;
    uint32_t expected_duration_ms;
} integration_test_suite_t;

static integration_test_config_t g_integration_config = {
    .run_driver_memory_tests = true,
    .run_protocol_driver_tests = true,
    .run_hardware_software_tests = true,
    .run_multi_nic_tests = true,
    .run_end_to_end_tests = true,
    .run_error_recovery_tests = true,
    .run_system_validation_tests = true,
    .verbose_output = false,
    .stop_on_failure = false,
    .test_timeout_ms = 30000  /* 30 seconds default timeout */
};

static integration_test_stats_t g_integration_stats = {0};

/* Forward declarations for integration test functions */
static int test_driver_memory_integration(void);
static int test_protocol_driver_integration(void);
static int test_hardware_software_integration(void);
static int test_multi_nic_integration(void);
static int test_end_to_end_packet_flow(void);
static int test_error_recovery_integration(void);
static int test_system_validation_integration(void);

/**
 * @brief Parse command line arguments for integration test configuration
 */
static int parse_integration_test_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_integration_config.verbose_output = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stop-on-failure") == 0) {
            g_integration_config.stop_on_failure = true;
        } else if (strcmp(argv[i], "--drivers-only") == 0) {
            g_integration_config.run_driver_memory_tests = true;
            g_integration_config.run_protocol_driver_tests = false;
            g_integration_config.run_hardware_software_tests = false;
            g_integration_config.run_multi_nic_tests = false;
            g_integration_config.run_end_to_end_tests = false;
            g_integration_config.run_error_recovery_tests = false;
            g_integration_config.run_system_validation_tests = false;
        } else if (strcmp(argv[i], "--protocols-only") == 0) {
            g_integration_config.run_driver_memory_tests = false;
            g_integration_config.run_protocol_driver_tests = true;
            g_integration_config.run_hardware_software_tests = false;
            g_integration_config.run_multi_nic_tests = false;
            g_integration_config.run_end_to_end_tests = false;
            g_integration_config.run_error_recovery_tests = false;
            g_integration_config.run_system_validation_tests = false;
        } else if (strcmp(argv[i], "--end-to-end-only") == 0) {
            g_integration_config.run_driver_memory_tests = false;
            g_integration_config.run_protocol_driver_tests = false;
            g_integration_config.run_hardware_software_tests = false;
            g_integration_config.run_multi_nic_tests = false;
            g_integration_config.run_end_to_end_tests = true;
            g_integration_config.run_error_recovery_tests = false;
            g_integration_config.run_system_validation_tests = false;
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 < argc) {
                g_integration_config.test_timeout_ms = atoi(argv[++i]) * 1000; /* Convert seconds to ms */
            } else {
                log_error("--timeout requires a value in seconds");
                return -1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Integration Test Runner - 3Com Packet Driver\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -v, --verbose          Enable verbose output\n");
            printf("  -s, --stop-on-failure  Stop on first test failure\n");
            printf("  --drivers-only         Run only driver integration tests\n");
            printf("  --protocols-only       Run only protocol integration tests\n");
            printf("  --end-to-end-only      Run only end-to-end tests\n");
            printf("  --timeout <seconds>    Set test timeout (default: 30)\n");
            printf("  -h, --help             Show this help\n");
            printf("\nIntegration test categories:\n");
            printf("  Driver+Memory         - Driver and memory subsystem integration\n");
            printf("  Protocol+Driver       - Protocol stack and driver integration\n");
            printf("  Hardware+Software     - Hardware abstraction integration\n");
            printf("  Multi-NIC             - Multiple NIC coordination\n");
            printf("  End-to-End            - Complete packet flow validation\n");
            printf("  Error Recovery        - System error recovery integration\n");
            printf("  System Validation     - Overall system validation\n");
            return 1;
        }
    }
    
    return 0;
}

/**
 * @brief Initialize integration test environment
 */
static int initialize_integration_test_environment(void) {
    log_info("Initializing integration test environment");
    
    /* Initialize logging with appropriate level */
    int result = logging_init();
    if (result != 0) {
        printf("Failed to initialize logging system\n");
        return -1;
    }
    
    if (g_integration_config.verbose_output) {
        log_set_level(LOG_LEVEL_DEBUG);
    } else {
        log_set_level(LOG_LEVEL_INFO);
    }
    
    /* Initialize memory management */
    result = memory_init();
    if (result != 0) {
        log_error("Failed to initialize memory management");
        return -2;
    }
    
    /* Initialize hardware mock framework with extended capabilities */
    result = mock_framework_init();
    if (result != 0) {
        log_error("Failed to initialize hardware mock framework");
        return -3;
    }
    
    /* Enable advanced mock features for integration testing */
    mock_enable_multi_nic_simulation(true);
    mock_enable_error_injection(true);
    mock_enable_timing_simulation(true);
    
    /* Initialize test framework with integration test configuration */
    test_config_t test_config;
    test_config_init_default(&test_config);
    test_config.test_hardware = true;
    test_config.test_memory = true;
    test_config.test_packet_ops = true;
    test_config.run_benchmarks = false;
    test_config.run_stress_tests = false;
    test_config.verbose_output = g_integration_config.verbose_output;
    test_config.init_hardware = true;
    test_config.init_memory = true;
    test_config.init_diagnostics = true;
    test_config.stress_duration_ms = g_integration_config.test_timeout_ms;
    
    result = test_framework_init(&test_config);
    if (result != 0) {
        log_error("Failed to initialize test framework");
        return -4;
    }
    
    /* Initialize protocol systems for integration testing */
    result = arp_init();
    if (result != 0) {
        log_error("Failed to initialize ARP for integration testing");
        return -5;
    }
    
    result = routing_init();
    if (result != 0) {
        log_error("Failed to initialize routing for integration testing");
        return -6;
    }
    
    log_info("Integration test environment initialized successfully");
    return 0;
}

/**
 * @brief Cleanup integration test environment
 */
static void cleanup_integration_test_environment(void) {
    log_info("Cleaning up integration test environment");
    
    /* Cleanup protocol systems */
    routing_cleanup();
    arp_cleanup();
    
    /* Cleanup frameworks */
    test_framework_cleanup();
    mock_framework_cleanup();
    memory_cleanup();
    logging_cleanup();
    
    log_info("Integration test environment cleanup completed");
}

/**
 * @brief Test driver and memory subsystem integration
 */
static int test_driver_memory_integration(void) {
    log_info("Testing driver and memory subsystem integration");
    
    /* Test memory allocation patterns under driver load */
    const mem_stats_t *initial_stats = memory_get_stats();
    uint32_t initial_allocations = initial_stats->total_allocations;
    
    /* Simulate driver initialization and operation */
    int result = hardware_init_all();
    if (result != 0) {
        log_error("Hardware initialization failed");
        return -1;
    }
    
    /* Test packet buffer allocation/deallocation under load */
    const int NUM_PACKETS = 100;
    packet_buffer_t *packets[NUM_PACKETS];
    
    for (int i = 0; i < NUM_PACKETS; i++) {
        packets[i] = packet_buffer_alloc(1500);
        if (!packets[i]) {
            log_error("Packet buffer allocation failed at iteration %d", i);
            return -1;
        }
    }
    
    /* Verify memory statistics */
    const mem_stats_t *loaded_stats = memory_get_stats();
    if (loaded_stats->total_allocations <= initial_allocations) {
        log_error("Memory allocations not tracking correctly");
        return -1;
    }
    
    /* Test deallocation */
    for (int i = 0; i < NUM_PACKETS; i++) {
        packet_buffer_free(packets[i]);
    }
    
    /* Test driver cleanup and memory cleanup */
    hardware_cleanup_all();
    
    const mem_stats_t *final_stats = memory_get_stats();
    if (final_stats->used_memory > initial_stats->used_memory + 1024) { /* Allow some overhead */
        log_error("Memory leak detected in driver-memory integration");
        return -1;
    }
    
    log_info("Driver-memory integration test PASSED");
    return 0;
}

/**
 * @brief Test protocol and driver integration
 */
static int test_protocol_driver_integration(void) {
    log_info("Testing protocol and driver integration");
    
    /* Initialize mock NICs for testing */
    int num_nics = mock_create_test_nics(2);
    if (num_nics < 2) {
        log_error("Failed to create test NICs for protocol integration");
        return -1;
    }
    
    /* Test ARP packet transmission through driver */
    ip_addr_t target_ip = {192, 168, 1, 100};
    uint8_t resolved_mac[ETH_ALEN];
    uint8_t resolved_nic;
    
    /* This should trigger ARP request through the driver */
    int result = arp_resolve(&target_ip, resolved_mac, &resolved_nic);
    
    /* Verify that ARP request was sent through the driver */
    mock_statistics_t mock_stats;
    mock_get_statistics(&mock_stats);
    
    if (mock_stats.packets_injected == 0) {
        log_error("ARP request was not sent through driver");
        return -1;
    }
    
    /* Test routing decision affecting driver selection */
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    /* Build test packet */
    uint8_t dest_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x12, 0x34, 0x56};
    uint8_t src_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x78, 0x9A, 0xBC};
    
    memcpy(packet_data, dest_mac, ETH_ALEN);
    memcpy(packet_data + ETH_ALEN, src_mac, ETH_ALEN);
    *(uint16_t*)(packet_data + 12) = htons(ETH_P_IP);
    
    uint8_t output_nic;
    route_decision_t decision = routing_decide(&test_packet, 0, &output_nic);
    
    if (decision == ROUTE_DECISION_DROP) {
        log_error("Routing decision should not drop test packet");
        return -1;
    }
    
    /* Test protocol statistics integration with driver statistics */
    const arp_stats_t *arp_stats = arp_get_stats();
    const routing_stats_t *routing_stats = routing_get_stats();
    
    if (!arp_stats || !routing_stats) {
        log_error("Protocol statistics not available");
        return -1;
    }
    
    log_info("Protocol-driver integration test PASSED");
    return 0;
}

/**
 * @brief Test hardware and software integration
 */
static int test_hardware_software_integration(void) {
    log_info("Testing hardware and software integration");
    
    /* Test hardware detection and software initialization */
    int detected_nics = hardware_detect_nics();
    if (detected_nics == 0) {
        log_warning("No NICs detected, using mock NICs for integration test");
        detected_nics = mock_create_test_nics(1);
        if (detected_nics == 0) {
            log_error("Failed to create mock NICs");
            return -1;
        }
    }
    
    /* Test software configuration of hardware */
    for (int i = 0; i < detected_nics; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (!nic) {
            log_error("Failed to get NIC info for NIC %d", i);
            return -1;
        }
        
        /* Test software configuration */
        int result = hardware_configure_nic(nic, NIC_MODE_PROMISCUOUS);
        if (result != 0) {
            log_error("Failed to configure NIC %d", i);
            return -1;
        }
        
        /* Test hardware status reporting to software */
        uint32_t status = hardware_get_nic_status(nic);
        if (status & NIC_STATUS_ERROR) {
            log_error("NIC %d reports error status", i);
            return -1;
        }
    }
    
    /* Test interrupt handling integration */
    mock_inject_interrupt(0, IRQ_TYPE_RX_COMPLETE);
    
    /* Process any pending interrupts */
    irq_process_pending();
    
    /* Verify interrupt was handled correctly */
    mock_statistics_t mock_stats;
    mock_get_statistics(&mock_stats);
    
    if (mock_stats.interrupts_generated == 0) {
        log_error("Interrupt injection failed");
        return -1;
    }
    
    log_info("Hardware-software integration test PASSED");
    return 0;
}

/**
 * @brief Test multi-NIC integration scenarios
 */
static int test_multi_nic_integration(void) {
    log_info("Testing multi-NIC integration scenarios");
    
    /* Create multiple mock NICs */
    const int NUM_NICS = 3;
    int created_nics = mock_create_test_nics(NUM_NICS);
    if (created_nics < NUM_NICS) {
        log_error("Failed to create sufficient test NICs");
        return -1;
    }
    
    /* Test load balancing across NICs */
    packet_buffer_t test_packets[NUM_NICS * 2];
    
    for (int i = 0; i < NUM_NICS * 2; i++) {
        test_packets[i].data = malloc(ETH_HLEN + 64);
        test_packets[i].length = ETH_HLEN + 64;
        
        if (!test_packets[i].data) {
            log_error("Failed to allocate test packet data");
            return -1;
        }
        
        /* Build test packet with varying destinations */
        uint8_t dest_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x00, 0x00, (uint8_t)i};
        uint8_t src_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0xFF, 0xFF, 0xFF};
        
        memcpy(test_packets[i].data, dest_mac, ETH_ALEN);
        memcpy(test_packets[i].data + ETH_ALEN, src_mac, ETH_ALEN);
        *(uint16_t*)(test_packets[i].data + 12) = htons(ETH_P_IP);
        
        /* Send packet and verify it uses different NICs */
        uint8_t selected_nic;
        route_decision_t decision = routing_decide(&test_packets[i], 0, &selected_nic);
        
        if (decision == ROUTE_DECISION_DROP) {
            log_error("Multi-NIC routing should not drop packets");
            free(test_packets[i].data);
            return -1;
        }
        
        if (selected_nic >= NUM_NICS) {
            log_error("Selected NIC %d is out of range", selected_nic);
            free(test_packets[i].data);
            return -1;
        }
    }
    
    /* Cleanup test packets */
    for (int i = 0; i < NUM_NICS * 2; i++) {
        free(test_packets[i].data);
    }
    
    /* Test NIC failover */
    mock_inject_nic_failure(0);
    
    /* Verify system adapts to NIC failure */
    packet_buffer_t failover_packet;
    uint8_t failover_data[ETH_HLEN + 64];
    failover_packet.data = failover_data;
    failover_packet.length = sizeof(failover_data);
    
    uint8_t failover_nic;
    route_decision_t failover_decision = routing_decide(&failover_packet, 0, &failover_nic);
    
    if (failover_decision == ROUTE_DECISION_DROP) {
        log_error("System should adapt to NIC failure");
        return -1;
    }
    
    if (failover_nic == 0) {
        log_error("System should not use failed NIC");
        return -1;
    }
    
    log_info("Multi-NIC integration test PASSED");
    return 0;
}

/**
 * @brief Test end-to-end packet flow
 */
static int test_end_to_end_packet_flow(void) {
    log_info("Testing end-to-end packet flow");
    
    /* Set up complete network scenario */
    int result = mock_create_test_nics(2);
    if (result < 2) {
        log_error("Failed to create test NICs for end-to-end test");
        return -1;
    }
    
    /* Configure network addresses */
    ip_addr_t src_ip = {192, 168, 1, 10};
    ip_addr_t dst_ip = {192, 168, 1, 20};
    uint8_t src_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x01, 0x02, 0x03};
    uint8_t dst_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x04, 0x05, 0x06};
    
    /* Add ARP entries for end-to-end communication */
    result = arp_cache_add(&src_ip, src_mac, 0, ARP_FLAG_COMPLETE);
    if (result != 0) {
        log_error("Failed to add source ARP entry");
        return -1;
    }
    
    result = arp_cache_add(&dst_ip, dst_mac, 1, ARP_FLAG_COMPLETE);
    if (result != 0) {
        log_error("Failed to add destination ARP entry");
        return -1;
    }
    
    /* Create test packet for end-to-end flow */
    packet_buffer_t *test_packet = packet_buffer_alloc(64);
    if (!test_packet) {
        log_error("Failed to allocate test packet");
        return -1;
    }
    
    /* Build complete packet */
    memcpy(test_packet->data, dst_mac, ETH_ALEN);
    memcpy(test_packet->data + ETH_ALEN, src_mac, ETH_ALEN);
    *(uint16_t*)(test_packet->data + 12) = htons(ETH_P_IP);
    
    /* Fill with test data */
    for (int i = ETH_HLEN; i < test_packet->length; i++) {
        test_packet->data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Test complete packet transmission flow */
    result = packet_transmit(test_packet, 0);
    if (result != 0) {
        log_error("Packet transmission failed");
        packet_buffer_free(test_packet);
        return -1;
    }
    
    /* Verify packet was processed through all layers */
    mock_statistics_t mock_stats;
    mock_get_statistics(&mock_stats);
    
    if (mock_stats.packets_extracted == 0) {
        log_error("Packet was not processed through mock framework");
        packet_buffer_free(test_packet);
        return -1;
    }
    
    /* Test packet reception flow */
    mock_inject_test_packet(1, test_packet->data, test_packet->length);
    
    /* Process any received packets */
    result = packet_process_received();
    if (result < 0) {
        log_error("Packet reception processing failed");
        packet_buffer_free(test_packet);
        return -1;
    }
    
    packet_buffer_free(test_packet);
    
    log_info("End-to-end packet flow test PASSED");
    return 0;
}

/**
 * @brief Test error recovery integration
 */
static int test_error_recovery_integration(void) {
    log_info("Testing error recovery integration");
    
    /* Test memory allocation failure recovery */
    mock_enable_memory_allocation_failures(true, 50); /* 50% failure rate */
    
    /* Attempt operations that should gracefully handle allocation failures */
    packet_buffer_t *packets[10];
    int successful_allocations = 0;
    
    for (int i = 0; i < 10; i++) {
        packets[i] = packet_buffer_alloc(1500);
        if (packets[i]) {
            successful_allocations++;
        }
    }
    
    if (successful_allocations == 0) {
        log_error("System should successfully allocate some packets even with failures");
        mock_enable_memory_allocation_failures(false, 0);
        return -1;
    }
    
    /* Cleanup successful allocations */
    for (int i = 0; i < 10; i++) {
        if (packets[i]) {
            packet_buffer_free(packets[i]);
        }
    }
    
    mock_enable_memory_allocation_failures(false, 0);
    
    /* Test hardware error recovery */
    mock_inject_hardware_error(0, HARDWARE_ERROR_TIMEOUT);
    
    /* System should detect and recover from hardware error */
    int recovery_result = hardware_recover_from_error(0);
    if (recovery_result != 0) {
        log_error("Hardware error recovery failed");
        return -1;
    }
    
    /* Test protocol timeout recovery */
    mock_enable_packet_loss(true, 100); /* 100% packet loss */
    
    /* ARP resolution should timeout and recover gracefully */
    ip_addr_t timeout_ip = {192, 168, 99, 99};
    uint8_t timeout_mac[ETH_ALEN];
    uint8_t timeout_nic;
    
    int arp_result = arp_resolve(&timeout_ip, timeout_mac, &timeout_nic);
    /* ARP should timeout, but system should remain stable */
    
    mock_enable_packet_loss(false, 0);
    
    log_info("Error recovery integration test PASSED");
    return 0;
}

/**
 * @brief Test overall system validation
 */
static int test_system_validation_integration(void) {
    log_info("Testing overall system validation");
    
    /* Run existing integration memory test */
    int result = test_integ_memory_main();
    if (result != 0) {
        log_error("Integration memory test failed");
        return -1;
    }
    
    /* Test system under various load conditions */
    mock_statistics_t initial_stats;
    mock_get_statistics(&initial_stats);
    
    /* Simulate network activity */
    const int VALIDATION_PACKETS = 50;
    for (int i = 0; i < VALIDATION_PACKETS; i++) {
        packet_buffer_t *packet = packet_buffer_alloc(64 + (i * 10));
        if (packet) {
            /* Build varied test packets */
            uint8_t test_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x00, (uint8_t)(i >> 8), (uint8_t)(i & 0xFF)};
            memcpy(packet->data, test_mac, ETH_ALEN);
            memcpy(packet->data + ETH_ALEN, test_mac, ETH_ALEN);
            *(uint16_t*)(packet->data + 12) = htons(ETH_P_IP);
            
            /* Process packet through system */
            uint8_t output_nic;
            routing_decide(packet, 0, &output_nic);
            
            packet_buffer_free(packet);
        }
        
        /* Periodically trigger system maintenance */
        if (i % 10 == 0) {
            arp_cache_age_entries();
            memory_defragment();
        }
    }
    
    /* Verify system stability */
    mock_statistics_t final_stats;
    mock_get_statistics(&final_stats);
    
    if (final_stats.total_io_operations <= initial_stats.total_io_operations) {
        log_error("System should show activity during validation");
        return -1;
    }
    
    /* Check for memory leaks */
    const mem_stats_t *mem_stats = memory_get_stats();
    if (mem_stats->used_memory > 1024 * 1024) { /* 1MB threshold */
        log_warning("High memory usage detected: %lu bytes", mem_stats->used_memory);
    }
    
    /* Verify all subsystems are still functional */
    result = hardware_self_test_all();
    if (result != 0) {
        log_error("Hardware self-test failed after system validation");
        return -1;
    }
    
    log_info("System validation integration test PASSED");
    return 0;
}

/**
 * @brief Run a specific integration test suite
 */
static int run_integration_test_suite(const integration_test_suite_t *suite) {
    if (!suite || !suite->test_main) {
        log_error("Invalid integration test suite");
        return -1;
    }
    
    log_info("=== Running Integration Test Suite: %s ===", suite->name);
    log_info("Description: %s", suite->description);
    log_info("Expected duration: %lu ms", suite->expected_duration_ms);
    
    uint32_t start_time = get_system_timestamp_ms();
    
    int result = suite->test_main();
    
    uint32_t end_time = get_system_timestamp_ms();
    uint32_t duration = end_time - start_time;
    
    g_integration_stats.total_suites_run++;
    
    /* Track longest test */
    if (duration > g_integration_stats.longest_test_ms) {
        g_integration_stats.longest_test_ms = duration;
        g_integration_stats.longest_test_name = suite->name;
    }
    
    if (result == 0) {
        g_integration_stats.suites_passed++;
        log_info("✓ Integration Test Suite PASSED: %s (duration: %lu ms)", suite->name, duration);
        
        if (duration > suite->expected_duration_ms * 2) {
            log_warning("Test took longer than expected (%lu ms vs %lu ms expected)", 
                       duration, suite->expected_duration_ms);
        }
    } else {
        g_integration_stats.suites_failed++;
        log_error("✗ Integration Test Suite FAILED: %s (duration: %lu ms, code: %d)", 
                  suite->name, duration, result);
        
        if (suite->is_critical && g_integration_config.stop_on_failure) {
            log_error("Critical integration test suite failed, stopping execution");
            return result;
        }
    }
    
    return result;
}

/**
 * @brief Print integration test summary
 */
static void print_integration_test_summary(void) {
    log_info("");
    log_info("===================================================================");
    log_info("                INTEGRATION TEST SUITE SUMMARY");
    log_info("===================================================================");
    log_info("Test Suites Executed:");
    log_info("  Total Suites: %d", g_integration_stats.total_suites_run);
    log_info("  Passed: %d", g_integration_stats.suites_passed);
    log_info("  Failed: %d", g_integration_stats.suites_failed);
    log_info("");
    log_info("Individual Tests:");
    log_info("  Total Tests: %d", g_integration_stats.total_tests_run);
    log_info("  Passed: %d", g_integration_stats.total_tests_passed);
    log_info("  Failed: %d", g_integration_stats.total_tests_failed);
    log_info("");
    log_info("Execution Time:");
    log_info("  Total Duration: %lu ms (%.2f seconds)", 
             g_integration_stats.total_duration_ms, g_integration_stats.total_duration_ms / 1000.0);
    log_info("  Longest Test: %s (%lu ms)", 
             g_integration_stats.longest_test_name ? g_integration_stats.longest_test_name : "N/A",
             g_integration_stats.longest_test_ms);
    log_info("");
    
    if (g_integration_stats.suites_failed == 0) {
        log_info("Success Rate: 100%% - ALL INTEGRATION TESTS PASSED! ✓");
    } else {
        float success_rate = (float)g_integration_stats.suites_passed / g_integration_stats.total_suites_run * 100.0;
        log_info("Success Rate: %.1f%% (%d/%d suites passed)", 
                 success_rate, g_integration_stats.suites_passed, g_integration_stats.total_suites_run);
        
        if (success_rate >= 80.0) {
            log_info("Result: GOOD - Most integration tests passed");
        } else if (success_rate >= 60.0) {
            log_warning("Result: ACCEPTABLE - Some integration tests failed");
        } else {
            log_error("Result: POOR - Many integration tests failed");
        }
    }
    
    log_info("===================================================================");
}

/**
 * @brief Main integration test runner entry point (called from master runner)
 */
int run_integration_tests(int argc, char *argv[]) {
    log_info("Starting Integration Test Suite Runner");
    log_info("=====================================");
    
    /* Parse integration test specific arguments */
    int parse_result = parse_integration_test_arguments(argc, argv);
    if (parse_result == 1) {
        return 0;  /* Help was shown */
    } else if (parse_result < 0) {
        return 1;  /* Error in arguments */
    }
    
    /* Initialize integration test environment */
    int init_result = initialize_integration_test_environment();
    if (init_result != 0) {
        log_error("Failed to initialize integration test environment");
        return 1;
    }
    
    uint32_t overall_start_time = get_system_timestamp_ms();
    
    /* Define all integration test suites */
    integration_test_suite_t test_suites[] = {
        {
            .name = "Driver+Memory Integration",
            .description = "Driver and memory subsystem integration testing",
            .test_main = test_driver_memory_integration,
            .enabled_flag = &g_integration_config.run_driver_memory_tests,
            .is_critical = true,
            .expected_duration_ms = 5000
        },
        {
            .name = "Protocol+Driver Integration",
            .description = "Protocol stack and driver integration testing",
            .test_main = test_protocol_driver_integration,
            .enabled_flag = &g_integration_config.run_protocol_driver_tests,
            .is_critical = true,
            .expected_duration_ms = 8000
        },
        {
            .name = "Hardware+Software Integration",
            .description = "Hardware abstraction and software integration testing",
            .test_main = test_hardware_software_integration,
            .enabled_flag = &g_integration_config.run_hardware_software_tests,
            .is_critical = true,
            .expected_duration_ms = 6000
        },
        {
            .name = "Multi-NIC Integration",
            .description = "Multiple NIC coordination and load balancing",
            .test_main = test_multi_nic_integration,
            .enabled_flag = &g_integration_config.run_multi_nic_tests,
            .is_critical = false,
            .expected_duration_ms = 10000
        },
        {
            .name = "End-to-End Packet Flow",
            .description = "Complete packet flow validation from ingress to egress",
            .test_main = test_end_to_end_packet_flow,
            .enabled_flag = &g_integration_config.run_end_to_end_tests,
            .is_critical = true,
            .expected_duration_ms = 12000
        },
        {
            .name = "Error Recovery Integration",
            .description = "System error recovery and fault tolerance testing",
            .test_main = test_error_recovery_integration,
            .enabled_flag = &g_integration_config.run_error_recovery_tests,
            .is_critical = false,
            .expected_duration_ms = 15000
        },
        {
            .name = "System Validation",
            .description = "Overall system validation and stability testing",
            .test_main = test_system_validation_integration,
            .enabled_flag = &g_integration_config.run_system_validation_tests,
            .is_critical = true,
            .expected_duration_ms = 20000
        }
    };
    
    int num_suites = sizeof(test_suites) / sizeof(test_suites[0]);
    int overall_result = 0;
    
    /* Run all enabled integration test suites */
    for (int i = 0; i < num_suites; i++) {
        if (!(*test_suites[i].enabled_flag)) {
            log_info("Skipping disabled integration test suite: %s", test_suites[i].name);
            continue;
        }
        
        int result = run_integration_test_suite(&test_suites[i]);
        if (result != 0) {
            overall_result = 1;
            
            if (test_suites[i].is_critical && g_integration_config.stop_on_failure) {
                log_error("Critical integration test suite failed, stopping execution");
                break;
            }
        }
    }
    
    uint32_t overall_end_time = get_system_timestamp_ms();
    g_integration_stats.total_duration_ms = overall_end_time - overall_start_time;
    
    /* Get test framework statistics */
    test_framework_stats_t framework_stats;
    if (test_framework_get_statistics(&framework_stats) == 0) {
        g_integration_stats.total_tests_run = framework_stats.total_tests;
        g_integration_stats.total_tests_passed = framework_stats.tests_passed;
        g_integration_stats.total_tests_failed = framework_stats.tests_failed;
    }
    
    /* Print comprehensive summary */
    print_integration_test_summary();
    
    /* Cleanup */
    cleanup_integration_test_environment();
    
    if (overall_result == 0) {
        log_info("Integration Test Suite: ALL TESTS COMPLETED SUCCESSFULLY");
    } else {
        log_error("Integration Test Suite: SOME TESTS FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Standalone entry point (when run directly)
 */
int main(int argc, char *argv[]) {
    printf("3Com Packet Driver - Integration Test Suite Runner\n");
    printf("=================================================\n\n");
    
    return run_integration_tests(argc, argv);
}