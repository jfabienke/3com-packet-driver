/**
 * @file test_eeprom.c
 * @brief Comprehensive EEPROM reading functionality tests
 *
 * This test suite validates the robust EEPROM reading implementation
 * for Sprint 0B.1, covering timeout protection, error handling, 
 * MAC address extraction, and hardware validation for both 3C515-TX
 * and 3C509B NICs.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "../common/test_common.h"
#include "../../include/eeprom.h"
#include "../../include/hardware.h"
#include "../../include/3c515.h"
#include "../../include/3c509b.h"
#include "../../include/logging.h"
#include "../helpers/helper_mock_hardware.h"

/* Test fixtures and constants */
#define TEST_IO_BASE_3C515      0x300
#define TEST_IO_BASE_3C509B     0x320
#define TEST_TIMEOUT_MS         100

/* Mock EEPROM data for testing */
static uint16_t mock_3c515_eeprom[EEPROM_MAX_SIZE] = {
    0x1234, 0x5678, 0x9ABC,  /* MAC: 34:12:78:56:BC:9A */
    0x5051,                   /* Device ID: 3C515-TX */
    0x1998,                   /* Mfg date */
    0xDEAD,                   /* Mfg data */
    0x0180,                   /* Config word: 100BaseTX, Full duplex */
    0x6D50,                   /* Vendor ID: 3Com */
    0x0000,                   /* Software config */
    0x00FF,                   /* Capabilities */
    /* Fill remaining with pattern */
    [10 ... 62] = 0xA5A5,
    0x1234                    /* Checksum placeholder */
};

static uint16_t mock_3c509b_eeprom[32] = {
    0x1234, 0x5678, 0x9ABC,  /* MAC: 34:12:78:56:BC:9A */
    0x6D50,                   /* Device ID: 3C509B */
    0x1995,                   /* Mfg date */
    0xBEEF,                   /* Mfg data */
    0x0040,                   /* Config word: 10BaseT */
    0x6D50,                   /* Vendor ID: 3Com */
    0x0300,                   /* I/O config */
    0x3000,                   /* IRQ config (IRQ 3) */
    0x0000, 0x0000, 0x0000,
    0x0040,                   /* Media config: 10BaseT */
    /* Fill remaining */
    [14 ... 30] = 0x5A5A,
    0x4321                    /* Checksum placeholder */
};

/* Test function prototypes */
static int test_eeprom_init_cleanup(void);
static int test_3c515_eeprom_read_basic(void);
static int test_3c509b_eeprom_read_basic(void);
static int test_eeprom_timeout_protection(void);
static int test_eeprom_error_handling(void);
static int test_mac_address_extraction(void);
static int test_configuration_parsing(void);
static int test_hardware_validation(void);
static int test_eeprom_statistics(void);
static int test_eeprom_diagnostic_functions(void);
static int test_eeprom_checksum_validation(void);

/* Helper functions */
static void setup_mock_3c515_eeprom(void);
static void setup_mock_3c509b_eeprom(void);
static void calculate_mock_checksums(void);

/**
 * @brief Main EEPROM test runner
 */
int test_eeprom_main(void) {
    int failed_tests = 0;
    int total_tests = 0;
    
    LOG_INFO("=== EEPROM Functionality Test Suite ===");
    LOG_INFO("Testing Sprint 0B.1 EEPROM reading implementation");
    
    /* Calculate proper checksums for mock data */
    calculate_mock_checksums();
    
    /* Test cases */
    TEST_RUN(test_eeprom_init_cleanup);
    TEST_RUN(test_3c515_eeprom_read_basic);
    TEST_RUN(test_3c509b_eeprom_read_basic);
    TEST_RUN(test_eeprom_timeout_protection);
    TEST_RUN(test_eeprom_error_handling);
    TEST_RUN(test_mac_address_extraction);
    TEST_RUN(test_configuration_parsing);
    TEST_RUN(test_hardware_validation);
    TEST_RUN(test_eeprom_statistics);
    TEST_RUN(test_eeprom_diagnostic_functions);
    TEST_RUN(test_eeprom_checksum_validation);
    
    LOG_INFO("=== EEPROM Test Results ===");
    LOG_INFO("Total tests: %d", total_tests);
    LOG_INFO("Failed tests: %d", failed_tests);
    LOG_INFO("Success rate: %.1f%%", 
             total_tests > 0 ? (100.0 * (total_tests - failed_tests) / total_tests) : 0.0);
    
    return failed_tests;
}

