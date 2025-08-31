/**
 * @file runner_main.c
 * @brief Master test runner for comprehensive packet operations and all test categories
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This is the main test runner that orchestrates all test suites including:
 * - Unit tests (drivers, protocols, hardware, memory, etc.)
 * - Integration tests
 * - Performance benchmarks
 * - Stress tests
 * - Hardware mocking validation
 */

#include "../common/test_framework.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include "../../include/hardware_mock.h"
#include <stdio.h>
#include <string.h>

/* External test runner functions */
extern int run_unit_tests(int argc, char *argv[]);
extern int run_integration_tests(int argc, char *argv[]);
extern int run_performance_tests(int argc, char *argv[]);
extern int run_stress_tests(int argc, char *argv[]);
extern int run_driver_tests(int argc, char *argv[]);
extern int run_protocol_tests(int argc, char *argv[]);

/* Test suite configuration */
typedef struct {
    const char *name;
    const char *description;
    int (*test_main)(int argc, char *argv[]);
    bool enabled;
    bool required;  /* If true, failure stops execution */
    const char *category;
} test_suite_t;

/* Test execution results */
typedef struct {
    int total_suites;
    int passed_suites;
    int failed_suites;
    int skipped_suites;
    uint32_t total_duration_ms;
    bool overall_success;
} test_execution_summary_t;

/* Forward declarations */
static int initialize_test_environment(void);
static void cleanup_test_environment(void);
static uint32_t get_test_timestamp(void);
static void print_test_banner(void);
static void print_test_summary(const test_execution_summary_t *summary);
static int run_test_suite(const test_suite_t *suite, int argc, char *argv[], uint32_t *duration_ms);
static bool should_continue_after_failure(const test_suite_t *suite, bool stop_on_failure);
static void print_usage(const char *program_name);

/**
 * @brief Main test runner entry point
 * @param argc Argument count
 * @param argv Arguments
 * @return 0 on success, non-zero on failure
 */
