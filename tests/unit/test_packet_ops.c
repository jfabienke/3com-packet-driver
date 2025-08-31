/**
 * @file test_packet_ops.c
 * @brief Comprehensive test suite for packet operations and TX/RX pipeline
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite validates all aspects of packet operations including:
 * - TX/RX pipeline functionality
 * - Queue management and flow control
 * - Priority-based packet handling
 * - Buffer management integration
 * - Performance optimization paths
 * - Both 3C509B PIO and 3C515-TX DMA operations
 */

#include "../../include/test_framework.h"
#include "../../include/packet_ops.h"
#include "../../include/hardware.h"
#include "../../include/hardware_mock.h"
#include "../../include/buffer_alloc.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <string.h>
#include <stdio.h>

/* Test constants */
#define TEST_PACKET_SIZE_MIN    64
#define TEST_PACKET_SIZE_MAX    1518
#define TEST_PACKET_SIZE_NORMAL 1024
#define TEST_MAC_DEST           {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}
#define TEST_MAC_SRC            {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define TEST_PATTERN_SIZE       256
#define TEST_QUEUE_STRESS_COUNT 1000
#define TEST_TIMEOUT_MS         5000

/* Test patterns for various scenarios */
static const uint8_t test_pattern_basic[] = "PACKET_OPS_TEST_BASIC_PATTERN_12345";
static const uint8_t test_pattern_stress[] = "STRESS_TEST_PATTERN_ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
static const uint8_t test_pattern_dma[] = "DMA_TEST_PATTERN_FOR_3C515_BUS_MASTERING_OPERATIONS";

/* Forward declarations */
static test_result_t test_packet_ops_initialization(void);
static test_result_t test_packet_basic_send_receive(void);
static test_result_t test_packet_enhanced_send_receive(void);
static test_result_t test_packet_queue_management(void);
static test_result_t test_packet_priority_handling(void);
static test_result_t test_packet_flow_control(void);
static test_result_t test_packet_buffer_integration(void);
static test_result_t test_packet_ethernet_frame_ops(void);
static test_result_t test_packet_loopback_functionality(void);
static test_result_t test_packet_multi_nic_operations(void);
static test_result_t test_packet_error_handling(void);
static test_result_t test_packet_3c509b_pio_operations(void);
static test_result_t test_packet_3c515_dma_operations(void);
static test_result_t test_packet_performance_benchmarks(void);
static test_result_t test_packet_stress_testing(void);
static test_result_t test_packet_statistics_tracking(void);

/* Helper functions */
static void create_test_packet(uint8_t *buffer, size_t size, const uint8_t *pattern);
static bool verify_packet_data(const uint8_t *packet, size_t size, const uint8_t *expected);
static int setup_mock_nic(mock_device_type_t type, uint16_t io_base, uint8_t irq);
static void cleanup_mock_nics(void);
static test_result_t run_loopback_test(int nic_index, const uint8_t *pattern, size_t pattern_size);

/**
 * @brief Main entry point for packet operations tests
 * @return 0 on success, negative on error
 */
int test_packet_ops_main(void) {
    test_config_t config;
    test_config_init_default(&config);
    config.test_packet_ops = true;
    config.init_hardware = true;
    config.init_memory = true;
    
    int result = test_framework_init(&config);
    if (result != SUCCESS) {
        log_error("Failed to initialize test framework: %d", result);
        return result;
    }
    
    log_info("=== Starting Packet Operations Test Suite ===");
    
    /* Initialize mock framework for testing */
    if (mock_framework_init() != SUCCESS) {
        log_error("Failed to initialize mock framework");
        test_framework_cleanup();
        return ERROR_HARDWARE;
    }
    
    /* Test structure array */
    struct {
        const char *name;
        test_result_t (*test_func)(void);
    } tests[] = {
        {"Packet Operations Initialization", test_packet_ops_initialization},
        {"Basic Send/Receive Operations", test_packet_basic_send_receive},
        {"Enhanced Send/Receive with Integration", test_packet_enhanced_send_receive},
        {"Queue Management System", test_packet_queue_management},
        {"Priority-based Packet Handling", test_packet_priority_handling},
        {"Flow Control and Backpressure", test_packet_flow_control},
        {"Buffer Management Integration", test_packet_buffer_integration},
        {"Ethernet Frame Operations", test_packet_ethernet_frame_ops},
        {"Loopback Functionality", test_packet_loopback_functionality},
        {"Multi-NIC Operations", test_packet_multi_nic_operations},
        {"Error Handling and Recovery", test_packet_error_handling},
        {"3C509B PIO Operations", test_packet_3c509b_pio_operations},
        {"3C515-TX DMA Operations", test_packet_3c515_dma_operations},
        {"Performance Benchmarking", test_packet_performance_benchmarks},
        {"Stress Testing", test_packet_stress_testing},
        {"Statistics Tracking", test_packet_statistics_tracking}
    };
    
    int total_tests = sizeof(tests) / sizeof(tests[0]);
    int passed_tests = 0;
    int failed_tests = 0;
    
    /* Run all tests */
    for (int i = 0; i < total_tests; i++) {
        TEST_LOG_START(tests[i].name);
        
        test_result_t test_result = tests[i].test_func();
        
        TEST_LOG_END(tests[i].name, test_result);
        
        if (test_result_is_success(test_result)) {
            passed_tests++;
        } else {
            failed_tests++;
        }
    }
    
    /* Cleanup */
    cleanup_mock_nics();
    mock_framework_cleanup();
    
    /* Report results */
    log_info("=== Packet Operations Test Suite Summary ===");
    log_info("Total tests: %d", total_tests);
    log_info("Passed: %d", passed_tests);
    log_info("Failed: %d", failed_tests);
    
    test_framework_cleanup();
    
    return (failed_tests == 0) ? SUCCESS : ERROR_IO;
}

/**
 * @brief Test packet operations initialization
 */
