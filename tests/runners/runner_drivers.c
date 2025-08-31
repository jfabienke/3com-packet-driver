/**
 * @file runner_drivers.c
 * @brief Comprehensive test runner for 3C509B and 3C515-TX network card drivers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test runner orchestrates comprehensive testing of both NIC drivers,
 * providing options for running individual tests, full suites, or stress tests.
 */

#include "../common/test_framework.h"
#include "../common/hardware_mock.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Forward declarations from test modules */
extern int run_3c509b_comprehensive_tests(void);
extern int run_3c515_comprehensive_tests(void);
extern int run_3c509b_test_by_name(const char *test_name);
extern int run_3c515_test_by_name(const char *test_name);

/* Test configuration and state */
typedef struct {
    bool run_3c509b_tests;
    bool run_3c515_tests;
    bool run_stress_tests;
    bool run_individual_tests;
    bool verbose_output;
    char specific_test[64];
    char specific_driver[16];
} driver_test_config_t;

/* Test statistics */
typedef struct {
    int total_tests_run;
    int total_tests_passed;
    int total_tests_failed;
    int suites_run;
    int suites_passed;
    uint32_t total_duration_ms;
} driver_test_statistics_t;

static driver_test_config_t g_test_config = {
    .run_3c509b_tests = true,
    .run_3c515_tests = true,
    .run_stress_tests = false,
    .run_individual_tests = false,
    .verbose_output = false,
    .specific_test = "",
    .specific_driver = ""
};

static driver_test_statistics_t g_test_stats = {0};

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Comprehensive test runner for 3Com NIC drivers\n\n");
    printf("OPTIONS:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --verbose           Enable verbose output\n");
    printf("  -3c509b                 Run only 3C509B tests\n");
    printf("  -3c515                  Run only 3C515-TX tests\n");
    printf("  -stress                 Include stress tests\n");
    printf("  -test <name>            Run specific test by name\n");
    printf("  -driver <driver>        Specify driver for specific test (3c509b|3c515)\n");
    printf("  -list                   List available tests\n");
    printf("\n");
    printf("EXAMPLES:\n");
    printf("  %s                                    # Run all driver tests\n", program_name);
    printf("  %s -3c509b -verbose                   # Run 3C509B tests with verbose output\n", program_name);
    printf("  %s -test window_selection -driver 3c509b  # Run specific 3C509B test\n", program_name);
    printf("  %s -stress                            # Run all tests including stress tests\n", program_name);
}

/**
 * @brief List available tests
 */
static void list_available_tests(void) {
    printf("Available 3C509B tests:\n");
    printf("  window_selection    - Test window selection mechanism\n");
    printf("  eeprom_read        - Test EEPROM read operations\n");
    printf("  mac_address        - Test MAC address reading from EEPROM\n");
    printf("  media_setup        - Test media auto-detection and setup\n");
    printf("  rx_filter          - Test receive filter configuration\n");
    printf("  packet_tx          - Test packet transmission\n");
    printf("  packet_rx          - Test packet reception\n");
    printf("  error_handling     - Test error handling and edge cases\n");
    printf("  self_test          - Test self-test functionality\n");
    printf("  interrupts         - Test interrupt handling\n");
    printf("  stress             - Test stress conditions\n");
    printf("\n");
    
    printf("Available 3C515-TX tests:\n");
    printf("  descriptor_init    - Test descriptor ring initialization\n");
    printf("  dma_setup          - Test DMA engine setup and configuration\n");
    printf("  dma_tx             - Test DMA transmission\n");
    printf("  dma_rx             - Test DMA reception\n");
    printf("  ring_management    - Test descriptor ring management\n");
    printf("  pci_config         - Test PCI configuration\n");
    printf("  performance        - Test performance optimization paths\n");
    printf("  error_recovery     - Test error recovery mechanisms\n");
    printf("  bus_mastering      - Test bus mastering DMA operations\n");
    printf("  stress             - Test stress conditions\n");
}

/**
 * @brief Parse command line arguments
 */
