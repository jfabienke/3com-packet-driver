/**
 * @file test_framework.c
 * @brief Testing Framework Implementation for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 4 Infrastructure
 * 
 * This file implements the testing framework for module validation,
 * integration testing, and quality assurance.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <dos.h>

/* Global test runner instance */
static test_runner_t* g_current_runner = NULL;

/* Forward declarations */
static bool execute_test_case(test_runner_t* runner, test_suite_t* suite, test_case_t* test);
static bool execute_test_suite(test_runner_t* runner, test_suite_t* suite);
static void update_suite_statistics(test_suite_t* suite);
static void update_runner_statistics(test_runner_t* runner);
static uint32_t get_current_time_ms(void);
static void log_test_result(test_context_t* ctx, test_case_t* test, test_result_t result);
static void generate_xml_report(test_runner_t* runner, FILE* file);
static void generate_text_report(test_runner_t* runner, FILE* file);

/* ============================================================================
 * Test Framework Initialization and Shutdown
 * ============================================================================ */

/**
 * @brief Initialize test framework
 */
bool test_framework_init(test_runner_t* runner, const test_runner_config_t* config)
{
    if (!runner) {
        return false;
    }
    
    /* Clear runner structure */
    memset(runner, 0, sizeof(test_runner_t));
    
    /* Set default configuration if none provided */
    if (config) {
        runner->config = *config;
    } else {
        /* Default configuration */
        runner->config.enabled_categories = TEST_CATEGORY_ALL;
        runner->config.min_severity = TEST_SEVERITY_LOW;
        runner->config.stop_on_failure = false;
        runner->config.verbose_output = false;
        runner->config.default_timeout = DEFAULT_TEST_TIMEOUT;
        strcpy(runner->config.log_filename, "test_log.txt");
        strcpy(runner->config.report_filename, "test_report.xml");
    }
    
    /* Initialize context */
    memset(&runner->context, 0, sizeof(test_context_t));
    runner->context.verbose_output = runner->config.verbose_output;
    runner->context.stop_on_failure = runner->config.stop_on_failure;
    
    /* Open log file */
    runner->context.log_file = fopen(runner->config.log_filename, "w");
    if (!runner->context.log_file) {
        printf("Warning: Could not open log file %s\n", runner->config.log_filename);
    }
    
    /* Initialize statistics */
    memset(&runner->stats, 0, sizeof(test_runner_stats_t));
    runner->stats.start_time = get_current_time_ms();
    
    runner->initialized = true;
    g_current_runner = runner;
    
    printf("Test Framework v%d.%d initialized\n", 
           TEST_FRAMEWORK_VERSION_MAJOR, TEST_FRAMEWORK_VERSION_MINOR);
    
    return true;
}

/**
 * @brief Shutdown test framework
 */
void test_framework_shutdown(test_runner_t* runner)
{
    if (!runner || !runner->initialized) {
        return;
    }
    
    /* Close log file */
    if (runner->context.log_file) {
        fclose(runner->context.log_file);
        runner->context.log_file = NULL;
    }
    
    /* Clean up test data */
    if (runner->context.test_data && runner->context.cleanup_test_data) {
        free(runner->context.test_data);
        runner->context.test_data = NULL;
    }
    
    /* Clean up mocks */
    test_cleanup_mocks(&runner->context);
    
    runner->initialized = false;
    g_current_runner = NULL;
    
    printf("Test Framework shutdown complete\n");
}

/* ============================================================================
 * Test Suite and Case Management
 * ============================================================================ */

/**
 * @brief Add test suite to runner
 */
bool test_runner_add_suite(test_runner_t* runner, test_suite_t* suite)
{
    if (!runner || !suite || runner->suite_count >= MAX_TEST_SUITES) {
        return false;
    }
    
    /* Copy suite to runner */
    runner->suites[runner->suite_count] = *suite;
    runner->suite_count++;
    runner->stats.total_suites++;
    runner->stats.total_tests += suite->test_count;
    
    printf("Added test suite: %s (%d tests)\n", suite->name, suite->test_count);
    
    return true;
}

