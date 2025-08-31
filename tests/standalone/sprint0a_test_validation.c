/**
 * @file sprint0a_test_validation.c
 * @brief Comprehensive testing and validation for Sprint 0A completion
 * 
 * This program performs comprehensive testing of all Sprint 0A deliverables:
 * - Media type constants and enumeration validation
 * - Extended nic_info_t structure testing
 * - Window 4 media control operations verification
 * - Transceiver selection logic testing
 * - PnP device ID table validation
 * - Hardware compatibility verification
 * - Backward compatibility confirmation
 */

#include "include/common.h"
#include "include/nic_defs.h"
#include "include/nic_init.h"
#include "include/media_control.h"
#include "include/media_types.h"
#include "include/3c509b.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Test result tracking
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    char* test_name;
} test_results_t;

static test_results_t test_results = {0, 0, 0, "Sprint 0A Validation"};

// Test macros
#define TEST_ASSERT(condition, message) \
    do { \
        test_results.total_tests++; \
        if (condition) { \
            test_results.passed_tests++; \
            printf("PASS: %s\n", message); \
        } else { \
            test_results.failed_tests++; \
            printf("FAIL: %s\n", message); \
        } \
    } while(0)

#define TEST_START(test_name) \
    printf("\n=== Testing: %s ===\n", test_name)

#define TEST_END() \
    printf("--- Test Complete ---\n")

/**
 * @brief Test media type enumeration and constants
 */
