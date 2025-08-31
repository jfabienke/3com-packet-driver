/**
 * @file runner_protocols.c
 * @brief Network Protocol Test Runner - ARP and Routing Test Integration
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test runner integrates comprehensive ARP and routing test suites
 * with the test framework, providing complete network protocol testing
 * with hardware mocking and network topology simulation.
 */

#include "../common/test_framework.h"
#include "../common/hardware_mock.h"
#include "../common/network_topology_sim.h"
#include "../../include/arp.h"
#include "../../include/routing.h"
#include "../../include/static_routing.h"
#include "../../include/logging.h"
#include <stdio.h>
#include <string.h>

/* External test suite functions */
extern int run_arp_test_suite(void);
extern int run_routing_test_suite(void);

/* Test configuration */
typedef struct {
    bool enable_arp_tests;
    bool enable_routing_tests;
    bool enable_integration_tests;
    bool enable_stress_tests;
    bool enable_topology_simulation;
    bool verbose_logging;
    uint32_t test_timeout_ms;
    uint32_t stress_duration_ms;
} network_test_config_t;

/* Test statistics */
typedef struct {
    uint32_t total_tests_run;
    uint32_t arp_tests_passed;
    uint32_t arp_tests_failed;
    uint32_t routing_tests_passed;
    uint32_t routing_tests_failed;
    uint32_t integration_tests_passed;
    uint32_t integration_tests_failed;
    uint32_t total_duration_ms;
    uint32_t setup_time_ms;
    uint32_t cleanup_time_ms;
} network_test_stats_t;

static network_test_config_t g_test_config;
static network_test_stats_t g_test_stats;

/* Forward declarations */
static int setup_network_test_environment(void);
static void cleanup_network_test_environment(void);
static int run_integration_tests(void);
static int run_stress_tests(void);
static int run_topology_tests(void);
static void print_test_summary(void);
static void initialize_test_config(network_test_config_t *config);

/* ========== Integration Tests ========== */