/**
 * @brief Add test case to suite
 */
bool test_suite_add_test(test_suite_t* suite, const char* name, test_function_t test_func)
{
    test_case_t* test;
    
    if (!suite || !name || !test_func || suite->test_count >= MAX_TESTS_PER_SUITE) {
        return false;
    }
    
    test = &suite->tests[suite->test_count];
    
    /* Initialize test case */
    memset(test, 0, sizeof(test_case_t));
    strncpy(test->name, name, MAX_TEST_NAME_LENGTH - 1);
    test->name[MAX_TEST_NAME_LENGTH - 1] = '\0';
    test->test_func = test_func;
    test->categories = TEST_CATEGORY_UNIT;
    test->severity = TEST_SEVERITY_MEDIUM;
    test->timeout_ms = DEFAULT_TEST_TIMEOUT;
    test->enabled = true;
    test->result = TEST_RESULT_SKIP;
    
    suite->test_count++;
    
    return true;
}

/* ============================================================================
 * Test Execution Functions
 * ============================================================================ */

/**
 * @brief Run all tests
 */
bool test_runner_run_all(test_runner_t* runner)
{
    bool success = true;
    
    if (!runner || !runner->initialized) {
        return false;
    }
    
    runner->running = true;
    runner->stats.start_time = get_current_time_ms();
    
    printf("Running all test suites (%d suites, %d tests)\n", 
           runner->suite_count, runner->stats.total_tests);
    
    if (runner->progress_callback) {
        runner->progress_callback("Starting test execution");
    }
    
    /* Execute all suites */
    for (int i = 0; i < runner->suite_count; i++) {
        test_suite_t* suite = &runner->suites[i];
        
        if (!suite->enabled) {
            printf("Skipping disabled suite: %s\n", suite->name);
            continue;
        }
        
        if (!execute_test_suite(runner, suite)) {
            success = false;
            if (runner->config.stop_on_failure) {
                printf("Stopping execution due to suite failure\n");
                break;
            }
        }
    }
    
    /* Update final statistics */
    runner->stats.end_time = get_current_time_ms();
    runner->stats.total_execution_time = runner->stats.end_time - runner->stats.start_time;
    update_runner_statistics(runner);
    
    /* Generate report */
    test_runner_generate_report(runner, runner->config.report_filename);
    
    runner->running = false;
    
    printf("\n=== Test Execution Complete ===\n");
    printf("Total Tests: %d\n", runner->stats.tests_run);
    printf("Passed: %d\n", runner->stats.tests_passed);
    printf("Failed: %d\n", runner->stats.tests_failed);
    printf("Skipped: %d\n", runner->stats.tests_skipped);
    printf("Errors: %d\n", runner->stats.tests_errors);
    printf("Execution Time: %d ms\n", runner->stats.total_execution_time);
    
    return success && (runner->stats.tests_failed == 0) && (runner->stats.tests_errors == 0);
}

/**
 * @brief Run specific test suite
 */
bool test_runner_run_suite(test_runner_t* runner, const char* suite_name)
{
    if (!runner || !suite_name) {
        return false;
    }
    
    /* Find suite by name */
    for (int i = 0; i < runner->suite_count; i++) {
        if (strcmp(runner->suites[i].name, suite_name) == 0) {
            return execute_test_suite(runner, &runner->suites[i]);
        }
    }
    
    printf("Test suite not found: %s\n", suite_name);
    return false;
}

/**
 * @brief Run specific test case
 */