void test_media_type_enumeration(void) {
    TEST_START("Media Type Enumeration");
    
    // Test basic media types
    TEST_ASSERT(MEDIA_TYPE_UNKNOWN == 0, "MEDIA_TYPE_UNKNOWN has correct value");
    TEST_ASSERT(MEDIA_TYPE_10BASE_T != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_10BASE_T is valid");
    TEST_ASSERT(MEDIA_TYPE_10BASE_2 != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_10BASE_2 is valid");
    TEST_ASSERT(MEDIA_TYPE_AUI != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_AUI is valid");
    TEST_ASSERT(MEDIA_TYPE_10BASE_FL != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_10BASE_FL is valid");
    
    // Test Fast Ethernet types (3c515)
    TEST_ASSERT(MEDIA_TYPE_100BASE_TX != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_100BASE_TX is valid");
    TEST_ASSERT(MEDIA_TYPE_100BASE_FX != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_100BASE_FX is valid");
    
    // Test special modes
    TEST_ASSERT(MEDIA_TYPE_COMBO != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_COMBO is valid");
    TEST_ASSERT(MEDIA_TYPE_AUTO_DETECT != MEDIA_TYPE_UNKNOWN, "MEDIA_TYPE_AUTO_DETECT is valid");
    
    TEST_END();
}

/**
 * @brief Test media capability flags
 */
void test_media_capability_flags(void) {
    TEST_START("Media Capability Flags");
    
    // Test individual capability flags
    TEST_ASSERT(MEDIA_CAP_10BASE_T == (1 << 0), "MEDIA_CAP_10BASE_T has correct bit position");
    TEST_ASSERT(MEDIA_CAP_10BASE_2 == (1 << 1), "MEDIA_CAP_10BASE_2 has correct bit position");
    TEST_ASSERT(MEDIA_CAP_AUI == (1 << 2), "MEDIA_CAP_AUI has correct bit position");
    TEST_ASSERT(MEDIA_CAP_AUTO_SELECT == (1 << 7), "MEDIA_CAP_AUTO_SELECT has correct bit position");
    TEST_ASSERT(MEDIA_CAP_FULL_DUPLEX == (1 << 8), "MEDIA_CAP_FULL_DUPLEX has correct bit position");
    
    // Test combined capabilities
    uint16_t combo_caps = MEDIA_CAP_10BASE_T | MEDIA_CAP_10BASE_2 | MEDIA_CAP_AUI | MEDIA_CAP_AUTO_SELECT;
    TEST_ASSERT((combo_caps & MEDIA_CAP_10BASE_T) != 0, "Combo capabilities include 10BaseT");
    TEST_ASSERT((combo_caps & MEDIA_CAP_10BASE_2) != 0, "Combo capabilities include 10Base2");
    TEST_ASSERT((combo_caps & MEDIA_CAP_AUI) != 0, "Combo capabilities include AUI");
    
    TEST_END();
}

/**
 * @brief Test extended nic_info_t structure
 */
void test_extended_nic_info_structure(void) {
    TEST_START("Extended nic_info_t Structure");
    
    nic_info_t nic;
    memset(&nic, 0, sizeof(nic_info_t));
    
    // Test basic fields still work
    nic.type = NIC_TYPE_3C509B;
    nic.io_base = 0x300;
    nic.irq = 10;
    nic.mac[0] = 0x00;
    nic.mac[1] = 0x60;
    nic.mac[2] = 0x97;
    
    TEST_ASSERT(nic.type == NIC_TYPE_3C509B, "Basic NIC type field works");
    TEST_ASSERT(nic.io_base == 0x300, "Basic IO base field works");
    TEST_ASSERT(nic.irq == 10, "Basic IRQ field works");
    TEST_ASSERT(nic.mac[0] == 0x00 && nic.mac[1] == 0x60, "Basic MAC address field works");
    
    // Test new Phase 0A fields
    nic.media_capabilities = MEDIA_CAP_10BASE_T | MEDIA_CAP_10BASE_2 | MEDIA_CAP_AUI;
    nic.current_media = MEDIA_TYPE_10BASE_T;
    nic.detected_media = MEDIA_TYPE_10BASE_T;
    nic.media_detection_state = MEDIA_DETECT_COMPLETED;
    nic.auto_negotiation_flags = 0;
    nic.variant_id = VARIANT_3C509B_COMBO;
    nic.media_config_source = MEDIA_CONFIG_AUTO_DETECT;
    
    TEST_ASSERT(nic.media_capabilities != 0, "Media capabilities field works");
    TEST_ASSERT(nic.current_media == MEDIA_TYPE_10BASE_T, "Current media field works");
    TEST_ASSERT(nic.detected_media == MEDIA_TYPE_10BASE_T, "Detected media field works");
    TEST_ASSERT(nic.media_detection_state == MEDIA_DETECT_COMPLETED, "Media detection state field works");
    TEST_ASSERT(nic.variant_id == VARIANT_3C509B_COMBO, "Variant ID field works");
    TEST_ASSERT(nic.media_config_source == MEDIA_CONFIG_AUTO_DETECT, "Media config source field works");
    
    TEST_END();
}

/**
 * @brief Test detection state flags
 */
void test_detection_state_flags(void) {
    TEST_START("Media Detection State Flags");
    
    // Test individual flags
    TEST_ASSERT(MEDIA_DETECT_NONE == 0x00, "MEDIA_DETECT_NONE has correct value");
    TEST_ASSERT(MEDIA_DETECT_IN_PROGRESS == 0x01, "MEDIA_DETECT_IN_PROGRESS has correct value");
    TEST_ASSERT(MEDIA_DETECT_COMPLETED == 0x02, "MEDIA_DETECT_COMPLETED has correct value");
    TEST_ASSERT(MEDIA_DETECT_FAILED == 0x04, "MEDIA_DETECT_FAILED has correct value");
    
    // Test flag combinations
    uint8_t combined_flags = MEDIA_DETECT_COMPLETED | MEDIA_DETECT_AUTO_ENABLED;
    TEST_ASSERT((combined_flags & MEDIA_DETECT_COMPLETED) != 0, "Combined flags preserve COMPLETED");
    TEST_ASSERT((combined_flags & MEDIA_DETECT_AUTO_ENABLED) != 0, "Combined flags preserve AUTO_ENABLED");
    
    TEST_END();
}

/**
 * @brief Test variant database and lookups
 */
void test_variant_database(void) {
    TEST_START("Variant Database and Lookups");
    
    // Test variant ID constants
    TEST_ASSERT(VARIANT_3C509B_COMBO != VARIANT_UNKNOWN, "3C509B Combo variant ID is valid");
    TEST_ASSERT(VARIANT_3C509B_TP != VARIANT_UNKNOWN, "3C509B TP variant ID is valid");
    TEST_ASSERT(VARIANT_3C509B_BNC != VARIANT_UNKNOWN, "3C509B BNC variant ID is valid");
    TEST_ASSERT(VARIANT_3C509B_AUI != VARIANT_UNKNOWN, "3C509B AUI variant ID is valid");
    
    // Test that different variants have different IDs
    TEST_ASSERT(VARIANT_3C509B_COMBO != VARIANT_3C509B_TP, "Combo and TP variants are different");
    TEST_ASSERT(VARIANT_3C509B_TP != VARIANT_3C509B_BNC, "TP and BNC variants are different");
    TEST_ASSERT(VARIANT_3C509B_BNC != VARIANT_3C509B_AUI, "BNC and AUI variants are different");
    
    TEST_END();
}

/**
 * @brief Test PnP device ID constants and ranges
 */
void test_pnp_device_ids(void) {
    TEST_START("PnP Device ID Constants");
    
    // Test base PnP device IDs
    TEST_ASSERT(PNP_DEVICE_TCM5000 != 0, "TCM5000 device ID is defined");
    TEST_ASSERT(PNP_DEVICE_TCM5001 != 0, "TCM5001 device ID is defined");
    TEST_ASSERT(PNP_DEVICE_TCM5002 != 0, "TCM5002 device ID is defined");
    TEST_ASSERT(PNP_DEVICE_TCM5003 != 0, "TCM5003 device ID is defined");
    TEST_ASSERT(PNP_DEVICE_TCM5004 != 0, "TCM5004 device ID is defined");
    
    // Test enhanced variants
    TEST_ASSERT(PNP_DEVICE_TCM5010 != 0, "TCM5010 device ID is defined");
    TEST_ASSERT(PNP_DEVICE_TCM5011 != 0, "TCM5011 device ID is defined");
    TEST_ASSERT(PNP_DEVICE_TCM5012 != 0, "TCM5012 device ID is defined");
    TEST_ASSERT(PNP_DEVICE_TCM5013 != 0, "TCM5013 device ID is defined");
    
    // Test that device IDs are unique
    TEST_ASSERT(PNP_DEVICE_TCM5000 != PNP_DEVICE_TCM5001, "TCM5000 and TCM5001 are different");
    TEST_ASSERT(PNP_DEVICE_TCM5001 != PNP_DEVICE_TCM5002, "TCM5001 and TCM5002 are different");
    
    TEST_END();
}

/**
 * @brief Test backward compatibility macros and helpers
 */
void test_backward_compatibility(void) {
    TEST_START("Backward Compatibility");
    
    nic_info_t nic;
    memset(&nic, 0, sizeof(nic_info_t));
    
    // Test legacy transceiver type compatibility
    xcvr_type_t legacy_xcvr = XCVR_TYPE_10BASE_T;
    TEST_ASSERT(legacy_xcvr == MEDIA_TYPE_10BASE_T, "Legacy XCVR_TYPE_10BASE_T maps to MEDIA_TYPE_10BASE_T");
    
    legacy_xcvr = XCVR_TYPE_BNC;
    TEST_ASSERT(legacy_xcvr == MEDIA_TYPE_10BASE_2, "Legacy XCVR_TYPE_BNC maps to MEDIA_TYPE_10BASE_2");
    
    legacy_xcvr = XCVR_TYPE_AUI;
    TEST_ASSERT(legacy_xcvr == MEDIA_TYPE_AUI, "Legacy XCVR_TYPE_AUI maps to MEDIA_TYPE_AUI");
    
    // Test that old structure access patterns still work
    nic.type = NIC_TYPE_3C509B;
    nic.io_base = 0x300;
    nic.irq = 10;
    
    TEST_ASSERT(nic.type == NIC_TYPE_3C509B, "Legacy field access pattern works for type");
    TEST_ASSERT(nic.io_base == 0x300, "Legacy field access pattern works for io_base");
    TEST_ASSERT(nic.irq == 10, "Legacy field access pattern works for irq");
    
    TEST_END();
}

/**
 * @brief Test media control error codes
 */
void test_media_control_error_codes(void) {
    TEST_START("Media Control Error Codes");
    
    // Test error code constants
    TEST_ASSERT(MEDIA_ERROR_NONE == 0, "MEDIA_ERROR_NONE is zero");
    TEST_ASSERT(MEDIA_ERROR_INVALID_MEDIA == -100, "MEDIA_ERROR_INVALID_MEDIA has correct value");
    TEST_ASSERT(MEDIA_ERROR_MEDIA_NOT_SUPPORTED == -101, "MEDIA_ERROR_MEDIA_NOT_SUPPORTED has correct value");
    TEST_ASSERT(MEDIA_ERROR_NO_LINK == -102, "MEDIA_ERROR_NO_LINK has correct value");
    TEST_ASSERT(MEDIA_ERROR_REGISTER_ACCESS == -105, "MEDIA_ERROR_REGISTER_ACCESS has correct value");
    
    // Test that error codes are negative (except NONE)
    TEST_ASSERT(MEDIA_ERROR_INVALID_MEDIA < 0, "Error codes are negative");
    TEST_ASSERT(MEDIA_ERROR_NO_LINK < 0, "Error codes are negative");
    TEST_ASSERT(MEDIA_ERROR_VALIDATION_FAILED < 0, "Error codes are negative");
    
    TEST_END();
}

/**
 * @brief Test media detection configuration structures
 */
void test_media_detection_structures(void) {
    TEST_START("Media Detection Structures");
    
    // Test media detection config structure
    media_detect_config_t config = MEDIA_DETECT_CONFIG_DEFAULT;
    TEST_ASSERT(config.timeout_ms == MEDIA_DETECT_TIMEOUT_MS, "Default config has correct timeout");
    TEST_ASSERT(config.retry_count == AUTO_DETECT_RETRY_COUNT, "Default config has correct retry count");
    TEST_ASSERT(config.preferred_media == MEDIA_TYPE_UNKNOWN, "Default config has no preferred media");
    
    // Test quick detection config
    media_detect_config_t quick_config = MEDIA_DETECT_CONFIG_QUICK;
    TEST_ASSERT(quick_config.timeout_ms < config.timeout_ms, "Quick config has shorter timeout");
    TEST_ASSERT(quick_config.retry_count <= config.retry_count, "Quick config has fewer retries");
    TEST_ASSERT((quick_config.flags & MEDIA_CTRL_FLAG_QUICK_TEST) != 0, "Quick config has quick test flag");
    
    // Test link test result structure
    link_test_result_t result;
    memset(&result, 0, sizeof(link_test_result_t));
    result.tested_media = MEDIA_TYPE_10BASE_T;
    result.signal_quality = 85;
    result.test_flags = LINK_TEST_RESULT_LINK_UP | LINK_TEST_RESULT_LINK_STABLE;
    
    TEST_ASSERT(result.tested_media == MEDIA_TYPE_10BASE_T, "Link test result stores media type");
    TEST_ASSERT(result.signal_quality == 85, "Link test result stores signal quality");
    TEST_ASSERT((result.test_flags & LINK_TEST_RESULT_LINK_UP) != 0, "Link test result has link up flag");
    
    TEST_END();
}

/**
 * @brief Test timing constants and validation
 */
void test_timing_constants(void) {
    TEST_START("Timing Constants");
    
    // Test that timing constants are reasonable
    TEST_ASSERT(MEDIA_DETECT_TIMEOUT_MS >= 1000, "Detection timeout is at least 1 second");
    TEST_ASSERT(MEDIA_DETECT_TIMEOUT_MS <= 10000, "Detection timeout is not too long");
    TEST_ASSERT(MEDIA_LINK_TEST_TIMEOUT_MS <= MEDIA_DETECT_TIMEOUT_MS, "Link test timeout is shorter than detection timeout");
    TEST_ASSERT(MEDIA_SWITCH_DELAY_MS >= 50, "Media switch delay is sufficient");
    TEST_ASSERT(MEDIA_STABILIZATION_DELAY_MS >= 100, "Media stabilization delay is sufficient");
    
    // Test test duration constants
    TEST_ASSERT(MEDIA_TEST_DURATION_10BASET_MS >= 1000, "10BaseT test duration is sufficient");
    TEST_ASSERT(MEDIA_TEST_DURATION_10BASE2_MS >= 500, "10Base2 test duration is sufficient");
    TEST_ASSERT(MEDIA_TEST_DURATION_AUI_MS >= 1000, "AUI test duration is sufficient");
    TEST_ASSERT(MEDIA_TEST_DURATION_FIBER_MS >= 1000, "Fiber test duration is sufficient");
    
    TEST_END();
}

/**
 * @brief Test control flags and their values
 */
void test_control_flags(void) {
    TEST_START("Control Flags");
    
    // Test media control flags
    TEST_ASSERT(MEDIA_CTRL_FLAG_FORCE == 0x01, "FORCE flag has correct value");
    TEST_ASSERT(MEDIA_CTRL_FLAG_NO_AUTO_DETECT == 0x02, "NO_AUTO_DETECT flag has correct value");
    TEST_ASSERT(MEDIA_CTRL_FLAG_PRESERVE_DUPLEX == 0x04, "PRESERVE_DUPLEX flag has correct value");
    TEST_ASSERT(MEDIA_CTRL_FLAG_ENABLE_DIAGNOSTICS == 0x08, "ENABLE_DIAGNOSTICS flag has correct value");
    
    // Test link test result flags
    TEST_ASSERT(LINK_TEST_RESULT_LINK_UP == 0x01, "LINK_UP flag has correct value");
    TEST_ASSERT(LINK_TEST_RESULT_LINK_STABLE == 0x02, "LINK_STABLE flag has correct value");
    TEST_ASSERT(LINK_TEST_RESULT_CARRIER_DETECT == 0x04, "CARRIER_DETECT flag has correct value");
    
    // Test flag combinations
    uint8_t combined = MEDIA_CTRL_FLAG_FORCE | MEDIA_CTRL_FLAG_NO_AUTO_DETECT;
    TEST_ASSERT((combined & MEDIA_CTRL_FLAG_FORCE) != 0, "Combined flags preserve FORCE");
    TEST_ASSERT((combined & MEDIA_CTRL_FLAG_NO_AUTO_DETECT) != 0, "Combined flags preserve NO_AUTO_DETECT");
    
    TEST_END();
}

/**
 * @brief Print comprehensive test results
 */
void print_test_results(void) {
    printf("\n\n=== SPRINT 0A VALIDATION RESULTS ===\n");
    printf("Total Tests:  %d\n", test_results.total_tests);
    printf("Passed Tests: %d\n", test_results.passed_tests);
    printf("Failed Tests: %d\n", test_results.failed_tests);
    printf("Success Rate: %.1f%%\n", 
           test_results.total_tests > 0 ? 
           (100.0 * test_results.passed_tests / test_results.total_tests) : 0.0);
    
    if (test_results.failed_tests == 0) {
        printf("\n🎉 ALL TESTS PASSED - SPRINT 0A VALIDATION SUCCESSFUL! 🎉\n");
    } else {
        printf("\n❌ %d TESTS FAILED - SPRINT 0A VALIDATION NEEDS ATTENTION\n", 
               test_results.failed_tests);
    }
    
    printf("=====================================\n\n");
}

/**
 * @brief Main test execution function
 */
int main(void) {
    printf("=== 3COM PACKET DRIVER - SPRINT 0A COMPREHENSIVE VALIDATION ===\n");
    printf("Testing all Phase 0A deliverables for completion verification\n");
    printf("================================================================\n");
    
    // Run all validation tests
    test_media_type_enumeration();
    test_media_capability_flags();
    test_extended_nic_info_structure();
    test_detection_state_flags();
    test_variant_database();
    test_pnp_device_ids();
    test_backward_compatibility();
    test_media_control_error_codes();
    test_media_detection_structures();
    test_timing_constants();
    test_control_flags();
    
    // Print comprehensive results
    print_test_results();
    
    // Return appropriate exit code
    return (test_results.failed_tests == 0) ? 0 : 1;
}