/**
 * @brief Test EEPROM subsystem initialization and cleanup
 */
static int test_eeprom_init_cleanup(void) {
    LOG_DEBUG("Testing EEPROM subsystem initialization and cleanup");
    
    /* Test initialization */
    int result = eeprom_init();
    TEST_ASSERT(result == EEPROM_SUCCESS, "EEPROM initialization should succeed");
    
    /* Test double initialization (should be safe) */
    result = eeprom_init();
    TEST_ASSERT(result == EEPROM_SUCCESS, "Double EEPROM initialization should be safe");
    
    /* Test statistics are cleared */
    eeprom_stats_t stats;
    eeprom_get_stats(&stats);
    TEST_ASSERT(stats.total_reads == 0, "Initial statistics should be zero");
    
    /* Test cleanup */
    eeprom_cleanup();
    
    /* Re-initialize for other tests */
    result = eeprom_init();
    TEST_ASSERT(result == EEPROM_SUCCESS, "EEPROM re-initialization should succeed");
    
    return 0;
}

/**
 * @brief Test basic 3C515-TX EEPROM reading
 */
static int test_3c515_eeprom_read_basic(void) {
    LOG_DEBUG("Testing 3C515-TX EEPROM reading");
    
    setup_mock_3c515_eeprom();
    
    eeprom_config_t config;
    int result = read_3c515_eeprom(TEST_IO_BASE_3C515, &config);
    
    TEST_ASSERT(result == EEPROM_SUCCESS, "3C515 EEPROM read should succeed");
    TEST_ASSERT(config.data_valid == true, "EEPROM data should be marked valid");
    
    /* Verify MAC address */
    TEST_ASSERT(config.mac_address[0] == 0x34, "MAC byte 0 should be correct");
    TEST_ASSERT(config.mac_address[1] == 0x12, "MAC byte 1 should be correct");
    TEST_ASSERT(config.mac_address[2] == 0x78, "MAC byte 2 should be correct");
    TEST_ASSERT(config.mac_address[3] == 0x56, "MAC byte 3 should be correct");
    TEST_ASSERT(config.mac_address[4] == 0xBC, "MAC byte 4 should be correct");
    TEST_ASSERT(config.mac_address[5] == 0x9A, "MAC byte 5 should be correct");
    
    /* Verify device and vendor IDs */
    TEST_ASSERT(config.device_id == 0x5051, "Device ID should be 3C515-TX");
    TEST_ASSERT(config.vendor_id == 0x6D50, "Vendor ID should be 3Com");
    
    /* Verify capabilities */
    TEST_ASSERT(config.full_duplex_cap == true, "Should have full duplex capability");
    TEST_ASSERT(config.speed_100mbps_cap == true, "Should have 100Mbps capability");
    
    return 0;
}

/**
 * @brief Test basic 3C509B EEPROM reading
 */
static int test_3c509b_eeprom_read_basic(void) {
    LOG_DEBUG("Testing 3C509B EEPROM reading");
    
    setup_mock_3c509b_eeprom();
    
    eeprom_config_t config;
    int result = read_3c509b_eeprom(TEST_IO_BASE_3C509B, &config);
    
    TEST_ASSERT(result == EEPROM_SUCCESS, "3C509B EEPROM read should succeed");
    TEST_ASSERT(config.data_valid == true, "EEPROM data should be marked valid");
    
    /* Verify MAC address */
    TEST_ASSERT(config.mac_address[0] == 0x34, "MAC byte 0 should be correct");
    TEST_ASSERT(config.mac_address[1] == 0x12, "MAC byte 1 should be correct");
    
    /* Verify device and vendor IDs */
    TEST_ASSERT(config.device_id == 0x6D50, "Device ID should be 3C509B");
    TEST_ASSERT(config.vendor_id == 0x6D50, "Vendor ID should be 3Com");
    
    /* Verify 3C509B specific capabilities */
    TEST_ASSERT(config.full_duplex_cap == false, "3C509B should not have full duplex");
    TEST_ASSERT(config.speed_100mbps_cap == false, "3C509B should not have 100Mbps");
    
    /* Verify IRQ configuration */
    TEST_ASSERT(config.irq_config == 3, "IRQ should be configured for IRQ 3");
    
    return 0;
}