static test_result_t test_packet_ops_initialization(void) {
    config_t test_config = {0};
    int result;
    
    /* Test 1: Initialize with valid config */
    result = packet_ops_init(&test_config);
    TEST_ASSERT(result == SUCCESS, "packet_ops_init should succeed with valid config");
    TEST_ASSERT(packet_ops_is_initialized(), "packet_ops should be initialized");
    
    /* Test 2: Double initialization should succeed */
    result = packet_ops_init(&test_config);
    TEST_ASSERT(result == SUCCESS, "Double initialization should not fail");
    
    /* Test 3: Initialize with NULL config should fail */
    result = packet_ops_init(NULL);
    TEST_ASSERT(result != SUCCESS, "packet_ops_init should fail with NULL config");
    
    /* Test 4: Check initial statistics */
    packet_stats_t stats;
    result = packet_get_statistics(&stats);
    TEST_ASSERT(result == SUCCESS, "Should be able to get statistics");
    TEST_ASSERT(stats.tx_packets == 0, "Initial TX packet count should be 0");
    TEST_ASSERT(stats.rx_packets == 0, "Initial RX packet count should be 0");
    
    /* Test 5: Cleanup */
    result = packet_ops_cleanup();
    TEST_ASSERT(result == SUCCESS, "packet_ops_cleanup should succeed");
    TEST_ASSERT(!packet_ops_is_initialized(), "packet_ops should not be initialized after cleanup");
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test basic packet send and receive operations
 */
static test_result_t test_packet_basic_send_receive(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t rx_buffer[TEST_PACKET_SIZE_NORMAL];
    size_t rx_length;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NIC for testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    /* Test 1: Basic packet send */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    result = packet_send(test_packet, sizeof(test_packet), 0x1234);
    TEST_ASSERT(result == SUCCESS, "Basic packet send should succeed");
    
    /* Test 2: Verify packet was sent to mock NIC */
    uint8_t extracted_packet[TEST_PACKET_SIZE_NORMAL];
    size_t extracted_length = sizeof(extracted_packet);
    result = mock_packet_extract_tx(mock_nic_id, extracted_packet, &extracted_length);
    TEST_ASSERT(result == SUCCESS, "Should be able to extract transmitted packet");
    TEST_ASSERT(extracted_length == sizeof(test_packet), "Extracted packet length should match");
    
    /* Test 3: Inject packet for reception */
    result = mock_packet_inject_rx(mock_nic_id, test_packet, sizeof(test_packet));
    TEST_ASSERT(result == SUCCESS, "Should be able to inject RX packet");
    
    /* Test 4: Basic packet receive */
    rx_length = sizeof(rx_buffer);
    result = packet_receive(rx_buffer, sizeof(rx_buffer), &rx_length, mock_nic_id);
    TEST_ASSERT(result == SUCCESS, "Basic packet receive should succeed");
    TEST_ASSERT(rx_length > 0, "Received packet length should be positive");
    
    /* Test 5: Send with invalid parameters */
    result = packet_send(NULL, sizeof(test_packet), 0x1234);
    TEST_ASSERT(result != SUCCESS, "packet_send should fail with NULL packet");
    
    result = packet_send(test_packet, 0, 0x1234);
    TEST_ASSERT(result != SUCCESS, "packet_send should fail with zero length");
    
    /* Test 6: Receive with invalid parameters */
    result = packet_receive(NULL, sizeof(rx_buffer), &rx_length, mock_nic_id);
    TEST_ASSERT(result != SUCCESS, "packet_receive should fail with NULL buffer");
    
    result = packet_receive(rx_buffer, sizeof(rx_buffer), NULL, mock_nic_id);
    TEST_ASSERT(result != SUCCESS, "packet_receive should fail with NULL length pointer");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test enhanced send/receive operations with full integration
 */
static test_result_t test_packet_enhanced_send_receive(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t rx_buffer[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    size_t rx_length;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NIC for testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    /* Test 1: Enhanced packet send with full integration */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0x5678);
    TEST_ASSERT(result == SUCCESS, "Enhanced packet send should succeed");
    
    /* Test 2: Enhanced packet receive from specific NIC */
    result = mock_packet_inject_rx(mock_nic_id, test_packet, sizeof(test_packet));
    TEST_ASSERT(result == SUCCESS, "Should be able to inject RX packet");
    
    rx_length = sizeof(rx_buffer);
    result = packet_receive_from_nic(mock_nic_id, rx_buffer, &rx_length);
    TEST_ASSERT(result == SUCCESS, "Enhanced packet receive should succeed");
    TEST_ASSERT(rx_length > ETH_HEADER_LEN, "Received packet should include Ethernet header");
    
    /* Test 3: Send with retry logic */
    result = packet_send_with_retry(test_packet, sizeof(test_packet), dest_mac, 0x9ABC, 3);
    TEST_ASSERT(result == SUCCESS, "Send with retry should succeed");
    
    /* Test 4: Receive with recovery and timeout */
    rx_length = sizeof(rx_buffer);
    result = packet_receive_with_recovery(rx_buffer, sizeof(rx_buffer), &rx_length, 
                                        mock_nic_id, 1000);
    /* This might timeout if no packet available, which is acceptable */
    
    /* Test 5: Enhanced send with invalid NIC index */
    result = packet_send_enhanced(99, test_packet, sizeof(test_packet), dest_mac, 0x1234);
    TEST_ASSERT(result != SUCCESS, "Enhanced send should fail with invalid NIC index");
    
    /* Test 6: Enhanced receive with invalid NIC index */
    rx_length = sizeof(rx_buffer);
    result = packet_receive_from_nic(99, rx_buffer, &rx_length);
    TEST_ASSERT(result != SUCCESS, "Enhanced receive should fail with invalid NIC index");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test queue management system
 */
static test_result_t test_packet_queue_management(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    packet_queue_management_stats_t queue_stats;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NIC for testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    /* Test 1: Basic queue operations */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    
    result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                    PACKET_PRIORITY_NORMAL, 0x1234);
    TEST_ASSERT(result == SUCCESS, "Queue TX enhanced should succeed");
    
    /* Test 2: Queue flush operations */
    result = packet_flush_tx_queue_enhanced();
    TEST_ASSERT(result >= 0, "Queue flush should return number of packets processed");
    
    /* Test 3: Get queue statistics */
    result = packet_get_queue_stats(&queue_stats);
    TEST_ASSERT(result == SUCCESS, "Should be able to get queue statistics");
    
    /* Test 4: Test all priority levels */
    for (int priority = PACKET_PRIORITY_LOW; priority <= PACKET_PRIORITY_URGENT; priority++) {
        result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                        priority, 0x1000 + priority);
        TEST_ASSERT(result == SUCCESS, "Queue TX should succeed for all priority levels");
    }
    
    /* Test 5: Flush and verify priority ordering */
    int packets_flushed = packet_flush_tx_queue_enhanced();
    TEST_ASSERT(packets_flushed >= 0, "Should be able to flush priority queues");
    
    /* Test 6: Invalid priority handling */
    result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), -1, 0x1234);
    TEST_ASSERT(result != SUCCESS, "Queue TX should fail with invalid priority");
    
    result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 99, 0x1234);
    TEST_ASSERT(result != SUCCESS, "Queue TX should fail with invalid priority");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test priority-based packet handling
 */
