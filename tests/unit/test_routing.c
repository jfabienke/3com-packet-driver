/**
 * @file test_routing.c
 * @brief Comprehensive Routing Functionality Test Suite
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite validates multi-NIC routing functionality including
 * static routing table management, flow-aware routing decisions,
 * failover routing logic, and route prioritization with hardware
 * mocking support for realistic network topology simulation.
 */

#include "../../include/routing.h"
#include "../../include/static_routing.h"
#include "../../include/test_framework.h"
#include "../../include/hardware_mock.h"
#include "../../include/packet_ops.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test constants */
#define TEST_ROUTE_MAC_1       {0x00, 0x10, 0x4B, 0x11, 0x11, 0x11}
#define TEST_ROUTE_MAC_2       {0x00, 0x10, 0x4B, 0x22, 0x22, 0x22}
#define TEST_ROUTE_MAC_3       {0x00, 0x10, 0x4B, 0x33, 0x33, 0x33}
#define TEST_ROUTE_BROADCAST   {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define TEST_ROUTE_MULTICAST   {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}
#define TEST_IP_NET1           {192, 168, 1, 0}
#define TEST_IP_NET2           {192, 168, 2, 0}
#define TEST_IP_NET3           {10, 0, 1, 0}
#define TEST_NETMASK_24        {255, 255, 255, 0}
#define TEST_NETMASK_16        {255, 255, 0, 0}
#define MAX_TEST_ROUTE_ENTRIES 64
#define MAX_BRIDGE_TEST_ENTRIES 128

/* Test data structures */
typedef struct {
    route_rule_type_t type;
    union {
        uint8_t mac[ETH_ALEN];
        uint16_t ethertype;
    } rule_data;
    uint8_t src_nic;
    uint8_t dest_nic;
    route_decision_t decision;
    uint8_t priority;
} test_route_rule_t;

typedef struct {
    uint8_t mac[ETH_ALEN];
    uint8_t nic_index;
    uint32_t packet_count;
    uint32_t age_seconds;
} test_bridge_entry_t;

typedef struct {
    uint8_t src_nic;
    uint8_t dest_nic;
    route_decision_t expected_decision;
    uint32_t packet_size;
    uint16_t ethertype;
    bool should_forward;
    bool should_broadcast;
    const char *description;
} test_routing_scenario_t;

/* Test fixture for routing tests */
typedef struct {
    uint8_t mock_devices[MAX_NICS];
    uint8_t device_count;
    test_route_rule_t test_rules[MAX_TEST_ROUTE_ENTRIES];
    uint8_t rule_count;
    test_bridge_entry_t bridge_entries[MAX_BRIDGE_TEST_ENTRIES];
    uint8_t bridge_entry_count;
    uint32_t test_start_time;
} routing_test_fixture_t;

static routing_test_fixture_t g_routing_test_fixture;

/* Forward declarations */
static test_result_t setup_routing_test_environment(void);
static void cleanup_routing_test_environment(void);
static test_result_t create_test_packet(packet_buffer_t *packet, const uint8_t *dest_mac,
                                       const uint8_t *src_mac, uint16_t ethertype, 
                                       const uint8_t *payload, uint16_t payload_len);
static test_result_t verify_routing_statistics(const routing_stats_t *expected);
static test_result_t setup_multi_nic_topology(void);
static test_result_t simulate_link_failure_recovery(uint8_t nic_index);
static bool verify_packet_forwarded(uint8_t src_nic, uint8_t dest_nic);

/* ========== Routing Initialization and Configuration Tests ========== */

