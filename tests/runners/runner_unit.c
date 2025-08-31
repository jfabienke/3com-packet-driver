/**
 * @file runner_unit.c
 * @brief Unit Test Runner - Comprehensive testing of all individual components
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test runner executes all unit tests including:
 * - Hardware abstraction layer tests
 * - Memory management tests
 * - API function tests
 * - Packet operation tests
 * - XMS memory tests
 * - IRQ handling tests
 * - Assembly API integration tests
 */

#include "../common/test_framework.h"
#include "../common/hardware_mock.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <stdio.h>
#include <string.h>

/* External test suite functions from unit test modules */
extern int test_3c509b_main(void);
extern int test_3c515_main(void);
extern int test_api_main(void);
extern int test_arp_main(void);
extern int test_asm_api_main(void);
extern int test_hardware_main(void);
extern int test_irq_main(void);
extern int test_memory_main(void);
extern int test_packet_ops_main(void);
extern int test_routing_main(void);
extern int test_xms_main(void);

/* Unit test configuration */
typedef struct {
    bool run_hardware_tests;
    bool run_memory_tests;
    bool run_api_tests;
    bool run_packet_tests;
    bool run_driver_tests;
    bool run_protocol_tests;
    bool run_asm_tests;
    bool run_irq_tests;
    bool run_xms_tests;
    bool verbose_output;
    bool stop_on_failure;
    const char *specific_test_suite;
} unit_test_config_t;

/* Unit test statistics */
typedef struct {
    int total_suites_run;
    int suites_passed;
    int suites_failed;
    int total_tests_run;
    int total_tests_passed;
    int total_tests_failed;
    uint32_t total_duration_ms;
} unit_test_stats_t;

/* Test suite definition */
typedef struct {
    const char *name;
    const char *description;
    int (*test_main)(void);
    bool *enabled_flag;
    bool is_critical;
} unit_test_suite_t;

static unit_test_config_t g_unit_config = {
    .run_hardware_tests = true,
    .run_memory_tests = true,
    .run_api_tests = true,
    .run_packet_tests = true,
    .run_driver_tests = true,
    .run_protocol_tests = true,
    .run_asm_tests = true,
    .run_irq_tests = true,
    .run_xms_tests = true,
    .verbose_output = false,
    .stop_on_failure = false,
    .specific_test_suite = NULL
};

static unit_test_stats_t g_unit_stats = {0};

/**
 * @brief Parse command line arguments for unit test configuration
 */
static int parse_unit_test_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_unit_config.verbose_output = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stop-on-failure") == 0) {
            g_unit_config.stop_on_failure = true;
        } else if (strcmp(argv[i], "--hardware-only") == 0) {
            g_unit_config.run_hardware_tests = true;
            g_unit_config.run_memory_tests = false;
            g_unit_config.run_api_tests = false;
            g_unit_config.run_packet_tests = false;
            g_unit_config.run_driver_tests = false;
            g_unit_config.run_protocol_tests = false;
            g_unit_config.run_asm_tests = false;
            g_unit_config.run_irq_tests = false;
            g_unit_config.run_xms_tests = false;
        } else if (strcmp(argv[i], "--memory-only") == 0) {
            g_unit_config.run_hardware_tests = false;
            g_unit_config.run_memory_tests = true;
            g_unit_config.run_api_tests = false;
            g_unit_config.run_packet_tests = false;
            g_unit_config.run_driver_tests = false;
            g_unit_config.run_protocol_tests = false;
            g_unit_config.run_asm_tests = false;
            g_unit_config.run_irq_tests = false;
            g_unit_config.run_xms_tests = false;
        } else if (strcmp(argv[i], "--drivers-only") == 0) {
            g_unit_config.run_hardware_tests = false;
            g_unit_config.run_memory_tests = false;
            g_unit_config.run_api_tests = false;
            g_unit_config.run_packet_tests = false;
            g_unit_config.run_driver_tests = true;
            g_unit_config.run_protocol_tests = false;
            g_unit_config.run_asm_tests = false;
            g_unit_config.run_irq_tests = false;
            g_unit_config.run_xms_tests = false;
        } else if (strcmp(argv[i], "--suite") == 0) {
            if (i + 1 < argc) {
                g_unit_config.specific_test_suite = argv[++i];
            } else {
                log_error("--suite requires a test suite name");
                return -1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Unit Test Runner - 3Com Packet Driver\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -v, --verbose        Enable verbose output\n");
            printf("  -s, --stop-on-failure Stop on first test failure\n");
            printf("  --hardware-only      Run only hardware tests\n");
            printf("  --memory-only        Run only memory tests\n");
            printf("  --drivers-only       Run only driver tests\n");
            printf("  --suite <name>       Run specific test suite\n");
            printf("  -h, --help           Show this help\n");
            printf("\nAvailable test suites:\n");
            printf("  hardware    - Hardware abstraction layer tests\n");
            printf("  memory      - Memory management tests\n");
            printf("  api         - API function tests\n");
            printf("  packets     - Packet operation tests\n");
            printf("  drivers     - Driver-specific tests (3C509B + 3C515-TX)\n");
            printf("  protocols   - Protocol tests (ARP + routing)\n");
            printf("  assembly    - Assembly API integration tests\n");
            printf("  irq         - Interrupt handling tests\n");
            printf("  xms         - XMS memory tests\n");
            return 1;
        }
    }
    
    return 0;
}

