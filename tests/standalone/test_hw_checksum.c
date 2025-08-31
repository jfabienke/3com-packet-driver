/**
 * @file test_hw_checksum.c
 * @brief Test program for hardware checksum implementation
 *
 * This test validates the hardware checksumming system implementation
 * for Sprint 2.1, focusing on software checksum calculations since
 * 3C515-TX and 3C509B do not support hardware checksumming.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "include/hw_checksum.h"
#include "include/nic_capabilities.h"
#include "include/logging.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test packet data */
static uint8_t test_ip_packet[] = {
    /* Ethernet header (14 bytes) */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  /* Destination MAC */
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,  /* Source MAC */
    0x08, 0x00,                          /* EtherType: IPv4 */
    
    /* IPv4 header (20 bytes) */
    0x45,                                /* Version (4) + IHL (5) */
    0x00,                                /* TOS */
    0x00, 0x2E,                          /* Total Length: 46 bytes */
    0x12, 0x34,                          /* ID */
    0x40, 0x00,                          /* Flags + Fragment Offset */
    0x40,                                /* TTL: 64 */
    0x11,                                /* Protocol: UDP */
    0x00, 0x00,                          /* Header Checksum (to be calculated) */
    0xC0, 0xA8, 0x01, 0x01,              /* Source IP: 192.168.1.1 */
    0xC0, 0xA8, 0x01, 0x02,              /* Dest IP: 192.168.1.2 */
    
    /* UDP header (8 bytes) */
    0x04, 0xD2,                          /* Source Port: 1234 */
    0x00, 0x50,                          /* Dest Port: 80 */
    0x00, 0x1A,                          /* Length: 26 bytes */
    0x00, 0x00,                          /* Checksum (to be calculated) */
    
    /* UDP payload (18 bytes) */
    'H', 'e', 'l', 'l', 'o', ' ',
    'W', 'o', 'r', 'l', 'd', '!',
    ' ', 'T', 'e', 's', 't', 0x00
};

/* Test functions */
static int test_checksum_initialization(void);
static int test_capability_detection(void);
static int test_ip_checksum(void);
static int test_udp_checksum(void);
static int test_packet_processing(void);
static int test_statistics(void);
static void print_test_results(int passed, int total);

int main(void) {
    printf("=== Hardware Checksum Test Suite ===\n");
    printf("Testing Sprint 2.1 implementation\n\n");
    
    int tests_passed = 0;
    int total_tests = 0;
    
    /* Initialize logging */
    /* Assuming logging is already initialized */
    
    printf("Running checksum tests...\n\n");
    
    /* Test 1: System initialization */
    printf("Test 1: Checksum system initialization\n");
    total_tests++;
    if (test_checksum_initialization() == 0) {
        printf("✓ PASSED\n\n");
        tests_passed++;
    } else {
        printf("✗ FAILED\n\n");
    }
    
    /* Test 2: Capability detection */
    printf("Test 2: Capability detection for 3C515-TX and 3C509B\n");
    total_tests++;
    if (test_capability_detection() == 0) {
        printf("✓ PASSED\n\n");
        tests_passed++;
    } else {
        printf("✗ FAILED\n\n");
    }
    
    /* Test 3: IP checksum calculation */
    printf("Test 3: IPv4 header checksum calculation\n");
    total_tests++;
    if (test_ip_checksum() == 0) {
        printf("✓ PASSED\n\n");
        tests_passed++;
    } else {
        printf("✗ FAILED\n\n");
    }
    
    /* Test 4: UDP checksum calculation */
    printf("Test 4: UDP checksum calculation\n");
    total_tests++;
    if (test_udp_checksum() == 0) {
        printf("✓ PASSED\n\n");
        tests_passed++;
    } else {
        printf("✗ FAILED\n\n");
    }
    
    /* Test 5: Full packet processing */
    printf("Test 5: Complete packet checksum processing\n");
    total_tests++;
    if (test_packet_processing() == 0) {
        printf("✓ PASSED\n\n");
        tests_passed++;
    } else {
        printf("✗ FAILED\n\n");
    }
    
    /* Test 6: Statistics collection */
    printf("Test 6: Statistics collection and reporting\n");
    total_tests++;
    if (test_statistics() == 0) {
        printf("✓ PASSED\n\n");
        tests_passed++;
    } else {
        printf("✗ FAILED\n\n");
    }
    
    /* Print final results */
    print_test_results(tests_passed, total_tests);
    
    /* Cleanup */
    hw_checksum_cleanup();
    
    return (tests_passed == total_tests) ? 0 : 1;
}