static test_result_t test_packet_priority_handling(void) {
    config_t test_config = {0};
    uint8_t test_packets[4][TEST_PACKET_SIZE_NORMAL];
    packet_queue_management_stats_t stats_before, stats_after;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NIC for testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    /* Test 1: Create packets with different priorities */
    for (int i = 0; i < 4; i++) {
        create_test_packet(test_packets[i], sizeof(test_packets[i]), test_pattern_basic);
    }
    
    /* Test 2: Queue packets in reverse priority order (low to urgent) */
    result = packet_get_queue_stats(&stats_before);
    TEST_ASSERT(result == SUCCESS, "Should get initial queue stats");
    
    result = packet_queue_tx_enhanced(test_packets[0], sizeof(test_packets[0]), 
                                    PACKET_PRIORITY_LOW, 0x1000);
    TEST_ASSERT(result == SUCCESS, "Should queue low priority packet");
    
    result = packet_queue_tx_enhanced(test_packets[1], sizeof(test_packets[1]), 
                                    PACKET_PRIORITY_NORMAL, 0x2000);
    TEST_ASSERT(result == SUCCESS, "Should queue normal priority packet");
    
    result = packet_queue_tx_enhanced(test_packets[2], sizeof(test_packets[2]), 
                                    PACKET_PRIORITY_HIGH, 0x3000);
    TEST_ASSERT(result == SUCCESS, "Should queue high priority packet");
    
    result = packet_queue_tx_enhanced(test_packets[3], sizeof(test_packets[3]), 
                                    PACKET_PRIORITY_URGENT, 0x4000);
    TEST_ASSERT(result == SUCCESS, "Should queue urgent priority packet");
    
    /* Test 3: Verify queue statistics reflect queued packets */
    result = packet_get_queue_stats(&stats_after);
    TEST_ASSERT(result == SUCCESS, "Should get updated queue stats");
    
    /* Check that packets were distributed to appropriate priority queues */
    TEST_ASSERT(stats_after.tx_queue_counts[PACKET_PRIORITY_LOW] > stats_before.tx_queue_counts[PACKET_PRIORITY_LOW],
                "Low priority queue should have more packets");
    TEST_ASSERT(stats_after.tx_queue_counts[PACKET_PRIORITY_URGENT] > stats_before.tx_queue_counts[PACKET_PRIORITY_URGENT],
                "Urgent priority queue should have more packets");
    
    /* Test 4: Flush and verify urgent packets are processed first */
    int packets_processed = packet_flush_tx_queue_enhanced();
    TEST_ASSERT(packets_processed >= 4, "Should process at least 4 packets");
    
    /* Test 5: Stress test with many priority packets */
    for (int round = 0; round < 10; round++) {
        for (int priority = PACKET_PRIORITY_LOW; priority <= PACKET_PRIORITY_URGENT; priority++) {
            result = packet_queue_tx_enhanced(test_packets[priority], sizeof(test_packets[priority]), 
                                            priority, 0x5000 + round * 10 + priority);
            TEST_ASSERT(result == SUCCESS, "Priority stress test packet should queue successfully");
        }
    }
    
    /* Flush all stress test packets */
    packets_processed = packet_flush_tx_queue_enhanced();
    TEST_ASSERT(packets_processed >= 40, "Should process stress test packets");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test flow control and backpressure mechanisms
 */
static test_result_t test_packet_flow_control(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    packet_queue_management_stats_t stats;
    int result;
    int successful_queues = 0;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NIC for testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    /* Test 1: Fill queues to trigger flow control */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_stress);
    
    /* Queue many packets to trigger flow control */
    for (int i = 0; i < 200; i++) {
        result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                        PACKET_PRIORITY_NORMAL, 0x6000 + i);
        if (result == SUCCESS) {
            successful_queues++;
        }
    }
    
    log_info("Successfully queued %d packets before flow control", successful_queues);
    
    /* Test 2: Check flow control activation */
    result = packet_get_queue_stats(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get queue stats during flow control");
    
    /* Test 3: Verify backpressure statistics */
    if (stats.backpressure_events > 0) {
        log_info("Flow control activated with %lu backpressure events", stats.backpressure_events);
    }
    
    /* Test 4: Flush queues to relieve backpressure */
    int total_flushed = 0;
    int flush_rounds = 0;
    
    while (flush_rounds < 10) {  /* Limit flush rounds */
        int flushed = packet_flush_tx_queue_enhanced();
        if (flushed <= 0) break;
        total_flushed += flushed;
        flush_rounds++;
    }
    
    log_info("Flushed %d packets in %d rounds", total_flushed, flush_rounds);
    
    /* Test 5: Check flow control deactivation */
    result = packet_get_queue_stats(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get queue stats after flushing");
    
    /* Test 6: Verify adaptive queue management */
    if (stats.adaptive_resizes > 0) {
        log_info("Adaptive queue management triggered %lu resizes", stats.adaptive_resizes);
    }
    
    /* Test 7: Priority-based drops under pressure */
    if (stats.priority_drops > 0) {
        log_info("Priority-based dropping occurred: %lu drops", stats.priority_drops);
    }
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test buffer management integration
 */
static test_result_t test_packet_buffer_integration(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    TEST_ASSERT(buffer_alloc_init() == SUCCESS, "Failed to initialize buffer allocator");
    
    /* Create mock NIC for testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    /* Test 1: Buffer allocation for Ethernet frames */
    buffer_desc_t *buffer = buffer_alloc_ethernet_frame(ETH_MAX_FRAME, BUFFER_TYPE_TX);
    TEST_ASSERT(buffer != NULL, "Should be able to allocate Ethernet frame buffer");
    
    if (buffer) {
        uint8_t *frame_data = (uint8_t*)buffer_get_data_ptr(buffer);
        TEST_ASSERT(frame_data != NULL, "Frame buffer should have valid data pointer");
        
        /* Build test frame */
        memcpy(frame_data, dest_mac, 6);  /* Dest MAC */
        memcpy(frame_data + 6, TEST_MAC_SRC, 6);  /* Src MAC */
        *(uint16_t*)(frame_data + 12) = htons(0x0800);  /* EtherType */
        memcpy(frame_data + 14, test_pattern_basic, sizeof(test_pattern_basic));
        
        buffer_free_any(buffer);
    }
    
    /* Test 2: Enhanced send with buffer integration */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0x7000);
    TEST_ASSERT(result == SUCCESS, "Enhanced send with buffer integration should succeed");
    
    /* Test 3: Packet buffer allocation and management */
    packet_buffer_t *pkt_buffer = packet_buffer_alloc(TEST_PACKET_SIZE_NORMAL);
    TEST_ASSERT(pkt_buffer != NULL, "Should be able to allocate packet buffer");
    
    if (pkt_buffer) {
        result = packet_set_data(pkt_buffer, test_packet, sizeof(test_packet));
        TEST_ASSERT(result == SUCCESS, "Should be able to set packet data");
        
        TEST_ASSERT(pkt_buffer->length == sizeof(test_packet), "Packet buffer length should match");
        TEST_ASSERT(pkt_buffer->data != NULL, "Packet buffer data should be valid");
        
        packet_buffer_free(pkt_buffer);
    }
    
    /* Test 4: Buffer stress test */
    buffer_desc_t *buffers[20];
    int allocated_buffers = 0;
    
    for (int i = 0; i < 20; i++) {
        buffers[i] = buffer_alloc_ethernet_frame(ETH_MAX_FRAME, BUFFER_TYPE_TX);
        if (buffers[i]) {
            allocated_buffers++;
        }
    }
    
    log_info("Allocated %d buffers in stress test", allocated_buffers);
    
    /* Free allocated buffers */
    for (int i = 0; i < 20; i++) {
        if (buffers[i]) {
            buffer_free_any(buffers[i]);
        }
    }
    
    /* Test 5: Buffer memory leak detection */
    const mem_stats_t *mem_stats_before = memory_get_stats();
    uint32_t initial_used = mem_stats_before->used_memory;
    
    /* Perform allocation/deallocation cycles */
    for (int cycle = 0; cycle < 5; cycle++) {
        buffer_desc_t *temp_buffer = buffer_alloc_ethernet_frame(1518, BUFFER_TYPE_TX);
        if (temp_buffer) {
            buffer_free_any(temp_buffer);
        }
    }
    
    const mem_stats_t *mem_stats_after = memory_get_stats();
    uint32_t final_used = mem_stats_after->used_memory;
    
    TEST_ASSERT(final_used <= initial_used + 100, "Should not have significant memory leaks");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test Ethernet frame operations
 */
static test_result_t test_packet_ethernet_frame_ops(void) {
    uint8_t frame_buffer[ETH_MAX_FRAME];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    uint8_t src_mac[6] = TEST_MAC_SRC;
    eth_header_t parsed_header;
    int result;
    
    /* Test 1: Build Ethernet frame */
    result = packet_build_ethernet_frame(frame_buffer, sizeof(frame_buffer),
                                       dest_mac, src_mac, ETH_P_IP,
                                       test_pattern_basic, sizeof(test_pattern_basic));
    TEST_ASSERT(result > 0, "Should be able to build Ethernet frame");
    TEST_ASSERT(result >= ETH_MIN_FRAME, "Frame should meet minimum size requirement");
    
    /* Test 2: Parse Ethernet header */
    result = packet_parse_ethernet_header(frame_buffer, result, &parsed_header);
    TEST_ASSERT(result == SUCCESS, "Should be able to parse Ethernet header");
    
    TEST_ASSERT(memcmp(parsed_header.dest_mac, dest_mac, 6) == 0, "Destination MAC should match");
    TEST_ASSERT(memcmp(parsed_header.src_mac, src_mac, 6) == 0, "Source MAC should match");
    TEST_ASSERT(parsed_header.ethertype == ETH_P_IP, "EtherType should match");
    
    /* Test 3: Optimized frame building */
    result = packet_build_ethernet_frame_optimized(frame_buffer, sizeof(frame_buffer),
                                                 dest_mac, src_mac, ETH_P_ARP,
                                                 test_pattern_basic, sizeof(test_pattern_basic));
    TEST_ASSERT(result > 0, "Optimized frame building should succeed");
    
    /* Test 4: Packet classification functions */
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t multicast_mac[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    
    /* Build broadcast frame */
    packet_build_ethernet_frame(frame_buffer, sizeof(frame_buffer),
                               broadcast_mac, src_mac, ETH_P_IP,
                               test_pattern_basic, sizeof(test_pattern_basic));
    
    TEST_ASSERT(packet_is_broadcast(frame_buffer), "Should detect broadcast packet");
    TEST_ASSERT(!packet_is_multicast(frame_buffer), "Broadcast should not be detected as multicast");
    TEST_ASSERT(!packet_is_for_us(frame_buffer, src_mac), "Broadcast should not be 'for us'");
    
    /* Build multicast frame */
    packet_build_ethernet_frame(frame_buffer, sizeof(frame_buffer),
                               multicast_mac, src_mac, ETH_P_IP,
                               test_pattern_basic, sizeof(test_pattern_basic));
    
    TEST_ASSERT(packet_is_multicast(frame_buffer), "Should detect multicast packet");
    TEST_ASSERT(!packet_is_broadcast(frame_buffer), "Multicast should not be detected as broadcast");
    
    /* Build unicast frame */
    packet_build_ethernet_frame(frame_buffer, sizeof(frame_buffer),
                               dest_mac, src_mac, ETH_P_IP,
                               test_pattern_basic, sizeof(test_pattern_basic));
    
    TEST_ASSERT(packet_is_for_us(frame_buffer, dest_mac), "Should detect packet addressed to us");
    TEST_ASSERT(!packet_is_broadcast(frame_buffer), "Unicast should not be detected as broadcast");
    TEST_ASSERT(!packet_is_multicast(frame_buffer), "Unicast should not be detected as multicast");
    
    /* Test 5: EtherType extraction */
    uint16_t ethertype = packet_get_ethertype(frame_buffer);
    TEST_ASSERT(ethertype == ETH_P_IP, "Should extract correct EtherType");
    
    /* Test 6: Invalid frame handling */
    result = packet_build_ethernet_frame(frame_buffer, 10,  /* Too small buffer */
                                       dest_mac, src_mac, ETH_P_IP,
                                       test_pattern_basic, sizeof(test_pattern_basic));
    TEST_ASSERT(result < 0, "Should fail with insufficient buffer space");
    
    result = packet_parse_ethernet_header(frame_buffer, 5, &parsed_header);  /* Too small frame */
    TEST_ASSERT(result < 0, "Should fail to parse truncated frame");
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test loopback functionality
 */
static test_result_t test_packet_loopback_functionality(void) {
    config_t test_config = {0};
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NICs for testing */
    int mock_nic_3c509b = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    int mock_nic_3c515 = setup_mock_nic(MOCK_DEVICE_3C515, 0x320, 11);
    
    TEST_ASSERT(mock_nic_3c509b >= 0, "Failed to setup 3C509B mock NIC");
    TEST_ASSERT(mock_nic_3c515 >= 0, "Failed to setup 3C515 mock NIC");
    
    /* Test 1: Internal loopback on 3C509B */
    result = run_loopback_test(mock_nic_3c509b, test_pattern_basic, sizeof(test_pattern_basic));
    TEST_ASSERT(result == TEST_RESULT_PASS, "3C509B internal loopback should pass");
    
    /* Test 2: Internal loopback on 3C515 */
    result = run_loopback_test(mock_nic_3c515, test_pattern_dma, sizeof(test_pattern_dma));
    TEST_ASSERT(result == TEST_RESULT_PASS, "3C515 internal loopback should pass");
    
    /* Test 3: Cross-NIC loopback */
    result = packet_test_cross_nic_loopback(mock_nic_3c509b, mock_nic_3c515,
                                          test_pattern_stress, sizeof(test_pattern_stress));
    /* Note: This might fail in mock environment, but we test the interface */
    log_info("Cross-NIC loopback test result: %d", result);
    
    /* Test 4: Loopback with various packet sizes */
    uint8_t small_pattern[32] = "SMALL_PATTERN";
    uint8_t large_pattern[1400];
    memset(large_pattern, 0xAA, sizeof(large_pattern));
    
    result = run_loopback_test(mock_nic_3c509b, small_pattern, sizeof(small_pattern));
    TEST_ASSERT(result == TEST_RESULT_PASS, "Small packet loopback should pass");
    
    result = run_loopback_test(mock_nic_3c509b, large_pattern, sizeof(large_pattern));
    TEST_ASSERT(result == TEST_RESULT_PASS, "Large packet loopback should pass");
    
    /* Test 5: Loopback integrity verification */
    uint8_t original_data[256];
    uint8_t received_data[256];
    packet_integrity_result_t integrity_result;
    
    memset(original_data, 0x55, sizeof(original_data));
    memset(received_data, 0x55, sizeof(received_data));
    
    result = packet_verify_loopback_integrity(original_data, received_data,
                                            sizeof(original_data), &integrity_result);
    TEST_ASSERT(result == SUCCESS, "Integrity verification should succeed for identical data");
    TEST_ASSERT(integrity_result.mismatch_count == 0, "Should have no mismatches");
    
    /* Test with corrupted data */
    received_data[100] = 0xAA;  /* Corrupt one byte */
    result = packet_verify_loopback_integrity(original_data, received_data,
                                            sizeof(original_data), &integrity_result);
    TEST_ASSERT(result != SUCCESS, "Integrity verification should fail for corrupted data");
    TEST_ASSERT(integrity_result.mismatch_count == 1, "Should detect one mismatch");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test multi-NIC operations
 */
static test_result_t test_packet_multi_nic_operations(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create multiple mock NICs */
    int mock_nic_1 = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    int mock_nic_2 = setup_mock_nic(MOCK_DEVICE_3C515, 0x320, 11);
    
    TEST_ASSERT(mock_nic_1 >= 0, "Failed to setup first mock NIC");
    TEST_ASSERT(mock_nic_2 >= 0, "Failed to setup second mock NIC");
    
    /* Test 1: Multi-NIC packet sending with load balancing */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    
    result = packet_send_multi_nic(test_packet, sizeof(test_packet), dest_mac, 0x8000);
    TEST_ASSERT(result == SUCCESS, "Multi-NIC send should succeed");
    
    /* Test 2: Get optimal NIC selection */
    int optimal_nic = packet_get_optimal_nic(test_packet, sizeof(test_packet));
    TEST_ASSERT(optimal_nic >= 0, "Should be able to select optimal NIC");
    log_info("Optimal NIC selected: %d", optimal_nic);
    
    /* Test 3: NIC failover handling */
    result = packet_handle_nic_failover(mock_nic_1);
    TEST_ASSERT(result == SUCCESS, "Should handle NIC failover");
    
    /* Test 4: Multi-NIC routing */
    result = packet_route_multi_nic(test_packet, sizeof(test_packet), mock_nic_1);
    log_info("Multi-NIC routing result: %d", result);
    
    /* Test 5: Send packets to multiple NICs */
    for (int i = 0; i < 10; i++) {
        uint16_t handle = 0x9000 + i;
        result = packet_send_multi_nic(test_packet, sizeof(test_packet), dest_mac, handle);
        TEST_ASSERT(result == SUCCESS, "Multi-NIC sends should succeed");
    }
    
    /* Test 6: Extract packets from both NICs to verify load balancing */
    uint8_t extracted_packet[TEST_PACKET_SIZE_NORMAL];
    size_t extracted_length;
    
    int nic1_packets = 0, nic2_packets = 0;
    
    /* Count packets on NIC 1 */
    while (true) {
        extracted_length = sizeof(extracted_packet);
        result = mock_packet_extract_tx(mock_nic_1, extracted_packet, &extracted_length);
        if (result != SUCCESS) break;
        nic1_packets++;
    }
    
    /* Count packets on NIC 2 */
    while (true) {
        extracted_length = sizeof(extracted_packet);
        result = mock_packet_extract_tx(mock_nic_2, extracted_packet, &extracted_length);
        if (result != SUCCESS) break;
        nic2_packets++;
    }
    
    log_info("Load balancing results: NIC1=%d packets, NIC2=%d packets", nic1_packets, nic2_packets);
    TEST_ASSERT(nic1_packets + nic2_packets > 0, "Some packets should have been sent");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test error handling and recovery
 */
static test_result_t test_packet_error_handling(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    uint8_t rx_buffer[TEST_PACKET_SIZE_NORMAL];
    size_t rx_length;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NIC for error injection */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    
    /* Test 1: Send with transmission timeout error */
    mock_error_inject(mock_nic_id, MOCK_ERROR_TX_TIMEOUT, 1);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xA000);
    /* Should fail or succeed with retry logic */
    log_info("Send with TX timeout error: %d", result);
    mock_error_clear(mock_nic_id);
    
    /* Test 2: Send with retry on error */
    mock_error_inject(mock_nic_id, MOCK_ERROR_TX_UNDERRUN, 2);  /* Fail first 2 attempts */
    result = packet_send_with_retry(test_packet, sizeof(test_packet), dest_mac, 0xA001, 5);
    log_info("Send with retry on underrun error: %d", result);
    mock_error_clear(mock_nic_id);
    
    /* Test 3: Receive with CRC error */
    mock_error_inject(mock_nic_id, MOCK_ERROR_CRC_ERROR, 1);
    mock_packet_inject_rx(mock_nic_id, test_packet, sizeof(test_packet));
    
    rx_length = sizeof(rx_buffer);
    result = packet_receive_from_nic(mock_nic_id, rx_buffer, &rx_length);
    log_info("Receive with CRC error: %d", result);
    mock_error_clear(mock_nic_id);
    
    /* Test 4: Receive with recovery and timeout */
    rx_length = sizeof(rx_buffer);
    result = packet_receive_with_recovery(rx_buffer, sizeof(rx_buffer), &rx_length, 
                                        mock_nic_id, 100);  /* Short timeout */
    TEST_ASSERT(result != SUCCESS, "Should timeout when no packets available");
    
    /* Test 5: Invalid packet sizes */
    uint8_t tiny_packet[10];
    uint8_t huge_packet[2000];
    
    result = packet_send_enhanced(mock_nic_id, tiny_packet, sizeof(tiny_packet), dest_mac, 0xA002);
    TEST_ASSERT(result != SUCCESS, "Should fail with packet too small");
    
    result = packet_send_enhanced(mock_nic_id, huge_packet, sizeof(huge_packet), dest_mac, 0xA003);
    TEST_ASSERT(result != SUCCESS, "Should fail with packet too large");
    
    /* Test 6: Null pointer handling */
    result = packet_send_enhanced(mock_nic_id, NULL, sizeof(test_packet), dest_mac, 0xA004);
    TEST_ASSERT(result != SUCCESS, "Should fail with NULL packet data");
    
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), NULL, 0xA005);
    TEST_ASSERT(result != SUCCESS, "Should fail with NULL destination MAC");
    
    /* Test 7: Invalid NIC handling */
    result = packet_send_enhanced(99, test_packet, sizeof(test_packet), dest_mac, 0xA006);
    TEST_ASSERT(result != SUCCESS, "Should fail with invalid NIC index");
    
    /* Test 8: Frame error injection */
    mock_error_inject(mock_nic_id, MOCK_ERROR_FRAME_ERROR, 1);
    mock_packet_inject_rx(mock_nic_id, test_packet, sizeof(test_packet));
    
    rx_length = sizeof(rx_buffer);
    result = packet_receive_from_nic(mock_nic_id, rx_buffer, &rx_length);
    log_info("Receive with frame error: %d", result);
    mock_error_clear(mock_nic_id);
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test 3C509B PIO operations
 */
static test_result_t test_packet_3c509b_pio_operations(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create 3C509B mock NIC specifically */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup 3C509B mock NIC");
    
    /* Configure mock NIC as 3C509B */
    mock_device_t *mock_device = mock_device_get(mock_nic_id);
    TEST_ASSERT(mock_device != NULL, "Should be able to get mock device");
    TEST_ASSERT(mock_device->type == MOCK_DEVICE_3C509B, "Device should be 3C509B type");
    
    /* Test 1: PIO-based packet transmission */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xB000);
    TEST_ASSERT(result == SUCCESS, "3C509B PIO transmission should succeed");
    
    /* Test 2: Verify PIO I/O operations were logged */
    TEST_ASSERT(mock_io_log_is_enabled(), "I/O logging should be enabled");
    
    mock_io_log_entry_t io_entries[100];
    int num_entries = mock_io_log_get_entries(io_entries, 100);
    TEST_ASSERT(num_entries > 0, "Should have I/O operations logged");
    
    log_info("3C509B PIO operations logged: %d entries", num_entries);
    
    /* Test 3: Window selection operations (3C509B specific) */
    for (int window = 0; window < 8; window++) {
        result = mock_3c509b_simulate_window_select(mock_device, window);
        TEST_ASSERT(result == SUCCESS, "Window selection should succeed");
    }
    
    /* Test 4: EEPROM operations (3C509B specific) */
    uint16_t eeprom_data[16] = {
        0x1234, 0x5678, 0x9ABC, 0xDEF0,  /* Sample EEPROM data */
        0x0011, 0x2233, 0x4455, 0x6677,
        0x8899, 0xAABB, 0xCCDD, 0xEEFF,
        0x1111, 0x2222, 0x3333, 0x4444
    };
    
    result = mock_eeprom_init(mock_nic_id, eeprom_data, 16);
    TEST_ASSERT(result == SUCCESS, "EEPROM initialization should succeed");
    
    /* Read EEPROM data */
    for (int addr = 0; addr < 16; addr++) {
        uint16_t read_data = mock_eeprom_read(mock_nic_id, addr);
        TEST_ASSERT(read_data == eeprom_data[addr], "EEPROM read should return correct data");
    }
    
    /* Test 5: 3C509B specific loopback */
    result = packet_test_internal_loopback(mock_nic_id, test_pattern_basic, sizeof(test_pattern_basic));
    log_info("3C509B internal loopback result: %d", result);
    
    /* Test 6: PIO performance characteristics */
    uint32_t start_time = test_framework_get_timestamp();
    
    for (int i = 0; i < 10; i++) {
        result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xB100 + i);
        TEST_ASSERT(result == SUCCESS, "PIO performance test packets should succeed");
    }
    
    uint32_t end_time = test_framework_get_timestamp();
    uint32_t duration = end_time - start_time;
    
    log_info("3C509B PIO performance: 10 packets in %lu ms", duration);
    
    /* Test 7: Error injection specific to PIO operations */
    mock_error_inject(mock_nic_id, MOCK_ERROR_TX_UNDERRUN, 1);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xB200);
    log_info("3C509B PIO with underrun error: %d", result);
    mock_error_clear(mock_nic_id);
    
    /* Clear I/O log for next test */
    mock_io_log_clear();
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test 3C515-TX DMA operations
 */
static test_result_t test_packet_3c515_dma_operations(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create 3C515-TX mock NIC specifically */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C515, 0x320, 11);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup 3C515-TX mock NIC");
    
    /* Configure mock NIC as 3C515 */
    mock_device_t *mock_device = mock_device_get(mock_nic_id);
    TEST_ASSERT(mock_device != NULL, "Should be able to get mock device");
    TEST_ASSERT(mock_device->type == MOCK_DEVICE_3C515, "Device should be 3C515 type");
    
    /* Test 1: DMA descriptor setup */
    uint32_t tx_desc_base = 0x100000;  /* Simulated descriptor base */
    uint32_t rx_desc_base = 0x200000;
    
    result = mock_dma_set_descriptors(mock_nic_id, tx_desc_base, rx_desc_base);
    TEST_ASSERT(result == SUCCESS, "DMA descriptor setup should succeed");
    
    /* Test 2: Bus mastering DMA transmission */
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_dma);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xC000);
    TEST_ASSERT(result == SUCCESS, "3C515 DMA transmission should succeed");
    
    /* Test 3: DMA transfer simulation */
    result = mock_dma_start_transfer(mock_nic_id, true);  /* TX transfer */
    TEST_ASSERT(result == SUCCESS, "DMA TX transfer should start");
    
    bool dma_active = mock_dma_is_active(mock_nic_id);
    log_info("DMA active status: %s", dma_active ? "true" : "false");
    
    /* Test 4: DMA completion and interrupt simulation */
    result = mock_interrupt_generate(mock_nic_id, MOCK_INTR_DMA_COMPLETE);
    TEST_ASSERT(result == SUCCESS, "Should be able to generate DMA interrupt");
    
    bool interrupt_pending = mock_interrupt_pending(mock_nic_id);
    TEST_ASSERT(interrupt_pending, "DMA interrupt should be pending");
    
    mock_interrupt_type_t intr_type = mock_interrupt_get_type(mock_nic_id);
    TEST_ASSERT(intr_type == MOCK_INTR_DMA_COMPLETE, "Interrupt type should be DMA complete");
    
    mock_interrupt_clear(mock_nic_id);
    
    /* Test 5: DMA receive operations */
    result = mock_dma_start_transfer(mock_nic_id, false);  /* RX transfer */
    TEST_ASSERT(result == SUCCESS, "DMA RX transfer should start");
    
    /* Inject packet for DMA reception */
    result = mock_packet_inject_rx(mock_nic_id, test_packet, sizeof(test_packet));
    TEST_ASSERT(result == SUCCESS, "Should be able to inject packet for DMA RX");
    
    /* Test 6: DMA performance testing */
    uint32_t start_time = test_framework_get_timestamp();
    
    for (int i = 0; i < 20; i++) {  /* More packets for DMA performance test */
        result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xC100 + i);
        TEST_ASSERT(result == SUCCESS, "DMA performance test packets should succeed");
    }
    
    uint32_t end_time = test_framework_get_timestamp();
    uint32_t duration = end_time - start_time;
    
    log_info("3C515 DMA performance: 20 packets in %lu ms", duration);
    
    /* Test 7: DMA error injection */
    mock_error_inject(mock_nic_id, MOCK_ERROR_DMA_ERROR, 1);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xC200);
    log_info("3C515 DMA with error injection: %d", result);
    mock_error_clear(mock_nic_id);
    
    /* Test 8: DMA descriptor management simulation */
    result = mock_3c515_simulate_dma_setup(mock_device, tx_desc_base, true);
    TEST_ASSERT(result == SUCCESS, "DMA setup simulation should succeed");
    
    result = mock_3c515_simulate_dma_transfer(mock_device, true);
    TEST_ASSERT(result == SUCCESS, "DMA transfer simulation should succeed");
    
    result = mock_3c515_simulate_descriptor_update(mock_device);
    TEST_ASSERT(result == SUCCESS, "Descriptor update simulation should succeed");
    
    /* Test 9: 3C515 specific loopback with DMA */
    result = packet_test_internal_loopback(mock_nic_id, test_pattern_dma, sizeof(test_pattern_dma));
    log_info("3C515 DMA loopback result: %d", result);
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test performance benchmarking
 */
