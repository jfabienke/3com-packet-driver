/**
 * @file api_test.c
 * @brief Packet Driver API test and validation
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file contains tests to validate Packet Driver Specification compliance.
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "../include/api.h"
#include "../include/logging.h"

/* Test packet types */
#define TEST_PACKET_TYPE_IP    0x0800
#define TEST_PACKET_TYPE_ARP   0x0806
#define TEST_PACKET_TYPE_ALL   0x0000

/* Test interface */
#define TEST_INTERFACE_NUM     0

/* Test results tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/**
 * @brief Test receiver function for packet delivery validation
 */
void far test_receiver(void) {
    /* Simple test receiver that just logs the call */
    /* In real implementation, this would process the packet */
    /* Assembly calling convention: AX=handle, CX=length, DS:SI=packet */
    printf("Test receiver called\n");
}

/**
 * @brief Run a test with result checking
 */
#define RUN_TEST(name, condition) do { \
    printf("Test: %s ... ", name); \
    if (condition) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
        tests_failed++; \
    } \
} while(0)

/**
 * @brief Test driver_info function
 */
int test_driver_info(void) {
    pd_driver_info_t info;
    int result;
    
    printf("\n=== Testing driver_info function ===\n");
    
    result = pd_get_driver_info(&info);
    RUN_TEST("pd_get_driver_info return value", result == 0);
    RUN_TEST("driver version", info.version == 0x0100);
    RUN_TEST("driver class", info.class == PD_CLASS_ETHERNET);
    RUN_TEST("driver type", info.type == PD_TYPE_3COM);
    RUN_TEST("driver name length", strlen(info.name) > 0);
    
    printf("Driver Info: v%04X, class=%d, type=%d, name='%s'\n",
           info.version, info.class, info.type, info.name);
    
    return (tests_failed == 0);
}

/**
 * @brief Test handle management
 */
int test_handle_management(void) {
    pd_access_params_t access_params;
    int handle1, handle2, result;
    
    printf("\n=== Testing handle management ===\n");
    
    /* Set up access parameters */
    access_params.class = PD_CLASS_ETHERNET;
    access_params.type = TEST_PACKET_TYPE_IP;
    access_params.number = TEST_INTERFACE_NUM;
    access_params.basic = 1;
    access_params.receiver = test_receiver;
    
    /* Test handle allocation */
    handle1 = pd_handle_access_type(&access_params);
    RUN_TEST("first handle allocation", handle1 > 0);
    RUN_TEST("handle validation", pd_validate_handle(handle1));
    
    /* Test second handle allocation */
    access_params.type = TEST_PACKET_TYPE_ARP;
    handle2 = pd_handle_access_type(&access_params);
    RUN_TEST("second handle allocation", handle2 > 0);
    RUN_TEST("handles are different", handle1 != handle2);
    
    /* Test handle release */
    result = pd_release_handle(handle1);
    RUN_TEST("handle release", result == 0);
    RUN_TEST("released handle invalid", !pd_validate_handle(handle1));
    RUN_TEST("other handle still valid", pd_validate_handle(handle2));
    
    /* Clean up */
    pd_release_handle(handle2);
    
    return (tests_failed == 0);
}

/**
 * @brief Test packet type filtering
 */
int test_packet_filtering(void) {
    pd_access_params_t access_params;
    int handle, result;
    uint8_t test_packet[60];
    
    printf("\n=== Testing packet filtering ===\n");
    
    /* Set up test packet */
    memset(test_packet, 0, sizeof(test_packet));
    test_packet[12] = 0x08;  /* IP packet type high byte */
    test_packet[13] = 0x00;  /* IP packet type low byte */
    
    /* Set up IP packet handler */
    access_params.class = PD_CLASS_ETHERNET;
    access_params.type = TEST_PACKET_TYPE_IP;
    access_params.number = TEST_INTERFACE_NUM;
    access_params.basic = 1;
    access_params.receiver = test_receiver;
    
    handle = pd_handle_access_type(&access_params);
    RUN_TEST("IP handler allocated", handle > 0);
    
    /* Test packet processing */
    result = api_process_received_packet(test_packet, sizeof(test_packet), 0);
    RUN_TEST("IP packet delivered", result == 0);
    
    /* Test with ARP packet (should not be delivered to IP handler) */
    test_packet[12] = 0x08;  /* ARP packet type high byte */
    test_packet[13] = 0x06;  /* ARP packet type low byte */
    
    result = api_process_received_packet(test_packet, sizeof(test_packet), 0);
    RUN_TEST("ARP packet not delivered to IP handler", result == API_ERR_NO_HANDLERS);
    
    /* Clean up */
    pd_release_handle(handle);
    
    return (tests_failed == 0);
}