static int test_checksum_initialization(void) {
    int result = hw_checksum_init(CHECKSUM_MODE_AUTO);
    if (result != HW_CHECKSUM_SUCCESS) {
        printf("  Initialization failed with code %d\n", result);
        return -1;
    }
    
    printf("  Checksum system initialized successfully\n");
    return 0;
}

static int test_capability_detection(void) {
    /* Create mock NIC contexts for testing */
    nic_context_t ctx_3c515 = {0};
    nic_context_t ctx_3c509b = {0};
    
    const nic_info_entry_t *info_3c515 = nic_get_info_entry(NIC_TYPE_3C515_TX);
    const nic_info_entry_t *info_3c509b = nic_get_info_entry(NIC_TYPE_3C509B);
    
    if (!info_3c515 || !info_3c509b) {
        printf("  Failed to get NIC info entries\n");
        return -1;
    }
    
    ctx_3c515.info = info_3c515;
    ctx_3c509b.info = info_3c509b;
    
    /* Test 3C515-TX capabilities */
    uint32_t caps_3c515 = hw_checksum_detect_capabilities(&ctx_3c515);
    if (caps_3c515 != 0) {
        printf("  3C515-TX incorrectly reports hardware checksum support\n");
        return -1;
    }
    
    /* Test 3C509B capabilities */
    uint32_t caps_3c509b = hw_checksum_detect_capabilities(&ctx_3c509b);
    if (caps_3c509b != 0) {
        printf("  3C509B incorrectly reports hardware checksum support\n");
        return -1;
    }
    
    /* Test optimal mode selection */
    checksum_mode_t mode_3c515 = hw_checksum_get_optimal_mode(&ctx_3c515, CHECKSUM_PROTO_IP);
    checksum_mode_t mode_3c509b = hw_checksum_get_optimal_mode(&ctx_3c509b, CHECKSUM_PROTO_UDP);
    
    if (mode_3c515 != CHECKSUM_MODE_SOFTWARE || mode_3c509b != CHECKSUM_MODE_SOFTWARE) {
        printf("  Incorrect optimal mode selection\n");
        return -1;
    }
    
    printf("  Both NICs correctly detected as software-only\n");
    printf("  3C515-TX capabilities: 0x%08X (expected: 0x00000000)\n", caps_3c515);
    printf("  3C509B capabilities: 0x%08X (expected: 0x00000000)\n", caps_3c509b);
    return 0;
}

static int test_ip_checksum(void) {
    /* Make a copy of the test packet for modification */
    uint8_t packet[sizeof(test_ip_packet)];
    memcpy(packet, test_ip_packet, sizeof(test_ip_packet));
    
    /* Get IP header (skip Ethernet header) */
    uint8_t *ip_header = packet + 14;
    
    /* Calculate IP checksum */
    int result = hw_checksum_calculate_ip(ip_header, 20);
    if (result != HW_CHECKSUM_SUCCESS) {
        printf("  IP checksum calculation failed with code %d\n", result);
        return -1;
    }
    
    uint16_t calculated_checksum = (ip_header[10] << 8) | ip_header[11];
    printf("  Calculated IP checksum: 0x%04X\n", calculated_checksum);
    
    /* Validate the checksum */
    checksum_result_t validation = hw_checksum_validate_ip(ip_header, 20);
    if (validation != CHECKSUM_RESULT_VALID) {
        printf("  IP checksum validation failed: %s\n", 
               hw_checksum_result_to_string(validation));
        return -1;
    }
    
    printf("  IP checksum validation: %s\n", hw_checksum_result_to_string(validation));
    return 0;
}

static int test_udp_checksum(void) {
    /* Make a copy of the test packet for modification */
    uint8_t packet[sizeof(test_ip_packet)];
    memcpy(packet, test_ip_packet, sizeof(test_ip_packet));
    
    /* Get headers */
    uint8_t *ip_header = packet + 14;
    uint8_t *udp_header = ip_header + 20;
    
    /* Create checksum context */
    checksum_context_t ctx = {0};
    ctx.mode = CHECKSUM_MODE_SOFTWARE;
    ctx.protocol = CHECKSUM_PROTO_UDP;
    
    /* Calculate pseudo-header sum */
    uint32_t src_ip = *(uint32_t*)(ip_header + 12);
    uint32_t dst_ip = *(uint32_t*)(ip_header + 16);
    uint16_t udp_len = (udp_header[4] << 8) | udp_header[5];
    ctx.pseudo_header_sum = sw_checksum_pseudo_header(src_ip, dst_ip, 17, udp_len);
    
    /* Calculate UDP checksum */
    int result = hw_checksum_calculate_udp(&ctx, udp_header, udp_len);
    if (result != HW_CHECKSUM_SUCCESS) {
        printf("  UDP checksum calculation failed with code %d\n", result);
        return -1;
    }
    
    uint16_t calculated_checksum = (udp_header[6] << 8) | udp_header[7];
    printf("  Calculated UDP checksum: 0x%04X\n", calculated_checksum);
    
    /* Validate the checksum */
    checksum_result_t validation = hw_checksum_validate_udp(ip_header, udp_header, udp_len);
    if (validation != CHECKSUM_RESULT_VALID) {
        printf("  UDP checksum validation failed: %s\n", 
               hw_checksum_result_to_string(validation));
        return -1;
    }
    
    printf("  UDP checksum validation: %s\n", hw_checksum_result_to_string(validation));
    return 0;
}

