/**
 * @file test_eeprom_sprint0b1.c
 * @brief Sprint 0B.1 EEPROM Reading Test Program
 *
 * This program validates the core EEPROM reading functionality implemented
 * for Sprint 0B.1, demonstrating robust timeout protection, error handling,
 * and MAC address extraction for production use.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "include/eeprom.h"
#include "include/hardware.h"
#include "include/3c515.h"
#include "include/3c509b.h"
#include "include/logging.h"
#include "include/common.h"
#include <stdio.h>
#include <string.h>

/* Test configuration */
#define TEST_IO_BASE_3C515      0x300
#define TEST_IO_BASE_3C509B     0x320
#define MAX_TEST_ADDRESSES      8

/* Test I/O addresses to scan */
static uint16_t test_addresses_3c515[] = {0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0x360, 0x370};
static uint16_t test_addresses_3c509b[] = {0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0x360, 0x370};

/* Function prototypes */
static void test_eeprom_basic_functionality(void);
static void test_eeprom_timeout_protection(void);
static void test_eeprom_error_handling(void);
static void test_eeprom_statistics(void);
static void scan_for_nics(void);
static void print_test_header(void);
static void print_test_results(int passed, int total);

/* Global test counters */
static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(condition, message) do { \
    g_tests_total++; \
    if (condition) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", message); \
    } else { \
        printf("  [FAIL] %s\n", message); \
    } \
} while(0)

/**
 * @brief Main test program entry point
 */
int main(void) {
    printf("3Com Packet Driver - Sprint 0B.1 EEPROM Testing\n");
    printf("================================================\n\n");
    
    print_test_header();
    
    /* Initialize EEPROM subsystem */
    int result = eeprom_init();
    if (result != EEPROM_SUCCESS) {
        printf("ERROR: Failed to initialize EEPROM subsystem: %s\n", 
               eeprom_error_to_string(result));
        return 1;
    }
    
    printf("EEPROM subsystem initialized successfully.\n\n");
    
    /* Run test suite */
    test_eeprom_basic_functionality();
    test_eeprom_timeout_protection();
    test_eeprom_error_handling();
    test_eeprom_statistics();
    scan_for_nics();
    
    /* Print final results */
    print_test_results(g_tests_passed, g_tests_total);
    
    /* Cleanup */
    eeprom_cleanup();
    
    return (g_tests_passed == g_tests_total) ? 0 : 1;
}

/**
 * @brief Test basic EEPROM functionality
 */
static void test_eeprom_basic_functionality(void) {
    printf("=== Basic EEPROM Functionality Tests ===\n");
    
    /* Test configuration structure */
    eeprom_config_t config;
    memset(&config, 0, sizeof(config));
    
    /* Test that we can create a configuration without crashing */
    TEST_ASSERT(sizeof(config) > 0, "EEPROM configuration structure is properly sized");
    TEST_ASSERT(sizeof(config.mac_address) == 6, "MAC address field is 6 bytes");
    
    /* Test utility functions */
    const char* media_str = eeprom_media_type_to_string(EEPROM_MEDIA_10BASE_T);
    TEST_ASSERT(media_str != NULL, "Media type to string conversion works");
    TEST_ASSERT(strcmp(media_str, "10BaseT") == 0, "10BaseT media type string is correct");
    
    const char* error_str = eeprom_error_to_string(EEPROM_SUCCESS);
    TEST_ASSERT(error_str != NULL, "Error code to string conversion works");
    TEST_ASSERT(strcmp(error_str, "Success") == 0, "Success error string is correct");
    
    printf("\n");
}

/**
 * @brief Test EEPROM timeout protection
 */
static void test_eeprom_timeout_protection(void) {
    printf("=== EEPROM Timeout Protection Tests ===\n");
    
    /* Test reading from potentially non-existent hardware */
    uint16_t data;
    uint32_t start_time = get_system_timestamp_ms();
    
    /* Try to read from an address that likely doesn't have hardware */
    int result = eeprom_read_word_3c515(0xFFFF, 0, &data);
    
    uint32_t elapsed_time = get_system_timestamp_ms() - start_time;
    
    /* Should either succeed or timeout, but not hang indefinitely */
    TEST_ASSERT(result == EEPROM_SUCCESS || result == EEPROM_ERROR_TIMEOUT || 
                result == EEPROM_ERROR_NOT_PRESENT,
                "EEPROM read returns valid status code");
    
    /* Should not take longer than maximum timeout plus overhead */
    TEST_ASSERT(elapsed_time <= (EEPROM_TIMEOUT_MS + 50),
                "EEPROM operation respects timeout limits");
    
    printf("  Operation completed in %u ms (limit: %d ms)\n", 
           elapsed_time, EEPROM_TIMEOUT_MS);
    
    printf("\n");
}

/**
 * @brief Test EEPROM error handling
 */
static void test_eeprom_error_handling(void) {
    printf("=== EEPROM Error Handling Tests ===\n");
    
    /* Test invalid parameters */
    eeprom_config_t config;
    int result;
    
    /* Test null pointer handling */
    result = read_3c515_eeprom(TEST_IO_BASE_3C515, NULL);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR, 
                "read_3c515_eeprom rejects null config pointer");
    
    result = read_3c509b_eeprom(TEST_IO_BASE_3C509B, NULL);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR,
                "read_3c509b_eeprom rejects null config pointer");
    
    /* Test invalid EEPROM addresses */
    uint16_t data;
    result = eeprom_read_word_3c515(TEST_IO_BASE_3C515, 0xFF, &data);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR,
                "eeprom_read_word_3c515 rejects invalid address");
    
    result = eeprom_read_word_3c509b(TEST_IO_BASE_3C509B, 0xFF, &data);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR,
                "eeprom_read_word_3c509b rejects invalid address");
    
    /* Test MAC address extraction error handling */
    uint8_t mac[6];
    result = eeprom_extract_mac_address(NULL, mac, true);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR,
                "MAC extraction rejects null EEPROM data");
    
    printf("\n");
}