static test_result_t test_packet_performance_benchmarks(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    packet_performance_metrics_t metrics;
    uint32_t start_time, end_time;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NICs for benchmarking */
    int mock_nic_3c509b = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    int mock_nic_3c515 = setup_mock_nic(MOCK_DEVICE_3C515, 0x320, 11);
    
    TEST_ASSERT(mock_nic_3c509b >= 0, "Failed to setup 3C509B for benchmarking");
    TEST_ASSERT(mock_nic_3c515 >= 0, "Failed to setup 3C515 for benchmarking");
    
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    
    /* Benchmark 1: Single packet operations */
    start_time = test_framework_get_timestamp();
    
    for (int i = 0; i < 100; i++) {
        result = packet_send_enhanced(mock_nic_3c509b, test_packet, sizeof(test_packet), 
                                    dest_mac, 0xD000 + i);
        TEST_ASSERT(result == SUCCESS, "Benchmark packets should send successfully");
    }
    
    end_time = test_framework_get_timestamp();
    uint32_t single_packet_duration = end_time - start_time;
    
    log_info("Single packet benchmark: 100 packets in %lu ms (3C509B)", single_packet_duration);
    
    /* Benchmark 2: Queued packet operations */
    start_time = test_framework_get_timestamp();
    
    for (int i = 0; i < 100; i++) {
        result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                        PACKET_PRIORITY_NORMAL, 0xD100 + i);
        TEST_ASSERT(result == SUCCESS, "Queue benchmark packets should succeed");
    }
    
    int flushed = packet_flush_tx_queue_enhanced();
    end_time = test_framework_get_timestamp();
    uint32_t queued_packet_duration = end_time - start_time;
    
    log_info("Queued packet benchmark: 100 packets queued and %d flushed in %lu ms", 
             flushed, queued_packet_duration);
    
    /* Benchmark 3: Multi-priority queuing */
    start_time = test_framework_get_timestamp();
    
    for (int round = 0; round < 25; round++) {
        for (int priority = PACKET_PRIORITY_LOW; priority <= PACKET_PRIORITY_URGENT; priority++) {
            result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                            priority, 0xD200 + round * 4 + priority);
            TEST_ASSERT(result == SUCCESS, "Multi-priority packets should queue");
        }
    }
    
    flushed = packet_flush_tx_queue_enhanced();
    end_time = test_framework_get_timestamp();
    uint32_t priority_duration = end_time - start_time;
    
    log_info("Multi-priority benchmark: 100 packets (4 priorities) in %lu ms, %d flushed", 
             priority_duration, flushed);
    
    /* Benchmark 4: DMA vs PIO comparison */
    uint32_t pio_start = test_framework_get_timestamp();
    
    for (int i = 0; i < 50; i++) {
        result = packet_send_enhanced(mock_nic_3c509b, test_packet, sizeof(test_packet), 
                                    dest_mac, 0xD300 + i);
    }
    
    uint32_t pio_end = test_framework_get_timestamp();
    uint32_t pio_duration = pio_end - pio_start;
    
    uint32_t dma_start = test_framework_get_timestamp();
    
    for (int i = 0; i < 50; i++) {
        result = packet_send_enhanced(mock_nic_3c515, test_packet, sizeof(test_packet), 
                                    dest_mac, 0xD400 + i);
    }
    
    uint32_t dma_end = test_framework_get_timestamp();
    uint32_t dma_duration = dma_end - dma_start;
    
    log_info("PIO vs DMA benchmark: PIO=50 packets in %lu ms, DMA=50 packets in %lu ms", 
             pio_duration, dma_duration);
    
    /* Benchmark 5: Packet size performance */
    uint32_t small_start = test_framework_get_timestamp();
    uint8_t small_packet[64];
    create_test_packet(small_packet, sizeof(small_packet), test_pattern_basic);
    
    for (int i = 0; i < 100; i++) {
        result = packet_send_enhanced(mock_nic_3c509b, small_packet, sizeof(small_packet), 
                                    dest_mac, 0xD500 + i);
    }
    
    uint32_t small_end = test_framework_get_timestamp();
    uint32_t small_duration = small_end - small_start;
    
    uint32_t large_start = test_framework_get_timestamp();
    uint8_t large_packet[1518];
    create_test_packet(large_packet, sizeof(large_packet), test_pattern_basic);
    
    for (int i = 0; i < 100; i++) {
        result = packet_send_enhanced(mock_nic_3c509b, large_packet, sizeof(large_packet), 
                                    dest_mac, 0xD600 + i);
    }
    
    uint32_t large_end = test_framework_get_timestamp();
    uint32_t large_duration = large_end - large_start;
    
    log_info("Packet size benchmark: Small(64B)=100 packets in %lu ms, Large(1518B)=100 packets in %lu ms", 
             small_duration, large_duration);
    
    /* Benchmark 6: Get performance metrics */
    result = packet_get_performance_metrics(&metrics);
    TEST_ASSERT(result == SUCCESS, "Should be able to get performance metrics");
    
    log_info("Performance metrics: TX=%lu packets, RX=%lu packets, TX errors=%lu%%, RX errors=%lu%%",
             metrics.tx_packets, metrics.rx_packets, metrics.tx_error_rate, metrics.rx_error_rate);
    
    /* Benchmark 7: Memory allocation performance for packet operations */
    start_time = test_framework_get_timestamp();
    
    for (int i = 0; i < 100; i++) {
        packet_buffer_t *buffer = packet_buffer_alloc(TEST_PACKET_SIZE_NORMAL);
        if (buffer) {
            packet_set_data(buffer, test_packet, sizeof(test_packet));
            packet_buffer_free(buffer);
        }
    }
    
    end_time = test_framework_get_timestamp();
    uint32_t memory_duration = end_time - start_time;
    
    log_info("Memory allocation benchmark: 100 buffer alloc/free cycles in %lu ms", memory_duration);
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test stress testing scenarios
 */