bool test_runner_run_test(test_runner_t* runner, const char* suite_name, const char* test_name)
{
    test_suite_t* suite = NULL;
    test_case_t* test = NULL;
    
    if (!runner || !suite_name || !test_name) {
        return false;
    }
    
    /* Find suite */
    for (int i = 0; i < runner->suite_count; i++) {
        if (strcmp(runner->suites[i].name, suite_name) == 0) {
            suite = &runner->suites[i];
            break;
        }
    }
    
    if (!suite) {
        printf("Test suite not found: %s\n", suite_name);
        return false;
    }
    
    /* Find test */
    for (int i = 0; i < suite->test_count; i++) {
        if (strcmp(suite->tests[i].name, test_name) == 0) {
            test = &suite->tests[i];
            break;
        }
    }
    
    if (!test) {
        printf("Test case not found: %s.%s\n", suite_name, test_name);
        return false;
    }
    
    return execute_test_case(runner, suite, test);
}

/**
 * @brief Execute a test suite
 */
static bool execute_test_suite(test_runner_t* runner, test_suite_t* suite)
{
    bool success = true;
    uint32_t suite_start_time;
    
    printf("\n--- Running Test Suite: %s ---\n", suite->name);
    
    if (runner->progress_callback) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Running suite: %s", suite->name);
        runner->progress_callback(msg);
    }
    
    suite_start_time = get_current_time_ms();
    
    /* Clear suite statistics */
    suite->tests_run = 0;
    suite->tests_passed = 0;
    suite->tests_failed = 0;
    suite->tests_skipped = 0;
    suite->tests_errors = 0;
    suite->total_execution_time = 0;
    
    /* Run suite setup if available */
    if (suite->suite_setup) {
        if (!suite->suite_setup(&runner->context)) {
            printf("Suite setup failed for: %s\n", suite->name);
            return false;
        }
    }
    
    /* Execute all tests in suite */
    for (int i = 0; i < suite->test_count; i++) {
        test_case_t* test = &suite->tests[i];
        
        if (!test->enabled) {
            test->result = TEST_RESULT_SKIP;
            suite->tests_skipped++;
            continue;
        }
        
        /* Check if test category is enabled */
        if (!(test->categories & runner->config.enabled_categories)) {
            test->result = TEST_RESULT_SKIP;
            suite->tests_skipped++;
            continue;
        }
        
        /* Check severity level */
        if (test->severity < runner->config.min_severity) {
            test->result = TEST_RESULT_SKIP;
            suite->tests_skipped++;
            continue;
        }
        
        if (!execute_test_case(runner, suite, test)) {
            success = false;
            if (runner->config.stop_on_failure) {
                printf("Stopping suite execution due to test failure\n");
                break;
            }
        }
    }
    
    /* Run suite teardown if available */
    if (suite->suite_teardown) {
        suite->suite_teardown(&runner->context);
    }
    
    /* Update suite timing */
    suite->total_execution_time = get_current_time_ms() - suite_start_time;
    
    /* Update suite statistics */
    update_suite_statistics(suite);
    
    printf("Suite %s completed: %d/%d tests passed\n", 
           suite->name, suite->tests_passed, suite->tests_run);
    
    return success;
}

/**
 * @brief Execute a single test case
 */