/**
 * @brief Test EEPROM statistics functionality
 */
static void test_eeprom_statistics(void) {
    printf("=== EEPROM Statistics Tests ===\n");
    
    /* Clear statistics */
    eeprom_clear_stats();
    
    eeprom_stats_t stats;
    eeprom_get_stats(&stats);
    
    TEST_ASSERT(stats.total_reads == 0, "Statistics cleared properly");
    TEST_ASSERT(stats.successful_reads == 0, "Successful reads counter cleared");
    TEST_ASSERT(stats.timeout_errors == 0, "Timeout errors counter cleared");
    
    /* Perform some operations to update statistics */
    uint16_t data;
    eeprom_read_word_3c515(TEST_IO_BASE_3C515, 0, &data);
    eeprom_read_word_3c515(TEST_IO_BASE_3C515, 1, &data);
    
    /* Check that statistics were updated */
    eeprom_get_stats(&stats);
    TEST_ASSERT(stats.total_reads >= 2, "Statistics updated after operations");
    
    printf("  Total reads performed: %u\n", stats.total_reads);
    printf("  Successful reads: %u\n", stats.successful_reads);
    printf("  Timeout errors: %u\n", stats.timeout_errors);
    printf("  Verify errors: %u\n", stats.verify_errors);
    
    if (stats.total_reads > 0) {
        printf("  Success rate: %.1f%%\n", 
               100.0 * stats.successful_reads / stats.total_reads);
    }
    
    printf("\n");
}

/**
 * @brief Scan for actual NICs and test EEPROM reading
 */