int main(int argc, char *argv[]) {
    bool verbose = false;
    bool stop_on_failure = false;
    bool run_performance = true;
    bool run_stress = false;
    bool run_unit = true;
    bool run_integration = true;
    bool run_drivers = true;
    bool run_protocols = true;
    const char *specific_category = NULL;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stop-on-failure") == 0) {
            stop_on_failure = true;
        } else if (strcmp(argv[i], "--no-performance") == 0) {
            run_performance = false;
        } else if (strcmp(argv[i], "--stress") == 0) {
            run_stress = true;
        } else if (strcmp(argv[i], "--unit-only") == 0) {
            run_integration = false;
            run_performance = false;
            run_stress = false;
        } else if (strcmp(argv[i], "--integration-only") == 0) {
            run_unit = false;
            run_performance = false;
            run_stress = false;
        } else if (strcmp(argv[i], "--drivers-only") == 0) {
            specific_category = "drivers";
            run_unit = false;
            run_integration = false;
            run_performance = false;
            run_stress = false;
            run_protocols = false;
        } else if (strcmp(argv[i], "--protocols-only") == 0) {
            specific_category = "protocols";
            run_unit = false;
            run_integration = false;
            run_performance = false;
            run_stress = false;
            run_drivers = false;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    print_test_banner();
    
    /* Initialize test environment */
    log_info("Initializing comprehensive test environment...");
    int init_result = initialize_test_environment();
    if (init_result != 0) {
        log_error("Failed to initialize test environment: %d", init_result);
        return 1;
    }
    
    /* Configure test suites */
    test_suite_t test_suites[] = {
        {
            .name = "Driver Tests",
            .description = "Comprehensive testing of 3C509B and 3C515-TX NIC drivers",
            .test_main = run_driver_tests,
            .enabled = run_drivers,
            .required = true,
            .category = "drivers"
        },
        {
            .name = "Protocol Tests",
            .description = "Network protocol testing (ARP, routing, packet handling)",
            .test_main = run_protocol_tests,
            .enabled = run_protocols,
            .required = true,
            .category = "protocols"
        },
        {
            .name = "Unit Tests",
            .description = "All unit tests (hardware, memory, API, packet operations)",
            .test_main = run_unit_tests,
            .enabled = run_unit,
            .required = true,
            .category = "unit"
        },
        {
            .name = "Integration Tests", 
            .description = "Cross-component integration and system-level tests",
            .test_main = run_integration_tests,
            .enabled = run_integration,
            .required = true,
            .category = "integration"
        },
        {
            .name = "Performance Tests",
            .description = "Throughput, latency, and comparative performance analysis",
            .test_main = run_performance_tests,
            .enabled = run_performance,
            .required = false,
            .category = "performance"
        },
        {
            .name = "Stress Tests",
            .description = "Resource stress testing and stability validation",
            .test_main = run_stress_tests,
            .enabled = run_stress,
            .required = false,
            .category = "stress"
        }
    };
    
    int num_suites = sizeof(test_suites) / sizeof(test_suites[0]);
    
    /* Filter by specific category if requested */
    if (specific_category) {
        for (int i = 0; i < num_suites; i++) {
            if (strcmp(test_suites[i].category, specific_category) != 0) {
                test_suites[i].enabled = false;
            }
        }
    }
    
    /* Initialize execution summary */
    test_execution_summary_t summary = {
        .total_suites = 0,
        .passed_suites = 0,
        .failed_suites = 0,
        .skipped_suites = 0,
        .total_duration_ms = 0,
        .overall_success = true
    };
    
    uint32_t overall_start_time = get_test_timestamp();
    
    log_info("Starting test execution with %d test suites", num_suites);
    if (verbose) {
        log_info("Verbose mode enabled");
    }
    if (stop_on_failure) {
        log_info("Stop-on-failure mode enabled");
    }
    
    /* Execute test suites */
    for (int i = 0; i < num_suites; i++) {
        const test_suite_t *suite = &test_suites[i];
        
        if (!suite->enabled) {
            log_info("Skipping disabled test suite: %s", suite->name);
            summary.skipped_suites++;
            continue;
        }
        
        log_info("");
        log_info("=================================================================");
        log_info("EXECUTING TEST SUITE: %s", suite->name);
        log_info("Description: %s", suite->description);
        log_info("Category: %s", suite->category);
        log_info("=================================================================");
        
        uint32_t suite_duration = 0;
        int suite_result = run_test_suite(suite, argc, argv, &suite_duration);
        
        summary.total_suites++;
        summary.total_duration_ms += suite_duration;
        
        if (suite_result == 0) {
            log_info("✓ TEST SUITE PASSED: %s (duration: %lu ms)", suite->name, suite_duration);
            summary.passed_suites++;
        } else {
            log_error("✗ TEST SUITE FAILED: %s (duration: %lu ms, exit code: %d)", 
                      suite->name, suite_duration, suite_result);
            summary.failed_suites++;
            summary.overall_success = false;
            
            if (!should_continue_after_failure(suite, stop_on_failure)) {
                log_error("Stopping execution due to critical test suite failure");
                break;
            }
        }
    }
    
    uint32_t overall_end_time = get_test_timestamp();
    summary.total_duration_ms = overall_end_time - overall_start_time;
    
    /* Print comprehensive summary */
    print_test_summary(&summary);
    
    /* Generate test report */
    log_info("Generating comprehensive test report...");
    test_framework_stats_t framework_stats;
    if (test_framework_get_statistics(&framework_stats) == 0) {
        log_info("Framework Statistics:");
        log_info("  Total framework tests: %d", framework_stats.total_tests);
        log_info("  Framework tests passed: %d", framework_stats.tests_passed);
        log_info("  Framework tests failed: %d", framework_stats.tests_failed);
        log_info("  Framework tests skipped: %d", framework_stats.tests_skipped);
        log_info("  Framework benchmarks: %d", framework_stats.benchmarks_run);
    }
    
    /* Hardware mock statistics */
    mock_statistics_t mock_stats;
    if (mock_get_statistics(&mock_stats) == 0) {
        log_info("Hardware Mock Statistics:");
        log_info("  Total I/O operations: %lu", mock_stats.total_io_operations);
        log_info("  Read operations: %lu", mock_stats.read_operations);
        log_info("  Write operations: %lu", mock_stats.write_operations);
        log_info("  Packets injected: %lu", mock_stats.packets_injected);
        log_info("  Packets extracted: %lu", mock_stats.packets_extracted);
        log_info("  Interrupts generated: %lu", mock_stats.interrupts_generated);
        log_info("  Errors injected: %lu", mock_stats.errors_injected);
    }
    
    /* Memory usage statistics */
    const mem_stats_t *mem_stats = memory_get_stats();
    if (mem_stats) {
        log_info("Memory Usage Statistics:");
        log_info("  Current usage: %lu bytes", mem_stats->used_memory);
        log_info("  Peak usage: %lu bytes", mem_stats->peak_usage);
        log_info("  Total allocations: %lu", mem_stats->total_allocations);
        log_info("  Total deallocations: %lu", mem_stats->total_deallocations);
    }
    
    /* Cleanup */
    cleanup_test_environment();
    
    /* Final result */
    if (summary.overall_success) {
        log_info("");
        log_info("🎉 ALL TESTS COMPLETED SUCCESSFULLY! 🎉");
        log_info("Test execution summary: %d/%d suites passed", 
                 summary.passed_suites, summary.total_suites);
        return 0;
    } else {
        log_error("");
        log_error("❌ TEST EXECUTION FAILED");
        log_error("Test execution summary: %d/%d suites passed, %d failed", 
                  summary.passed_suites, summary.total_suites, summary.failed_suites);
        return 1;
    }
}