static bool execute_test_case(test_runner_t* runner, test_suite_t* suite, test_case_t* test)
{
    uint32_t test_start_time;
    test_result_t result;
    bool setup_success = true;
    
    if (runner->config.verbose_output) {
        printf("  Running: %s", test->name);
    }
    
    /* Set up test context */
    runner->context.current_test = test;
    runner->context.current_suite = suite;
    runner->context.assertion_count = 0;
    runner->context.assertion_failures = 0;
    runner->context.checkpoint_count = 0;
    memset(runner->context.last_assertion_error, 0, MAX_ERROR_MSG_LENGTH);
    
    test_start_time = get_current_time_ms();
    runner->context.start_time = test_start_time;
    
    test->run_count++;
    suite->tests_run++;
    runner->stats.tests_run++;
    
    /* Run test setup if available */
    if (test->setup_func) {
        setup_success = test->setup_func(&runner->context);
        if (!setup_success) {
            result = TEST_RESULT_ERROR;
            strcpy(test->error_message, "Test setup failed");
            goto test_complete;
        }
    }
    
    /* Execute the test */
    result = test->test_func(&runner->context);
    
    /* Check for assertion failures */
    if (result == TEST_RESULT_PASS && runner->context.assertion_failures > 0) {
        result = TEST_RESULT_FAIL;
        strcpy(test->error_message, runner->context.last_assertion_error);
    }
    
test_complete:
    /* Run test teardown if available */
    if (test->teardown_func) {
        test->teardown_func(&runner->context);
    }
    
    /* Update test results */
    test->result = result;
    test->execution_time_ms = get_current_time_ms() - test_start_time;
    
    /* Update counters */
    switch (result) {
        case TEST_RESULT_PASS:
            test->pass_count++;
            suite->tests_passed++;
            runner->stats.tests_passed++;
            break;
        case TEST_RESULT_FAIL:
            test->fail_count++;
            suite->tests_failed++;
            runner->stats.tests_failed++;
            break;
        case TEST_RESULT_SKIP:
            suite->tests_skipped++;
            runner->stats.tests_skipped++;
            break;
        case TEST_RESULT_ERROR:
        case TEST_RESULT_TIMEOUT:
            suite->tests_errors++;
            runner->stats.tests_errors++;
            break;
    }
    
    /* Log result */
    log_test_result(&runner->context, test, result);
    
    /* Call result callback */
    if (runner->result_callback) {
        runner->result_callback(test, result);
    }
    
    /* Print result */
    if (runner->config.verbose_output) {
        const char* result_str;
        switch (result) {
            case TEST_RESULT_PASS: result_str = "PASS"; break;
            case TEST_RESULT_FAIL: result_str = "FAIL"; break;
            case TEST_RESULT_SKIP: result_str = "SKIP"; break;
            case TEST_RESULT_ERROR: result_str = "ERROR"; break;
            case TEST_RESULT_TIMEOUT: result_str = "TIMEOUT"; break;
            default: result_str = "UNKNOWN"; break;
        }
        printf(" [%s] (%d ms)\n", result_str, test->execution_time_ms);
        
        if (result != TEST_RESULT_PASS && strlen(test->error_message) > 0) {
            printf("    Error: %s\n", test->error_message);
        }
    } else {
        /* Simple progress indicator */
        switch (result) {
            case TEST_RESULT_PASS: printf("."); break;
            case TEST_RESULT_FAIL: printf("F"); break;
            case TEST_RESULT_SKIP: printf("S"); break;
            case TEST_RESULT_ERROR: printf("E"); break;
            case TEST_RESULT_TIMEOUT: printf("T"); break;
        }
        fflush(stdout);
    }
    
    return (result == TEST_RESULT_PASS);
}

/* ============================================================================
 * Assertion Implementation Functions
 * ============================================================================ */

bool test_assert_true_impl(test_context_t* ctx, bool condition, const char* expr, 
                          const char* file, int line)
{
    ctx->assertion_count++;
    
    if (!condition) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: %s at %s:%d", expr, file, line);
        return false;
    }
    
    return true;
}

bool test_assert_false_impl(test_context_t* ctx, bool condition, const char* expr,
                           const char* file, int line)
{
    ctx->assertion_count++;
    
    if (condition) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: !(%s) at %s:%d", expr, file, line);
        return false;
    }
    
    return true;
}

bool test_assert_equal_impl(test_context_t* ctx, long expected, long actual,
                           const char* expected_expr, const char* actual_expr,
                           const char* file, int line)
{
    ctx->assertion_count++;
    
    if (expected != actual) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: %s == %s (%ld != %ld) at %s:%d",
                expected_expr, actual_expr, expected, actual, file, line);
        return false;
    }
    
    return true;
}

bool test_assert_not_equal_impl(test_context_t* ctx, long expected, long actual,
                               const char* expected_expr, const char* actual_expr,
                               const char* file, int line)
{
    ctx->assertion_count++;
    
    if (expected == actual) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: %s != %s (%ld == %ld) at %s:%d",
                expected_expr, actual_expr, expected, actual, file, line);
        return false;
    }
    
    return true;
}