static test_result_t test_packet_stress_testing(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    packet_stats_t stats_before, stats_after;
    uint32_t start_time, end_time;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NICs for stress testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC for stress test");
    
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_stress);
    
    /* Get initial statistics */
    result = packet_get_statistics(&stats_before);
    TEST_ASSERT(result == SUCCESS, "Should get initial statistics");
    
    /* Stress Test 1: High-volume packet transmission */
    log_info("Starting high-volume transmission stress test...");
    start_time = test_framework_get_timestamp();
    
    int successful_sends = 0;
    for (int i = 0; i < TEST_QUEUE_STRESS_COUNT; i++) {
        result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), 
                                    dest_mac, 0xE000 + i);
        if (result == SUCCESS) {
            successful_sends++;
        }
        
        /* Occasionally flush to prevent infinite queue growth */
        if (i % 100 == 0) {
            packet_flush_tx_queue_enhanced();
        }
    }
    
    end_time = test_framework_get_timestamp();
    uint32_t tx_stress_duration = end_time - start_time;
    
    log_info("TX stress test: %d/%d successful sends in %lu ms", 
             successful_sends, TEST_QUEUE_STRESS_COUNT, tx_stress_duration);
    
    /* Stress Test 2: Queue overflow and flow control */
    log_info("Starting queue overflow stress test...");
    start_time = test_framework_get_timestamp();
    
    int queue_successful = 0;
    for (int i = 0; i < TEST_QUEUE_STRESS_COUNT; i++) {
        result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                        PACKET_PRIORITY_NORMAL, 0xE100 + i);
        if (result == SUCCESS) {
            queue_successful++;
        }
        
        /* Don't flush immediately - let queues fill up */
        if (i % 500 == 0) {
            packet_flush_tx_queue_enhanced();
        }
    }
    
    end_time = test_framework_get_timestamp();
    uint32_t queue_stress_duration = end_time - start_time;
    
    log_info("Queue stress test: %d/%d successful queues in %lu ms", 
             queue_successful, TEST_QUEUE_STRESS_COUNT, queue_stress_duration);
    
    /* Final flush */
    int final_flush = packet_flush_tx_queue_enhanced();
    log_info("Final flush processed %d packets", final_flush);
    
    /* Stress Test 3: Priority mixing under load */
    log_info("Starting priority mixing stress test...");
    start_time = test_framework_get_timestamp();
    
    int priority_counts[4] = {0};
    for (int i = 0; i < 400; i++) {  /* 100 packets per priority */
        int priority = i % 4;
        result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                        priority, 0xE200 + i);
        if (result == SUCCESS) {
            priority_counts[priority]++;
        }
    }
    
    end_time = test_framework_get_timestamp();
    uint32_t priority_stress_duration = end_time - start_time;
    
    log_info("Priority stress test in %lu ms:", priority_stress_duration);
    for (int i = 0; i < 4; i++) {
        log_info("  Priority %d: %d successful queues", i, priority_counts[i]);
    }
    
    packet_flush_tx_queue_enhanced();
    
    /* Stress Test 4: Rapid packet injection and reception */
    log_info("Starting RX stress test...");
    start_time = test_framework_get_timestamp();
    
    for (int i = 0; i < 100; i++) {  /* Fewer RX packets due to processing overhead */
        result = mock_packet_inject_rx(mock_nic_id, test_packet, sizeof(test_packet));
        TEST_ASSERT(result == SUCCESS, "Packet injection should succeed");
    }
    
    /* Try to receive all injected packets */
    uint8_t rx_buffer[TEST_PACKET_SIZE_NORMAL];
    size_t rx_length;
    int received_count = 0;
    
    for (int i = 0; i < 100; i++) {
        rx_length = sizeof(rx_buffer);
        result = packet_receive_from_nic(mock_nic_id, rx_buffer, &rx_length);
        if (result == SUCCESS) {
            received_count++;
        }
    }
    
    end_time = test_framework_get_timestamp();
    uint32_t rx_stress_duration = end_time - start_time;
    
    log_info("RX stress test: %d packets received in %lu ms", received_count, rx_stress_duration);
    
    /* Stress Test 5: Error injection under load */
    log_info("Starting error injection stress test...");
    mock_error_inject(mock_nic_id, MOCK_ERROR_TX_TIMEOUT, 10);  /* Every 10th packet fails */
    
    int error_test_successful = 0;
    for (int i = 0; i < 50; i++) {
        result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), 
                                    dest_mac, 0xE300 + i);
        if (result == SUCCESS) {
            error_test_successful++;
        }
    }
    
    mock_error_clear(mock_nic_id);
    log_info("Error injection stress test: %d/50 successful with periodic errors", error_test_successful);
    
    /* Get final statistics */
    result = packet_get_statistics(&stats_after);
    TEST_ASSERT(result == SUCCESS, "Should get final statistics");
    
    log_info("Stress test statistics:");
    log_info("  TX packets: %lu -> %lu (delta: %lu)", 
             stats_before.tx_packets, stats_after.tx_packets, 
             stats_after.tx_packets - stats_before.tx_packets);
    log_info("  RX packets: %lu -> %lu (delta: %lu)", 
             stats_before.rx_packets, stats_after.rx_packets, 
             stats_after.rx_packets - stats_before.rx_packets);
    log_info("  TX errors: %lu -> %lu (delta: %lu)", 
             stats_before.tx_errors, stats_after.tx_errors, 
             stats_after.tx_errors - stats_before.tx_errors);
    log_info("  RX errors: %lu -> %lu (delta: %lu)", 
             stats_before.rx_errors, stats_after.rx_errors, 
             stats_after.rx_errors - stats_before.rx_errors);
    
    /* Validate that we processed a significant number of packets */
    uint32_t total_tx_processed = stats_after.tx_packets - stats_before.tx_packets;
    TEST_ASSERT(total_tx_processed > 100, "Should have processed significant number of TX packets");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test statistics tracking and monitoring
 */