/**
 * @brief Initialize unit test environment
 */
static int initialize_unit_test_environment(void) {
    log_info("Initializing unit test environment");
    
    /* Initialize logging with appropriate level */
    int result = logging_init();
    if (result != 0) {
        printf("Failed to initialize logging system\n");
        return -1;
    }
    
    if (g_unit_config.verbose_output) {
        log_set_level(LOG_LEVEL_DEBUG);
    } else {
        log_set_level(LOG_LEVEL_INFO);
    }
    
    /* Initialize memory management */
    result = memory_init();
    if (result != 0) {
        log_error("Failed to initialize memory management");
        return -2;
    }
    
    /* Initialize hardware mock framework */
    result = mock_framework_init();
    if (result != 0) {
        log_error("Failed to initialize hardware mock framework");
        return -3;
    }
    
    /* Initialize test framework with unit test configuration */
    test_config_t test_config;
    test_config_init_default(&test_config);
    test_config.test_hardware = g_unit_config.run_hardware_tests;
    test_config.test_memory = g_unit_config.run_memory_tests;
    test_config.test_packet_ops = g_unit_config.run_packet_tests;
    test_config.run_benchmarks = false;  /* Unit tests don't run benchmarks */
    test_config.run_stress_tests = false; /* Unit tests don't run stress tests */
    test_config.verbose_output = g_unit_config.verbose_output;
    test_config.init_hardware = true;
    test_config.init_memory = true;
    test_config.init_diagnostics = true;
    
    result = test_framework_init(&test_config);
    if (result != 0) {
        log_error("Failed to initialize test framework");
        return -4;
    }
    
    log_info("Unit test environment initialized successfully");
    return 0;
}

/**
 * @brief Cleanup unit test environment
 */
static void cleanup_unit_test_environment(void) {
    log_info("Cleaning up unit test environment");
    
    test_framework_cleanup();
    mock_framework_cleanup();
    memory_cleanup();
    logging_cleanup();
    
    log_info("Unit test environment cleanup completed");
}

/**
 * @brief Run a specific unit test suite
 */
static int run_unit_test_suite(const unit_test_suite_t *suite) {
    if (!suite || !suite->test_main) {
        log_error("Invalid test suite");
        return -1;
    }
    
    log_info("=== Running Unit Test Suite: %s ===", suite->name);
    log_info("Description: %s", suite->description);
    
    uint32_t start_time = get_system_timestamp_ms();
    
    int result = suite->test_main();
    
    uint32_t end_time = get_system_timestamp_ms();
    uint32_t duration = end_time - start_time;
    
    g_unit_stats.total_suites_run++;
    
    if (result == 0) {
        g_unit_stats.suites_passed++;
        log_info("✓ Unit Test Suite PASSED: %s (duration: %lu ms)", suite->name, duration);
    } else {
        g_unit_stats.suites_failed++;
        log_error("✗ Unit Test Suite FAILED: %s (duration: %lu ms, code: %d)", 
                  suite->name, duration, result);
        
        if (suite->is_critical && g_unit_config.stop_on_failure) {
            log_error("Critical unit test suite failed, stopping execution");
            return result;
        }
    }
    
    return result;
}

/**
 * @brief Print unit test summary
 */