/**
 * @brief Test send packet functionality
 */
int test_send_packet(void) {
    pd_access_params_t access_params;
    pd_send_params_t send_params;
    int handle, result;
    uint8_t test_packet[60];
    
    printf("\n=== Testing send packet ===\n");
    
    /* Set up test packet */
    memset(test_packet, 0, sizeof(test_packet));
    test_packet[12] = 0x08;  /* IP packet type */
    test_packet[13] = 0x00;
    
    /* Allocate handle for sending */
    access_params.class = PD_CLASS_ETHERNET;
    access_params.type = TEST_PACKET_TYPE_IP;
    access_params.number = TEST_INTERFACE_NUM;
    access_params.basic = 1;
    access_params.receiver = test_receiver;
    
    handle = pd_handle_access_type(&access_params);
    RUN_TEST("send handle allocated", handle > 0);
    
    /* Test packet send */
    send_params.buffer = test_packet;
    send_params.length = sizeof(test_packet);
    
    result = pd_send_packet(handle, &send_params);
    /* Note: This may fail if hardware layer is not available */
    printf("Send result: %d (may fail without hardware)\n", result);
    
    /* Test invalid packet size */
    send_params.length = 30;  /* Too small */
    result = pd_send_packet(handle, &send_params);
    RUN_TEST("reject small packet", result != 0);
    
    send_params.length = 2000;  /* Too large */
    result = pd_send_packet(handle, &send_params);
    RUN_TEST("reject large packet", result != 0);
    
    /* Clean up */
    pd_release_handle(handle);
    
    return (tests_failed == 0);
}

/**
 * @brief Test error handling
 */
int test_error_handling(void) {
    int result;
    
    printf("\n=== Testing error handling ===\n");
    
    /* Test invalid handle operations */
    result = pd_validate_handle(0xFFFF);
    RUN_TEST("invalid handle rejected", !result);
    
    result = pd_release_handle(0xFFFF);
    RUN_TEST("invalid handle release fails", result != 0);
    
    result = pd_send_packet(0xFFFF, NULL);
    RUN_TEST("send with invalid handle fails", result != 0);
    
    /* Test NULL parameter handling */
    result = pd_get_driver_info(NULL);
    RUN_TEST("NULL parameter rejected", result != 0);
    
    result = pd_handle_access_type(NULL);
    RUN_TEST("NULL access params rejected", result < 0);
    
    return (tests_failed == 0);
}

/**
 * @brief Main test function
 */
int main(int argc, char *argv[]) {
    config_t test_config = {0};  /* Basic test configuration */
    int overall_result = 0;
    
    printf("3Com Packet Driver API Test Suite\n");
    printf("==================================\n");
    
    /* Initialize API for testing */
    if (api_init(&test_config) != 0) {
        printf("FATAL: Failed to initialize API for testing\n");
        return 1;
    }
    
    /* Run test suites */
    test_driver_info();
    test_handle_management();
    test_packet_filtering();
    test_send_packet();
    test_error_handling();
    
    /* Print results summary */
    printf("\n=== Test Results Summary ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Total tests:  %d\n", tests_passed + tests_failed);
    
    overall_result = (tests_failed == 0) ? 0 : 1;
    
    if (overall_result == 0) {
        printf("\nALL TESTS PASSED - API appears compliant\n");
    } else {
        printf("\nSOME TESTS FAILED - API needs fixes\n");
    }
    
    /* Cleanup */
    api_cleanup();
    
    return overall_result;
}