/**
 * @brief Initialize test environment
 * @return 0 on success, negative on error
 */
static int initialize_test_environment(void) {
    /* Initialize logging system */
    if (logging_init() != 0) {
        printf("Failed to initialize logging system\n");
        return -1;
    }
    
    /* Initialize memory management */
    if (memory_init() != 0) {
        log_error("Failed to initialize memory management");
        return -2;
    }
    
    /* Initialize mock framework */
    if (mock_framework_init() != 0) {
        log_error("Failed to initialize hardware mock framework");
        return -3;
    }
    
    /* Initialize test framework with comprehensive configuration */
    test_config_t config;
    test_config_init_default(&config);
    config.test_hardware = true;
    config.test_memory = true;
    config.test_packet_ops = true;
    config.run_benchmarks = true;
    config.run_stress_tests = true;
    config.init_hardware = true;
    config.init_memory = true;
    config.init_diagnostics = true;
    config.verbose_output = true;
    config.stress_duration_ms = 30000;      /* 30 seconds for stress tests */
    config.benchmark_duration_ms = 10000;   /* 10 seconds for benchmarks */
    
    if (test_framework_init(&config) != 0) {
        log_error("Failed to initialize test framework");
        return -4;
    }
    
    log_info("Test environment initialized successfully");
    return 0;
}

/**
 * @brief Cleanup test environment
 */
static void cleanup_test_environment(void) {
    log_info("Cleaning up test environment...");
    
    /* Generate final test framework report */
    test_framework_generate_report();
    
    /* Cleanup frameworks in reverse order */
    test_framework_cleanup();
    mock_framework_cleanup();
    
    log_info("Test environment cleanup completed");
}

/**
 * @brief Get test timestamp
 * @return Timestamp in milliseconds
 */
static uint32_t get_test_timestamp(void) {
    /* In a real implementation, this would use actual system time */
    static uint32_t counter = 0;
    return ++counter * 10;  /* 10ms increments for testing */
}

/**
 * @brief Print test banner
 */
static void print_test_banner(void) {
    printf("\n");
    printf("===================================================================\n");
    printf("   3Com Packet Driver - Master Test Suite Runner\n");
    printf("   Support for 3C515-TX and 3C509B NICs\n");
    printf("===================================================================\n");
    printf("   Testing Components:\n");
    printf("   • Driver Tests (3C509B PIO + 3C515-TX DMA)\n");
    printf("   • Protocol Tests (ARP, Routing, Packet Handling)\n");
    printf("   • Unit Tests (Hardware, Memory, API, Packet Operations)\n");
    printf("   • Integration Tests (Cross-component validation)\n");
    printf("   • Performance Tests (Throughput, Latency)\n");
    printf("   • Stress Tests (Resource limits, Stability)\n");
    printf("===================================================================\n");
    printf("\n");
}

/**
 * @brief Print comprehensive test summary
 * @param summary Test execution summary
 */