bool test_assert_null_impl(test_context_t* ctx, const void* ptr, const char* expr,
                          const char* file, int line)
{
    ctx->assertion_count++;
    
    if (ptr != NULL) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: %s == NULL at %s:%d", expr, file, line);
        return false;
    }
    
    return true;
}

bool test_assert_not_null_impl(test_context_t* ctx, const void* ptr, const char* expr,
                              const char* file, int line)
{
    ctx->assertion_count++;
    
    if (ptr == NULL) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: %s != NULL at %s:%d", expr, file, line);
        return false;
    }
    
    return true;
}

bool test_assert_string_equal_impl(test_context_t* ctx, const char* expected, const char* actual,
                                  const char* expected_expr, const char* actual_expr,
                                  const char* file, int line)
{
    ctx->assertion_count++;
    
    if (!expected && !actual) {
        return true;  /* Both NULL is equal */
    }
    
    if (!expected || !actual || strcmp(expected, actual) != 0) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: %s == %s (\"%s\" != \"%s\") at %s:%d",
                expected_expr, actual_expr, 
                expected ? expected : "(null)", 
                actual ? actual : "(null)", 
                file, line);
        return false;
    }
    
    return true;
}

bool test_assert_memory_equal_impl(test_context_t* ctx, const void* expected, const void* actual,
                                  size_t size, const char* expected_expr, const char* actual_expr,
                                  const char* file, int line)
{
    ctx->assertion_count++;
    
    if (!expected && !actual) {
        return true;  /* Both NULL is equal */
    }
    
    if (!expected || !actual || memcmp(expected, actual, size) != 0) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Assertion failed: memcmp(%s, %s, %zu) == 0 at %s:%d",
                expected_expr, actual_expr, size, file, line);
        return false;
    }
    
    return true;
}

void test_fail_impl(test_context_t* ctx, const char* message, const char* file, int line)
{
    ctx->assertion_failures++;
    snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
            "Test failed: %s at %s:%d", message, file, line);
}

/* ============================================================================
 * Performance Testing Implementation
 * ============================================================================ */

void test_perf_start_impl(test_context_t* ctx)
{
    ctx->start_time = get_current_time_ms();
    ctx->checkpoint_count = 0;
}

void test_perf_checkpoint_impl(test_context_t* ctx, const char* name)
{
    if (ctx->checkpoint_count < 16) {
        ctx->checkpoint_times[ctx->checkpoint_count] = get_current_time_ms();
        ctx->checkpoint_count++;
    }
}

bool test_perf_end_assert_impl(test_context_t* ctx, uint32_t max_time_ms)
{
    uint32_t elapsed = get_current_time_ms() - ctx->start_time;
    
    if (elapsed > max_time_ms) {
        ctx->assertion_failures++;
        snprintf(ctx->last_assertion_error, MAX_ERROR_MSG_LENGTH,
                "Performance assertion failed: %d ms > %d ms", elapsed, max_time_ms);
        return false;
    }
    
    return true;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static uint32_t get_current_time_ms(void)
{
    /* Simple DOS timing - would use proper timer in production */
    return clock() * 1000 / CLOCKS_PER_SEC;
}

static void log_test_result(test_context_t* ctx, test_case_t* test, test_result_t result)
{
    if (!ctx->log_file) return;
    
    const char* result_str;
    switch (result) {
        case TEST_RESULT_PASS: result_str = "PASS"; break;
        case TEST_RESULT_FAIL: result_str = "FAIL"; break;
        case TEST_RESULT_SKIP: result_str = "SKIP"; break;
        case TEST_RESULT_ERROR: result_str = "ERROR"; break;
        case TEST_RESULT_TIMEOUT: result_str = "TIMEOUT"; break;
        default: result_str = "UNKNOWN"; break;
    }
    
    fprintf(ctx->log_file, "%s.%s: %s (%d ms)\n",
            ctx->current_suite->name, test->name, result_str, test->execution_time_ms);
    
    if (result != TEST_RESULT_PASS && strlen(test->error_message) > 0) {
        fprintf(ctx->log_file, "  Error: %s\n", test->error_message);
    }
    
    fflush(ctx->log_file);
}

static void update_suite_statistics(test_suite_t* suite)
{
    /* Statistics are updated during test execution */
}

static void update_runner_statistics(test_runner_t* runner)
{
    /* Statistics are updated during test execution */
}

/* ============================================================================
 * Report Generation
 * ============================================================================ */

bool test_runner_generate_report(test_runner_t* runner, const char* filename)
{
    FILE* file;
    const char* ext;
    
    if (!runner || !filename) {
        return false;
    }
    
    file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not create report file %s\n", filename);
        return false;
    }
    
    /* Determine format based on file extension */
    ext = strrchr(filename, '.');
    if (ext && strcmp(ext, ".xml") == 0) {
        generate_xml_report(runner, file);
    } else {
        generate_text_report(runner, file);
    }
    
    fclose(file);
    
    printf("Test report generated: %s\n", filename);
    return true;
}