static void print_unit_test_summary(void) {
    log_info("");
    log_info("===================================================================");
    log_info("                    UNIT TEST SUITE SUMMARY");
    log_info("===================================================================");
    log_info("Test Suites Executed:");
    log_info("  Total Suites: %d", g_unit_stats.total_suites_run);
    log_info("  Passed: %d", g_unit_stats.suites_passed);
    log_info("  Failed: %d", g_unit_stats.suites_failed);
    log_info("");
    log_info("Individual Tests:");
    log_info("  Total Tests: %d", g_unit_stats.total_tests_run);
    log_info("  Passed: %d", g_unit_stats.total_tests_passed);
    log_info("  Failed: %d", g_unit_stats.total_tests_failed);
    log_info("");
    log_info("Execution Time:");
    log_info("  Total Duration: %lu ms (%.2f seconds)", 
             g_unit_stats.total_duration_ms, g_unit_stats.total_duration_ms / 1000.0);
    log_info("");
    
    if (g_unit_stats.suites_failed == 0) {
        log_info("Success Rate: 100%% - ALL UNIT TESTS PASSED! ✓");
    } else {
        float success_rate = (float)g_unit_stats.suites_passed / g_unit_stats.total_suites_run * 100.0;
        log_info("Success Rate: %.1f%% (%d/%d suites passed)", 
                 success_rate, g_unit_stats.suites_passed, g_unit_stats.total_suites_run);
        
        if (success_rate >= 80.0) {
            log_info("Result: GOOD - Most unit tests passed");
        } else if (success_rate >= 60.0) {
            log_warning("Result: ACCEPTABLE - Some unit tests failed");
        } else {
            log_error("Result: POOR - Many unit tests failed");
        }
    }
    
    log_info("===================================================================");
}

/**
 * @brief Main unit test runner entry point (called from master runner)
 */