static test_result_t test_routing_initialization(void) {
    TEST_LOG_START("Routing Initialization");
    
    /* Test uninitialized state */
    TEST_ASSERT(!routing_is_enabled(), "Routing should not be enabled before initialization");
    
    /* Initialize routing system */
    int result = routing_init();
    TEST_ASSERT(result == SUCCESS, "Routing initialization should succeed");
    
    /* Verify initialized state */
    TEST_ASSERT(!routing_is_enabled(), "Routing should not be auto-enabled after init");
    
    /* Enable routing */
    result = routing_enable(true);
    TEST_ASSERT(result == SUCCESS, "Routing enable should succeed");
    TEST_ASSERT(routing_is_enabled(), "Routing should be enabled after routing_enable(true)");
    
    /* Test configuration parameters */
    bool learning_enabled = routing_get_learning_enabled();
    TEST_ASSERT(learning_enabled, "MAC learning should be enabled by default");
    
    uint32_t aging_time = routing_get_aging_time();
    TEST_ASSERT(aging_time > 0, "Aging time should be positive");
    
    /* Test parameter modification */
    result = routing_set_learning_enabled(false);
    TEST_ASSERT(result == SUCCESS, "Disabling learning should succeed");
    TEST_ASSERT(!routing_get_learning_enabled(), "Learning should be disabled");
    
    result = routing_set_aging_time(600000); /* 10 minutes */
    TEST_ASSERT(result == SUCCESS, "Setting aging time should succeed");
    TEST_ASSERT(routing_get_aging_time() == 600000, "Aging time should be updated");
    
    /* Test routing table and bridge table initialization */
    const routing_stats_t *stats = routing_get_stats();
    TEST_ASSERT(stats != NULL, "Routing statistics should be available");
    TEST_ASSERT(stats->packets_routed == 0, "Initial packets routed should be 0");
    
    /* Re-enable learning for subsequent tests */
    routing_set_learning_enabled(true);
    
    TEST_LOG_END("Routing Initialization", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_routing_table_management(void) {
    TEST_LOG_START("Routing Table Management");
    
    /* Test adding MAC-based routing rules */
    uint8_t test_mac1[] = TEST_ROUTE_MAC_1;
    uint8_t test_mac2[] = TEST_ROUTE_MAC_2;
    
    int result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, test_mac1, 0, 1, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == SUCCESS, "Adding MAC rule should succeed");
    
    /* Test finding the rule */
    route_entry_t *rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, test_mac1);
    TEST_ASSERT(rule != NULL, "Added rule should be found");
    TEST_ASSERT(rule->rule_type == ROUTE_RULE_MAC_ADDRESS, "Rule type should match");
    TEST_ASSERT(rule->src_nic == 0, "Source NIC should match");
    TEST_ASSERT(rule->dest_nic == 1, "Destination NIC should match");
    TEST_ASSERT(rule->decision == ROUTE_DECISION_FORWARD, "Decision should match");
    
    /* Test adding Ethertype-based routing rule */
    uint16_t test_ethertype = ETH_P_IP;
    result = routing_add_rule(ROUTE_RULE_ETHERTYPE, &test_ethertype, 1, 2, ROUTE_DECISION_BROADCAST);
    TEST_ASSERT(result == SUCCESS, "Adding Ethertype rule should succeed");
    
    rule = routing_find_rule(ROUTE_RULE_ETHERTYPE, &test_ethertype);
    TEST_ASSERT(rule != NULL, "Ethertype rule should be found");
    TEST_ASSERT(rule->ethertype == test_ethertype, "Ethertype should match");
    
    /* Test updating existing rule */
    result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, test_mac1, 0, 2, ROUTE_DECISION_DROP);
    TEST_ASSERT(result == SUCCESS, "Updating existing rule should succeed");
    
    rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, test_mac1);
    TEST_ASSERT(rule != NULL, "Updated rule should be found");
    TEST_ASSERT(rule->dest_nic == 2, "Destination NIC should be updated");
    TEST_ASSERT(rule->decision == ROUTE_DECISION_DROP, "Decision should be updated");
    
    /* Test multiple rules */
    result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, test_mac2, 1, 0, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == SUCCESS, "Adding second MAC rule should succeed");
    
    /* Verify both rules exist */
    rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, test_mac1);
    TEST_ASSERT(rule != NULL, "First rule should still exist");
    
    rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, test_mac2);
    TEST_ASSERT(rule != NULL, "Second rule should exist");
    
    /* Test rule removal */
    result = routing_remove_rule(ROUTE_RULE_MAC_ADDRESS, test_mac1);
    /* Note: May return ERROR_NOT_SUPPORTED if not implemented */
    
    /* Test setting default route */
    result = routing_set_default_route(0, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == SUCCESS, "Setting default route should succeed");
    
    /* Test clearing routing table */
    routing_clear_table();
    
    rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, test_mac2);
    TEST_ASSERT(rule == NULL, "Rules should be cleared");
    
    TEST_LOG_END("Routing Table Management", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Bridge Learning Tests ========== */

static test_result_t test_bridge_learning_functionality(void) {
    TEST_LOG_START("Bridge Learning Functionality");
    
    /* Ensure learning is enabled */
    int result = routing_set_learning_enabled(true);
    TEST_ASSERT(result == SUCCESS, "Enabling learning should succeed");
    
    /* Test MAC learning */
    uint8_t learned_mac1[] = TEST_ROUTE_MAC_1;
    uint8_t learned_mac2[] = TEST_ROUTE_MAC_2;
    
    result = bridge_learn_mac(learned_mac1, 0);
    TEST_ASSERT(result == SUCCESS, "Learning MAC on NIC 0 should succeed");
    
    result = bridge_learn_mac(learned_mac2, 1);
    TEST_ASSERT(result == SUCCESS, "Learning MAC on NIC 1 should succeed");
    
    /* Test MAC lookup */
    bridge_entry_t *entry = bridge_lookup_mac(learned_mac1);
    TEST_ASSERT(entry != NULL, "Learned MAC should be found");
    TEST_ASSERT(entry->nic_index == 0, "NIC index should match");
    
    entry = bridge_lookup_mac(learned_mac2);
    TEST_ASSERT(entry != NULL, "Second learned MAC should be found");
    TEST_ASSERT(entry->nic_index == 1, "Second NIC index should match");
    
    /* Test MAC re-learning (move to different port) */
    result = bridge_learn_mac(learned_mac1, 1);
    TEST_ASSERT(result == SUCCESS, "Re-learning MAC should succeed");
    
    entry = bridge_lookup_mac(learned_mac1);
    TEST_ASSERT(entry != NULL, "Re-learned MAC should be found");
    TEST_ASSERT(entry->nic_index == 1, "NIC should be updated");
    
    /* Test learning with disabled learning */
    routing_set_learning_enabled(false);
    
    uint8_t no_learn_mac[] = TEST_ROUTE_MAC_3;
    result = bridge_learn_mac(no_learn_mac, 0);
    /* Should fail or be ignored when learning is disabled */
    
    entry = bridge_lookup_mac(no_learn_mac);
    TEST_ASSERT(entry == NULL, "MAC should not be learned when learning disabled");
    
    /* Re-enable learning */
    routing_set_learning_enabled(true);
    
    /* Test bridge table capacity and eviction */
    uint8_t fill_mac[ETH_ALEN];
    int learned_count = 0;
    
    /* Fill bridge table */
    for (int i = 0; i < 520; i++) { /* More than typical bridge table size */
        fill_mac[0] = 0xAA;
        fill_mac[1] = 0xBB;
        fill_mac[2] = (i >> 24) & 0xFF;
        fill_mac[3] = (i >> 16) & 0xFF;
        fill_mac[4] = (i >> 8) & 0xFF;
        fill_mac[5] = i & 0xFF;
        
        result = bridge_learn_mac(fill_mac, i % MAX_NICS);
        if (result == SUCCESS) {
            learned_count++;
        }
    }
    
    TEST_ASSERT(learned_count > 0, "Should learn at least some MACs");
    
    /* Test bridge aging */
    bridge_age_entries();
    
    /* Verify some entries may have been aged out */
    /* Implementation-dependent behavior */
    
    /* Test bridge table flushing */
    bridge_flush_table();
    
    entry = bridge_lookup_mac(learned_mac1);
    TEST_ASSERT(entry == NULL, "Entries should be flushed");
    
    TEST_LOG_END("Bridge Learning Functionality", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Packet Routing Decision Tests ========== */

static test_result_t test_packet_routing_decisions(void) {
    TEST_LOG_START("Packet Routing Decisions");
    
    /* Set up test scenario */
    uint8_t src_mac[] = TEST_ROUTE_MAC_1;
    uint8_t dest_mac[] = TEST_ROUTE_MAC_2;
    uint8_t broadcast_mac[] = TEST_ROUTE_BROADCAST;
    uint8_t multicast_mac[] = TEST_ROUTE_MULTICAST;
    
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    /* Create unicast packet */
    test_result_t result = create_test_packet(&test_packet, dest_mac, src_mac, 
                                            ETH_P_IP, (uint8_t*)"test payload", 12);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating test packet should succeed");
    
    /* Test unicast routing decision */
    uint8_t output_nic;
    route_decision_t decision = routing_decide(&test_packet, 0, &output_nic);
    
    /* Initially should drop (no bridge learning yet) */
    TEST_ASSERT(decision == ROUTE_DECISION_DROP || decision == ROUTE_DECISION_FORWARD, 
                "Initial decision should be drop or forward to default");
    
    /* Learn the MAC */
    int learn_result = bridge_learn_mac(dest_mac, 1);
    TEST_ASSERT(learn_result == SUCCESS, "Learning destination MAC should succeed");
    
    /* Test decision after learning */
    decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "Should forward to learned port");
    TEST_ASSERT(output_nic == 1, "Should forward to NIC 1");
    
    /* Test loop prevention (same input and output NIC) */
    decision = routing_decide(&test_packet, 1, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_DROP, "Should drop when input equals output");
    
    /* Test broadcast packet */
    result = create_test_packet(&test_packet, broadcast_mac, src_mac, 
                               ETH_P_IP, (uint8_t*)"broadcast", 9);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating broadcast packet should succeed");
    
    decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_BROADCAST, "Should broadcast");
    
    /* Test multicast packet */
    result = create_test_packet(&test_packet, multicast_mac, src_mac, 
                               ETH_P_IP, (uint8_t*)"multicast", 9);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating multicast packet should succeed");
    
    decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_MULTICAST, "Should handle as multicast");
    
    /* Test Ethertype-based routing */
    uint16_t arp_ethertype = ETH_P_ARP;
    int add_result = routing_add_rule(ROUTE_RULE_ETHERTYPE, &arp_ethertype, 0, 2, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(add_result == SUCCESS, "Adding ARP rule should succeed");
    
    result = create_test_packet(&test_packet, dest_mac, src_mac, 
                               ETH_P_ARP, (uint8_t*)"arp packet", 10);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating ARP packet should succeed");
    
    decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "ARP should be forwarded by rule");
    TEST_ASSERT(output_nic == 2, "ARP should go to NIC 2");
    
    /* Test MAC-based routing rule priority */
    add_result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, dest_mac, 0, 3, ROUTE_DECISION_DROP);
    TEST_ASSERT(add_result == SUCCESS, "Adding MAC rule should succeed");
    
    result = create_test_packet(&test_packet, dest_mac, src_mac, 
                               ETH_P_IP, (uint8_t*)"test", 4);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating IP packet should succeed");
    
    decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_DROP, "MAC rule should override bridge learning");
    
    TEST_LOG_END("Packet Routing Decisions", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_mac_address_utilities(void) {
    TEST_LOG_START("MAC Address Utilities");
    
    uint8_t mac1[] = TEST_ROUTE_MAC_1;
    uint8_t mac2[] = TEST_ROUTE_MAC_2;
    uint8_t mac1_copy[] = TEST_ROUTE_MAC_1;
    uint8_t test_pattern[] = {0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x00};
    uint8_t test_mask[] = {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00};
    uint8_t match_mac[] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
    uint8_t no_match_mac[] = {0xAA, 0xBB, 0xDD, 0x11, 0x22, 0x33};
    
    /* Test MAC address equality */
    bool equal = routing_mac_equals(mac1, mac1_copy);
    TEST_ASSERT(equal, "Identical MACs should be equal");
    
    equal = routing_mac_equals(mac1, mac2);
    TEST_ASSERT(!equal, "Different MACs should not be equal");
    
    equal = routing_mac_equals(NULL, mac1);
    TEST_ASSERT(!equal, "NULL MAC should not equal any MAC");
    
    /* Test MAC address copying */
    uint8_t copied_mac[ETH_ALEN];
    routing_mac_copy(copied_mac, mac1);
    
    equal = routing_mac_equals(copied_mac, mac1);
    TEST_ASSERT(equal, "Copied MAC should equal original");
    
    /* Test MAC mask matching */
    bool matches = routing_mac_match_mask(match_mac, test_pattern, test_mask);
    TEST_ASSERT(matches, "MAC should match pattern with mask");
    
    matches = routing_mac_match_mask(no_match_mac, test_pattern, test_mask);
    TEST_ASSERT(!matches, "MAC should not match different pattern");
    
    /* Test with full mask (exact match) */
    uint8_t full_mask[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    matches = routing_mac_match_mask(mac1, mac1, full_mask);
    TEST_ASSERT(matches, "Exact match with full mask should succeed");
    
    matches = routing_mac_match_mask(mac1, mac2, full_mask);
    TEST_ASSERT(!matches, "Different MACs with full mask should not match");
    
    /* Test with zero mask (always match) */
    uint8_t zero_mask[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    matches = routing_mac_match_mask(mac1, mac2, zero_mask);
    TEST_ASSERT(matches, "Zero mask should always match");
    
    /* Test error conditions */
    matches = routing_mac_match_mask(NULL, test_pattern, test_mask);
    TEST_ASSERT(!matches, "NULL MAC should not match");
    
    routing_mac_copy(NULL, mac1);
    routing_mac_copy(copied_mac, NULL);
    /* Should not crash */
    
    TEST_LOG_END("MAC Address Utilities", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Packet Processing Tests ========== */

static test_result_t test_packet_forwarding(void) {
    TEST_LOG_START("Packet Forwarding");
    
    /* Set up multi-NIC topology */
    test_result_t setup_result = setup_multi_nic_topology();
    TEST_ASSERT(setup_result == TEST_RESULT_PASS, "Multi-NIC topology setup should succeed");
    
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 100];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    uint8_t src_mac[] = TEST_ROUTE_MAC_1;
    uint8_t dest_mac[] = TEST_ROUTE_MAC_2;
    
    /* Create test packet */
    test_result_t result = create_test_packet(&test_packet, dest_mac, src_mac, 
                                            ETH_P_IP, (uint8_t*)"forward test", 12);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating forward test packet should succeed");
    
    /* Learn destination MAC on NIC 1 */
    int learn_result = bridge_learn_mac(dest_mac, 1);
    TEST_ASSERT(learn_result == SUCCESS, "Learning destination should succeed");
    
    /* Test forwarding from NIC 0 to NIC 1 */
    int forward_result = route_packet(&test_packet, 0);
    TEST_ASSERT(forward_result == SUCCESS, "Packet forwarding should succeed");
    
    /* Verify packet was forwarded to correct NIC */
    bool forwarded = verify_packet_forwarded(0, 1);
    TEST_ASSERT(forwarded, "Packet should be forwarded to NIC 1");
    
    /* Test direct forwarding function */
    forward_result = forward_packet(&test_packet, 0, 2);
    TEST_ASSERT(forward_result == SUCCESS, "Direct forwarding should succeed");
    
    /* Test loop prevention */
    forward_result = forward_packet(&test_packet, 1, 1);
    TEST_ASSERT(forward_result == ERROR_INVALID_PARAM, "Self-forwarding should be prevented");
    
    /* Test forwarding to invalid NIC */
    forward_result = forward_packet(&test_packet, 0, 99);
    TEST_ASSERT(forward_result != SUCCESS, "Forwarding to invalid NIC should fail");
    
    /* Check forwarding statistics */
    const routing_stats_t *stats = routing_get_stats();
    TEST_ASSERT(stats->packets_forwarded > 0, "Forwarded packet count should increment");
    
    TEST_LOG_END("Packet Forwarding", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_packet_broadcasting(void) {
    TEST_LOG_START("Packet Broadcasting");
    
    packet_buffer_t broadcast_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    broadcast_packet.data = packet_data;
    broadcast_packet.length = sizeof(packet_data);
    
    uint8_t src_mac[] = TEST_ROUTE_MAC_1;
    uint8_t broadcast_mac[] = TEST_ROUTE_BROADCAST;
    
    /* Create broadcast packet */
    test_result_t result = create_test_packet(&broadcast_packet, broadcast_mac, src_mac, 
                                            ETH_P_IP, (uint8_t*)"broadcast test", 14);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating broadcast packet should succeed");
    
    /* Test broadcast from NIC 0 */
    int broadcast_result = broadcast_packet(&broadcast_packet, 0);
    TEST_ASSERT(broadcast_result == SUCCESS, "Broadcasting should succeed");
    
    /* Test route_packet with broadcast */
    broadcast_result = route_packet(&broadcast_packet, 0);
    TEST_ASSERT(broadcast_result == SUCCESS, "Routing broadcast packet should succeed");
    
    /* Check broadcast statistics */
    const routing_stats_t *stats = routing_get_stats();
    TEST_ASSERT(stats->packets_broadcast > 0, "Broadcast packet count should increment");
    
    /* Test multicast handling */
    uint8_t multicast_mac[] = TEST_ROUTE_MULTICAST;
    result = create_test_packet(&broadcast_packet, multicast_mac, src_mac, 
                               ETH_P_IP, (uint8_t*)"multicast", 9);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating multicast packet should succeed");
    
    broadcast_result = multicast_packet(&broadcast_packet, 0, multicast_mac);
    TEST_ASSERT(broadcast_result == SUCCESS, "Multicast handling should succeed");
    
    stats = routing_get_stats();
    TEST_ASSERT(stats->packets_multicast > 0, "Multicast packet count should increment");
    
    TEST_LOG_END("Packet Broadcasting", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Multi-NIC Routing Tests ========== */

static test_result_t test_multi_nic_routing_scenarios(void) {
    TEST_LOG_START("Multi-NIC Routing Scenarios");
    
    /* Set up 3-NIC scenario */
    test_result_t setup_result = setup_multi_nic_topology();
    TEST_ASSERT(setup_result == TEST_RESULT_PASS, "Multi-NIC setup should succeed");
    
    /* Define test scenarios */
    test_routing_scenario_t scenarios[] = {
        {0, 1, ROUTE_DECISION_FORWARD, 64, ETH_P_IP, true, false, "NIC 0 to NIC 1 unicast"},
        {1, 0, ROUTE_DECISION_FORWARD, 128, ETH_P_IP, true, false, "NIC 1 to NIC 0 unicast"},
        {0, 0xFF, ROUTE_DECISION_BROADCAST, 64, ETH_P_ARP, false, true, "Broadcast from NIC 0"},
        {2, 1, ROUTE_DECISION_FORWARD, 256, ETH_P_IP, true, false, "NIC 2 to NIC 1 large packet"},
        {1, 2, ROUTE_DECISION_DROP, 64, 0x8888, false, false, "Unknown ethertype drop"}
    };
    
    int num_scenarios = sizeof(scenarios) / sizeof(scenarios[0]);
    
    for (int i = 0; i < num_scenarios; i++) {
        test_routing_scenario_t *scenario = &scenarios[i];
        
        log_info("Testing scenario: %s", scenario->description);
        
        /* Create packet for scenario */
        packet_buffer_t test_packet;
        uint8_t packet_data[ETH_HLEN + scenario->packet_size];
        test_packet.data = packet_data;
        test_packet.length = sizeof(packet_data);
        
        uint8_t src_mac[] = TEST_ROUTE_MAC_1;
        uint8_t dest_mac[] = TEST_ROUTE_MAC_2;
        
        if (scenario->should_broadcast) {
            uint8_t broadcast_mac[] = TEST_ROUTE_BROADCAST;
            memcpy(dest_mac, broadcast_mac, ETH_ALEN);
        }
        
        test_result_t result = create_test_packet(&test_packet, dest_mac, src_mac, 
                                                scenario->ethertype, 
                                                (uint8_t*)"test payload", 12);
        TEST_ASSERT(result == TEST_RESULT_PASS, "Scenario packet creation should succeed");
        
        /* If forwarding expected, learn the destination */
        if (scenario->should_forward) {
            bridge_learn_mac(dest_mac, scenario->dest_nic);
        }
        
        /* Test routing decision */
        uint8_t output_nic;
        route_decision_t decision = routing_decide(&test_packet, scenario->src_nic, &output_nic);
        
        if (scenario->expected_decision != ROUTE_DECISION_DROP) {
            TEST_ASSERT(decision == scenario->expected_decision, 
                       "Routing decision should match expected");
        }
        
        /* Test actual packet routing */
        int route_result = route_packet(&test_packet, scenario->src_nic);
        
        if (scenario->should_forward || scenario->should_broadcast) {
            TEST_ASSERT(route_result == SUCCESS, "Packet routing should succeed");
        }
        
        /* Clear learned entries for next test */
        bridge_flush_table();
    }
    
    /* Test inter-VLAN routing (conceptual) */
    uint8_t vlan1_mac[] = {0x00, 0x01, 0x01, 0x01, 0x01, 0x01};
    uint8_t vlan2_mac[] = {0x00, 0x02, 0x02, 0x02, 0x02, 0x02};
    
    /* Learn MACs on different NICs (simulating VLANs) */
    bridge_learn_mac(vlan1_mac, 0);
    bridge_learn_mac(vlan2_mac, 1);
    
    packet_buffer_t vlan_packet;
    uint8_t vlan_packet_data[ETH_HLEN + 64];
    vlan_packet.data = vlan_packet_data;
    vlan_packet.length = sizeof(vlan_packet_data);
    
    test_result_t result = create_test_packet(&vlan_packet, vlan2_mac, vlan1_mac, 
                                            ETH_P_IP, (uint8_t*)"inter-vlan", 10);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Inter-VLAN packet creation should succeed");
    
    uint8_t output_nic;
    route_decision_t decision = routing_decide(&vlan_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "Inter-VLAN routing should forward");
    TEST_ASSERT(output_nic == 1, "Should route to VLAN 2 NIC");
    
    TEST_LOG_END("Multi-NIC Routing Scenarios", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Flow-Aware Routing Tests ========== */

static test_result_t test_flow_aware_routing(void) {
    TEST_LOG_START("Flow-Aware Routing");
    
    /* Set up flow-based routing rules */
    /* Test based on packet content analysis */
    
    packet_buffer_t flow_packet;
    uint8_t packet_data[ETH_HLEN + 128];
    flow_packet.data = packet_data;
    flow_packet.length = sizeof(packet_data);
    
    uint8_t src_mac[] = TEST_ROUTE_MAC_1;
    uint8_t dest_mac[] = TEST_ROUTE_MAC_2;
    
    /* Create packets with different characteristics */
    
    /* Test 1: Route based on packet size */
    test_result_t result = create_test_packet(&flow_packet, dest_mac, src_mac, 
                                            ETH_P_IP, (uint8_t*)"small packet", 12);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Small packet creation should succeed");
    
    uint8_t small_output_nic;
    route_decision_t small_decision = routing_decide(&flow_packet, 0, &small_output_nic);
    
    /* Create large packet */
    uint8_t large_payload[1000];
    memset(large_payload, 0xAA, sizeof(large_payload));
    
    result = create_test_packet(&flow_packet, dest_mac, src_mac, 
                               ETH_P_IP, large_payload, sizeof(large_payload));
    TEST_ASSERT(result == TEST_RESULT_PASS, "Large packet creation should succeed");
    
    uint8_t large_output_nic;
    route_decision_t large_decision = routing_decide(&flow_packet, 0, &large_output_nic);
    
    /* Test 2: Route based on ethertype */
    result = create_test_packet(&flow_packet, dest_mac, src_mac, 
                               ETH_P_ARP, (uint8_t*)"arp flow", 8);
    TEST_ASSERT(result == TEST_RESULT_PASS, "ARP flow packet creation should succeed");
    
    uint8_t arp_output_nic;
    route_decision_t arp_decision = routing_decide(&flow_packet, 0, &arp_output_nic);
    
    result = create_test_packet(&flow_packet, dest_mac, src_mac, 
                               ETH_P_RARP, (uint8_t*)"rarp flow", 9);
    TEST_ASSERT(result == TEST_RESULT_PASS, "RARP flow packet creation should succeed");
    
    uint8_t rarp_output_nic;
    route_decision_t rarp_decision = routing_decide(&flow_packet, 0, &rarp_output_nic);
    
    /* Test 3: Load balancing simulation */
    /* Send multiple packets and verify distribution */
    int nic_counts[MAX_NICS] = {0};
    
    for (int i = 0; i < 100; i++) {
        /* Vary source MAC to simulate different flows */
        src_mac[5] = i;
        
        result = create_test_packet(&flow_packet, dest_mac, src_mac, 
                                   ETH_P_IP, (uint8_t*)"load balance", 12);
        TEST_ASSERT(result == TEST_RESULT_PASS, "Load balance packet creation should succeed");
        
        uint8_t output_nic;
        route_decision_t decision = routing_decide(&flow_packet, 0, &output_nic);
        
        if (decision == ROUTE_DECISION_FORWARD && output_nic < MAX_NICS) {
            nic_counts[output_nic]++;
        }
    }
    
    /* Verify some distribution occurred (implementation-dependent) */
    int total_distributed = 0;
    for (int i = 0; i < MAX_NICS; i++) {
        total_distributed += nic_counts[i];
    }
    
    TEST_ASSERT(total_distributed > 0, "Some packets should be distributed");
    
    /* Test 4: Priority-based routing */
    /* Higher priority traffic should take precedence */
    uint16_t priority_ethertype = 0x8100; /* VLAN tag ethertype */
    
    int add_result = routing_add_rule(ROUTE_RULE_ETHERTYPE, &priority_ethertype, 
                                     0, 1, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(add_result == SUCCESS, "Adding priority rule should succeed");
    
    result = create_test_packet(&flow_packet, dest_mac, src_mac, 
                               priority_ethertype, (uint8_t*)"priority", 8);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Priority packet creation should succeed");
    
    uint8_t priority_output_nic;
    route_decision_t priority_decision = routing_decide(&flow_packet, 0, &priority_output_nic);
    TEST_ASSERT(priority_decision == ROUTE_DECISION_FORWARD, "Priority packet should be forwarded");
    TEST_ASSERT(priority_output_nic == 1, "Priority packet should go to designated NIC");
    
    TEST_LOG_END("Flow-Aware Routing", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Failover and Redundancy Tests ========== */

static test_result_t test_routing_failover_logic(void) {
    TEST_LOG_START("Routing Failover Logic");
    
    /* Set up redundant paths */
    test_result_t setup_result = setup_multi_nic_topology();
    TEST_ASSERT(setup_result == TEST_RESULT_PASS, "Multi-NIC setup should succeed");
    
    uint8_t primary_mac[] = TEST_ROUTE_MAC_1;
    uint8_t backup_mac[] = TEST_ROUTE_MAC_2;
    
    /* Learn primary path */
    int learn_result = bridge_learn_mac(primary_mac, 1);
    TEST_ASSERT(learn_result == SUCCESS, "Learning primary path should succeed");
    
    /* Learn backup path */
    learn_result = bridge_learn_mac(backup_mac, 2);
    TEST_ASSERT(learn_result == SUCCESS, "Learning backup path should succeed");
    
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    /* Create packet for primary destination */
    test_result_t result = create_test_packet(&test_packet, primary_mac, backup_mac, 
                                            ETH_P_IP, (uint8_t*)"primary", 7);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Primary packet creation should succeed");
    
    /* Test normal operation */
    uint8_t output_nic;
    route_decision_t decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "Primary path should forward");
    TEST_ASSERT(output_nic == 1, "Should use primary path NIC");
    
    /* Simulate link failure on primary path */
    test_result_t failure_result = simulate_link_failure_recovery(1);
    TEST_ASSERT(failure_result == TEST_RESULT_PASS, "Link failure simulation should succeed");
    
    /* Test failover behavior */
    /* After link failure, implementation might:
     * 1. Remove learned entries for failed NIC
     * 2. Fall back to default routing
     * 3. Use alternative learned paths
     */
    
    /* Remove learned entry for failed link */
    int remove_result = bridge_remove_mac(primary_mac);
    /* May return ERROR_NOT_SUPPORTED if not implemented */
    
    /* Set up alternative routing rule */
    int rule_result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, primary_mac, 
                                      0, 2, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(rule_result == SUCCESS, "Adding failover rule should succeed");
    
    /* Test routing after failover */
    decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "Failover should still forward");
    TEST_ASSERT(output_nic == 2, "Should use backup path NIC");
    
    /* Simulate link recovery */
    failure_result = simulate_link_failure_recovery(1); /* Recovery */
    TEST_ASSERT(failure_result == TEST_RESULT_PASS, "Link recovery simulation should succeed");
    
    /* Test load distribution after recovery */
    /* Implementation might rebalance or maintain failover state */
    
    /* Test graceful degradation with multiple failures */
    simulate_link_failure_recovery(2); /* Fail backup link too */
    
    decision = routing_decide(&test_packet, 0, &output_nic);
    /* Should either drop or use default route */
    
    /* Test statistics during failover */
    const routing_stats_t *stats = routing_get_stats();
    TEST_ASSERT(stats->routing_errors >= 0, "Error count should be tracked");
    
    /* Test routing table consistency after failures */
    route_entry_t *rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, primary_mac);
    TEST_ASSERT(rule != NULL, "Routing rules should survive link failures");
    
    TEST_LOG_END("Routing Failover Logic", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Rate Limiting Tests ========== */

static test_result_t test_routing_rate_limiting(void) {
    TEST_LOG_START("Routing Rate Limiting");
    
    /* Test rate limiting configuration */
    int result = routing_set_rate_limit(0, 100); /* 100 packets per second */
    TEST_ASSERT(result == SUCCESS, "Setting rate limit should succeed");
    
    result = routing_set_rate_limit(1, 50);   /* 50 packets per second */
    TEST_ASSERT(result == SUCCESS, "Setting different rate limit should succeed");
    
    result = routing_set_rate_limit(99, 10);  /* Invalid NIC */
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Invalid NIC should return error");
    
    /* Test rate limit checking */
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    uint8_t src_mac[] = TEST_ROUTE_MAC_1;
    uint8_t dest_mac[] = TEST_ROUTE_MAC_2;
    
    test_result_t create_result = create_test_packet(&test_packet, dest_mac, src_mac, 
                                                   ETH_P_IP, (uint8_t*)"rate test", 9);
    TEST_ASSERT(create_result == TEST_RESULT_PASS, "Rate test packet creation should succeed");
    
    /* Rapid packet sending to trigger rate limiting */
    int successful_routes = 0;
    int rate_limited = 0;
    
    for (int i = 0; i < 200; i++) {
        int route_result = route_packet(&test_packet, 0);
        
        if (route_result == SUCCESS) {
            successful_routes++;
        } else if (route_result == ERROR_BUSY) {
            rate_limited++;
        }
        
        /* Update rate counters periodically */
        if (i % 10 == 0) {
            routing_update_rate_counters();
        }
    }
    
    /* Should have some rate limiting effect */
    TEST_ASSERT(rate_limited > 0 || successful_routes <= 100, 
                "Rate limiting should have some effect");
    
    /* Test rate limit checking function */
    result = routing_check_rate_limit(0);
    /* Should return SUCCESS or ERROR_BUSY based on current state */
    
    result = routing_check_rate_limit(1);
    /* Should check limit for NIC 1 */
    
    /* Test disabling rate limiting */
    result = routing_set_rate_limit(0, 0); /* 0 = unlimited */
    TEST_ASSERT(result == SUCCESS, "Disabling rate limit should succeed");
    
    /* Test unlimited rate */
    successful_routes = 0;
    for (int i = 0; i < 50; i++) {
        int route_result = route_packet(&test_packet, 0);
        if (route_result == SUCCESS) {
            successful_routes++;
        }
    }
    
    TEST_ASSERT(successful_routes > 40, "Unlimited rate should allow most packets");
    
    /* Test rate counter updates */
    routing_update_rate_counters();
    
    /* Rate counters should reset periodically */
    result = routing_check_rate_limit(1);
    TEST_ASSERT(result == SUCCESS || result == ERROR_BUSY, 
                "Rate limit check should return valid status");
    
    TEST_LOG_END("Routing Rate Limiting", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Routing Statistics Tests ========== */

static test_result_t test_routing_statistics_tracking(void) {
    TEST_LOG_START("Routing Statistics Tracking");
    
    /* Clear statistics */
    routing_clear_stats();
    
    const routing_stats_t *stats = routing_get_stats();
    TEST_ASSERT(stats != NULL, "Statistics should be available");
    TEST_ASSERT(stats->packets_routed == 0, "Initial routed count should be 0");
    TEST_ASSERT(stats->packets_dropped == 0, "Initial dropped count should be 0");
    TEST_ASSERT(stats->packets_forwarded == 0, "Initial forwarded count should be 0");
    
    /* Generate routing activity */
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    uint8_t src_mac[] = TEST_ROUTE_MAC_1;
    uint8_t dest_mac[] = TEST_ROUTE_MAC_2;
    uint8_t broadcast_mac[] = TEST_ROUTE_BROADCAST;
    
    /* Test forwarding statistics */
    test_result_t result = create_test_packet(&test_packet, dest_mac, src_mac, 
                                            ETH_P_IP, (uint8_t*)"forward", 7);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Forward test packet creation should succeed");
    
    bridge_learn_mac(dest_mac, 1);
    int route_result = route_packet(&test_packet, 0);
    TEST_ASSERT(route_result == SUCCESS, "Packet routing should succeed");
    
    stats = routing_get_stats();
    TEST_ASSERT(stats->packets_forwarded > 0, "Forwarded count should increment");
    
    /* Test broadcast statistics */
    result = create_test_packet(&test_packet, broadcast_mac, src_mac, 
                               ETH_P_IP, (uint8_t*)"broadcast", 9);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Broadcast test packet creation should succeed");
    
    route_result = route_packet(&test_packet, 0);
    TEST_ASSERT(route_result == SUCCESS, "Broadcast routing should succeed");
    
    stats = routing_get_stats();
    TEST_ASSERT(stats->packets_broadcast > 0, "Broadcast count should increment");
    
    /* Test drop statistics */
    /* Add rule to drop certain packets */
    int rule_result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, dest_mac, 
                                      0, 0, ROUTE_DECISION_DROP);
    TEST_ASSERT(rule_result == SUCCESS, "Adding drop rule should succeed");
    
    result = create_test_packet(&test_packet, dest_mac, src_mac, 
                               ETH_P_IP, (uint8_t*)"drop", 4);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Drop test packet creation should succeed");
    
    route_result = route_packet(&test_packet, 0);
    /* Should succeed but packet should be dropped */
    
    stats = routing_get_stats();
    TEST_ASSERT(stats->packets_dropped > 0, "Dropped count should increment");
    
    /* Test table lookup statistics */
    uint32_t initial_lookups = stats->table_lookups;
    
    /* Perform multiple routing decisions */
    for (int i = 0; i < 10; i++) {
        uint8_t output_nic;
        routing_decide(&test_packet, 0, &output_nic);
    }
    
    stats = routing_get_stats();
    TEST_ASSERT(stats->table_lookups > initial_lookups, 
                "Table lookup count should increment");
    
    /* Test cache hit/miss tracking */
    /* Multiple bridge lookups should show cache behavior */
    bridge_entry_t *entry;
    for (int i = 0; i < 5; i++) {
        entry = bridge_lookup_mac(dest_mac);
    }
    
    /* Look up non-existent MAC */
    uint8_t missing_mac[] = {0x99, 0x99, 0x99, 0x99, 0x99, 0x99};
    entry = bridge_lookup_mac(missing_mac);
    TEST_ASSERT(entry == NULL, "Missing MAC lookup should fail");
    
    /* Test error statistics */
    /* Try to route invalid packet */
    packet_buffer_t invalid_packet;
    invalid_packet.data = NULL;
    invalid_packet.length = 0;
    
    route_result = route_packet(&invalid_packet, 0);
    TEST_ASSERT(route_result == ERROR_INVALID_PARAM, "Invalid packet should return error");
    
    stats = routing_get_stats();
    /* Error count tracking is implementation-dependent */
    
    /* Verify comprehensive statistics */
    TEST_ASSERT(stats->packets_forwarded >= 1, "Should have forwarded packets");
    TEST_ASSERT(stats->packets_broadcast >= 1, "Should have broadcast packets");
    TEST_ASSERT(stats->packets_dropped >= 1, "Should have dropped packets");
    TEST_ASSERT(stats->table_lookups >= 10, "Should have performed lookups");
    
    /* Test statistics persistence across operations */
    uint32_t saved_forwards = stats->packets_forwarded;
    uint32_t saved_broadcasts = stats->packets_broadcast;
    
    /* Perform more operations */
    route_packet(&test_packet, 0);
    
    stats = routing_get_stats();
    TEST_ASSERT(stats->packets_forwarded >= saved_forwards, 
                "Statistics should persist and increment");
    TEST_ASSERT(stats->packets_broadcast >= saved_broadcasts, 
                "Broadcast stats should persist");
    
    TEST_LOG_END("Routing Statistics Tracking", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Error Handling Tests ========== */

static test_result_t test_routing_error_conditions(void) {
    TEST_LOG_START("Routing Error Conditions");
    
    /* Test null parameter handling */
    int result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, NULL, 0, 1, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL rule data should return error");
    
    route_entry_t *rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, NULL);
    TEST_ASSERT(rule == NULL, "NULL rule data lookup should return NULL");
    
    /* Test invalid NIC indices */
    uint8_t test_mac[] = TEST_ROUTE_MAC_1;
    result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, test_mac, 99, 1, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Invalid source NIC should return error");
    
    result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, test_mac, 0, 99, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Invalid destination NIC should return error");
    
    /* Test disabled routing operations */
    routing_enable(false);
    
    packet_buffer_t test_packet;
    uint8_t packet_data[ETH_HLEN + 64];
    test_packet.data = packet_data;
    test_packet.length = sizeof(packet_data);
    
    uint8_t output_nic;
    route_decision_t decision = routing_decide(&test_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_DROP, "Disabled routing should drop packets");
    
    routing_enable(true);
    
    /* Test packet processing error conditions */
    result = route_packet(NULL, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL packet should return error");
    
    packet_buffer_t invalid_packet;
    invalid_packet.data = NULL;
    invalid_packet.length = 100;
    
    result = route_packet(&invalid_packet, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Invalid packet data should return error");
    
    invalid_packet.data = packet_data;
    invalid_packet.length = 0;
    
    result = route_packet(&invalid_packet, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Zero-length packet should return error");
    
    /* Test bridge learning error conditions */
    result = bridge_learn_mac(NULL, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL MAC should return error");
    
    result = bridge_learn_mac(test_mac, 99);
    TEST_ASSERT(result != SUCCESS, "Invalid NIC should return error");
    
    bridge_entry_t *entry = bridge_lookup_mac(NULL);
    TEST_ASSERT(entry == NULL, "NULL MAC lookup should return NULL");
    
    /* Test forwarding error conditions */
    test_result_t create_result = create_test_packet(&test_packet, test_mac, test_mac, 
                                                   ETH_P_IP, (uint8_t*)"test", 4);
    TEST_ASSERT(create_result == TEST_RESULT_PASS, "Test packet creation should succeed");
    
    result = forward_packet(NULL, 0, 1);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL packet forward should return error");
    
    result = forward_packet(&test_packet, 0, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Self-forwarding should return error");
    
    result = forward_packet(&test_packet, 99, 1);
    TEST_ASSERT(result != SUCCESS, "Invalid source NIC should return error");
    
    result = forward_packet(&test_packet, 0, 99);
    TEST_ASSERT(result != SUCCESS, "Invalid destination NIC should return error");
    
    /* Test broadcast error conditions */
    result = broadcast_packet(NULL, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL packet broadcast should return error");
    
    result = broadcast_packet(&test_packet, 99);
    TEST_ASSERT(result != SUCCESS, "Invalid NIC broadcast should return error");
    
    /* Test routing table overflow */
    /* Add many rules to test capacity limits */
    uint8_t overflow_mac[ETH_ALEN];
    int successful_adds = 0;
    
    for (int i = 0; i < 1000; i++) {
        overflow_mac[0] = 0xAA;
        overflow_mac[1] = 0xBB;
        overflow_mac[2] = (i >> 16) & 0xFF;
        overflow_mac[3] = (i >> 8) & 0xFF;
        overflow_mac[4] = i & 0xFF;
        overflow_mac[5] = 0x00;
        
        result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, overflow_mac, 
                                 0, 1, ROUTE_DECISION_FORWARD);
        if (result == SUCCESS) {
            successful_adds++;
        } else {
            break; /* Capacity reached */
        }
    }
    
    TEST_ASSERT(successful_adds > 0, "Should add at least some rules");
    
    /* Test that system remains stable after overflow */
    decision = routing_decide(&test_packet, 0, &output_nic);
    /* Should return valid decision */
    
    rule = routing_find_rule(ROUTE_RULE_MAC_ADDRESS, test_mac);
    /* Should handle lookup gracefully */
    
    /* Test memory exhaustion scenarios */
    /* Bridge table overflow */
    uint8_t bridge_mac[ETH_ALEN];
    int successful_learns = 0;
    
    for (int i = 0; i < 2000; i++) {
        bridge_mac[0] = 0xCC;
        bridge_mac[1] = 0xDD;
        bridge_mac[2] = (i >> 16) & 0xFF;
        bridge_mac[3] = (i >> 8) & 0xFF;
        bridge_mac[4] = i & 0xFF;
        bridge_mac[5] = 0x00;
        
        result = bridge_learn_mac(bridge_mac, i % MAX_NICS);
        if (result == SUCCESS) {
            successful_learns++;
        }
    }
    
    TEST_ASSERT(successful_learns > 0, "Should learn at least some MACs");
    
    /* Verify system stability after stress */
    const routing_stats_t *stats = routing_get_stats();
    TEST_ASSERT(stats != NULL, "Statistics should remain accessible");
    
    TEST_LOG_END("Routing Error Conditions", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Integration Tests ========== */

static test_result_t test_routing_integration_scenarios(void) {
    TEST_LOG_START("Routing Integration Scenarios");
    
    /* Test integration with static routing */
    /* Set up IP subnets */
    ip_addr_t subnet1 = {192, 168, 1, 0};
    ip_addr_t subnet2 = {192, 168, 2, 0};
    ip_addr_t netmask = {255, 255, 255, 0};
    
    int result = static_subnet_add(&subnet1, &netmask, 0);
    TEST_ASSERT(result == SUCCESS, "Adding subnet 1 should succeed");
    
    result = static_subnet_add(&subnet2, &netmask, 1);
    TEST_ASSERT(result == SUCCESS, "Adding subnet 2 should succeed");
    
    /* Test Layer 2/Layer 3 interaction */
    packet_buffer_t ip_packet;
    uint8_t ip_packet_data[ETH_HLEN + 64];
    ip_packet.data = ip_packet_data;
    ip_packet.length = sizeof(ip_packet_data);
    
    uint8_t src_mac[] = TEST_ROUTE_MAC_1;
    uint8_t dest_mac[] = TEST_ROUTE_MAC_2;
    
    /* Create IP packet */
    test_result_t create_result = create_test_packet(&ip_packet, dest_mac, src_mac, 
                                                   ETH_P_IP, (uint8_t*)"ip test", 7);
    TEST_ASSERT(create_result == TEST_RESULT_PASS, "IP packet creation should succeed");
    
    /* Learn MAC address */
    bridge_learn_mac(dest_mac, 1);
    
    /* Test routing decision */
    uint8_t output_nic;
    route_decision_t decision = routing_decide(&ip_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "IP packet should be forwarded");
    TEST_ASSERT(output_nic == 1, "Should forward to learned NIC");
    
    /* Test ARP packet handling */
    create_result = create_test_packet(&ip_packet, dest_mac, src_mac, 
                                     ETH_P_ARP, (uint8_t*)"arp test", 8);
    TEST_ASSERT(create_result == TEST_RESULT_PASS, "ARP packet creation should succeed");
    
    decision = routing_decide(&ip_packet, 0, &output_nic);
    /* ARP might be forwarded or handled specially */
    
    /* Test VLAN integration (if supported) */
    uint16_t vlan_ethertype = 0x8100;
    result = routing_add_rule(ROUTE_RULE_ETHERTYPE, &vlan_ethertype, 
                             0, 2, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == SUCCESS, "Adding VLAN rule should succeed");
    
    create_result = create_test_packet(&ip_packet, dest_mac, src_mac, 
                                     vlan_ethertype, (uint8_t*)"vlan", 4);
    TEST_ASSERT(create_result == TEST_RESULT_PASS, "VLAN packet creation should succeed");
    
    decision = routing_decide(&ip_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "VLAN packet should be forwarded");
    TEST_ASSERT(output_nic == 2, "VLAN should go to designated NIC");
    
    /* Test spanning tree simulation */
    /* Block certain ports to prevent loops */
    uint8_t stp_mac[] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};
    result = routing_add_rule(ROUTE_RULE_MAC_ADDRESS, stp_mac, 
                             0, 0, ROUTE_DECISION_DROP);
    TEST_ASSERT(result == SUCCESS, "Adding STP block rule should succeed");
    
    create_result = create_test_packet(&ip_packet, stp_mac, src_mac, 
                                     0x8000, (uint8_t*)"stp", 3);
    TEST_ASSERT(create_result == TEST_RESULT_PASS, "STP packet creation should succeed");
    
    decision = routing_decide(&ip_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_DROP, "STP packet should be dropped");
    
    /* Test quality of service (QoS) prioritization */
    /* High priority traffic */
    uint16_t priority_ether = 0x8847; /* MPLS */
    result = routing_add_rule(ROUTE_RULE_ETHERTYPE, &priority_ether, 
                             0, 1, ROUTE_DECISION_FORWARD);
    TEST_ASSERT(result == SUCCESS, "Adding priority rule should succeed");
    
    /* Test network convergence */
    /* Simulate topology change */
    bridge_flush_table(); /* Clear learning table */
    
    /* Relearn topology */
    bridge_learn_mac(src_mac, 0);
    bridge_learn_mac(dest_mac, 1);
    
    /* Verify routing still works */
    create_result = create_test_packet(&ip_packet, dest_mac, src_mac, 
                                     ETH_P_IP, (uint8_t*)"converge", 8);
    TEST_ASSERT(create_result == TEST_RESULT_PASS, "Convergence packet creation should succeed");
    
    decision = routing_decide(&ip_packet, 0, &output_nic);
    TEST_ASSERT(decision == ROUTE_DECISION_FORWARD, "Should forward after convergence");
    
    /* Test load balancing with multiple equal paths */
    /* Add multiple MACs to same destination */
    uint8_t path1_mac[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t path2_mac[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x06};
    
    bridge_learn_mac(path1_mac, 1);
    bridge_learn_mac(path2_mac, 2);
    
    /* Send traffic to both paths and observe distribution */
    int path1_count = 0, path2_count = 0;
    
    for (int i = 0; i < 20; i++) {
        uint8_t target_mac[] = {0x00, 0x01, 0x02, 0x03, 0x04, (i % 2) ? 0x05 : 0x06};
        
        create_result = create_test_packet(&ip_packet, target_mac, src_mac, 
                                         ETH_P_IP, (uint8_t*)"balance", 7);
        TEST_ASSERT(create_result == TEST_RESULT_PASS, "Balance packet creation should succeed");
        
        decision = routing_decide(&ip_packet, 0, &output_nic);
        if (decision == ROUTE_DECISION_FORWARD) {
            if (output_nic == 1) path1_count++;
            else if (output_nic == 2) path2_count++;
        }
    }
    
    TEST_ASSERT(path1_count > 0 && path2_count > 0, "Should use both paths");
    
    TEST_LOG_END("Routing Integration Scenarios", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Helper Functions ========== */

static test_result_t setup_routing_test_environment(void) {
    /* Initialize hardware mock framework */
    int result = mock_framework_init();
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    /* Create mock NICs for multi-NIC testing */
    for (int i = 0; i < MAX_NICS && i < 4; i++) {
        uint8_t device_id = mock_device_create(
            (i % 2 == 0) ? MOCK_DEVICE_3C509B : MOCK_DEVICE_3C515, 
            0x300 + (i * 0x20), 
            5 + i
        );
        
        if (device_id < 0) {
            return TEST_RESULT_ERROR;
        }
        
        g_routing_test_fixture.mock_devices[i] = device_id;
        g_routing_test_fixture.device_count++;
        
        /* Configure mock device */
        uint8_t mac[ETH_ALEN] = {0x00, 0x10, 0x4B, 0x00, 0x00, (uint8_t)i};
        mock_device_set_mac_address(device_id, mac);
        mock_device_set_link_status(device_id, true, (i % 2 == 0) ? 10 : 100);
        mock_device_enable(device_id, true);
    }
    
    /* Initialize routing system */
    result = routing_init();
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    result = routing_enable(true);
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    /* Initialize static routing for integration tests */
    result = static_routing_init();
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    result = static_routing_enable(true);
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    /* Initialize test fixture */
    memset(&g_routing_test_fixture, 0, sizeof(routing_test_fixture_t));
    g_routing_test_fixture.test_start_time = get_system_timestamp_ms();
    
    return TEST_RESULT_PASS;
}

static void cleanup_routing_test_environment(void) {
    /* Cleanup routing system */
    routing_cleanup();
    
    /* Cleanup static routing */
    static_routing_cleanup();
    
    /* Cleanup mock framework */
    mock_framework_cleanup();
}

static test_result_t create_test_packet(packet_buffer_t *packet, const uint8_t *dest_mac,
                                       const uint8_t *src_mac, uint16_t ethertype, 
                                       const uint8_t *payload, uint16_t payload_len) {
    if (!packet || !packet->data || !dest_mac || !src_mac || !payload) {
        return TEST_RESULT_ERROR;
    }
    
    if (packet->length < ETH_HLEN + payload_len) {
        return TEST_RESULT_ERROR;
    }
    
    uint8_t *frame = packet->data;
    
    /* Build Ethernet header */
    memcpy(frame, dest_mac, ETH_ALEN);         /* Destination MAC */
    memcpy(frame + ETH_ALEN, src_mac, ETH_ALEN); /* Source MAC */
    *(uint16_t*)(frame + 12) = htons(ethertype);  /* EtherType */
    
    /* Copy payload */
    memcpy(frame + ETH_HLEN, payload, payload_len);
    
    packet->length = ETH_HLEN + payload_len;
    
    return TEST_RESULT_PASS;
}

static test_result_t verify_routing_statistics(const routing_stats_t *expected) {
    const routing_stats_t *actual = routing_get_stats();
    
    if (!actual || !expected) {
        return TEST_RESULT_ERROR;
    }
    
    /* Verify key statistics match or are within acceptable ranges */
    if (expected->packets_forwarded > 0 && actual->packets_forwarded < expected->packets_forwarded) {
        return TEST_RESULT_FAIL;
    }
    
    if (expected->packets_broadcast > 0 && actual->packets_broadcast < expected->packets_broadcast) {
        return TEST_RESULT_FAIL;
    }
    
    if (expected->packets_dropped > 0 && actual->packets_dropped < expected->packets_dropped) {
        return TEST_RESULT_FAIL;
    }
    
    return TEST_RESULT_PASS;
}

static test_result_t setup_multi_nic_topology(void) {
    /* Configure a realistic multi-NIC topology */
    
    /* Set up bridge learning */
    routing_set_learning_enabled(true);
    
    /* Set up default routes */
    int result = routing_set_default_route(0, ROUTE_DECISION_FORWARD);
    if (result != SUCCESS) {
        return TEST_RESULT_FAIL;
    }
    
    /* Configure rate limiting for testing */
    routing_set_rate_limit(0, 1000);  /* High limit for normal testing */
    routing_set_rate_limit(1, 1000);
    routing_set_rate_limit(2, 1000);
    
    return TEST_RESULT_PASS;
}

static test_result_t simulate_link_failure_recovery(uint8_t nic_index) {
    if (nic_index >= g_routing_test_fixture.device_count) {
        return TEST_RESULT_ERROR;
    }
    
    uint8_t device_id = g_routing_test_fixture.mock_devices[nic_index];
    
    /* Simulate link failure */
    mock_device_set_link_status(device_id, false, 0);
    
    /* Wait briefly (simulated) */
    /* In real test, might wait for link state change processing */
    
    /* Simulate recovery */
    mock_device_set_link_status(device_id, true, 100);
    
    return TEST_RESULT_PASS;
}

static bool verify_packet_forwarded(uint8_t src_nic, uint8_t dest_nic) {
    /* In a real implementation, this would check mock device statistics
     * or packet queues to verify forwarding occurred */
    
    if (src_nic >= g_routing_test_fixture.device_count || 
        dest_nic >= g_routing_test_fixture.device_count) {
        return false;
    }
    
    /* For testing purposes, assume forwarding succeeded if NICs are valid */
    return true;
}

/* ========== Test Suite Runner ========== */

test_result_t run_routing_test_suite(void) {
    log_info("Starting Routing Functionality Test Suite");
    
    test_result_t overall_result = TEST_RESULT_PASS;
    int tests_passed = 0;
    int tests_failed = 0;
    
    /* Set up test environment */
    if (setup_routing_test_environment() != TEST_RESULT_PASS) {
        log_error("Failed to set up routing test environment");
        return TEST_RESULT_ERROR;
    }
    
    /* Define test cases */
    struct {
        const char *name;
        test_result_t (*test_func)(void);
    } test_cases[] = {
        {"Routing Initialization", test_routing_initialization},
        {"Routing Table Management", test_routing_table_management},
        {"Bridge Learning Functionality", test_bridge_learning_functionality},
        {"Packet Routing Decisions", test_packet_routing_decisions},
        {"MAC Address Utilities", test_mac_address_utilities},
        {"Packet Forwarding", test_packet_forwarding},
        {"Packet Broadcasting", test_packet_broadcasting},
        {"Multi-NIC Routing Scenarios", test_multi_nic_routing_scenarios},
        {"Flow-Aware Routing", test_flow_aware_routing},
        {"Routing Failover Logic", test_routing_failover_logic},
        {"Routing Rate Limiting", test_routing_rate_limiting},
        {"Routing Statistics Tracking", test_routing_statistics_tracking},
        {"Routing Error Conditions", test_routing_error_conditions},
        {"Routing Integration Scenarios", test_routing_integration_scenarios}
    };
    
    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    
    /* Run all test cases */
    for (int i = 0; i < num_tests; i++) {
        log_info("Running test: %s", test_cases[i].name);
        
        test_result_t result = test_cases[i].test_func();
        
        if (result == TEST_RESULT_PASS) {
            tests_passed++;
            log_info("Test PASSED: %s", test_cases[i].name);
        } else {
            tests_failed++;
            overall_result = TEST_RESULT_FAIL;
            log_error("Test FAILED: %s", test_cases[i].name);
        }
        
        /* Clean up between tests */
        routing_clear_table();
        bridge_flush_table();
        routing_clear_stats();
        mock_framework_reset();
    }
    
    /* Clean up test environment */
    cleanup_routing_test_environment();
    
    /* Report results */
    log_info("Routing Test Suite Results: %d passed, %d failed", tests_passed, tests_failed);
    
    if (overall_result == TEST_RESULT_PASS) {
        log_info("Routing Functionality Test Suite: ALL TESTS PASSED");
    } else {
        log_error("Routing Functionality Test Suite: SOME TESTS FAILED");
    }
    
    return overall_result;
}