/**
 * @brief Test EEPROM timeout protection
 */
static int test_eeprom_timeout_protection(void) {
    LOG_DEBUG("Testing EEPROM timeout protection");
    
    /* This test would require mocking hardware that doesn't respond */
    /* For now, test that timeout values are reasonable */
    
    uint16_t data;
    uint32_t start_time = mock_get_system_time();
    
    /* Test with invalid I/O address that should timeout quickly */
    int result = eeprom_read_word_3c515(0xFFFF, 0, &data);
    
    uint32_t elapsed_time = mock_get_system_time() - start_time;
    
    /* Should either succeed quickly or fail with timeout */
    TEST_ASSERT(result == EEPROM_SUCCESS || result == EEPROM_ERROR_TIMEOUT,
                "Should either succeed or timeout");
    
    /* Should not take longer than maximum timeout */
    TEST_ASSERT(elapsed_time <= EEPROM_TIMEOUT_MS + 5, 
                "Should not exceed maximum timeout");
    
    return 0;
}

/**
 * @brief Test EEPROM error handling and recovery
 */
static int test_eeprom_error_handling(void) {
    LOG_DEBUG("Testing EEPROM error handling and recovery");
    
    /* Test invalid parameters */
    eeprom_config_t config;
    int result = read_3c515_eeprom(TEST_IO_BASE_3C515, NULL);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR, 
                "Should reject null config pointer");
    
    /* Test invalid EEPROM address */
    uint16_t data;
    result = eeprom_read_word_3c515(TEST_IO_BASE_3C515, 0xFF, &data);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR, 
                "Should reject invalid EEPROM address");
    
    /* Test statistics are updated on errors */
    eeprom_stats_t stats_before, stats_after;
    eeprom_get_stats(&stats_before);
    
    /* Cause an error */
    result = eeprom_read_word_3c509b(TEST_IO_BASE_3C509B, 99, &data);
    
    eeprom_get_stats(&stats_after);
    
    /* Should have incremented total reads */
    TEST_ASSERT(stats_after.total_reads > stats_before.total_reads,
                "Error should still increment total reads");
    
    return 0;
}

/**
 * @brief Test MAC address extraction functionality
 */
static int test_mac_address_extraction(void) {
    LOG_DEBUG("Testing MAC address extraction");
    
    uint8_t mac_address[6];
    
    /* Test 3C515 MAC extraction */
    int result = eeprom_extract_mac_address(mock_3c515_eeprom, mac_address, true);
    TEST_ASSERT(result == EEPROM_SUCCESS, "MAC extraction should succeed");
    
    /* Verify extracted MAC matches expected */
    TEST_ASSERT(mac_address[0] == 0x34, "Extracted MAC byte 0 correct");
    TEST_ASSERT(mac_address[1] == 0x12, "Extracted MAC byte 1 correct");
    TEST_ASSERT(mac_address[2] == 0x78, "Extracted MAC byte 2 correct");
    TEST_ASSERT(mac_address[3] == 0x56, "Extracted MAC byte 3 correct");
    TEST_ASSERT(mac_address[4] == 0xBC, "Extracted MAC byte 4 correct");
    TEST_ASSERT(mac_address[5] == 0x9A, "Extracted MAC byte 5 correct");
    
    /* Test 3C509B MAC extraction */
    result = eeprom_extract_mac_address(mock_3c509b_eeprom, mac_address, false);
    TEST_ASSERT(result == EEPROM_SUCCESS, "3C509B MAC extraction should succeed");
    
    /* Test error handling */
    result = eeprom_extract_mac_address(NULL, mac_address, true);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR, "Should reject null EEPROM data");
    
    result = eeprom_extract_mac_address(mock_3c515_eeprom, NULL, true);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR, "Should reject null MAC buffer");
    
    return 0;
}

