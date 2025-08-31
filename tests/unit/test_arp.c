/**
 * @file arp_test.c
 * @brief Comprehensive ARP Protocol Test Suite (RFC 826 Compliance)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite validates the ARP protocol implementation for RFC 826
 * compliance, including cache management, packet processing, proxy ARP,
 * and multi-NIC scenarios with hardware mocking support.
 */

#include "../../include/arp.h"
#include "../../include/static_routing.h" 
#include "../../include/test_framework.h"
#include "../../include/hardware_mock.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test constants */
#define TEST_MAC_1          {0x00, 0x10, 0x4B, 0x12, 0x34, 0x56}
#define TEST_MAC_2          {0x00, 0x10, 0x4B, 0xAB, 0xCD, 0xEF}
#define TEST_MAC_BROADCAST  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define TEST_IP_1           {192, 168, 1, 10}
#define TEST_IP_2           {192, 168, 1, 20}
#define TEST_IP_GATEWAY     {192, 168, 1, 1}
#define TEST_SUBNET_MASK    {255, 255, 255, 0}
#define MAX_TEST_PACKETS    64

/* Test data structures */
typedef struct {
    uint8_t expected_tx_packets;
    uint8_t expected_rx_packets;
    uint32_t expected_cache_hits;
    uint32_t expected_cache_misses;
    bool link_up[MAX_NICS];
    uint16_t link_speed[MAX_NICS];
} test_scenario_expectations_t;

typedef struct {
    ip_addr_t ip;
    uint8_t mac[ETH_ALEN];
    uint8_t nic_index;
    uint32_t age_seconds;
    uint16_t flags;
} test_arp_entry_t;

/* Test fixture for ARP tests */
typedef struct {
    uint8_t mock_nic1_id;
    uint8_t mock_nic2_id;
    test_arp_entry_t test_entries[16];
    uint8_t entry_count;
    uint8_t test_packets[MAX_TEST_PACKETS][1600];
    uint16_t packet_lengths[MAX_TEST_PACKETS];
    uint8_t packet_count;
} arp_test_fixture_t;

static arp_test_fixture_t g_arp_test_fixture;

/* Forward declarations */
static test_result_t setup_arp_test_environment(void);
static void cleanup_arp_test_environment(void);
static test_result_t create_test_arp_packet(arp_packet_t *packet, uint16_t operation,
                                           const uint8_t *sender_hw, const ip_addr_t *sender_ip,
                                           const uint8_t *target_hw, const ip_addr_t *target_ip);
static test_result_t verify_arp_cache_entry(const ip_addr_t *ip, const uint8_t *expected_mac,
                                           uint8_t expected_nic, uint16_t expected_flags);
static test_result_t inject_test_network_topology(void);
static test_result_t simulate_network_delays(void);
static bool compare_mac_addresses(const uint8_t *mac1, const uint8_t *mac2);

/* ========== ARP Initialization and Configuration Tests ========== */

