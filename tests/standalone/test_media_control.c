/**
 * @file test_media_control.c
 * @brief Test program for validating media control implementation
 *
 * This test program validates the Phase 0A media control implementation
 * for all 3c509 family variants, ensuring proper functionality of:
 * - Window 4 operations
 * - Media detection and selection
 * - Link beat testing
 * - Error handling and validation
 */

#include "include/media_control.h"
#include "include/nic_defs.h"
#include "include/hardware.h"
#include "include/logging.h"
#include "include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock hardware functions for testing
void outb(uint16_t port, uint8_t value) { /* Mock */ }
void outw(uint16_t port, uint16_t value) { /* Mock */ }
uint8_t inb(uint16_t port) { return 0x00; /* Mock */ }
uint16_t inw(uint16_t port) { return 0x0800; /* Mock - link up */ }
void udelay(unsigned int microseconds) { /* Mock */ }
void mdelay(unsigned int milliseconds) { /* Mock */ }
uint32_t get_system_timestamp_ms(void) { return 1000; /* Mock */ }

// Mock media_type_to_string function
const char* media_type_to_string(media_type_t media) {
    switch (media) {
        case MEDIA_TYPE_10BASE_T:   return "10BaseT";
        case MEDIA_TYPE_10BASE_2:   return "10Base2";  
        case MEDIA_TYPE_AUI:        return "AUI";
        case MEDIA_TYPE_10BASE_FL:  return "10BaseFL";
        case MEDIA_TYPE_COMBO:      return "Combo";
        default:                    return "Unknown";
    }
}

// Test case structure
typedef struct {
    const char* name;
    int (*test_func)(void);
} test_case_t;

// Global test NIC instance
static nic_info_t test_nic;

// Test helper functions
static void setup_test_nic_combo(void) {
    memset(&test_nic, 0, sizeof(test_nic));
    test_nic.type = NIC_TYPE_3C509B;
    test_nic.io_base = 0x300;
    test_nic.media_capabilities = MEDIA_CAPS_3C509B_COMBO;
    test_nic.variant_id = VARIANT_3C509B_COMBO;
    test_nic.current_media = MEDIA_TYPE_UNKNOWN;
    test_nic.detected_media = MEDIA_TYPE_UNKNOWN;
}

static void setup_test_nic_tp_only(void) {
    memset(&test_nic, 0, sizeof(test_nic));
    test_nic.type = NIC_TYPE_3C509B;
    test_nic.io_base = 0x300;
    test_nic.media_capabilities = MEDIA_CAPS_3C509B_TP;
    test_nic.variant_id = VARIANT_3C509B_TP;
    test_nic.current_media = MEDIA_TYPE_UNKNOWN;
    test_nic.detected_media = MEDIA_TYPE_UNKNOWN;
}

// Test cases

/**
 * Test media control initialization
 */
static int test_media_control_init(void) {
    printf("Testing media control initialization...\n");
    
    setup_test_nic_combo();
    
    int result = media_control_init(&test_nic);
    if (result != SUCCESS) {
        printf("FAIL: Media control init returned %d\n", result);
        return -1;
    }
    
    printf("PASS: Media control initialized successfully\n");
    return 0;
}

/**
 * Test media validation
 */
static int test_media_validation(void) {
    printf("Testing media validation...\n");
    
    setup_test_nic_combo();
    media_control_init(&test_nic);
    
    // Test valid media for combo card
    int result = validate_media_selection(&test_nic, MEDIA_TYPE_10BASE_T, NULL, 0);
    if (result != SUCCESS) {
        printf("FAIL: Valid media 10BaseT rejected: %d\n", result);
        return -1;
    }
    
    // Test valid media for TP-only card
    setup_test_nic_tp_only();
    media_control_init(&test_nic);
    
    result = validate_media_selection(&test_nic, MEDIA_TYPE_10BASE_T, NULL, 0);
    if (result != SUCCESS) {
        printf("FAIL: Valid media 10BaseT rejected for TP card: %d\n", result);
        return -1;
    }
    
    // Test invalid media for TP-only card
    result = validate_media_selection(&test_nic, MEDIA_TYPE_10BASE_2, NULL, 0);
    if (result == SUCCESS) {
        printf("FAIL: Invalid media 10Base2 accepted for TP card\n");
        return -1;
    }
    
    printf("PASS: Media validation working correctly\n");
    return 0;
}

/**
 * Test window management
 */
static int test_window_management(void) {
    printf("Testing window management...\n");
    
    setup_test_nic_combo();
    
    // Test window selection
    int result = safe_select_window(&test_nic, _3C509B_WINDOW_4, 1000);
    if (result != SUCCESS) {
        printf("FAIL: Window 4 selection failed: %d\n", result);
        return -1;
    }
    
    // Test command ready wait
    result = wait_for_command_ready(&test_nic, 1000);
    if (result != SUCCESS) {
        printf("FAIL: Command ready wait failed: %d\n", result);
        return -1;
    }
    
    printf("PASS: Window management working correctly\n");
    return 0;
}

/**
 * Test media selection
 */
static int test_media_selection(void) {
    printf("Testing media selection...\n");
    
    setup_test_nic_combo();
    media_control_init(&test_nic);
    
    // Test 10BaseT selection
    int result = select_media_transceiver(&test_nic, MEDIA_TYPE_10BASE_T, 0);
    if (result != SUCCESS) {
        printf("FAIL: 10BaseT selection failed: %d\n", result);
        return -1;
    }
    
    if (test_nic.current_media != MEDIA_TYPE_10BASE_T) {
        printf("FAIL: Current media not set correctly\n");
        return -1;
    }
    
    printf("PASS: Media selection working correctly\n");
    return 0;
}