/**
 * @brief Test configuration parsing functionality
 */
static int test_configuration_parsing(void) {
    LOG_DEBUG("Testing configuration parsing");
    
    eeprom_config_t config;
    
    /* Test 3C515 configuration parsing */
    int result = eeprom_parse_config(mock_3c515_eeprom, EEPROM_MAX_SIZE, &config, true);
    TEST_ASSERT(result == EEPROM_SUCCESS, "3C515 config parsing should succeed");
    
    /* Verify parsed values */
    TEST_ASSERT(config.device_id == 0x5051, "Device ID parsed correctly");
    TEST_ASSERT(config.vendor_id == 0x6D50, "Vendor ID parsed correctly");
    TEST_ASSERT(config.full_duplex_cap == true, "Full duplex capability parsed");
    TEST_ASSERT(config.speed_100mbps_cap == true, "100Mbps capability parsed");
    
    /* Test 3C509B configuration parsing */
    result = eeprom_parse_config(mock_3c509b_eeprom, 32, &config, false);
    TEST_ASSERT(result == EEPROM_SUCCESS, "3C509B config parsing should succeed");
    
    /* Verify 3C509B specific values */
    TEST_ASSERT(config.full_duplex_cap == false, "3C509B should not have full duplex");
    TEST_ASSERT(config.speed_100mbps_cap == false, "3C509B should not have 100Mbps");
    TEST_ASSERT(config.irq_config == 3, "IRQ configuration parsed correctly");
    
    /* Test error handling */
    result = eeprom_parse_config(NULL, EEPROM_MAX_SIZE, &config, true);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR, "Should reject null EEPROM data");
    
    result = eeprom_parse_config(mock_3c515_eeprom, 3, &config, true);
    TEST_ASSERT(result == EEPROM_ERROR_INVALID_ADDR, "Should reject insufficient data");
    
    return 0;
}

/**
 * @brief Test hardware validation functionality
 */
static int test_hardware_validation(void) {
    LOG_DEBUG("Testing hardware validation");
    
    setup_mock_3c515_eeprom();
    
    eeprom_config_t config;
    int result = read_3c515_eeprom(TEST_IO_BASE_3C515, &config);
    TEST_ASSERT(result == EEPROM_SUCCESS, "EEPROM read should succeed");
    
    /* Test hardware validation */
    result = eeprom_validate_hardware(TEST_IO_BASE_3C515, &config, true);
    TEST_ASSERT(result == EEPROM_SUCCESS || result == EEPROM_ERROR_NOT_PRESENT,
                "Hardware validation should provide valid result");
    
    /* Test accessibility test */
    result = eeprom_test_accessibility(TEST_IO_BASE_3C515, true);
    TEST_ASSERT(result == EEPROM_SUCCESS || result == EEPROM_ERROR_NOT_PRESENT,
                "Accessibility test should provide valid result");
    
    return 0;
}

/**
 * @brief Test EEPROM statistics functionality
 */
static int test_eeprom_statistics(void) {
    LOG_DEBUG("Testing EEPROM statistics");
    
    /* Clear statistics */
    eeprom_clear_stats();
    
    eeprom_stats_t stats;
    eeprom_get_stats(&stats);
    TEST_ASSERT(stats.total_reads == 0, "Statistics should be cleared");
    TEST_ASSERT(stats.successful_reads == 0, "Successful reads should be zero");
    
    /* Perform some EEPROM operations */
    setup_mock_3c515_eeprom();
    
    uint16_t data;
    eeprom_read_word_3c515(TEST_IO_BASE_3C515, 0, &data);
    eeprom_read_word_3c515(TEST_IO_BASE_3C515, 1, &data);
    
    /* Check statistics were updated */
    eeprom_get_stats(&stats);
    TEST_ASSERT(stats.total_reads >= 2, "Total reads should be updated");
    
    return 0;
}