static int parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_test_config.verbose_output = true;
        } else if (strcmp(argv[i], "-3c509b") == 0) {
            g_test_config.run_3c509b_tests = true;
            g_test_config.run_3c515_tests = false;
        } else if (strcmp(argv[i], "-3c515") == 0) {
            g_test_config.run_3c509b_tests = false;
            g_test_config.run_3c515_tests = true;
        } else if (strcmp(argv[i], "-stress") == 0) {
            g_test_config.run_stress_tests = true;
        } else if (strcmp(argv[i], "-test") == 0) {
            if (i + 1 < argc) {
                strncpy(g_test_config.specific_test, argv[++i], 
                       sizeof(g_test_config.specific_test) - 1);
                g_test_config.run_individual_tests = true;
            } else {
                printf("Error: -test requires a test name\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-driver") == 0) {
            if (i + 1 < argc) {
                strncpy(g_test_config.specific_driver, argv[++i], 
                       sizeof(g_test_config.specific_driver) - 1);
            } else {
                printf("Error: -driver requires a driver name\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-list") == 0) {
            list_available_tests();
            return 1;
        } else {
            printf("Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief Initialize test environment
 */
static int initialize_test_environment(void) {
    /* Initialize logging system */
    int result = logging_init();
    if (result != 0) {
        printf("Failed to initialize logging system\n");
        return result;
    }
    
    /* Set log level based on verbosity */
    if (g_test_config.verbose_output) {
        log_set_level(LOG_LEVEL_DEBUG);
    } else {
        log_set_level(LOG_LEVEL_INFO);
    }
    
    /* Initialize memory system */
    result = memory_init();
    if (result != 0) {
        log_error("Failed to initialize memory system");
        return result;
    }
    
    /* Initialize mock framework */
    result = mock_framework_init();
    if (result != 0) {
        log_error("Failed to initialize mock framework");
        return result;
    }
    
    log_info("Driver test environment initialized successfully");
    return 0;
}

/**
 * @brief Cleanup test environment
 */
static void cleanup_test_environment(void) {
    mock_framework_cleanup();
    memory_cleanup();
    logging_cleanup();
}

/**
 * @brief Run individual test by name and driver
 */
static int run_individual_test(const char *test_name, const char *driver_name) {
    int result = -1;
    
    log_info("Running individual test: %s for driver: %s", test_name, driver_name);
    
    if (strcmp(driver_name, "3c509b") == 0) {
        result = run_3c509b_test_by_name(test_name);
    } else if (strcmp(driver_name, "3c515") == 0) {
        result = run_3c515_test_by_name(test_name);
    } else {
        log_error("Unknown driver name: %s", driver_name);
        return -1;
    }
    
    g_test_stats.total_tests_run++;
    if (result == 0) {
        g_test_stats.total_tests_passed++;
        log_info("Individual test PASSED: %s (%s)", test_name, driver_name);
        return 0;
    } else {
        g_test_stats.total_tests_failed++;
        log_error("Individual test FAILED: %s (%s)", test_name, driver_name);
        return -1;
    }
}

/**
 * @brief Run comprehensive test suite
 */
static int run_comprehensive_tests(void) {
    int overall_result = 0;
    uint32_t start_time = 0; /* In real implementation, get actual timestamp */
    
    log_info("Starting comprehensive NIC driver tests");
    
    /* Run 3C509B tests */
    if (g_test_config.run_3c509b_tests) {
        log_info("=== Running 3C509B Test Suite ===");
        g_test_stats.suites_run++;
        
        int result = run_3c509b_comprehensive_tests();
        if (result == 0) {
            g_test_stats.suites_passed++;
            log_info("3C509B test suite PASSED");
        } else {
            overall_result = -1;
            log_error("3C509B test suite FAILED");
        }
    }
    
    /* Run 3C515-TX tests */
    if (g_test_config.run_3c515_tests) {
        log_info("=== Running 3C515-TX Test Suite ===");
        g_test_stats.suites_run++;
        
        int result = run_3c515_comprehensive_tests();
        if (result == 0) {
            g_test_stats.suites_passed++;
            log_info("3C515-TX test suite PASSED");
        } else {
            overall_result = -1;
            log_error("3C515-TX test suite FAILED");
        }
    }
    
    /* Run stress tests if requested */
    if (g_test_config.run_stress_tests) {
        log_info("=== Running Stress Tests ===");
        
        /* Run stress-specific tests for both drivers */
        if (g_test_config.run_3c509b_tests) {
            int stress_result = run_3c509b_test_by_name("stress");
            if (stress_result != 0) {
                overall_result = -1;
                log_error("3C509B stress test FAILED");
            } else {
                log_info("3C509B stress test PASSED");
            }
        }
        
        if (g_test_config.run_3c515_tests) {
            int stress_result = run_3c515_test_by_name("stress");
            if (stress_result != 0) {
                overall_result = -1;
                log_error("3C515-TX stress test FAILED");
            } else {
                log_info("3C515-TX stress test PASSED");
            }
        }
    }
    
    uint32_t end_time = start_time + 1000; /* Simulate 1 second duration */
    g_test_stats.total_duration_ms = end_time - start_time;
    
    return overall_result;
}

/**
 * @brief Print test statistics and summary
 */
static void print_test_summary(int overall_result) {
    printf("\n");
    printf("==================================================\n");
    printf("           NIC DRIVER TEST SUMMARY\n");
    printf("==================================================\n");
    printf("Test suites run:       %d\n", g_test_stats.suites_run);
    printf("Test suites passed:    %d\n", g_test_stats.suites_passed);
    printf("Test suites failed:    %d\n", g_test_stats.suites_run - g_test_stats.suites_passed);
    printf("Total tests run:       %d\n", g_test_stats.total_tests_run);
    printf("Total tests passed:    %d\n", g_test_stats.total_tests_passed);
    printf("Total tests failed:    %d\n", g_test_stats.total_tests_failed);
    printf("Total duration:        %d ms\n", g_test_stats.total_duration_ms);
    printf("==================================================\n");
    
    if (overall_result == 0) {
        printf("RESULT: ALL DRIVER TESTS PASSED\n");
    } else {
        printf("RESULT: SOME DRIVER TESTS FAILED\n");
    }
    printf("==================================================\n");
    
    /* Print hardware mock statistics if available */
    mock_statistics_t mock_stats;
    if (mock_get_statistics(&mock_stats) == 0) {
        printf("\nHardware Mock Statistics:\n");
        printf("  I/O operations:      %d\n", mock_stats.total_io_operations);
        printf("  Packets injected:    %d\n", mock_stats.packets_injected);
        printf("  Packets extracted:   %d\n", mock_stats.packets_extracted);
        printf("  Interrupts generated: %d\n", mock_stats.interrupts_generated);
        printf("  Errors injected:     %d\n", mock_stats.errors_injected);
    }
}

/**
 * @brief Validate test configuration
 */
static int validate_test_configuration(void) {
    /* Check if individual test requires driver specification */
    if (g_test_config.run_individual_tests) {
        if (strlen(g_test_config.specific_driver) == 0) {
            printf("Error: Individual test requires -driver specification\n");
            return -1;
        }
        
        if (strcmp(g_test_config.specific_driver, "3c509b") != 0 && 
            strcmp(g_test_config.specific_driver, "3c515") != 0) {
            printf("Error: Driver must be '3c509b' or '3c515'\n");
            return -1;
        }
    }
    
    /* Ensure at least one test type is enabled */
    if (!g_test_config.run_individual_tests && 
        !g_test_config.run_3c509b_tests && 
        !g_test_config.run_3c515_tests) {
        printf("Error: No tests selected to run\n");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Main driver test runner entry point (called from master runner)
 */
int run_driver_tests(int argc, char *argv[]) {
    int result = 0;
    
    log_info("Starting 3Com NIC Driver Test Suite");
    log_info("===================================");
    
    /* Parse driver-specific arguments */
    result = parse_arguments(argc, argv);
    if (result != 0) {
        return (result > 0) ? 0 : 1;  /* 1 = help/list, -1 = error */
    }
    
    /* Validate configuration */
    result = validate_test_configuration();
    if (result != 0) {
        return 1;
    }
    
    /* Initialize test environment */
    result = initialize_test_environment();
    if (result != 0) {
        log_error("Failed to initialize driver test environment");
        return 1;
    }
    
    /* Run tests based on configuration */
    if (g_test_config.run_individual_tests) {
        result = run_individual_test(g_test_config.specific_test, 
                                   g_test_config.specific_driver);
    } else {
        result = run_comprehensive_tests();
    }
    
    /* Print summary */
    print_test_summary(result);
    
    /* Cleanup */
    cleanup_test_environment();
    
    return (result == 0) ? 0 : 1;
}

/**
 * @brief Standalone entry point (when run directly)
 */
int main(int argc, char *argv[]) {
    printf("3Com NIC Driver Comprehensive Test Suite\n");
    printf("========================================\n\n");
    
    return run_driver_tests(argc, argv);
}

/**
 * @brief Quick validation test function
 * Can be called from other test frameworks
 */
int quick_driver_validation_test(void) {
    int result = initialize_test_environment();
    if (result != 0) {
        return result;
    }
    
    /* Run minimal tests for both drivers */
    int r1 = run_3c509b_test_by_name("window_selection");
    int r2 = run_3c515_test_by_name("descriptor_init");
    
    cleanup_test_environment();
    
    return (r1 == 0 && r2 == 0) ? 0 : -1;
}