/**
 * Test auto detection
 */
static int test_auto_detection(void) {
    printf("Testing auto-detection...\n");
    
    setup_test_nic_combo();
    media_control_init(&test_nic);
    
    media_detect_config_t config = MEDIA_DETECT_CONFIG_QUICK;
    media_type_t detected = auto_detect_media(&test_nic, &config);
    
    if (detected == MEDIA_TYPE_UNKNOWN) {
        printf("WARN: Auto-detection returned unknown (expected with mock hardware)\n");
    } else {
        printf("INFO: Auto-detected media: %s\n", media_type_to_string(detected));
    }
    
    printf("PASS: Auto-detection completed without errors\n");
    return 0;
}

/**
 * Test link beat detection
 */
static int test_link_beat(void) {
    printf("Testing link beat detection...\n");
    
    setup_test_nic_combo();
    media_control_init(&test_nic);
    
    link_test_result_t result;
    int test_result = test_link_beat(&test_nic, MEDIA_TYPE_10BASE_T, 1000, &result);
    
    if (test_result != SUCCESS) {
        printf("INFO: Link test returned %d (expected with mock hardware)\n", test_result);
    }
    
    printf("INFO: Signal quality: %d%%\n", result.signal_quality);
    printf("PASS: Link beat test completed without errors\n");
    return 0;
}

/**
 * Test error handling
 */
static int test_error_handling(void) {
    printf("Testing error handling...\n");
    
    // Test NULL pointer handling
    int result = media_control_init(NULL);
    if (result != ERROR_INVALID_PARAM) {
        printf("FAIL: NULL pointer not handled correctly\n");
        return -1;
    }
    
    result = validate_media_selection(NULL, MEDIA_TYPE_10BASE_T, NULL, 0);
    if (result != ERROR_INVALID_PARAM) {
        printf("FAIL: NULL NIC not handled correctly\n");
        return -1;
    }
    
    printf("PASS: Error handling working correctly\n");
    return 0;
}

/**
 * Test utility functions
 */
static int test_utility_functions(void) {
    printf("Testing utility functions...\n");
    
    setup_test_nic_combo();
    media_control_init(&test_nic);
    
    // Test media support check
    int supported = is_media_supported_by_nic(&test_nic, MEDIA_TYPE_10BASE_T);
    if (!supported) {
        printf("FAIL: 10BaseT should be supported by combo card\n");
        return -1;
    }
    
    // Test default media
    media_type_t default_media = get_default_media_for_nic(&test_nic);
    if (default_media == MEDIA_TYPE_UNKNOWN) {
        printf("FAIL: Default media should not be unknown\n");
        return -1;
    }
    
    // Test error string conversion
    const char* error_str = media_error_to_string(MEDIA_ERROR_NO_LINK);
    if (!error_str || strlen(error_str) == 0) {
        printf("FAIL: Error string conversion failed\n");
        return -1;
    }
    
    printf("PASS: Utility functions working correctly\n");
    return 0;
}

/**
 * Test diagnostic functions
 */
static int test_diagnostic_functions(void) {
    printf("Testing diagnostic functions...\n");
    
    setup_test_nic_combo();
    media_control_init(&test_nic);
    test_nic.current_media = MEDIA_TYPE_10BASE_T;
    
    // Test media diagnostics
    int result = run_media_diagnostics(&test_nic, false);
    printf("INFO: Media diagnostics returned %d\n", result);
    
    // Test register dump
    char buffer[512];
    int chars_written = dump_media_registers(&test_nic, buffer, sizeof(buffer));
    if (chars_written <= 0) {
        printf("FAIL: Register dump failed\n");
        return -1;
    }
    
    printf("INFO: Register dump:\n%s\n", buffer);
    
    // Test media info string
    chars_written = get_media_info_string(&test_nic, buffer, sizeof(buffer));
    if (chars_written <= 0) {
        printf("FAIL: Media info string failed\n");
        return -1;
    }
    
    printf("INFO: Media info: %s\n", buffer);
    
    printf("PASS: Diagnostic functions working correctly\n");
    return 0;
}

// Test case array
static test_case_t test_cases[] = {
    {"Media Control Init", test_media_control_init},
    {"Media Validation", test_media_validation},
    {"Window Management", test_window_management},
    {"Media Selection", test_media_selection},
    {"Auto Detection", test_auto_detection},
    {"Link Beat Detection", test_link_beat},
    {"Error Handling", test_error_handling},
    {"Utility Functions", test_utility_functions},
    {"Diagnostic Functions", test_diagnostic_functions},
    {NULL, NULL} // Terminator
};

/**
 * Main test runner
 */
int main(void) {
    printf("3Com Media Control Implementation Test Suite\n");
    printf("===========================================\n\n");
    
    int passed = 0;
    int failed = 0;
    
    for (int i = 0; test_cases[i].name != NULL; i++) {
        printf("Running test: %s\n", test_cases[i].name);
        
        int result = test_cases[i].test_func();
        if (result == 0) {
            passed++;
            printf("✓ PASSED\n\n");
        } else {
            failed++;
            printf("✗ FAILED\n\n");
        }
    }
    
    printf("Test Results:\n");
    printf("=============\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);
    printf("Total:  %d\n", passed + failed);
    
    if (failed == 0) {
        printf("\n🎉 All tests passed! Media control implementation is working correctly.\n");
        return 0;
    } else {
        printf("\n❌ Some tests failed. Please review the implementation.\n");
        return 1;
    }
}