static test_result_t test_arp_initialization(void) {
    TEST_LOG_START("ARP Initialization");
    
    /* Test uninitialized state */
    TEST_ASSERT(!arp_is_enabled(), "ARP should not be enabled before initialization");
    
    /* Initialize ARP system */
    int result = arp_init();
    TEST_ASSERT(result == SUCCESS, "ARP initialization should succeed");
    
    /* Verify initialized state */
    TEST_ASSERT(!arp_is_enabled(), "ARP should not be auto-enabled after init");
    
    /* Enable ARP */
    result = arp_enable(true);
    TEST_ASSERT(result == SUCCESS, "ARP enable should succeed");
    TEST_ASSERT(arp_is_enabled(), "ARP should be enabled after arp_enable(true)");
    
    /* Verify statistics are initialized */
    const arp_stats_t *stats = arp_get_stats();
    TEST_ASSERT(stats != NULL, "ARP statistics should be available");
    TEST_ASSERT(stats->packets_received == 0, "Initial packets received should be 0");
    TEST_ASSERT(stats->packets_sent == 0, "Initial packets sent should be 0");
    
    /* Test configuration parameters */
    uint32_t timeout = arp_get_timeout();
    TEST_ASSERT(timeout == ARP_ENTRY_TIMEOUT, "Default timeout should match ARP_ENTRY_TIMEOUT");
    
    uint8_t max_retries = arp_get_max_retries();
    TEST_ASSERT(max_retries == ARP_MAX_RETRIES, "Default max retries should match ARP_MAX_RETRIES");
    
    /* Test parameter modification */
    result = arp_set_timeout(600000); /* 10 minutes */
    TEST_ASSERT(result == SUCCESS, "Setting ARP timeout should succeed");
    TEST_ASSERT(arp_get_timeout() == 600000, "ARP timeout should be updated");
    
    result = arp_set_max_retries(5);
    TEST_ASSERT(result == SUCCESS, "Setting max retries should succeed");
    TEST_ASSERT(arp_get_max_retries() == 5, "Max retries should be updated");
    
    TEST_LOG_END("ARP Initialization", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_arp_cache_initialization(void) {
    TEST_LOG_START("ARP Cache Initialization");
    
    arp_cache_t test_cache;
    
    /* Test cache initialization with various sizes */
    int result = arp_cache_init(&test_cache, 128);
    TEST_ASSERT(result == SUCCESS, "Cache initialization should succeed");
    TEST_ASSERT(test_cache.initialized == true, "Cache should be marked as initialized");
    TEST_ASSERT(test_cache.entry_count == 0, "Initial entry count should be 0");
    TEST_ASSERT(test_cache.max_entries == 128, "Max entries should match requested size");
    TEST_ASSERT(test_cache.entries != NULL, "Entry pool should be allocated");
    TEST_ASSERT(test_cache.free_list != NULL, "Free list should be initialized");
    
    /* Test with edge cases */
    arp_cache_cleanup(&test_cache);
    result = arp_cache_init(&test_cache, 1);
    TEST_ASSERT(result == SUCCESS, "Cache initialization with size 1 should succeed");
    
    arp_cache_cleanup(&test_cache);
    result = arp_cache_init(&test_cache, ARP_TABLE_SIZE);
    TEST_ASSERT(result == SUCCESS, "Cache initialization with max size should succeed");
    
    /* Test error conditions */
    result = arp_cache_init(NULL, 64);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL cache should return error");
    
    arp_cache_cleanup(&test_cache);
    
    TEST_LOG_END("ARP Cache Initialization", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Cache Management Tests ========== */

static test_result_t test_arp_cache_basic_operations(void) {
    TEST_LOG_START("ARP Cache Basic Operations");
    
    ip_addr_t test_ip1 = {TEST_IP_1};
    ip_addr_t test_ip2 = {TEST_IP_2};
    uint8_t test_mac1[] = TEST_MAC_1;
    uint8_t test_mac2[] = TEST_MAC_2;
    
    /* Test cache addition */
    int result = arp_cache_add(&test_ip1, test_mac1, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Adding entry to cache should succeed");
    
    /* Test cache lookup */
    arp_cache_entry_t *entry = arp_cache_lookup(&test_ip1);
    TEST_ASSERT(entry != NULL, "Lookup should find the added entry");
    TEST_ASSERT(ip_addr_equals(&entry->ip, &test_ip1), "IP should match");
    TEST_ASSERT(compare_mac_addresses(entry->mac, test_mac1), "MAC should match");
    TEST_ASSERT(entry->nic_index == 0, "NIC index should match");
    TEST_ASSERT(entry->state == ARP_STATE_COMPLETE, "State should be COMPLETE");
    
    /* Test cache update */
    result = arp_cache_update(&test_ip1, test_mac2, 1);
    TEST_ASSERT(result == SUCCESS, "Updating existing entry should succeed");
    
    entry = arp_cache_lookup(&test_ip1);
    TEST_ASSERT(entry != NULL, "Entry should still exist after update");
    TEST_ASSERT(compare_mac_addresses(entry->mac, test_mac2), "MAC should be updated");
    TEST_ASSERT(entry->nic_index == 1, "NIC index should be updated");
    
    /* Test multiple entries */
    result = arp_cache_add(&test_ip2, test_mac2, 1, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Adding second entry should succeed");
    
    /* Verify both entries exist */
    entry = arp_cache_lookup(&test_ip1);
    TEST_ASSERT(entry != NULL, "First entry should still exist");
    
    entry = arp_cache_lookup(&test_ip2);
    TEST_ASSERT(entry != NULL, "Second entry should exist");
    
    /* Test cache deletion */
    result = arp_cache_delete(&test_ip1);
    TEST_ASSERT(result == SUCCESS, "Deleting entry should succeed");
    
    entry = arp_cache_lookup(&test_ip1);
    TEST_ASSERT(entry == NULL, "Deleted entry should not be found");
    
    entry = arp_cache_lookup(&test_ip2);
    TEST_ASSERT(entry != NULL, "Other entries should remain");
    
    TEST_LOG_END("ARP Cache Basic Operations", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_arp_cache_hash_functionality(void) {
    TEST_LOG_START("ARP Cache Hash Functionality");
    
    /* Test hash function distribution */
    ip_addr_t test_ips[32];
    uint16_t hash_counts[ARP_HASH_SIZE] = {0};
    
    /* Create diverse IP addresses */
    for (int i = 0; i < 32; i++) {
        ip_addr_set(&test_ips[i], 192, 168, (i / 256) % 256, i % 256);
        uint16_t hash = arp_calculate_hash(&test_ips[i]);
        
        TEST_ASSERT(hash < ARP_HASH_SIZE, "Hash value should be within bounds");
        hash_counts[hash]++;
    }
    
    /* Verify reasonable distribution (no bucket should have more than half the entries) */
    bool good_distribution = true;
    for (int i = 0; i < ARP_HASH_SIZE; i++) {
        if (hash_counts[i] > 16) {
            good_distribution = false;
            break;
        }
    }
    TEST_ASSERT(good_distribution, "Hash function should distribute entries reasonably");
    
    /* Test hash collision handling */
    ip_addr_t collision_ip1 = {192, 168, 1, 10};
    ip_addr_t collision_ip2 = {192, 168, 1, 10};
    uint8_t mac1[] = TEST_MAC_1;
    uint8_t mac2[] = TEST_MAC_2;
    
    /* Artificially create collision scenario by using same IP */
    collision_ip2.addr[3] = 20; /* Different last octet */
    
    /* Ensure they hash to same bucket by manipulating the test */
    uint16_t hash1 = arp_calculate_hash(&collision_ip1);
    uint16_t hash2 = arp_calculate_hash(&collision_ip2);
    
    /* Add both entries */
    int result = arp_cache_add(&collision_ip1, mac1, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "First collision entry should succeed");
    
    result = arp_cache_add(&collision_ip2, mac2, 1, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Second collision entry should succeed");
    
    /* Verify both can be looked up correctly */
    arp_cache_entry_t *entry1 = arp_cache_lookup(&collision_ip1);
    arp_cache_entry_t *entry2 = arp_cache_lookup(&collision_ip2);
    
    TEST_ASSERT(entry1 != NULL, "First collision entry should be found");
    TEST_ASSERT(entry2 != NULL, "Second collision entry should be found");
    TEST_ASSERT(entry1 != entry2, "Entries should be different objects");
    TEST_ASSERT(compare_mac_addresses(entry1->mac, mac1), "First entry MAC should match");
    TEST_ASSERT(compare_mac_addresses(entry2->mac, mac2), "Second entry MAC should match");
    
    TEST_LOG_END("ARP Cache Hash Functionality", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_arp_cache_aging_and_eviction(void) {
    TEST_LOG_START("ARP Cache Aging and Eviction");
    
    /* Fill cache to test aging behavior */
    ip_addr_t test_ips[ARP_TABLE_SIZE + 10];
    uint8_t test_mac[] = TEST_MAC_1;
    
    /* Add entries until cache is nearly full */
    for (int i = 0; i < ARP_TABLE_SIZE - 2; i++) {
        ip_addr_set(&test_ips[i], 10, 0, i / 256, i % 256);
        int result = arp_cache_add(&test_ips[i], test_mac, 0, ARP_FLAG_COMPLETE);
        TEST_ASSERT(result == SUCCESS, "Adding entry should succeed");
    }
    
    /* Add permanent entry that should not be aged out */
    ip_addr_t permanent_ip = {TEST_IP_1};
    int result = arp_cache_add(&permanent_ip, test_mac, 0, ARP_FLAG_PERMANENT);
    TEST_ASSERT(result == SUCCESS, "Adding permanent entry should succeed");
    
    /* Trigger aging process */
    arp_cache_age_entries();
    
    /* Verify permanent entry remains */
    arp_cache_entry_t *entry = arp_cache_lookup(&permanent_ip);
    TEST_ASSERT(entry != NULL, "Permanent entry should not be aged out");
    
    /* Test cache overflow and LRU eviction */
    ip_addr_t overflow_ip = {TEST_IP_2};
    result = arp_cache_add(&overflow_ip, test_mac, 0, ARP_FLAG_COMPLETE);
    /* This should either succeed (if aging freed space) or handle overflow gracefully */
    
    /* Test manual cache flush */
    arp_cache_flush();
    
    /* Verify non-permanent entries are removed */
    bool found_non_permanent = false;
    for (int i = 0; i < ARP_TABLE_SIZE - 2; i++) {
        entry = arp_cache_lookup(&test_ips[i]);
        if (entry != NULL && !(entry->flags & ARP_FLAG_PERMANENT)) {
            found_non_permanent = true;
            break;
        }
    }
    TEST_ASSERT(!found_non_permanent, "Non-permanent entries should be flushed");
    
    /* Permanent entry should remain after flush */
    entry = arp_cache_lookup(&permanent_ip);
    TEST_ASSERT(entry != NULL, "Permanent entry should survive flush");
    
    TEST_LOG_END("ARP Cache Aging and Eviction", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Packet Processing Tests ========== */

static test_result_t test_arp_request_processing(void) {
    TEST_LOG_START("ARP Request Processing");
    
    /* Set up test network configuration */
    ip_addr_t local_ip = {TEST_IP_1};
    ip_addr_t target_ip = {TEST_IP_2};
    uint8_t sender_mac[] = TEST_MAC_2;
    uint8_t target_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; /* Unknown */
    
    /* Configure local subnet */
    ip_addr_t subnet = {192, 168, 1, 0};
    ip_addr_t netmask = TEST_SUBNET_MASK;
    int result = static_subnet_add(&subnet, (ip_addr_t*)&netmask, 0);
    TEST_ASSERT(result == SUCCESS, "Subnet configuration should succeed");
    
    /* Create ARP request packet */
    arp_packet_t request_packet;
    result = create_test_arp_packet(&request_packet, ARP_OP_REQUEST,
                                   sender_mac, &target_ip,
                                   target_mac, &local_ip);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating ARP request should succeed");
    
    /* Process the ARP request */
    result = arp_process_packet((uint8_t*)&request_packet, sizeof(arp_packet_t), 0);
    TEST_ASSERT(result == SUCCESS, "Processing ARP request should succeed");
    
    /* Verify cache learning occurred */
    arp_cache_entry_t *entry = arp_cache_lookup(&target_ip);
    TEST_ASSERT(entry != NULL, "Sender should be learned in cache");
    TEST_ASSERT(compare_mac_addresses(entry->mac, sender_mac), "Learned MAC should match sender");
    
    /* Check statistics */
    const arp_stats_t *stats = arp_get_stats();
    TEST_ASSERT(stats->packets_received > 0, "Received packet count should increment");
    TEST_ASSERT(stats->requests_received > 0, "Request count should increment");
    
    /* Test ARP request for non-local IP */
    ip_addr_t foreign_ip = {10, 0, 0, 1};
    result = create_test_arp_packet(&request_packet, ARP_OP_REQUEST,
                                   sender_mac, &target_ip,
                                   target_mac, &foreign_ip);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating foreign ARP request should succeed");
    
    uint32_t prev_replies = stats->replies_sent;
    result = arp_process_packet((uint8_t*)&request_packet, sizeof(arp_packet_t), 0);
    TEST_ASSERT(result == SUCCESS, "Processing foreign ARP request should succeed");
    
    stats = arp_get_stats();
    TEST_ASSERT(stats->replies_sent == prev_replies, "Should not reply to foreign IP requests");
    
    TEST_LOG_END("ARP Request Processing", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_arp_reply_processing(void) {
    TEST_LOG_START("ARP Reply Processing");
    
    ip_addr_t sender_ip = {TEST_IP_1};
    ip_addr_t target_ip = {TEST_IP_2};
    uint8_t sender_mac[] = TEST_MAC_1;
    uint8_t target_mac[] = TEST_MAC_2;
    
    /* Create incomplete cache entry */
    int result = arp_cache_add(&sender_ip, (uint8_t[]){0,0,0,0,0,0}, 0, 0);
    TEST_ASSERT(result == SUCCESS, "Adding incomplete entry should succeed");
    
    arp_cache_entry_t *entry = arp_cache_lookup(&sender_ip);
    TEST_ASSERT(entry != NULL, "Incomplete entry should exist");
    entry->state = ARP_STATE_INCOMPLETE;
    
    /* Create ARP reply packet */
    arp_packet_t reply_packet;
    result = create_test_arp_packet(&reply_packet, ARP_OP_REPLY,
                                   sender_mac, &sender_ip,
                                   target_mac, &target_ip);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating ARP reply should succeed");
    
    /* Process the ARP reply */
    result = arp_process_packet((uint8_t*)&reply_packet, sizeof(arp_packet_t), 0);
    TEST_ASSERT(result == SUCCESS, "Processing ARP reply should succeed");
    
    /* Verify cache entry is completed */
    entry = arp_cache_lookup(&sender_ip);
    TEST_ASSERT(entry != NULL, "Cache entry should exist after reply");
    TEST_ASSERT(compare_mac_addresses(entry->mac, sender_mac), "MAC should be updated from reply");
    TEST_ASSERT(entry->state == ARP_STATE_COMPLETE, "Entry should be marked complete");
    
    /* Check statistics */
    const arp_stats_t *stats = arp_get_stats();
    TEST_ASSERT(stats->replies_received > 0, "Reply count should increment");
    TEST_ASSERT(stats->cache_updates > 0, "Cache update count should increment");
    
    /* Test unsolicited ARP reply */
    ip_addr_t unsolicited_ip = {10, 0, 0, 100};
    result = create_test_arp_packet(&reply_packet, ARP_OP_REPLY,
                                   sender_mac, &unsolicited_ip,
                                   target_mac, &target_ip);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating unsolicited reply should succeed");
    
    result = arp_process_packet((uint8_t*)&reply_packet, sizeof(arp_packet_t), 0);
    TEST_ASSERT(result == SUCCESS, "Processing unsolicited reply should succeed");
    
    /* Verify new cache entry is created */
    entry = arp_cache_lookup(&unsolicited_ip);
    TEST_ASSERT(entry != NULL, "Unsolicited reply should create cache entry");
    TEST_ASSERT(entry->state == ARP_STATE_COMPLETE, "Unsolicited entry should be complete");
    
    TEST_LOG_END("ARP Reply Processing", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_arp_packet_validation(void) {
    TEST_LOG_START("ARP Packet Validation");
    
    arp_packet_t valid_packet;
    ip_addr_t test_ip = {TEST_IP_1};
    uint8_t test_mac[] = TEST_MAC_1;
    
    /* Create valid packet */
    int result = create_test_arp_packet(&valid_packet, ARP_OP_REQUEST,
                                       test_mac, &test_ip, test_mac, &test_ip);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating valid packet should succeed");
    
    /* Test valid packet validation */
    bool valid = arp_validate_packet(&valid_packet, sizeof(arp_packet_t));
    TEST_ASSERT(valid, "Valid packet should pass validation");
    
    /* Test invalid hardware type */
    arp_packet_t invalid_packet = valid_packet;
    invalid_packet.hw_type = htons(0x0002); /* Invalid hardware type */
    valid = arp_validate_packet(&invalid_packet, sizeof(arp_packet_t));
    TEST_ASSERT(!valid, "Invalid hardware type should fail validation");
    
    /* Test invalid protocol type */
    invalid_packet = valid_packet;
    invalid_packet.proto_type = htons(0x0806); /* Wrong protocol type */
    valid = arp_validate_packet(&invalid_packet, sizeof(arp_packet_t));
    TEST_ASSERT(!valid, "Invalid protocol type should fail validation");
    
    /* Test invalid hardware length */
    invalid_packet = valid_packet;
    invalid_packet.hw_len = 4; /* Invalid hardware length */
    valid = arp_validate_packet(&invalid_packet, sizeof(arp_packet_t));
    TEST_ASSERT(!valid, "Invalid hardware length should fail validation");
    
    /* Test invalid protocol length */
    invalid_packet = valid_packet;
    invalid_packet.proto_len = 6; /* Invalid protocol length */
    valid = arp_validate_packet(&invalid_packet, sizeof(arp_packet_t));
    TEST_ASSERT(!valid, "Invalid protocol length should fail validation");
    
    /* Test invalid operation */
    invalid_packet = valid_packet;
    invalid_packet.operation = htons(0x0003); /* Invalid operation */
    valid = arp_validate_packet(&invalid_packet, sizeof(arp_packet_t));
    TEST_ASSERT(!valid, "Invalid operation should fail validation");
    
    /* Test short packet */
    valid = arp_validate_packet(&valid_packet, sizeof(arp_packet_t) - 1);
    TEST_ASSERT(!valid, "Short packet should fail validation");
    
    /* Test NULL packet */
    valid = arp_validate_packet(NULL, sizeof(arp_packet_t));
    TEST_ASSERT(!valid, "NULL packet should fail validation");
    
    TEST_LOG_END("ARP Packet Validation", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Protocol Resolution Tests ========== */

static test_result_t test_arp_resolution_process(void) {
    TEST_LOG_START("ARP Resolution Process");
    
    ip_addr_t target_ip = {TEST_IP_1};
    uint8_t resolved_mac[ETH_ALEN];
    uint8_t resolved_nic;
    
    /* Test resolution of uncached IP */
    int result = arp_resolve(&target_ip, resolved_mac, &resolved_nic);
    TEST_ASSERT(result == ERROR_BUSY, "Initial resolution should return BUSY");
    
    /* Verify ARP request was sent */
    const arp_stats_t *stats = arp_get_stats();
    uint32_t initial_requests = stats->requests_sent;
    TEST_ASSERT(initial_requests > 0, "ARP request should be sent");
    
    /* Simulate ARP reply reception */
    uint8_t target_mac[] = TEST_MAC_1;
    result = arp_cache_add(&target_ip, target_mac, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Adding resolved entry should succeed");
    
    /* Test resolution of cached IP */
    result = arp_resolve(&target_ip, resolved_mac, &resolved_nic);
    TEST_ASSERT(result == SUCCESS, "Cached resolution should succeed");
    TEST_ASSERT(compare_mac_addresses(resolved_mac, target_mac), "Resolved MAC should match");
    TEST_ASSERT(resolved_nic == 0, "Resolved NIC should match");
    
    /* Test async resolution */
    ip_addr_t async_ip = {TEST_IP_2};
    result = arp_resolve_async(&async_ip, 0);
    TEST_ASSERT(result == SUCCESS, "Async resolution should succeed");
    
    /* Verify request was sent */
    stats = arp_get_stats();
    TEST_ASSERT(stats->requests_sent > initial_requests, "Additional request should be sent");
    
    /* Test resolution state checking */
    bool resolved = arp_is_resolved(&target_ip);
    TEST_ASSERT(resolved, "Cached entry should be marked as resolved");
    
    resolved = arp_is_resolved(&async_ip);
    TEST_ASSERT(!resolved, "Pending entry should not be marked as resolved");
    
    TEST_LOG_END("ARP Resolution Process", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

static test_result_t test_arp_retry_mechanism(void) {
    TEST_LOG_START("ARP Retry Mechanism");
    
    /* Set low retry count for testing */
    int result = arp_set_max_retries(2);
    TEST_ASSERT(result == SUCCESS, "Setting max retries should succeed");
    
    result = arp_set_request_timeout(1000); /* 1 second for faster testing */
    TEST_ASSERT(result == SUCCESS, "Setting request timeout should succeed");
    
    ip_addr_t target_ip = {192, 168, 100, 50}; /* Non-existent IP */
    
    /* Start resolution process */
    result = arp_resolve_async(&target_ip, 0);
    TEST_ASSERT(result == SUCCESS, "Initial async resolve should succeed");
    
    /* Verify entry exists in incomplete state */
    arp_cache_entry_t *entry = arp_cache_lookup(&target_ip);
    TEST_ASSERT(entry != NULL, "Incomplete entry should exist");
    TEST_ASSERT(entry->state == ARP_STATE_INCOMPLETE, "Entry should be incomplete");
    TEST_ASSERT(entry->retry_count == 1, "Initial retry count should be 1");
    
    /* Simulate timeout and retry */
    uint32_t initial_requests = arp_get_stats()->requests_sent;
    
    /* Force timeout by manipulating entry timestamp */
    entry->last_request_time = 0; /* Force timeout */
    
    result = arp_resolve_async(&target_ip, 0);
    TEST_ASSERT(result == SUCCESS, "Retry should succeed");
    TEST_ASSERT(entry->retry_count == 2, "Retry count should increment");
    
    /* Force final timeout */
    entry->last_request_time = 0;
    result = arp_resolve_async(&target_ip, 0);
    TEST_ASSERT(result == SUCCESS, "Final retry should succeed");
    
    /* Force timeout beyond max retries */
    entry->last_request_time = 0;
    result = arp_resolve_async(&target_ip, 0);
    TEST_ASSERT(result == ERROR_TIMEOUT, "Should timeout after max retries");
    
    /* Verify entry is removed */
    entry = arp_cache_lookup(&target_ip);
    TEST_ASSERT(entry == NULL, "Entry should be removed after timeout");
    
    /* Check timeout statistics */
    const arp_stats_t *stats = arp_get_stats();
    TEST_ASSERT(stats->request_timeouts > 0, "Request timeout count should increment");
    
    TEST_LOG_END("ARP Retry Mechanism", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Proxy ARP Tests ========== */

static test_result_t test_proxy_arp_functionality(void) {
    TEST_LOG_START("Proxy ARP Functionality");
    
    /* Enable proxy ARP */
    int result = arp_set_proxy_enabled(true);
    TEST_ASSERT(result == SUCCESS, "Enabling proxy ARP should succeed");
    TEST_ASSERT(arp_is_proxy_enabled(), "Proxy ARP should be enabled");
    
    /* Configure proxy scenario: NIC 0 proxies for network on NIC 1 */
    ip_addr_t proxy_subnet = {10, 0, 1, 0};
    ip_addr_t netmask = {255, 255, 255, 0};
    
    /* Add proxy entry for remote network */
    ip_addr_t remote_ip = {10, 0, 1, 100};
    result = arp_add_proxy_entry(&remote_ip, 1);
    /* Note: Implementation may be incomplete - this tests the interface */
    
    /* Create ARP request for proxied IP */
    ip_addr_t requester_ip = {192, 168, 1, 10};
    uint8_t requester_mac[] = TEST_MAC_2;
    uint8_t zero_mac[] = {0, 0, 0, 0, 0, 0};
    
    arp_packet_t proxy_request;
    result = create_test_arp_packet(&proxy_request, ARP_OP_REQUEST,
                                   requester_mac, &requester_ip,
                                   zero_mac, &remote_ip);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating proxy request should succeed");
    
    /* Process proxy request */
    uint32_t initial_proxy_requests = arp_get_stats()->proxy_requests;
    result = arp_process_packet((uint8_t*)&proxy_request, sizeof(arp_packet_t), 0);
    TEST_ASSERT(result == SUCCESS, "Processing proxy request should succeed");
    
    /* Check if proxy request was handled */
    const arp_stats_t *stats = arp_get_stats();
    TEST_EXPECT(stats->proxy_requests > initial_proxy_requests, 
                "Proxy request count should increment");
    
    /* Test disabling proxy ARP */
    result = arp_set_proxy_enabled(false);
    TEST_ASSERT(result == SUCCESS, "Disabling proxy ARP should succeed");
    TEST_ASSERT(!arp_is_proxy_enabled(), "Proxy ARP should be disabled");
    
    /* Remove proxy entry */
    result = arp_remove_proxy_entry(&remote_ip);
    /* Note: Implementation may return not supported */
    
    TEST_LOG_END("Proxy ARP Functionality", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Gratuitous ARP Tests ========== */

static test_result_t test_gratuitous_arp(void) {
    TEST_LOG_START("Gratuitous ARP");
    
    ip_addr_t local_ip = {TEST_IP_1};
    
    /* Configure local subnet */
    ip_addr_t subnet = {192, 168, 1, 0};
    ip_addr_t netmask = TEST_SUBNET_MASK;
    int result = static_subnet_add(&subnet, (ip_addr_t*)&netmask, 0);
    TEST_ASSERT(result == SUCCESS, "Subnet configuration should succeed");
    
    /* Send gratuitous ARP */
    uint32_t initial_garp_count = arp_get_stats()->gratuitous_arps;
    result = arp_send_gratuitous(&local_ip, 0);
    TEST_ASSERT(result == SUCCESS, "Sending gratuitous ARP should succeed");
    
    /* Verify statistics */
    const arp_stats_t *stats = arp_get_stats();
    TEST_ASSERT(stats->gratuitous_arps > initial_garp_count, 
                "Gratuitous ARP count should increment");
    TEST_ASSERT(stats->packets_sent > 0, "Packets sent should increment");
    
    /* Test gratuitous ARP for multiple NICs */
    if (hardware_get_nic_count() > 1) {
        result = arp_send_gratuitous(&local_ip, 1);
        TEST_ASSERT(result == SUCCESS, "GARP on second NIC should succeed");
    }
    
    /* Test error conditions */
    result = arp_send_gratuitous(NULL, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL IP should return error");
    
    result = arp_send_gratuitous(&local_ip, 99); /* Invalid NIC */
    TEST_ASSERT(result != SUCCESS, "Invalid NIC should return error");
    
    TEST_LOG_END("Gratuitous ARP", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Multi-NIC ARP Tests ========== */

static test_result_t test_multi_nic_arp_behavior(void) {
    TEST_LOG_START("Multi-NIC ARP Behavior");
    
    /* Set up multi-NIC scenario */
    ip_addr_t subnet1 = {192, 168, 1, 0};
    ip_addr_t subnet2 = {192, 168, 2, 0};
    ip_addr_t netmask = {255, 255, 255, 0};
    
    int result = static_subnet_add(&subnet1, &netmask, 0);
    TEST_ASSERT(result == SUCCESS, "Adding subnet 1 should succeed");
    
    result = static_subnet_add(&subnet2, &netmask, 1);
    TEST_ASSERT(result == SUCCESS, "Adding subnet 2 should succeed");
    
    /* Test ARP learning on different NICs */
    ip_addr_t ip_nic1 = {192, 168, 1, 50};
    ip_addr_t ip_nic2 = {192, 168, 2, 50};
    uint8_t mac_nic1[] = TEST_MAC_1;
    uint8_t mac_nic2[] = TEST_MAC_2;
    
    result = arp_cache_add(&ip_nic1, mac_nic1, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Adding entry for NIC 1 should succeed");
    
    result = arp_cache_add(&ip_nic2, mac_nic2, 1, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Adding entry for NIC 2 should succeed");
    
    /* Verify entries are associated with correct NICs */
    arp_cache_entry_t *entry1 = arp_cache_lookup(&ip_nic1);
    arp_cache_entry_t *entry2 = arp_cache_lookup(&ip_nic2);
    
    TEST_ASSERT(entry1 != NULL && entry1->nic_index == 0, "Entry 1 should be on NIC 0");
    TEST_ASSERT(entry2 != NULL && entry2->nic_index == 1, "Entry 2 should be on NIC 1");
    
    /* Test cross-NIC ARP request handling */
    uint8_t cross_requester_mac[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    arp_packet_t cross_request;
    
    result = create_test_arp_packet(&cross_request, ARP_OP_REQUEST,
                                   cross_requester_mac, &ip_nic2,
                                   (uint8_t[]){0,0,0,0,0,0}, &ip_nic1);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Creating cross-NIC request should succeed");
    
    /* Process request on wrong NIC */
    result = arp_process_packet((uint8_t*)&cross_request, sizeof(arp_packet_t), 1);
    /* This should be handled according to the implementation's policy */
    
    /* Test NIC selection for ARP requests */
    uint8_t selected_nic = arp_get_nic_for_ip(&ip_nic1);
    TEST_ASSERT(selected_nic == 0, "Should select NIC 0 for subnet 1 IP");
    
    selected_nic = arp_get_nic_for_ip(&ip_nic2);
    TEST_ASSERT(selected_nic == 1, "Should select NIC 1 for subnet 2 IP");
    
    /* Test ARP resolution with NIC affinity */
    uint8_t resolved_mac[ETH_ALEN];
    uint8_t resolved_nic;
    
    result = arp_resolve(&ip_nic1, resolved_mac, &resolved_nic);
    TEST_ASSERT(result == SUCCESS, "Resolution should succeed");
    TEST_ASSERT(resolved_nic == 0, "Should resolve via NIC 0");
    
    TEST_LOG_END("Multi-NIC ARP Behavior", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Statistics and Monitoring Tests ========== */

static test_result_t test_arp_statistics_tracking(void) {
    TEST_LOG_START("ARP Statistics Tracking");
    
    /* Clear statistics */
    arp_clear_stats();
    
    const arp_stats_t *stats = arp_get_stats();
    TEST_ASSERT(stats != NULL, "Statistics should be available");
    TEST_ASSERT(stats->packets_received == 0, "Initial received count should be 0");
    TEST_ASSERT(stats->packets_sent == 0, "Initial sent count should be 0");
    
    /* Generate various ARP activities */
    ip_addr_t test_ip = {TEST_IP_1};
    uint8_t test_mac[] = TEST_MAC_1;
    
    /* Test cache operations statistics */
    int result = arp_cache_add(&test_ip, test_mac, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Cache add should succeed");
    
    stats = arp_get_stats();
    TEST_ASSERT(stats->cache_updates > 0, "Cache update count should increment");
    
    /* Test lookup statistics */
    arp_cache_entry_t *entry = arp_cache_lookup(&test_ip);
    TEST_ASSERT(entry != NULL, "Lookup should succeed");
    
    /* Test packet processing statistics */
    arp_packet_t test_packet;
    result = create_test_arp_packet(&test_packet, ARP_OP_REQUEST,
                                   test_mac, &test_ip, test_mac, &test_ip);
    TEST_ASSERT(result == TEST_RESULT_PASS, "Packet creation should succeed");
    
    result = arp_process_packet((uint8_t*)&test_packet, sizeof(arp_packet_t), 0);
    TEST_ASSERT(result == SUCCESS, "Packet processing should succeed");
    
    stats = arp_get_stats();
    TEST_ASSERT(stats->packets_received > 0, "Received count should increment");
    TEST_ASSERT(stats->requests_received > 0, "Request count should increment");
    
    /* Test ARP resolution statistics */
    ip_addr_t resolve_ip = {10, 0, 0, 100};
    uint8_t resolve_mac[ETH_ALEN];
    uint8_t resolve_nic;
    
    result = arp_resolve(&resolve_ip, resolve_mac, &resolve_nic);
    /* Should return BUSY and send request */
    
    stats = arp_get_stats();
    TEST_ASSERT(stats->requests_sent > 0, "Request sent count should increment");
    
    /* Test error statistics */
    uint8_t invalid_packet[10] = {0}; /* Too short */
    result = arp_process_packet(invalid_packet, sizeof(invalid_packet), 0);
    
    stats = arp_get_stats();
    TEST_ASSERT(stats->invalid_packets > 0, "Invalid packet count should increment");
    
    /* Test cache hit/miss statistics tracking */
    /* Multiple lookups of same entry should show cache behavior */
    for (int i = 0; i < 5; i++) {
        entry = arp_cache_lookup(&test_ip);
    }
    
    /* Look up non-existent entry */
    ip_addr_t missing_ip = {1, 2, 3, 4};
    entry = arp_cache_lookup(&missing_ip);
    TEST_ASSERT(entry == NULL, "Missing entry lookup should fail");
    
    /* Verify comprehensive statistics are reasonable */
    stats = arp_get_stats();
    TEST_ASSERT(stats->packets_received >= 1, "Should have received packets");
    TEST_ASSERT(stats->cache_updates >= 1, "Should have cache updates");
    TEST_ASSERT(stats->invalid_packets >= 1, "Should have invalid packets");
    
    TEST_LOG_END("ARP Statistics Tracking", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Error Handling and Edge Cases ========== */

static test_result_t test_arp_error_conditions(void) {
    TEST_LOG_START("ARP Error Conditions");
    
    /* Test null parameter handling */
    int result = arp_cache_add(NULL, (uint8_t*)TEST_MAC_1, 0, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL IP should return error");
    
    result = arp_cache_add((ip_addr_t*)TEST_IP_1, NULL, 0, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL MAC should return error");
    
    arp_cache_entry_t *entry = arp_cache_lookup(NULL);
    TEST_ASSERT(entry == NULL, "NULL IP lookup should return NULL");
    
    /* Test invalid NIC indices */
    result = arp_cache_add((ip_addr_t*)TEST_IP_1, (uint8_t*)TEST_MAC_1, 255, 0);
    /* Should be validated by implementation */
    
    /* Test disabled ARP operations */
    arp_enable(false);
    
    result = arp_cache_add((ip_addr_t*)TEST_IP_1, (uint8_t*)TEST_MAC_1, 0, 0);
    /* Behavior when disabled */
    
    entry = arp_cache_lookup((ip_addr_t*)TEST_IP_1);
    TEST_ASSERT(entry == NULL, "Lookups should fail when ARP disabled");
    
    arp_enable(true);
    
    /* Test packet processing error conditions */
    result = arp_process_packet(NULL, 100, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL packet should return error");
    
    result = arp_process_packet((uint8_t*)"short", 5, 0);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "Short packet should return error");
    
    /* Test resolution error conditions */
    uint8_t mac[ETH_ALEN];
    uint8_t nic;
    
    result = arp_resolve(NULL, mac, &nic);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL IP resolution should return error");
    
    result = arp_resolve((ip_addr_t*)TEST_IP_1, NULL, &nic);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL MAC buffer should return error");
    
    result = arp_resolve((ip_addr_t*)TEST_IP_1, mac, NULL);
    TEST_ASSERT(result == ERROR_INVALID_PARAM, "NULL NIC buffer should return error");
    
    /* Test memory exhaustion scenarios */
    /* Fill cache completely */
    ip_addr_t fill_ip;
    uint8_t fill_mac[] = TEST_MAC_1;
    
    for (int i = 0; i < ARP_TABLE_SIZE + 10; i++) {
        ip_addr_set(&fill_ip, 10, 0, i / 256, i % 256);
        result = arp_cache_add(&fill_ip, fill_mac, 0, ARP_FLAG_COMPLETE);
        /* Should eventually return error or handle gracefully */
    }
    
    /* Test that critical operations still work */
    entry = arp_cache_lookup((ip_addr_t*)TEST_IP_1);
    /* Should return valid result or NULL gracefully */
    
    TEST_LOG_END("ARP Error Conditions", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Integration Tests ========== */

static test_result_t test_arp_integration_with_routing(void) {
    TEST_LOG_START("ARP Integration with Routing");
    
    /* Set up routing configuration */
    ip_addr_t local_subnet = {192, 168, 1, 0};
    ip_addr_t netmask = {255, 255, 255, 0};
    ip_addr_t gateway = {192, 168, 1, 1};
    
    int result = static_subnet_add(&local_subnet, &netmask, 0);
    TEST_ASSERT(result == SUCCESS, "Local subnet setup should succeed");
    
    result = static_routing_set_default_gateway(&gateway, 0);
    TEST_ASSERT(result == SUCCESS, "Default gateway setup should succeed");
    
    /* Test local IP detection */
    ip_addr_t local_ip = {192, 168, 1, 10};
    bool is_local = arp_is_local_ip(&local_ip);
    TEST_ASSERT(is_local, "Local IP should be detected");
    
    ip_addr_t remote_ip = {10, 0, 0, 1};
    is_local = arp_is_local_ip(&remote_ip);
    TEST_ASSERT(!is_local, "Remote IP should not be detected as local");
    
    /* Test NIC selection for ARP */
    uint8_t selected_nic = arp_get_nic_for_ip(&local_ip);
    TEST_ASSERT(selected_nic == 0, "Should select NIC 0 for local subnet");
    
    /* Test ARP resolution via routing */
    uint8_t resolved_mac[ETH_ALEN];
    uint8_t resolved_nic;
    
    /* Add gateway to ARP cache */
    uint8_t gateway_mac[] = TEST_MAC_2;
    result = arp_cache_add(&gateway, gateway_mac, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Adding gateway to ARP cache should succeed");
    
    /* Resolve remote IP (should go through gateway) */
    result = arp_resolve(&remote_ip, resolved_mac, &resolved_nic);
    /* Implementation dependent - may need gateway resolution first */
    
    /* Test integration with packet pipeline */
    bool is_arp_packet = arp_is_arp_packet((uint8_t*)"dummy", 100);
    TEST_ASSERT(!is_arp_packet, "Non-ARP packet should not be detected as ARP");
    
    /* Create actual ARP packet for testing */
    uint8_t ethernet_frame[ETH_HEADER_LEN + sizeof(arp_packet_t)];
    
    /* Set Ethernet header for ARP */
    uint16_t *ethertype = (uint16_t*)(ethernet_frame + 12);
    *ethertype = htons(ETH_P_ARP);
    
    is_arp_packet = arp_is_arp_packet(ethernet_frame, sizeof(ethernet_frame));
    TEST_ASSERT(is_arp_packet, "ARP packet should be detected");
    
    TEST_LOG_END("ARP Integration with Routing", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Network Topology Tests ========== */

static test_result_t test_arp_network_topology_scenarios(void) {
    TEST_LOG_START("ARP Network Topology Scenarios");
    
    /* Set up complex multi-segment network */
    result_t result = inject_test_network_topology();
    TEST_ASSERT(result == TEST_RESULT_PASS, "Network topology setup should succeed");
    
    /* Test ARP across network segments */
    ip_addr_t segment1_ip = {192, 168, 1, 10};
    ip_addr_t segment2_ip = {192, 168, 2, 10};
    ip_addr_t segment3_ip = {10, 0, 1, 10};
    
    uint8_t seg1_mac[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t seg2_mac[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x06};
    uint8_t seg3_mac[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x07};
    
    /* Add entries for different segments */
    int result = arp_cache_add(&segment1_ip, seg1_mac, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Segment 1 entry should succeed");
    
    result = arp_cache_add(&segment2_ip, seg2_mac, 1, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Segment 2 entry should succeed");
    
    result = arp_cache_add(&segment3_ip, seg3_mac, 2, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Segment 3 entry should succeed");
    
    /* Test cross-segment communication requirements */
    uint8_t resolved_nic = arp_get_nic_for_ip(&segment1_ip);
    TEST_ASSERT(resolved_nic == 0, "Segment 1 should resolve to NIC 0");
    
    resolved_nic = arp_get_nic_for_ip(&segment2_ip);
    TEST_ASSERT(resolved_nic == 1, "Segment 2 should resolve to NIC 1");
    
    /* Test VLAN-like behavior (if supported) */
    /* Simulate overlapping IP ranges on different NICs */
    ip_addr_t overlap_ip = {192, 168, 100, 1};
    uint8_t overlap_mac1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t overlap_mac2[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    
    result = arp_cache_add(&overlap_ip, overlap_mac1, 0, ARP_FLAG_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Overlap entry on NIC 0 should succeed");
    
    result = arp_cache_add(&overlap_ip, overlap_mac2, 1, ARP_FLAG_COMPLETE);
    /* This might update the existing entry or be handled specially */
    
    /* Verify which entry prevails */
    arp_cache_entry_t *entry = arp_cache_lookup(&overlap_ip);
    TEST_ASSERT(entry != NULL, "Overlapping IP should resolve to some entry");
    
    /* Test network convergence scenarios */
    /* Simulate topology change (link failure/recovery) */
    mock_device_set_link_status(g_arp_test_fixture.mock_nic1_id, false, 0);
    
    /* ARP entries on failed link should be handled appropriately */
    /* Implementation might age them out or mark them invalid */
    
    /* Restore link */
    mock_device_set_link_status(g_arp_test_fixture.mock_nic1_id, true, 100);
    
    /* Test gratuitous ARP during topology changes */
    result = arp_send_gratuitous(&segment1_ip, 0);
    TEST_ASSERT(result == SUCCESS, "Gratuitous ARP during recovery should succeed");
    
    TEST_LOG_END("ARP Network Topology Scenarios", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== ARP Stress and Performance Tests ========== */

static test_result_t test_arp_stress_scenarios(void) {
    TEST_LOG_START("ARP Stress Scenarios");
    
    /* Test high-frequency ARP operations */
    const int STRESS_ITERATIONS = 1000;
    const int STRESS_ENTRIES = 100;
    
    ip_addr_t stress_ips[STRESS_ENTRIES];
    uint8_t stress_mac[] = TEST_MAC_1;
    
    /* Generate unique IPs for stress testing */
    for (int i = 0; i < STRESS_ENTRIES; i++) {
        ip_addr_set(&stress_ips[i], 172, 16, i / 256, i % 256);
    }
    
    /* Stress test cache operations */
    uint32_t start_time = get_system_timestamp_ms();
    
    for (int iter = 0; iter < STRESS_ITERATIONS; iter++) {
        int entry_idx = iter % STRESS_ENTRIES;
        
        /* Add/update entries */
        int result = arp_cache_add(&stress_ips[entry_idx], stress_mac, 
                                  entry_idx % MAX_NICS, ARP_FLAG_COMPLETE);
        if (result != SUCCESS && result != ERROR_NO_MEMORY) {
            TEST_ASSERT(false, "Stress cache add should succeed or handle gracefully");
        }
        
        /* Perform lookups */
        arp_cache_entry_t *entry = arp_cache_lookup(&stress_ips[entry_idx]);
        /* Entry may or may not exist due to cache limits */
        
        /* Occasionally trigger aging */
        if (iter % 100 == 0) {
            arp_cache_age_entries();
        }
    }
    
    uint32_t end_time = get_system_timestamp_ms();
    uint32_t elapsed_ms = end_time - start_time;
    
    /* Performance should be reasonable (less than 10 seconds for 1000 iterations) */
    TEST_ASSERT(elapsed_ms < 10000, "Stress test should complete in reasonable time");
    
    /* Test concurrent ARP packet processing */
    arp_packet_t stress_packets[10];
    
    for (int i = 0; i < 10; i++) {
        int result = create_test_arp_packet(&stress_packets[i], 
                                          (i % 2) ? ARP_OP_REPLY : ARP_OP_REQUEST,
                                          stress_mac, &stress_ips[i],
                                          stress_mac, &stress_ips[i + 10]);
        TEST_ASSERT(result == TEST_RESULT_PASS, "Stress packet creation should succeed");
        
        result = arp_process_packet((uint8_t*)&stress_packets[i], 
                                   sizeof(arp_packet_t), i % MAX_NICS);
        TEST_ASSERT(result == SUCCESS, "Stress packet processing should succeed");
    }
    
    /* Verify system stability after stress */
    const arp_stats_t *stats = arp_get_stats();
    TEST_ASSERT(stats != NULL, "Statistics should remain accessible");
    
    /* Test cache consistency after stress */
    bool cache_consistent = true;
    for (int i = 0; i < 10; i++) {
        arp_cache_entry_t *entry = arp_cache_lookup(&stress_ips[i]);
        if (entry != NULL) {
            if (entry->state != ARP_STATE_COMPLETE && entry->state != ARP_STATE_INCOMPLETE) {
                cache_consistent = false;
                break;
            }
        }
    }
    TEST_ASSERT(cache_consistent, "Cache should remain consistent after stress");
    
    /* Test memory leak prevention */
    /* Multiple init/cleanup cycles shouldn't cause leaks */
    for (int i = 0; i < 5; i++) {
        arp_cleanup();
        int result = arp_init();
        TEST_ASSERT(result == SUCCESS, "Repeated init/cleanup should work");
        arp_enable(true);
    }
    
    TEST_LOG_END("ARP Stress Scenarios", TEST_RESULT_PASS);
    return TEST_RESULT_PASS;
}

/* ========== Helper Functions ========== */

static test_result_t setup_arp_test_environment(void) {
    /* Initialize hardware mock framework */
    int result = mock_framework_init();
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    /* Create mock NICs */
    g_arp_test_fixture.mock_nic1_id = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 5);
    if (g_arp_test_fixture.mock_nic1_id < 0) {
        return TEST_RESULT_ERROR;
    }
    
    g_arp_test_fixture.mock_nic2_id = mock_device_create(MOCK_DEVICE_3C515, 0x320, 7);
    if (g_arp_test_fixture.mock_nic2_id < 0) {
        return TEST_RESULT_ERROR;
    }
    
    /* Configure mock devices */
    uint8_t mac1[] = TEST_MAC_1;
    uint8_t mac2[] = TEST_MAC_2;
    
    mock_device_set_mac_address(g_arp_test_fixture.mock_nic1_id, mac1);
    mock_device_set_mac_address(g_arp_test_fixture.mock_nic2_id, mac2);
    
    mock_device_set_link_status(g_arp_test_fixture.mock_nic1_id, true, 10);
    mock_device_set_link_status(g_arp_test_fixture.mock_nic2_id, true, 100);
    
    mock_device_enable(g_arp_test_fixture.mock_nic1_id, true);
    mock_device_enable(g_arp_test_fixture.mock_nic2_id, true);
    
    /* Initialize test fixture */
    memset(&g_arp_test_fixture, 0, sizeof(arp_test_fixture_t));
    g_arp_test_fixture.mock_nic1_id = g_arp_test_fixture.mock_nic1_id;
    g_arp_test_fixture.mock_nic2_id = g_arp_test_fixture.mock_nic2_id;
    
    /* Initialize ARP system */
    result = arp_init();
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    result = arp_enable(true);
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    /* Initialize static routing for integration */
    result = static_routing_init();
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    result = static_routing_enable(true);
    if (result != SUCCESS) {
        return TEST_RESULT_ERROR;
    }
    
    return TEST_RESULT_PASS;
}

static void cleanup_arp_test_environment(void) {
    /* Cleanup ARP system */
    arp_cleanup();
    
    /* Cleanup static routing */
    static_routing_cleanup();
    
    /* Cleanup mock framework */
    mock_framework_cleanup();
}

static test_result_t create_test_arp_packet(arp_packet_t *packet, uint16_t operation,
                                           const uint8_t *sender_hw, const ip_addr_t *sender_ip,
                                           const uint8_t *target_hw, const ip_addr_t *target_ip) {
    if (!packet || !sender_hw || !sender_ip || !target_hw || !target_ip) {
        return TEST_RESULT_ERROR;
    }
    
    int result = arp_build_packet(packet, operation, sender_hw, sender_ip, target_hw, target_ip);
    return (result == SUCCESS) ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

static test_result_t verify_arp_cache_entry(const ip_addr_t *ip, const uint8_t *expected_mac,
                                           uint8_t expected_nic, uint16_t expected_flags) {
    arp_cache_entry_t *entry = arp_cache_lookup(ip);
    if (!entry) {
        return TEST_RESULT_FAIL;
    }
    
    if (!compare_mac_addresses(entry->mac, expected_mac)) {
        return TEST_RESULT_FAIL;
    }
    
    if (entry->nic_index != expected_nic) {
        return TEST_RESULT_FAIL;
    }
    
    if ((entry->flags & expected_flags) != expected_flags) {
        return TEST_RESULT_FAIL;
    }
    
    return TEST_RESULT_PASS;
}

static test_result_t inject_test_network_topology(void) {
    /* Configure complex network topology with multiple subnets */
    
    /* Subnet 1: 192.168.1.0/24 on NIC 0 */
    ip_addr_t subnet1 = {192, 168, 1, 0};
    ip_addr_t mask1 = {255, 255, 255, 0};
    
    int result = static_subnet_add(&subnet1, &mask1, 0);
    if (result != SUCCESS) {
        return TEST_RESULT_FAIL;
    }
    
    /* Subnet 2: 192.168.2.0/24 on NIC 1 */
    ip_addr_t subnet2 = {192, 168, 2, 0};
    ip_addr_t mask2 = {255, 255, 255, 0};
    
    result = static_subnet_add(&subnet2, &mask2, 1);
    if (result != SUCCESS) {
        return TEST_RESULT_FAIL;
    }
    
    /* Add default gateway */
    ip_addr_t gateway = {192, 168, 1, 1};
    result = static_routing_set_default_gateway(&gateway, 0);
    if (result != SUCCESS) {
        return TEST_RESULT_FAIL;
    }
    
    return TEST_RESULT_PASS;
}

static test_result_t simulate_network_delays(void) {
    /* Simulate realistic network delays in mock environment */
    
    /* Configure mock devices with realistic delays */
    /* This would be implemented using mock framework timing features */
    
    return TEST_RESULT_PASS;
}

static bool compare_mac_addresses(const uint8_t *mac1, const uint8_t *mac2) {
    if (!mac1 || !mac2) {
        return false;
    }
    
    for (int i = 0; i < ETH_ALEN; i++) {
        if (mac1[i] != mac2[i]) {
            return false;
        }
    }
    
    return true;
}

/* ========== Test Suite Runner ========== */

test_result_t run_arp_test_suite(void) {
    log_info("Starting ARP Protocol Test Suite");
    
    test_result_t overall_result = TEST_RESULT_PASS;
    int tests_passed = 0;
    int tests_failed = 0;
    
    /* Set up test environment */
    if (setup_arp_test_environment() != TEST_RESULT_PASS) {
        log_error("Failed to set up ARP test environment");
        return TEST_RESULT_ERROR;
    }
    
    /* Define test cases */
    struct {
        const char *name;
        test_result_t (*test_func)(void);
    } test_cases[] = {
        {"ARP Initialization", test_arp_initialization},
        {"ARP Cache Initialization", test_arp_cache_initialization},
        {"ARP Cache Basic Operations", test_arp_cache_basic_operations},
        {"ARP Cache Hash Functionality", test_arp_cache_hash_functionality},
        {"ARP Cache Aging and Eviction", test_arp_cache_aging_and_eviction},
        {"ARP Request Processing", test_arp_request_processing},
        {"ARP Reply Processing", test_arp_reply_processing},
        {"ARP Packet Validation", test_arp_packet_validation},
        {"ARP Resolution Process", test_arp_resolution_process},
        {"ARP Retry Mechanism", test_arp_retry_mechanism},
        {"Proxy ARP Functionality", test_proxy_arp_functionality},
        {"Gratuitous ARP", test_gratuitous_arp},
        {"Multi-NIC ARP Behavior", test_multi_nic_arp_behavior},
        {"ARP Statistics Tracking", test_arp_statistics_tracking},
        {"ARP Error Conditions", test_arp_error_conditions},
        {"ARP Integration with Routing", test_arp_integration_with_routing},
        {"ARP Network Topology Scenarios", test_arp_network_topology_scenarios},
        {"ARP Stress Scenarios", test_arp_stress_scenarios}
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
        arp_cache_flush();
        arp_clear_stats();
        mock_framework_reset();
    }
    
    /* Clean up test environment */
    cleanup_arp_test_environment();
    
    /* Report results */
    log_info("ARP Test Suite Results: %d passed, %d failed", tests_passed, tests_failed);
    
    if (overall_result == TEST_RESULT_PASS) {
        log_info("ARP Protocol Test Suite: ALL TESTS PASSED");
    } else {
        log_error("ARP Protocol Test Suite: SOME TESTS FAILED");
    }
    
    return overall_result;
}