static void print_test_summary(const test_execution_summary_t *summary) {
    log_info("");
    log_info("===================================================================");
    log_info("                    MASTER TEST SUITE SUMMARY");
    log_info("===================================================================");
    log_info("Test Suite Execution:");
    log_info("  Total Suites: %d", summary->total_suites);
    log_info("  Passed: %d", summary->passed_suites);
    log_info("  Failed: %d", summary->failed_suites);
    log_info("  Skipped: %d", summary->skipped_suites);
    log_info("");
    log_info("Execution Time:");
    log_info("  Total Duration: %lu ms (%.2f seconds)", 
             summary->total_duration_ms, summary->total_duration_ms / 1000.0);
    log_info("  Average Suite Duration: %lu ms", 
             summary->total_suites > 0 ? summary->total_duration_ms / summary->total_suites : 0);
    log_info("");
    log_info("Success Rate:");
    
    if (summary->total_suites > 0) {
        float success_rate = (float)summary->passed_suites / summary->total_suites * 100.0;
        log_info("  Suite Success Rate: %.1f%% (%d/%d)", 
                 success_rate, summary->passed_suites, summary->total_suites);
        
        if (success_rate >= 100.0) {
            log_info("  Result: EXCELLENT - All test suites passed!");
        } else if (success_rate >= 90.0) {
            log_info("  Result: GOOD - Most test suites passed");
        } else if (success_rate >= 70.0) {
            log_info("  Result: ACCEPTABLE - Some test suites failed");
        } else {
            log_info("  Result: POOR - Many test suites failed");
        }
    } else {
        log_info("  No test suites were executed");
    }
    
    log_info("");
    log_info("Overall Status: %s", summary->overall_success ? "SUCCESS ✓" : "FAILURE ❌");
    log_info("===================================================================");
}

/**
 * @brief Run a specific test suite
 * @param suite Test suite to run
 * @param argc Command line argument count
 * @param argv Command line arguments
 * @param duration_ms Output parameter for duration
 * @return Test suite exit code
 */
static int run_test_suite(const test_suite_t *suite, int argc, char *argv[], uint32_t *duration_ms) {
    if (!suite || !suite->test_main || !duration_ms) {
        return -1;
    }
    
    uint32_t start_time = get_test_timestamp();
    
    log_info("Starting test suite: %s", suite->name);
    
    /* Run the test suite */
    int result = suite->test_main(argc, argv);
    
    uint32_t end_time = get_test_timestamp();
    *duration_ms = end_time - start_time;
    
    /* Validate result */
    if (result < 0) {
        log_error("Test suite %s returned error code: %d", suite->name, result);
    } else if (result > 0) {
        log_warning("Test suite %s returned warning code: %d", suite->name, result);
    }
    
    return result;
}

/**
 * @brief Determine if execution should continue after a failure
 * @param suite Failed test suite
 * @param stop_on_failure Global stop-on-failure flag
 * @return true if execution should continue
 */
static bool should_continue_after_failure(const test_suite_t *suite, bool stop_on_failure) {
    /* Always stop for required test suites */
    if (suite->required) {
        log_error("Required test suite failed: %s", suite->name);
        return false;
    }
    
    /* Stop if global flag is set */
    if (stop_on_failure) {
        log_error("Stop-on-failure mode active, stopping execution");
        return false;
    }
    
    /* Continue for non-required suites */
    log_warning("Non-required test suite failed, continuing execution: %s", suite->name);
    return true;
}

/**
 * @brief Print usage information
 * @param program_name Program name
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -v, --verbose        Enable verbose output\n");
    printf("  -s, --stop-on-failure Stop execution on first failure\n");
    printf("  --no-performance     Skip performance benchmarks\n");
    printf("  --stress             Include stress tests\n");
    printf("  --unit-only          Run only unit tests\n");
    printf("  --integration-only   Run only integration tests\n");
    printf("  --drivers-only       Run only driver tests\n");
    printf("  --protocols-only     Run only protocol tests\n");
    printf("  -h, --help           Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                   # Run all tests except stress\n", program_name);
    printf("  %s --unit-only       # Run only unit tests\n", program_name);
    printf("  %s --drivers-only -v # Run driver tests with verbose output\n", program_name);
    printf("  %s --stress          # Run all tests including stress tests\n", program_name);
}