int run_unit_tests(int argc, char *argv[]) {
    log_info("Starting Unit Test Suite Runner");
    log_info("===============================");
    
    /* Parse unit test specific arguments */
    int parse_result = parse_unit_test_arguments(argc, argv);
    if (parse_result == 1) {
        return 0;  /* Help was shown */
    } else if (parse_result < 0) {
        return 1;  /* Error in arguments */
    }
    
    /* Initialize unit test environment */
    int init_result = initialize_unit_test_environment();
    if (init_result != 0) {
        log_error("Failed to initialize unit test environment");
        return 1;
    }
    
    uint32_t overall_start_time = get_system_timestamp_ms();
    
    /* Define all unit test suites */
    unit_test_suite_t test_suites[] = {
        {
            .name = "Hardware Abstraction",
            .description = "Hardware abstraction layer, device detection, and I/O operations",
            .test_main = test_hardware_main,
            .enabled_flag = &g_unit_config.run_hardware_tests,
            .is_critical = true
        },
        {
            .name = "Memory Management",
            .description = "Memory allocation, deallocation, and management functions",
            .test_main = test_memory_main,
            .enabled_flag = &g_unit_config.run_memory_tests,
            .is_critical = true
        },
        {
            .name = "API Functions",
            .description = "Public API function testing and validation",
            .test_main = test_api_main,
            .enabled_flag = &g_unit_config.run_api_tests,
            .is_critical = true
        },
        {
            .name = "Packet Operations",
            .description = "Packet transmission, reception, and queue management",
            .test_main = test_packet_ops_main,
            .enabled_flag = &g_unit_config.run_packet_tests,
            .is_critical = true
        },
        {
            .name = "3C509B Driver",
            .description = "3C509B NIC driver specific functionality",
            .test_main = test_3c509b_main,
            .enabled_flag = &g_unit_config.run_driver_tests,
            .is_critical = false
        },
        {
            .name = "3C515-TX Driver",
            .description = "3C515-TX NIC driver specific functionality",
            .test_main = test_3c515_main,
            .enabled_flag = &g_unit_config.run_driver_tests,
            .is_critical = false
        },
        {
            .name = "ARP Protocol",
            .description = "ARP cache management and protocol implementation",
            .test_main = test_arp_main,
            .enabled_flag = &g_unit_config.run_protocol_tests,
            .is_critical = false
        },
        {
            .name = "Routing Protocol",
            .description = "Routing table management and packet forwarding",
            .test_main = test_routing_main,
            .enabled_flag = &g_unit_config.run_protocol_tests,
            .is_critical = false
        },
        {
            .name = "Assembly API",
            .description = "Assembly language API integration and calling conventions",
            .test_main = test_asm_api_main,
            .enabled_flag = &g_unit_config.run_asm_tests,
            .is_critical = false
        },
        {
            .name = "IRQ Handling",
            .description = "Interrupt request handling and multiplexing",
            .test_main = test_irq_main,
            .enabled_flag = &g_unit_config.run_irq_tests,
            .is_critical = false
        },
        {
            .name = "XMS Memory",
            .description = "Extended Memory Specification (XMS) management",
            .test_main = test_xms_main,
            .enabled_flag = &g_unit_config.run_xms_tests,
            .is_critical = false
        }
    };
    
    int num_suites = sizeof(test_suites) / sizeof(test_suites[0]);
    int overall_result = 0;
    
    /* Filter by specific test suite if requested */
    if (g_unit_config.specific_test_suite) {
        bool found = false;
        for (int i = 0; i < num_suites; i++) {
            const char *suite_name = test_suites[i].name;
            
            /* Check both full name and short name matches */
            if (strstr(suite_name, g_unit_config.specific_test_suite) != NULL ||
                strcmp(g_unit_config.specific_test_suite, "hardware") == 0 && strstr(suite_name, "Hardware") != NULL ||
                strcmp(g_unit_config.specific_test_suite, "memory") == 0 && strstr(suite_name, "Memory") != NULL ||
                strcmp(g_unit_config.specific_test_suite, "api") == 0 && strstr(suite_name, "API") != NULL ||
                strcmp(g_unit_config.specific_test_suite, "packets") == 0 && strstr(suite_name, "Packet") != NULL ||
                strcmp(g_unit_config.specific_test_suite, "drivers") == 0 && strstr(suite_name, "Driver") != NULL ||
                strcmp(g_unit_config.specific_test_suite, "protocols") == 0 && (strstr(suite_name, "ARP") != NULL || strstr(suite_name, "Routing") != NULL) ||
                strcmp(g_unit_config.specific_test_suite, "assembly") == 0 && strstr(suite_name, "Assembly") != NULL ||
                strcmp(g_unit_config.specific_test_suite, "irq") == 0 && strstr(suite_name, "IRQ") != NULL ||
                strcmp(g_unit_config.specific_test_suite, "xms") == 0 && strstr(suite_name, "XMS") != NULL) {
                
                found = true;
                int result = run_unit_test_suite(&test_suites[i]);
                if (result != 0) {
                    overall_result = 1;
                }
            }
        }
        
        if (!found) {
            log_error("Test suite '%s' not found", g_unit_config.specific_test_suite);
            overall_result = 1;
        }
    } else {
        /* Run all enabled test suites */
        for (int i = 0; i < num_suites; i++) {
            if (!(*test_suites[i].enabled_flag)) {
                log_info("Skipping disabled test suite: %s", test_suites[i].name);
                continue;
            }
            
            int result = run_unit_test_suite(&test_suites[i]);
            if (result != 0) {
                overall_result = 1;
                
                if (test_suites[i].is_critical && g_unit_config.stop_on_failure) {
                    log_error("Critical test suite failed, stopping execution");
                    break;
                }
            }
        }
    }
    
    uint32_t overall_end_time = get_system_timestamp_ms();
    g_unit_stats.total_duration_ms = overall_end_time - overall_start_time;
    
    /* Get test framework statistics */
    test_framework_stats_t framework_stats;
    if (test_framework_get_statistics(&framework_stats) == 0) {
        g_unit_stats.total_tests_run = framework_stats.total_tests;
        g_unit_stats.total_tests_passed = framework_stats.tests_passed;
        g_unit_stats.total_tests_failed = framework_stats.tests_failed;
    }
    
    /* Print comprehensive summary */
    print_unit_test_summary();
    
    /* Cleanup */
    cleanup_unit_test_environment();
    
    if (overall_result == 0) {
        log_info("Unit Test Suite: ALL TESTS COMPLETED SUCCESSFULLY");
    } else {
        log_error("Unit Test Suite: SOME TESTS FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Standalone entry point (when run directly)
 */
int main(int argc, char *argv[]) {
    printf("3Com Packet Driver - Unit Test Suite Runner\n");
    printf("==========================================\n\n");
    
    return run_unit_tests(argc, argv);
}