/**
 * @brief Test diagnostic helper functions
 */
static int test_eeprom_diagnostic_functions(void) {
    LOG_DEBUG("Testing EEPROM diagnostic functions");
    
    setup_mock_3c515_eeprom();
    
    /* Test diagnostic read functions */
    uint16_t data_3c515 = nic_read_eeprom_3c515(TEST_IO_BASE_3C515, 0);
    TEST_ASSERT(data_3c515 != 0xFFFF, "3C515 diagnostic read should return valid data");
    
    uint16_t data_3c509b = nic_read_eeprom_3c509b(TEST_IO_BASE_3C509B, 0);
    TEST_ASSERT(data_3c509b != 0xFFFF, "3C509B diagnostic read should return valid data");
    
    /* Test EEPROM dump function */
    char dump_buffer[1024];
    int dump_size = eeprom_dump_contents(TEST_IO_BASE_3C515, true, dump_buffer, sizeof(dump_buffer));
    TEST_ASSERT(dump_size > 0, "EEPROM dump should produce output");
    TEST_ASSERT(dump_size < sizeof(dump_buffer), "EEPROM dump should not overflow buffer");
    
    return 0;
}

/**
 * @brief Test EEPROM checksum validation
 */
static int test_eeprom_checksum_validation(void) {
    LOG_DEBUG("Testing EEPROM checksum validation");
    
    /* Test with valid checksums */
    bool valid = eeprom_validate_checksum(mock_3c515_eeprom, EEPROM_MAX_SIZE, true);
    TEST_ASSERT(valid == true, "3C515 checksum should be valid");
    
    valid = eeprom_validate_checksum(mock_3c509b_eeprom, 32, false);
    TEST_ASSERT(valid == true, "3C509B checksum should be valid");
    
    /* Test with corrupted checksum */
    uint16_t original_checksum = mock_3c515_eeprom[EEPROM_MAX_SIZE - 1];
    mock_3c515_eeprom[EEPROM_MAX_SIZE - 1] = 0x0000;
    
    valid = eeprom_validate_checksum(mock_3c515_eeprom, EEPROM_MAX_SIZE, true);
    TEST_ASSERT(valid == false, "Corrupted checksum should be invalid");
    
    /* Restore original checksum */
    mock_3c515_eeprom[EEPROM_MAX_SIZE - 1] = original_checksum;
    
    return 0;
}

/* Helper Functions */

static void setup_mock_3c515_eeprom(void) {
    /* Set up mock hardware to return our test EEPROM data */
    mock_device_t *device = mock_get_device_by_iobase(TEST_IO_BASE_3C515);
    if (device) {
        memcpy(device->eeprom.data, mock_3c515_eeprom, sizeof(mock_3c515_eeprom));
        device->eeprom.size = EEPROM_MAX_SIZE;
    }
}

static void setup_mock_3c509b_eeprom(void) {
    /* Set up mock hardware to return our test EEPROM data */
    mock_device_t *device = mock_get_device_by_iobase(TEST_IO_BASE_3C509B);
    if (device) {
        memcpy(device->eeprom.data, mock_3c509b_eeprom, sizeof(mock_3c509b_eeprom));
        device->eeprom.size = 32;
    }
}

static void calculate_mock_checksums(void) {
    /* Calculate and set proper checksums for test data */
    uint16_t checksum_3c515 = 0;
    for (int i = 0; i < EEPROM_MAX_SIZE - 1; i++) {
        checksum_3c515 += mock_3c515_eeprom[i];
    }
    mock_3c515_eeprom[EEPROM_MAX_SIZE - 1] = (~checksum_3c515) + 1;
    
    uint16_t checksum_3c509b = 0;
    for (int i = 0; i < 31; i++) {
        checksum_3c509b += mock_3c509b_eeprom[i];
    }
    mock_3c509b_eeprom[31] = (~checksum_3c509b) + 1;
}

/* Export test function for test runner */
TEST_EXPORT(test_eeprom_main);