static void generate_text_report(test_runner_t* runner, FILE* file)
{
    fprintf(file, "3Com Packet Driver Test Report\n");
    fprintf(file, "==============================\n\n");
    
    fprintf(file, "Test Framework Version: %d.%d\n", 
            TEST_FRAMEWORK_VERSION_MAJOR, TEST_FRAMEWORK_VERSION_MINOR);
    fprintf(file, "Execution Time: %d ms\n", runner->stats.total_execution_time);
    fprintf(file, "Test Suites: %d\n", runner->stats.total_suites);
    fprintf(file, "Total Tests: %d\n", runner->stats.total_tests);
    fprintf(file, "\nResults Summary:\n");
    fprintf(file, "  Passed:  %d\n", runner->stats.tests_passed);
    fprintf(file, "  Failed:  %d\n", runner->stats.tests_failed);
    fprintf(file, "  Skipped: %d\n", runner->stats.tests_skipped);
    fprintf(file, "  Errors:  %d\n", runner->stats.tests_errors);
    fprintf(file, "\n");
    
    /* Detailed results per suite */
    for (int i = 0; i < runner->suite_count; i++) {
        test_suite_t* suite = &runner->suites[i];
        
        fprintf(file, "Test Suite: %s\n", suite->name);
        fprintf(file, "  Description: %s\n", suite->description);
        fprintf(file, "  Tests Run: %d/%d\n", suite->tests_run, suite->test_count);
        fprintf(file, "  Passed: %d, Failed: %d, Skipped: %d, Errors: %d\n",
                suite->tests_passed, suite->tests_failed, 
                suite->tests_skipped, suite->tests_errors);
        fprintf(file, "  Execution Time: %d ms\n", suite->total_execution_time);
        
        /* Failed tests details */
        for (int j = 0; j < suite->test_count; j++) {
            test_case_t* test = &suite->tests[j];
            if (test->result == TEST_RESULT_FAIL || test->result == TEST_RESULT_ERROR) {
                fprintf(file, "    FAILED: %s - %s\n", test->name, test->error_message);
            }
        }
        fprintf(file, "\n");
    }
}