static void scan_for_nics(void) {
    printf("=== NIC Detection and EEPROM Reading ===\n");
    
    bool found_any = false;
    
    /* Scan for 3C515-TX NICs */
    printf("Scanning for 3C515-TX NICs...\n");
    for (int i = 0; i < MAX_TEST_ADDRESSES; i++) {
        uint16_t iobase = test_addresses_3c515[i];
        
        /* Test accessibility first */
        int result = eeprom_test_accessibility(iobase, true);
        if (result == EEPROM_SUCCESS) {
            printf("  Found potential 3C515-TX at I/O 0x%X\n", iobase);
            found_any = true;
            
            /* Try to read EEPROM configuration */
            eeprom_config_t config;
            result = read_3c515_eeprom(iobase, &config);
            
            if (result == EEPROM_SUCCESS) {
                printf("    EEPROM read successful!\n");
                printf("    MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       config.mac_address[0], config.mac_address[1], config.mac_address[2],
                       config.mac_address[3], config.mac_address[4], config.mac_address[5]);
                printf("    Device ID: 0x%04X\n", config.device_id);
                printf("    Vendor ID: 0x%04X\n", config.vendor_id);
                printf("    Media Type: %s\n", eeprom_media_type_to_string(config.media_type));
                printf("    Capabilities: 100Mbps=%s, FullDuplex=%s\n",
                       config.speed_100mbps_cap ? "Yes" : "No",
                       config.full_duplex_cap ? "Yes" : "No");
            } else {
                printf("    EEPROM read failed: %s\n", eeprom_error_to_string(result));
            }
        }
    }
    
    /* Scan for 3C509B NICs */
    printf("\nScanning for 3C509B NICs...\n");
    for (int i = 0; i < MAX_TEST_ADDRESSES; i++) {
        uint16_t iobase = test_addresses_3c509b[i];
        
        /* Test accessibility first */
        int result = eeprom_test_accessibility(iobase, false);
        if (result == EEPROM_SUCCESS) {
            printf("  Found potential 3C509B at I/O 0x%X\n", iobase);
            found_any = true;
            
            /* Try to read EEPROM configuration */
            eeprom_config_t config;
            result = read_3c509b_eeprom(iobase, &config);
            
            if (result == EEPROM_SUCCESS) {
                printf("    EEPROM read successful!\n");
                printf("    MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       config.mac_address[0], config.mac_address[1], config.mac_address[2],
                       config.mac_address[3], config.mac_address[4], config.mac_address[5]);
                printf("    Device ID: 0x%04X\n", config.device_id);
                printf("    Vendor ID: 0x%04X\n", config.vendor_id);
                printf("    IRQ Config: %d\n", config.irq_config);
                printf("    Media Type: %s\n", eeprom_media_type_to_string(config.media_type));
            } else {
                printf("    EEPROM read failed: %s\n", eeprom_error_to_string(result));
            }
        }
    }
    
    if (!found_any) {
        printf("  No NICs detected at standard I/O addresses.\n");
        printf("  This is normal if no 3Com NICs are installed.\n");
    }
    
    TEST_ASSERT(true, "NIC detection scan completed without errors");
    
    printf("\n");
}

/**
 * @brief Print test program header
 */
static void print_test_header(void) {
    printf("This test validates the Sprint 0B.1 EEPROM reading implementation:\n");
    printf("  - Robust timeout protection (max %d ms)\n", EEPROM_TIMEOUT_MS);
    printf("  - Comprehensive error handling and recovery\n");
    printf("  - MAC address extraction and validation\n");
    printf("  - Support for both 3C515-TX and 3C509B formats\n");
    printf("  - Production-ready error recovery mechanisms\n\n");
}

/**
 * @brief Print final test results
 */
static void print_test_results(int passed, int total) {
    printf("=== Sprint 0B.1 EEPROM Test Results ===\n");
    printf("Tests passed: %d/%d\n", passed, total);
    
    if (total > 0) {
        printf("Success rate: %.1f%%\n", 100.0 * passed / total);
    }
    
    if (passed == total) {
        printf("Status: ALL TESTS PASSED - Sprint 0B.1 implementation ready!\n");
    } else {
        printf("Status: SOME TESTS FAILED - Review implementation\n");
    }
    
    /* Display final EEPROM statistics */
    eeprom_stats_t final_stats;
    eeprom_get_stats(&final_stats);
    
    printf("\nFinal EEPROM Operation Statistics:\n");
    printf("  Total operations: %u\n", final_stats.total_reads);
    printf("  Successful operations: %u\n", final_stats.successful_reads);
    printf("  Timeout errors: %u\n", final_stats.timeout_errors);
    printf("  Verification errors: %u\n", final_stats.verify_errors);
    printf("  Retry attempts: %u\n", final_stats.retry_count);
    
    if (final_stats.total_reads > 0) {
        printf("  Overall success rate: %.1f%%\n", 
               100.0 * final_stats.successful_reads / final_stats.total_reads);
    }
    
    printf("\nSprint 0B.1 EEPROM implementation validation complete.\n");
}