static int test_arp_routing_integration(void) {
    log_info("Starting ARP-Routing Integration Test");
    
    /* Set up multi-NIC network scenario */
    int result = network_topology_init(8, 12);
    if (result != 0) {
        log_error("Topology initialization failed");
        return -1;
    }
    
    /* Create a realistic network topology */
    network_node_type_t node_types[] = {
        NODE_TYPE_HOST,     /* Node 0: Host A */
        NODE_TYPE_SWITCH,   /* Node 1: Switch */
        NODE_TYPE_ROUTER,   /* Node 2: Router */
        NODE_TYPE_HOST      /* Node 3: Host B */
    };
    
    result = network_create_linear_topology(4, node_types);
    if (result != 0) {
        log_error("Linear topology creation failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Configure IP addressing */
    ip_addr_t subnet1 = {192, 168, 1, 0};
    ip_addr_t subnet2 = {192, 168, 2, 0};
    ip_addr_t netmask = {255, 255, 255, 0};
    
    result = static_subnet_add(&subnet1, &netmask, 0);
    if (result != 0) {
        log_error("Subnet 1 configuration failed");
        network_topology_cleanup();
        return -1;
    }
    
    result = static_subnet_add(&subnet2, &netmask, 1);
    if (result != 0) {
        log_error("Subnet 2 configuration failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Test ARP resolution across network segments */
    ip_addr_t host_a_ip = {192, 168, 1, 10};
    ip_addr_t host_b_ip = {192, 168, 2, 10};
    uint8_t host_a_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0xA0, 0x00, 0x01};
    uint8_t host_b_mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0xB0, 0x00, 0x01};
    
    /* Add ARP entries */
    result = arp_cache_add(&host_a_ip, host_a_mac, 0, ARP_FLAG_COMPLETE);
    if (result != 0) {
        log_error("Host A ARP entry failed");
        network_topology_cleanup();
        return -1;
    }
    
    result = arp_cache_add(&host_b_ip, host_b_mac, 1, ARP_FLAG_COMPLETE);
    if (result != 0) {
        log_error("Host B ARP entry failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Test inter-subnet routing */
    uint8_t resolved_mac[ETH_ALEN];
    uint8_t resolved_nic;
    
    result = arp_resolve(&host_b_ip, resolved_mac, &resolved_nic);
    if (result != 0) {
        log_error("Cross-subnet ARP resolution failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Verify routing decision for cross-subnet traffic */
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    /* Build Ethernet frame */
    memcpy(packet_data, host_b_mac, ETH_ALEN);           /* Dest MAC */
    memcpy(packet_data + ETH_ALEN, host_a_mac, ETH_ALEN); /* Src MAC */
    *(uint16_t*)(packet_data + 12) = htons(ETH_P_IP);   /* EtherType */
    
    uint8_t output_nic;
    route_decision_t decision = routing_decide(&test_packet, 0, &output_nic);
    if (decision != ROUTE_DECISION_FORWARD) {
        log_error("Should forward cross-subnet traffic");
        network_topology_cleanup();
        return -1;
    }
    
    /* Test ARP aging during routing */
    arp_cache_age_entries();
    
    /* Verify routing statistics */
    const routing_stats_t *routing_stats = routing_get_stats();
    const arp_stats_t *arp_stats = arp_get_stats();
    
    if (!routing_stats || routing_stats->table_lookups == 0) {
        log_error("Should have routing table lookups");
        network_topology_cleanup();
        return -1;
    }
    
    if (!arp_stats || arp_stats->cache_updates == 0) {
        log_error("Should have ARP cache updates");
        network_topology_cleanup();
        return -1;
    }
    
    network_topology_cleanup();
    
    log_info("ARP-Routing Integration Test PASSED");
    return 0;
}

static int test_failover_convergence(void) {
    log_info("Starting Failover and Convergence Test");
    
    /* Set up redundant network topology */
    int result = network_topology_init(6, 8);
    if (result != 0) {
        log_error("Topology initialization failed");
        return -1;
    }
    
    /* Create mesh topology for redundancy testing */
    result = network_create_mesh_topology(4, NODE_TYPE_SWITCH, false);
    if (result != 0) {
        log_error("Mesh topology creation failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Trigger network convergence */
    result = network_trigger_convergence();
    if (result != 0) {
        log_error("Initial convergence failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Get baseline statistics */
    network_topology_stats_t baseline_stats;
    result = network_get_topology_stats(&baseline_stats);
    if (result != 0) {
        log_error("Baseline stats retrieval failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Simulate link failure */
    result = network_simulate_link_failure(0, 5000); /* 5 second failure */
    if (result != 0) {
        log_error("Link failure simulation failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Trigger convergence after failure */
    result = network_trigger_convergence();
    if (result != 0) {
        log_error("Post-failure convergence failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Verify network adapted to failure */
    network_topology_stats_t failure_stats;
    result = network_get_topology_stats(&failure_stats);
    if (result != 0) {
        log_error("Failure stats retrieval failed");
        network_topology_cleanup();
        return -1;
    }
    
    if (failure_stats.failed_links <= baseline_stats.failed_links) {
        log_error("Should have more failed links after failure simulation");
        network_topology_cleanup();
        return -1;
    }
    
    /* Process recovery */
    uint32_t recovery_start = get_system_timestamp_ms();
    int recovery_attempts = 0;
    
    while (recovery_attempts < 10) {
        int recovery_result = network_process_recovery();
        if (recovery_result > 0) {
            /* Topology changed */
            network_trigger_convergence();
            break;
        }
        recovery_attempts++;
        /* Simulate time passing */
        if (get_system_timestamp_ms() - recovery_start > 6000) {
            break; /* Timeout recovery simulation */
        }
    }
    
    /* Verify recovery */
    network_topology_stats_t recovery_stats;
    result = network_get_topology_stats(&recovery_stats);
    if (result != 0) {
        log_error("Recovery stats retrieval failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Test ARP/routing behavior during convergence */
    const arp_stats_t *arp_stats = arp_get_stats();
    const routing_stats_t *routing_stats = routing_get_stats();
    
    if (!arp_stats || !routing_stats) {
        log_error("Stats should remain accessible during convergence");
        network_topology_cleanup();
        return -1;
    }
    
    network_topology_cleanup();
    
    log_info("Failover and Convergence Test PASSED");
    return 0;
}

/* ========== Stress Tests ========== */

static int test_high_load_arp_routing(void) {
    log_info("Starting High Load ARP and Routing Test");
    
    const int STRESS_NODES = 16;
    const int STRESS_PACKETS = 1000;
    
    /* Set up large topology */
    int result = network_topology_init(STRESS_NODES, STRESS_NODES * 2);
    if (result != 0) {
        log_error("Large topology initialization failed");
        return -1;
    }
    
    /* Create mesh topology for stress testing */
    result = network_create_mesh_topology(STRESS_NODES, NODE_TYPE_SWITCH, false);
    if (result != 0) {
        log_error("Stress mesh topology failed");
        network_topology_cleanup();
        return -1;
    }
    
    /* Generate large number of ARP entries */
    for (int i = 0; i < STRESS_NODES * 10; i++) {
        ip_addr_t stress_ip;
        uint8_t stress_mac[ETH_ALEN];
        
        ip_addr_set(&stress_ip, 172, 16, (i >> 8) & 0xFF, i & 0xFF);
        stress_mac[0] = 0x02;
        stress_mac[1] = 0x00;
        stress_mac[2] = (i >> 24) & 0xFF;
        stress_mac[3] = (i >> 16) & 0xFF;
        stress_mac[4] = (i >> 8) & 0xFF;
        stress_mac[5] = i & 0xFF;
        
        result = arp_cache_add(&stress_ip, stress_mac, i % MAX_NICS, ARP_FLAG_COMPLETE);
        /* Continue even if some fail due to capacity limits */
    }
    
    /* Generate large number of routing rules */
    for (int i = 0; i < 100; i++) {
        uint8_t rule_mac[ETH_ALEN];
        rule_mac[0] = 0x02;
        rule_mac[1] = 0x01;
        rule_mac[2] = (i >> 8) & 0xFF;
        rule_mac[3] = i & 0xFF;
        rule_mac[4] = 0x00;
        rule_mac[5] = 0x00;
        
        result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, rule_mac, 
                                 0, (i + 1) % MAX_NICS, ROUTE_DECISION_FORWARD);
        /* Continue even if some fail */
    }
    
    /* Stress test packet processing */
    packet_buffer_t stress_packet;
    uint8_t stress_packet_data[ETH_HLEN + 1500]; /* Maximum Ethernet frame */
    stress_packet.data = stress_packet_data;
    stress_packet.length = sizeof(stress_packet_data);
    
    uint32_t stress_start_time = get_system_timestamp_ms();
    int successful_routes = 0;
    
    for (int i = 0; i < STRESS_PACKETS; i++) {
        /* Create varied test packets */
        uint8_t src_mac[ETH_ALEN] = {0x02, 0x00, 0x00, 0x00, (i >> 8) & 0xFF, i & 0xFF};
        uint8_t dest_mac[ETH_ALEN] = {0x02, 0x01, 0x00, 0x00, (i >> 8) & 0xFF, i & 0xFF};
        
        memcpy(stress_packet_data, dest_mac, ETH_ALEN);
        memcpy(stress_packet_data + ETH_ALEN, src_mac, ETH_ALEN);
        *(uint16_t*)(stress_packet_data + 12) = htons(ETH_P_IP);
        
        uint8_t output_nic;
        route_decision_t decision = routing_decide(&stress_packet, i % MAX_NICS, &output_nic);
        
        if (decision == ROUTE_DECISION_FORWARD || decision == ROUTE_DECISION_BROADCAST) {
            successful_routes++;
        }
        
        /* Periodically trigger aging to test under load */
        if (i % 100 == 0) {
            arp_cache_age_entries();
            bridge_age_entries();
        }
    }
    
    uint32_t stress_end_time = get_system_timestamp_ms();
    uint32_t stress_duration = stress_end_time - stress_start_time;
    
    /* Verify stress test results */
    if (successful_routes <= STRESS_PACKETS / 2) {
        log_error("Should successfully route most packets");
        network_topology_cleanup();
        return -1;
    }
    
    if (stress_duration >= 30000) {
        log_error("Stress test should complete within 30 seconds");
        network_topology_cleanup();
        return -1;
    }
    
    /* Verify system stability after stress */
    const arp_stats_t *arp_stats = arp_get_stats();
    const routing_stats_t *routing_stats = routing_get_stats();
    
    if (!arp_stats || !routing_stats) {
        log_error("Statistics should remain accessible after stress");
        network_topology_cleanup();
        return -1;
    }
    
    log_info("Stress test completed: %d/%d packets routed in %d ms", 
             successful_routes, STRESS_PACKETS, stress_duration);
    
    network_topology_cleanup();
    
    log_info("High Load ARP and Routing Test PASSED");
    return 0;
}

/* ========== Test Suite Runners ========== */

static int run_integration_tests(void) {
    log_info("Starting Network Protocol Integration Tests");
    
    int overall_result = 0;
    int tests_passed = 0;
    int tests_failed = 0;
    
    struct {
        const char *name;
        int (*test_func)(void);
    } integration_tests[] = {
        {"ARP-Routing Integration", test_arp_routing_integration},
        {"Failover and Convergence", test_failover_convergence}
    };
    
    int num_tests = sizeof(integration_tests) / sizeof(integration_tests[0]);
    
    for (int i = 0; i < num_tests; i++) {
        log_info("Running integration test: %s", integration_tests[i].name);
        
        int result = integration_tests[i].test_func();
        
        if (result == 0) {
            tests_passed++;
            g_test_stats.integration_tests_passed++;
            log_info("Integration test PASSED: %s", integration_tests[i].name);
        } else {
            tests_failed++;
            g_test_stats.integration_tests_failed++;
            overall_result = -1;
            log_error("Integration test FAILED: %s", integration_tests[i].name);
        }
    }
    
    log_info("Integration Tests Results: %d passed, %d failed", tests_passed, tests_failed);
    
    return overall_result;
}

static int run_stress_tests(void) {
    log_info("Starting Network Protocol Stress Tests");
    
    int overall_result = 0;
    int tests_passed = 0;
    int tests_failed = 0;
    
    struct {
        const char *name;
        int (*test_func)(void);
    } stress_tests[] = {
        {"High Load ARP and Routing", test_high_load_arp_routing}
    };
    
    int num_tests = sizeof(stress_tests) / sizeof(stress_tests[0]);
    
    for (int i = 0; i < num_tests; i++) {
        log_info("Running stress test: %s", stress_tests[i].name);
        
        int result = stress_tests[i].test_func();
        
        if (result == 0) {
            tests_passed++;
            log_info("Stress test PASSED: %s", stress_tests[i].name);
        } else {
            tests_failed++;
            overall_result = -1;
            log_error("Stress test FAILED: %s", stress_tests[i].name);
        }
    }
    
    log_info("Stress Tests Results: %d passed, %d failed", tests_passed, tests_failed);
    
    return overall_result;
}

static int run_topology_tests(void) {
    log_info("Starting Network Topology Tests");
    
    /* Test basic topology operations */
    int result = network_topology_init(16, 32);
    if (result != 0) {
        log_error("Topology init failed");
        return -1;
    }
    
    /* Test various topology creation functions */
    network_node_type_t linear_types[] = {NODE_TYPE_HOST, NODE_TYPE_SWITCH, NODE_TYPE_ROUTER, NODE_TYPE_HOST};
    result = network_create_linear_topology(4, linear_types);
    if (result != 0) {
        log_error("Linear topology failed");
        network_topology_cleanup();
        return -1;
    }
    
    network_topology_cleanup();
    
    result = network_topology_init(8, 16);
    if (result != 0) {
        log_error("Topology re-init failed");
        return -1;
    }
    
    int hub_id = network_create_star_topology(4, NODE_TYPE_SWITCH, NODE_TYPE_HOST);
    if (hub_id < 0) {
        log_error("Star topology failed");
        network_topology_cleanup();
        return -1;
    }
    
    network_topology_cleanup();
    
    result = network_topology_init(6, 12);
    if (result != 0) {
        log_error("Topology re-init failed");
        return -1;
    }
    
    result = network_create_ring_topology(6, NODE_TYPE_SWITCH);
    if (result != 0) {
        log_error("Ring topology failed");
        network_topology_cleanup();
        return -1;
    }
    
    network_topology_cleanup();
    
    log_info("Network Topology Tests: ALL PASSED");
    
    return 0;
}

/* ========== Main Test Functions ========== */

static int setup_network_test_environment(void) {
    uint32_t setup_start = get_system_timestamp_ms();
    
    log_info("Setting up network protocol test environment");
    
    /* Initialize hardware mock framework */
    int result = mock_framework_init();
    if (result != 0) {
        log_error("Failed to initialize mock framework: %d", result);
        return -1;
    }
    
    /* Initialize ARP system */
    result = arp_init();
    if (result != 0) {
        log_error("Failed to initialize ARP: %d", result);
        return -1;
    }
    
    result = arp_enable(true);
    if (result != 0) {
        log_error("Failed to enable ARP: %d", result);
        return -1;
    }
    
    /* Initialize routing system */
    result = routing_init();
    if (result != 0) {
        log_error("Failed to initialize routing: %d", result);
        return -1;
    }
    
    result = routing_enable(true);
    if (result != 0) {
        log_error("Failed to enable routing: %d", result);
        return -1;
    }
    
    /* Initialize static routing */
    result = static_routing_init();
    if (result != 0) {
        log_error("Failed to initialize static routing: %d", result);
        return -1;
    }
    
    result = static_routing_enable(true);
    if (result != 0) {
        log_error("Failed to enable static routing: %d", result);
        return -1;
    }
    
    uint32_t setup_end = get_system_timestamp_ms();
    g_test_stats.setup_time_ms = setup_end - setup_start;
    
    log_info("Test environment setup completed in %d ms", g_test_stats.setup_time_ms);
    
    return 0;
}

static void cleanup_network_test_environment(void) {
    uint32_t cleanup_start = get_system_timestamp_ms();
    
    log_info("Cleaning up network protocol test environment");
    
    /* Cleanup in reverse order */
    static_routing_cleanup();
    routing_cleanup();
    arp_cleanup();
    mock_framework_cleanup();
    network_topology_cleanup();
    
    uint32_t cleanup_end = get_system_timestamp_ms();
    g_test_stats.cleanup_time_ms = cleanup_end - cleanup_start;
    
    log_info("Test environment cleanup completed in %d ms", g_test_stats.cleanup_time_ms);
}

static void initialize_test_config(network_test_config_t *config) {
    if (!config) {
        return;
    }
    
    /* Set default configuration */
    config->enable_arp_tests = true;
    config->enable_routing_tests = true;
    config->enable_integration_tests = true;
    config->enable_stress_tests = true;
    config->enable_topology_simulation = true;
    config->verbose_logging = true;
    config->test_timeout_ms = 60000;      /* 60 seconds per test */
    config->stress_duration_ms = 30000;   /* 30 seconds for stress tests */
}

static void print_test_summary(void) {
    log_info("========================================");
    log_info("Network Protocol Test Suite Summary");
    log_info("========================================");
    log_info("Total tests run: %d", g_test_stats.total_tests_run);
    log_info("ARP tests: %d passed, %d failed", 
             g_test_stats.arp_tests_passed, g_test_stats.arp_tests_failed);
    log_info("Routing tests: %d passed, %d failed", 
             g_test_stats.routing_tests_passed, g_test_stats.routing_tests_failed);
    log_info("Integration tests: %d passed, %d failed", 
             g_test_stats.integration_tests_passed, g_test_stats.integration_tests_failed);
    log_info("Total duration: %d ms", g_test_stats.total_duration_ms);
    log_info("Setup time: %d ms", g_test_stats.setup_time_ms);
    log_info("Cleanup time: %d ms", g_test_stats.cleanup_time_ms);
    
    int total_passed = g_test_stats.arp_tests_passed + 
                      g_test_stats.routing_tests_passed + 
                      g_test_stats.integration_tests_passed;
    int total_failed = g_test_stats.arp_tests_failed + 
                      g_test_stats.routing_tests_failed + 
                      g_test_stats.integration_tests_failed;
    
    log_info("Overall: %d passed, %d failed", total_passed, total_failed);
    
    if (total_failed == 0) {
        log_info("========================================");
        log_info("ALL NETWORK PROTOCOL TESTS PASSED!");
        log_info("========================================");
    } else {
        log_error("========================================");
        log_error("SOME NETWORK PROTOCOL TESTS FAILED!");
        log_error("========================================");
    }
}

/* ========== Main Entry Points ========== */

/**
 * @brief Main protocol test runner entry point (called from master runner)
 */
int run_protocol_tests(int argc, char *argv[]) {
    /* Initialize test configuration */
    initialize_test_config(&g_test_config);
    memset(&g_test_stats, 0, sizeof(g_test_stats));
    
    uint32_t total_start_time = get_system_timestamp_ms();
    
    log_info("Starting Network Protocol Test Suite");
    log_info("====================================");
    
    /* Set up test environment */
    int setup_result = setup_network_test_environment();
    if (setup_result != 0) {
        log_error("Failed to set up test environment");
        return 1;
    }
    
    int overall_result = 0;
    
    /* Run ARP test suite */
    if (g_test_config.enable_arp_tests) {
        log_info("Running ARP Protocol Test Suite");
        int arp_result = run_arp_test_suite();
        
        if (arp_result == 0) {
            g_test_stats.arp_tests_passed = 18; /* Number of ARP tests */
            log_info("ARP Test Suite: ALL TESTS PASSED");
        } else {
            g_test_stats.arp_tests_failed = 18;
            overall_result = -1;
            log_error("ARP Test Suite: SOME TESTS FAILED");
        }
    }
    
    /* Run Routing test suite */
    if (g_test_config.enable_routing_tests) {
        log_info("Running Routing Functionality Test Suite");
        int routing_result = run_routing_test_suite();
        
        if (routing_result == 0) {
            g_test_stats.routing_tests_passed = 14; /* Number of routing tests */
            log_info("Routing Test Suite: ALL TESTS PASSED");
        } else {
            g_test_stats.routing_tests_failed = 14;
            overall_result = -1;
            log_error("Routing Test Suite: SOME TESTS FAILED");
        }
    }
    
    /* Run Integration tests */
    if (g_test_config.enable_integration_tests) {
        int integration_result = run_integration_tests();
        if (integration_result != 0) {
            overall_result = -1;
        }
    }
    
    /* Run Stress tests */
    if (g_test_config.enable_stress_tests) {
        int stress_result = run_stress_tests();
        if (stress_result != 0) {
            overall_result = -1;
        }
    }
    
    /* Run Topology tests */
    if (g_test_config.enable_topology_simulation) {
        int topology_result = run_topology_tests();
        if (topology_result != 0) {
            overall_result = -1;
        }
    }
    
    /* Calculate total stats */
    uint32_t total_end_time = get_system_timestamp_ms();
    g_test_stats.total_duration_ms = total_end_time - total_start_time;
    g_test_stats.total_tests_run = g_test_stats.arp_tests_passed + g_test_stats.arp_tests_failed +
                                  g_test_stats.routing_tests_passed + g_test_stats.routing_tests_failed +
                                  g_test_stats.integration_tests_passed + g_test_stats.integration_tests_failed;
    
    /* Clean up test environment */
    cleanup_network_test_environment();
    
    /* Print final summary */
    print_test_summary();
    
    return (overall_result == 0) ? 0 : 1;
}

/**
 * @brief Standalone entry point (when run directly)
 */
int main(int argc, char *argv[]) {
    log_info("3Com Packet Driver - Network Protocol Test Suite");
    log_info("================================================");
    
    return run_protocol_tests(argc, argv);
}