static void generate_xml_report(test_runner_t* runner, FILE* file)
{
    fprintf(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(file, "<testsuites tests=\"%d\" failures=\"%d\" errors=\"%d\" time=\"%.3f\">\n",
            runner->stats.total_tests, runner->stats.tests_failed, 
            runner->stats.tests_errors, runner->stats.total_execution_time / 1000.0);
    
    for (int i = 0; i < runner->suite_count; i++) {
        test_suite_t* suite = &runner->suites[i];
        
        fprintf(file, "  <testsuite name=\"%s\" tests=\"%d\" failures=\"%d\" errors=\"%d\" time=\"%.3f\">\n",
                suite->name, suite->test_count, suite->tests_failed, 
                suite->tests_errors, suite->total_execution_time / 1000.0);
        
        for (int j = 0; j < suite->test_count; j++) {
            test_case_t* test = &suite->tests[j];
            
            fprintf(file, "    <testcase name=\"%s\" time=\"%.3f\"",
                    test->name, test->execution_time_ms / 1000.0);
            
            if (test->result == TEST_RESULT_PASS) {
                fprintf(file, " />\n");
            } else {
                fprintf(file, ">\n");
                
                if (test->result == TEST_RESULT_FAIL) {
                    fprintf(file, "      <failure message=\"%s\" />\n", test->error_message);
                } else if (test->result == TEST_RESULT_ERROR) {
                    fprintf(file, "      <error message=\"%s\" />\n", test->error_message);
                } else if (test->result == TEST_RESULT_SKIP) {
                    fprintf(file, "      <skipped />\n");
                }
                
                fprintf(file, "    </testcase>\n");
            }
        }
        
        fprintf(file, "  </testsuite>\n");
    }
    
    fprintf(file, "</testsuites>\n");
}

/* ============================================================================
 * Mock Object Support (Simplified Implementation)
 * ============================================================================ */

void* test_create_mock(test_context_t* ctx, size_t size)
{
    if (ctx->mock_count >= 16) {
        return NULL;
    }
    
    void* mock = malloc(size);
    if (mock) {
        memset(mock, 0, size);
        ctx->mock_objects[ctx->mock_count] = mock;
        ctx->mock_count++;
    }
    
    return mock;
}

bool test_mock_expect_call(test_context_t* ctx, void* mock, const char* function_name)
{
    /* Simplified mock implementation */
    return true;
}

bool test_mock_verify(test_context_t* ctx, void* mock)
{
    /* Simplified mock verification */
    return true;
}

void test_cleanup_mocks(test_context_t* ctx)
{
    for (int i = 0; i < ctx->mock_count; i++) {
        if (ctx->mock_objects[i]) {
            free(ctx->mock_objects[i]);
            ctx->mock_objects[i] = NULL;
        }
    }
    ctx->mock_count = 0;
}

/* ============================================================================
 * Additional Utility Functions
 * ============================================================================ */

void* test_alloc_data(test_context_t* ctx, size_t size)
{
    if (ctx->test_data && ctx->cleanup_test_data) {
        free(ctx->test_data);
    }
    
    ctx->test_data = malloc(size);
    ctx->test_data_size = size;
    ctx->cleanup_test_data = true;
    
    if (ctx->test_data) {
        memset(ctx->test_data, 0, size);
    }
    
    return ctx->test_data;
}

void test_free_data(test_context_t* ctx, void* data)
{
    if (data == ctx->test_data) {
        free(ctx->test_data);
        ctx->test_data = NULL;
        ctx->test_data_size = 0;
        ctx->cleanup_test_data = false;
    } else {
        free(data);
    }
}

void test_log(test_context_t* ctx, const char* format, ...)
{
    va_list args;
    
    if (ctx->verbose_output) {
        printf("    LOG: ");
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        printf("\n");
    }
    
    if (ctx->log_file) {
        fprintf(ctx->log_file, "LOG: ");
        va_start(args, format);
        vfprintf(ctx->log_file, format, args);
        va_end(args);
        fprintf(ctx->log_file, "\n");
        fflush(ctx->log_file);
    }
}

void test_skip(test_context_t* ctx, const char* reason)
{
    /* Mark test for skipping */
    if (ctx->current_test) {
        ctx->current_test->result = TEST_RESULT_SKIP;
        strncpy(ctx->current_test->error_message, reason, MAX_ERROR_MSG_LENGTH - 1);
    }
}

void test_set_timeout(test_context_t* ctx, uint32_t timeout_ms)
{
    if (ctx->current_test) {
        ctx->current_test->timeout_ms = timeout_ms;
    }
}