static int test_packet_processing(void) {
    /* Create a mock NIC context */
    nic_context_t ctx = {0};
    const nic_info_entry_t *info = nic_get_info_entry(NIC_TYPE_3C515_TX);
    if (!info) {
        printf("  Failed to get 3C515-TX info\n");
        return -1;
    }
    ctx.info = info;
    
    /* Make a copy of the test packet */
    uint8_t packet[sizeof(test_ip_packet)];
    memcpy(packet, test_ip_packet, sizeof(test_ip_packet));
    
    /* Test TX path checksum calculation */
    uint32_t protocols = (1 << CHECKSUM_PROTO_IP) | (1 << CHECKSUM_PROTO_UDP);
    int result = hw_checksum_tx_calculate(&ctx, packet, sizeof(packet), protocols);
    if (result != HW_CHECKSUM_SUCCESS) {
        printf("  TX checksum calculation failed with code %d\n", result);
        return -1;
    }
    
    printf("  TX checksums calculated successfully\n");
    
    /* Test RX path checksum validation */
    uint32_t validation_results = 0;
    result = hw_checksum_rx_validate(&ctx, packet, sizeof(packet), &validation_results);
    if (result != HW_CHECKSUM_SUCCESS) {
        printf("  RX checksum validation failed with code %d\n", result);
        return -1;
    }
    
    /* Check validation results */
    checksum_result_t ip_result = (validation_results >> (CHECKSUM_PROTO_IP * 2)) & 0x3;
    checksum_result_t udp_result = (validation_results >> (CHECKSUM_PROTO_UDP * 2)) & 0x3;
    
    if (ip_result != CHECKSUM_RESULT_VALID || udp_result != CHECKSUM_RESULT_VALID) {
        printf("  Validation failed - IP: %s, UDP: %s\n",
               hw_checksum_result_to_string(ip_result),
               hw_checksum_result_to_string(udp_result));
        return -1;
    }
    
    printf("  RX validation results - IP: %s, UDP: %s\n",
           hw_checksum_result_to_string(ip_result),
           hw_checksum_result_to_string(udp_result));
    
    return 0;
}

static int test_statistics(void) {
    checksum_stats_t stats = {0};
    
    int result = hw_checksum_get_stats(&stats);
    if (result != HW_CHECKSUM_SUCCESS) {
        printf("  Failed to get statistics\n");
        return -1;
    }
    
    printf("  Statistics collected:\n");
    printf("    TX checksums: %lu\n", stats.tx_checksums_calculated);
    printf("    RX checksums: %lu\n", stats.rx_checksums_validated);
    printf("    Software fallbacks: %lu\n", stats.software_fallbacks);
    printf("    Hardware offloads: %lu (expected: 0)\n", stats.hardware_offloads);
    printf("    IP checksums: %lu\n", stats.ip_checksums);
    printf("    UDP checksums: %lu\n", stats.udp_checksums);
    
    /* Verify that no hardware offloads were reported */
    if (stats.hardware_offloads != 0) {
        printf("  Unexpected hardware offloads reported\n");
        return -1;
    }
    
    /* Verify some operations were recorded */
    if (stats.software_fallbacks == 0) {
        printf("  No software operations recorded\n");
        return -1;
    }
    
    return 0;
}

static void print_test_results(int passed, int total) {
    printf("=== Test Results ===\n");
    printf("Tests passed: %d/%d\n", passed, total);
    printf("Success rate: %.1f%%\n", (float)passed / total * 100.0);
    
    if (passed == total) {
        printf("✓ ALL TESTS PASSED\n");
        printf("\nSprint 2.1 hardware checksumming implementation validated:\n");
        printf("- Software checksumming working correctly\n");
        printf("- No false hardware capability reporting\n");
        printf("- Proper integration with capability system\n");
        printf("- Statistics collection functional\n");
        printf("- Ready for production use\n");
    } else {
        printf("✗ SOME TESTS FAILED\n");
        printf("Implementation needs review before production use.\n");
    }
    
    printf("====================\n");
}