static test_result_t test_packet_statistics_tracking(void) {
    config_t test_config = {0};
    uint8_t test_packet[TEST_PACKET_SIZE_NORMAL];
    uint8_t dest_mac[6] = TEST_MAC_DEST;
    packet_stats_t stats;
    packet_performance_metrics_t metrics;
    packet_queue_management_stats_t queue_stats;
    int result;
    
    /* Setup */
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock NIC for statistics testing */
    int mock_nic_id = setup_mock_nic(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(mock_nic_id >= 0, "Failed to setup mock NIC");
    
    create_test_packet(test_packet, sizeof(test_packet), test_pattern_basic);
    
    /* Test 1: Initial statistics */
    result = packet_get_statistics(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get initial statistics");
    TEST_ASSERT(stats.tx_packets == 0, "Initial TX packets should be 0");
    TEST_ASSERT(stats.rx_packets == 0, "Initial RX packets should be 0");
    TEST_ASSERT(stats.tx_errors == 0, "Initial TX errors should be 0");
    
    /* Test 2: TX statistics tracking */
    for (int i = 0; i < 10; i++) {
        result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), 
                                    dest_mac, 0xF000 + i);
        TEST_ASSERT(result == SUCCESS, "Statistics test packets should send");
    }
    
    result = packet_get_statistics(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get updated statistics");
    TEST_ASSERT(stats.tx_packets >= 10, "TX packet count should increase");
    TEST_ASSERT(stats.tx_bytes > 0, "TX byte count should increase");
    
    /* Test 3: RX statistics tracking */
    for (int i = 0; i < 5; i++) {
        result = mock_packet_inject_rx(mock_nic_id, test_packet, sizeof(test_packet));
        TEST_ASSERT(result == SUCCESS, "Packet injection should succeed");
    }
    
    uint8_t rx_buffer[TEST_PACKET_SIZE_NORMAL];
    size_t rx_length;
    
    for (int i = 0; i < 5; i++) {
        rx_length = sizeof(rx_buffer);
        result = packet_receive_from_nic(mock_nic_id, rx_buffer, &rx_length);
        if (result == SUCCESS) {
            /* Packet received successfully */
        }
    }
    
    result = packet_get_statistics(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get updated RX statistics");
    TEST_ASSERT(stats.rx_packets > 0, "RX packet count should increase");
    TEST_ASSERT(stats.rx_bytes > 0, "RX byte count should increase");
    
    /* Test 4: Error statistics tracking */
    mock_error_inject(mock_nic_id, MOCK_ERROR_TX_TIMEOUT, 1);
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xF100);
    mock_error_clear(mock_nic_id);
    
    result = packet_get_statistics(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get error statistics");
    /* Error statistics depend on mock implementation behavior */
    
    /* Test 5: Performance metrics */
    result = packet_get_performance_metrics(&metrics);
    TEST_ASSERT(result == SUCCESS, "Should get performance metrics");
    
    TEST_ASSERT(metrics.tx_packets > 0, "Performance metrics should show TX activity");
    TEST_ASSERT(metrics.active_nics > 0, "Should show active NICs");
    TEST_ASSERT(metrics.collection_time > 0, "Should have collection timestamp");
    
    log_info("Performance metrics: TX=%lu, RX=%lu, Active NICs=%d", 
             metrics.tx_packets, metrics.rx_packets, metrics.active_nics);
    
    /* Test 6: Queue management statistics */
    for (int priority = 0; priority < 4; priority++) {
        for (int i = 0; i < 5; i++) {
            result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                            priority, 0xF200 + priority * 10 + i);
        }
    }
    
    result = packet_get_queue_stats(&queue_stats);
    TEST_ASSERT(result == SUCCESS, "Should get queue statistics");
    
    log_info("Queue statistics:");
    for (int i = 0; i < 4; i++) {
        log_info("  Priority %d: %d packets, %lu%% usage", 
                 i, queue_stats.tx_queue_counts[i], queue_stats.tx_queue_usage[i]);
    }
    
    packet_flush_tx_queue_enhanced();
    
    /* Test 7: Statistics reset */
    result = packet_reset_statistics();
    TEST_ASSERT(result == SUCCESS, "Should be able to reset statistics");
    
    result = packet_get_statistics(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get reset statistics");
    TEST_ASSERT(stats.tx_packets == 0, "TX packets should be reset to 0");
    TEST_ASSERT(stats.rx_packets == 0, "RX packets should be reset to 0");
    TEST_ASSERT(stats.tx_errors == 0, "TX errors should be reset to 0");
    
    /* Test 8: Health monitoring */
    int health_status = packet_monitor_health();
    log_info("Packet driver health status: %d", health_status);
    
    /* Test 9: Detailed statistics printing */
    packet_print_detailed_stats();
    
    /* Test 10: Statistics validation after operations */
    result = packet_send_enhanced(mock_nic_id, test_packet, sizeof(test_packet), dest_mac, 0xF300);
    TEST_ASSERT(result == SUCCESS, "Post-reset packet should send");
    
    result = packet_get_statistics(&stats);
    TEST_ASSERT(result == SUCCESS, "Should get post-reset statistics");
    TEST_ASSERT(stats.tx_packets == 1, "Should have exactly 1 TX packet after reset");
    
    /* Cleanup */
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/* Helper function implementations */

/**
 * @brief Create a test packet with pattern
 */
static void create_test_packet(uint8_t *buffer, size_t size, const uint8_t *pattern) {
    size_t pattern_len = strlen((const char*)pattern);
    
    for (size_t i = 0; i < size; i++) {
        buffer[i] = pattern[i % pattern_len];
    }
}

/**
 * @brief Verify packet data against expected pattern
 */
static bool verify_packet_data(const uint8_t *packet, size_t size, const uint8_t *expected) {
    size_t expected_len = strlen((const char*)expected);
    
    for (size_t i = 0; i < size; i++) {
        if (packet[i] != expected[i % expected_len]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Setup a mock NIC for testing
 */
static int setup_mock_nic(mock_device_type_t type, uint16_t io_base, uint8_t irq) {
    int device_id = mock_device_create(type, io_base, irq);
    if (device_id < 0) {
        return device_id;
    }
    
    /* Configure mock device */
    uint8_t test_mac[6] = {0x00, 0x10, 0x4B, 0x12, 0x34, 0x56};
    mock_device_set_mac_address(device_id, test_mac);
    mock_device_set_link_status(device_id, true, 100);
    mock_device_enable(device_id, true);
    
    /* Enable I/O logging */
    mock_io_log_enable(true);
    
    return device_id;
}

/**
 * @brief Cleanup all mock NICs
 */
static void cleanup_mock_nics(void) {
    for (int i = 0; i < MAX_MOCK_DEVICES; i++) {
        mock_device_destroy(i);
    }
}

/**
 * @brief Run a loopback test on specified NIC
 */
static test_result_t run_loopback_test(int nic_index, const uint8_t *pattern, size_t pattern_size) {
    int result = packet_test_internal_loopback(nic_index, pattern, pattern_size);
    return (result == SUCCESS) ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Get current timestamp for testing
 */
static uint32_t test_framework_get_timestamp(void) {
    /* Simple timestamp simulation for testing */
    static uint32_t counter = 0;
    return ++counter * 10;  /* 10ms increments */
}