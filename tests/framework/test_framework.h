/**
 * @file test_framework.h
 * @brief Testing Framework Infrastructure for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 4 Infrastructure
 * 
 * This header defines the testing framework for module validation,
 * integration testing, and quality assurance.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Test Framework Version */
#define TEST_FRAMEWORK_VERSION_MAJOR 1
#define TEST_FRAMEWORK_VERSION_MINOR 0
#define TEST_FRAMEWORK_VERSION ((TEST_FRAMEWORK_VERSION_MAJOR << 8) | TEST_FRAMEWORK_VERSION_MINOR)

/* Test Result Codes */
typedef enum {
    TEST_RESULT_PASS     = 0,   /**< Test passed */
    TEST_RESULT_FAIL     = 1,   /**< Test failed */
    TEST_RESULT_SKIP     = 2,   /**< Test skipped */
    TEST_RESULT_ERROR    = 3,   /**< Test error */
    TEST_RESULT_TIMEOUT  = 4    /**< Test timeout */
} test_result_t;

/* Test Severity Levels */
typedef enum {
    TEST_SEVERITY_CRITICAL = 0, /**< Critical test failure */
    TEST_SEVERITY_HIGH     = 1, /**< High priority failure */
    TEST_SEVERITY_MEDIUM   = 2, /**< Medium priority failure */
    TEST_SEVERITY_LOW      = 3, /**< Low priority failure */
    TEST_SEVERITY_INFO     = 4  /**< Informational */
} test_severity_t;

/* Test Categories */
typedef enum {
    TEST_CATEGORY_UNIT        = 0x0001, /**< Unit tests */
    TEST_CATEGORY_INTEGRATION = 0x0002, /**< Integration tests */
    TEST_CATEGORY_PERFORMANCE = 0x0004, /**< Performance tests */
    TEST_CATEGORY_REGRESSION  = 0x0008, /**< Regression tests */
    TEST_CATEGORY_STRESS      = 0x0010, /**< Stress tests */
    TEST_CATEGORY_HARDWARE    = 0x0020, /**< Hardware tests */
    TEST_CATEGORY_MODULE      = 0x0040, /**< Module tests */
    TEST_CATEGORY_API         = 0x0080, /**< API tests */
    TEST_CATEGORY_MEMORY      = 0x0100, /**< Memory tests */
    TEST_CATEGORY_ALL         = 0xFFFF  /**< All categories */
} test_category_t;

/* Test Constants */
#define MAX_TEST_NAME_LENGTH    64
#define MAX_TEST_DESC_LENGTH    128
#define MAX_ERROR_MSG_LENGTH    256
#define MAX_TESTS_PER_SUITE     256
#define MAX_TEST_SUITES         32
#define DEFAULT_TEST_TIMEOUT    30000  /* 30 seconds */

/* Forward declarations */
typedef struct test_case test_case_t;
typedef struct test_suite test_suite_t;
typedef struct test_context test_context_t;
typedef struct test_runner test_runner_t;

/* ============================================================================
 * Test Case Definition
 * ============================================================================ */

/**
 * @brief Test function signature
 */
typedef test_result_t (*test_function_t)(test_context_t* ctx);

/**
 * @brief Test setup function signature
 */
typedef bool (*test_setup_function_t)(test_context_t* ctx);

/**
 * @brief Test teardown function signature
 */
typedef void (*test_teardown_function_t)(test_context_t* ctx);

/**
 * @brief Individual test case structure
 */
struct test_case {
    char name[MAX_TEST_NAME_LENGTH];        /**< Test name */
    char description[MAX_TEST_DESC_LENGTH]; /**< Test description */
    test_function_t test_func;              /**< Test function */
    test_setup_function_t setup_func;       /**< Setup function (optional) */
    test_teardown_function_t teardown_func; /**< Teardown function (optional) */
    test_category_t categories;             /**< Test categories */
    test_severity_t severity;               /**< Test severity */
    uint32_t timeout_ms;                    /**< Test timeout */
    bool enabled;                           /**< Test enabled flag */
    
    /* Test results */
    test_result_t result;                   /**< Test result */
    uint32_t execution_time_ms;             /**< Execution time */
    char error_message[MAX_ERROR_MSG_LENGTH]; /**< Error message */
    uint32_t run_count;                     /**< Number of times run */
    uint32_t pass_count;                    /**< Number of passes */
    uint32_t fail_count;                    /**< Number of failures */
};

/* ============================================================================
 * Test Suite Definition
 * ============================================================================ */

/**
 * @brief Test suite structure
 */
struct test_suite {
    char name[MAX_TEST_NAME_LENGTH];        /**< Suite name */
    char description[MAX_TEST_DESC_LENGTH]; /**< Suite description */
    test_case_t tests[MAX_TESTS_PER_SUITE]; /**< Test cases */
    uint16_t test_count;                    /**< Number of tests */
    
    /* Suite-level setup/teardown */
    test_setup_function_t suite_setup;     /**< Suite setup */
    test_teardown_function_t suite_teardown; /**< Suite teardown */
    
    /* Suite results */
    uint16_t tests_run;                     /**< Tests executed */
    uint16_t tests_passed;                  /**< Tests passed */
    uint16_t tests_failed;                  /**< Tests failed */
    uint16_t tests_skipped;                 /**< Tests skipped */
    uint16_t tests_errors;                  /**< Tests with errors */
    uint32_t total_execution_time;          /**< Total execution time */
    bool enabled;                           /**< Suite enabled flag */
};

/* ============================================================================
 * Test Context
 * ============================================================================ */

/**
 * @brief Test execution context
 */
struct test_context {
    /* Current test information */
    test_case_t* current_test;              /**< Currently executing test */
    test_suite_t* current_suite;            /**< Current test suite */
    
    /* Test data and state */
    void* test_data;                        /**< Test-specific data */
    size_t test_data_size;                  /**< Test data size */
    bool cleanup_test_data;                 /**< Auto-cleanup flag */
    
    /* Mock objects and stubs */
    void* mock_objects[16];                 /**< Mock object storage */
    uint8_t mock_count;                     /**< Number of mock objects */
    
    /* Assertions and validation */
    uint32_t assertion_count;               /**< Number of assertions */
    uint32_t assertion_failures;           /**< Failed assertions */
    char last_assertion_error[MAX_ERROR_MSG_LENGTH]; /**< Last assertion error */
    
    /* Test utilities */
    bool verbose_output;                    /**< Verbose output enabled */
    bool stop_on_failure;                   /**< Stop on first failure */
    FILE* log_file;                         /**< Test log file */
    
    /* Performance measurement */
    uint32_t start_time;                    /**< Test start time */
    uint32_t checkpoint_times[16];          /**< Performance checkpoints */
    uint8_t checkpoint_count;               /**< Number of checkpoints */
};

/* ============================================================================
 * Test Runner
 * ============================================================================ */

/**
 * @brief Test runner configuration
 */
typedef struct {
    test_category_t enabled_categories;     /**< Enabled test categories */
    test_severity_t min_severity;           /**< Minimum severity level */
    bool stop_on_failure;                   /**< Stop on first failure */
    bool verbose_output;                    /**< Verbose output */
    uint32_t default_timeout;               /**< Default test timeout */
    char log_filename[64];                  /**< Log file name */
    char report_filename[64];               /**< Report file name */
} test_runner_config_t;

/**
 * @brief Test runner statistics
 */
typedef struct {
    uint32_t total_suites;                  /**< Total test suites */
    uint32_t total_tests;                   /**< Total test cases */
    uint32_t tests_run;                     /**< Tests executed */
    uint32_t tests_passed;                  /**< Tests passed */
    uint32_t tests_failed;                  /**< Tests failed */
    uint32_t tests_skipped;                 /**< Tests skipped */
    uint32_t tests_errors;                  /**< Tests with errors */
    uint32_t total_execution_time;          /**< Total execution time */
    uint32_t start_time;                    /**< Test run start time */
    uint32_t end_time;                      /**< Test run end time */
} test_runner_stats_t;

/**
 * @brief Main test runner structure
 */
struct test_runner {
    test_suite_t suites[MAX_TEST_SUITES];   /**< Test suites */
    uint16_t suite_count;                   /**< Number of suites */
    
    test_runner_config_t config;            /**< Runner configuration */
    test_runner_stats_t stats;              /**< Execution statistics */
    test_context_t context;                 /**< Test execution context */
    
    bool initialized;                       /**< Runner initialized */
    bool running;                           /**< Test execution in progress */
    
    /* Callback functions */
    void (*progress_callback)(const char* message);
    void (*result_callback)(test_case_t* test, test_result_t result);
};

/* ============================================================================
 * Test Framework API
 * ============================================================================ */

/**
 * @brief Initialize test framework
 */
bool test_framework_init(test_runner_t* runner, const test_runner_config_t* config);

/**
 * @brief Shutdown test framework
 */
void test_framework_shutdown(test_runner_t* runner);

/**
 * @brief Add test suite to runner
 */
bool test_runner_add_suite(test_runner_t* runner, test_suite_t* suite);

/**
 * @brief Add test case to suite
 */
bool test_suite_add_test(test_suite_t* suite, const char* name, test_function_t test_func);

/**
 * @brief Run all tests
 */
bool test_runner_run_all(test_runner_t* runner);

/**
 * @brief Run specific test suite
 */
bool test_runner_run_suite(test_runner_t* runner, const char* suite_name);

/**
 * @brief Run specific test case
 */
bool test_runner_run_test(test_runner_t* runner, const char* suite_name, const char* test_name);

/**
 * @brief Generate test report
 */
bool test_runner_generate_report(test_runner_t* runner, const char* filename);

/* ============================================================================
 * Test Assertion Macros
 * ============================================================================ */

/**
 * @brief Assert that condition is true
 */
#define TEST_ASSERT_TRUE(ctx, condition) \
    test_assert_true_impl(ctx, (condition), #condition, __FILE__, __LINE__)

/**
 * @brief Assert that condition is false
 */
#define TEST_ASSERT_FALSE(ctx, condition) \
    test_assert_false_impl(ctx, (condition), #condition, __FILE__, __LINE__)

/**
 * @brief Assert that two values are equal
 */
#define TEST_ASSERT_EQUAL(ctx, expected, actual) \
    test_assert_equal_impl(ctx, (expected), (actual), #expected, #actual, __FILE__, __LINE__)

/**
 * @brief Assert that two values are not equal
 */
#define TEST_ASSERT_NOT_EQUAL(ctx, expected, actual) \
    test_assert_not_equal_impl(ctx, (expected), (actual), #expected, #actual, __FILE__, __LINE__)

/**
 * @brief Assert that pointer is NULL
 */
#define TEST_ASSERT_NULL(ctx, ptr) \
    test_assert_null_impl(ctx, (ptr), #ptr, __FILE__, __LINE__)

/**
 * @brief Assert that pointer is not NULL
 */
#define TEST_ASSERT_NOT_NULL(ctx, ptr) \
    test_assert_not_null_impl(ctx, (ptr), #ptr, __FILE__, __LINE__)

/**
 * @brief Assert that strings are equal
 */
#define TEST_ASSERT_STRING_EQUAL(ctx, expected, actual) \
    test_assert_string_equal_impl(ctx, (expected), (actual), #expected, #actual, __FILE__, __LINE__)

/**
 * @brief Assert that memory blocks are equal
 */
#define TEST_ASSERT_MEMORY_EQUAL(ctx, expected, actual, size) \
    test_assert_memory_equal_impl(ctx, (expected), (actual), (size), #expected, #actual, __FILE__, __LINE__)

/**
 * @brief Fail test with message
 */
#define TEST_FAIL(ctx, message) \
    test_fail_impl(ctx, (message), __FILE__, __LINE__)

/* ============================================================================
 * Performance Testing Macros
 * ============================================================================ */

/**
 * @brief Start performance measurement
 */
#define TEST_PERF_START(ctx) \
    test_perf_start_impl(ctx)

/**
 * @brief Record performance checkpoint
 */
#define TEST_PERF_CHECKPOINT(ctx, name) \
    test_perf_checkpoint_impl(ctx, (name))

/**
 * @brief End performance measurement and assert timing
 */
#define TEST_PERF_END_ASSERT_LESS_THAN(ctx, max_time_ms) \
    test_perf_end_assert_impl(ctx, (max_time_ms))

/* ============================================================================
 * Mock Object Support
 * ============================================================================ */

/**
 * @brief Create mock object
 */
void* test_create_mock(test_context_t* ctx, size_t size);

/**
 * @brief Set mock expectation
 */
bool test_mock_expect_call(test_context_t* ctx, void* mock, const char* function_name);

/**
 * @brief Verify mock expectations
 */
bool test_mock_verify(test_context_t* ctx, void* mock);

/**
 * @brief Clean up all mocks
 */
void test_cleanup_mocks(test_context_t* ctx);

/* ============================================================================
 * Test Utility Functions
 * ============================================================================ */

/**
 * @brief Allocate test data
 */
void* test_alloc_data(test_context_t* ctx, size_t size);

/**
 * @brief Free test data
 */
void test_free_data(test_context_t* ctx, void* data);

/**
 * @brief Log test message
 */
void test_log(test_context_t* ctx, const char* format, ...);

/**
 * @brief Skip current test
 */
void test_skip(test_context_t* ctx, const char* reason);

/**
 * @brief Set test timeout
 */
void test_set_timeout(test_context_t* ctx, uint32_t timeout_ms);

/* ============================================================================
 * Test Suite Macros for Easy Definition
 * ============================================================================ */

/**
 * @brief Begin test suite definition
 */
#define TEST_SUITE_BEGIN(suite_name, suite_desc) \
    static test_suite_t suite_name = { \
        .name = #suite_name, \
        .description = suite_desc, \
        .test_count = 0, \
        .enabled = true \
    };

/**
 * @brief Add test to suite
 */
#define TEST_CASE(suite_name, test_name, test_desc, test_func) \
    do { \
        if (suite_name.test_count < MAX_TESTS_PER_SUITE) { \
            test_case_t* test = &suite_name.tests[suite_name.test_count]; \
            strncpy(test->name, #test_name, MAX_TEST_NAME_LENGTH - 1); \
            strncpy(test->description, test_desc, MAX_TEST_DESC_LENGTH - 1); \
            test->test_func = test_func; \
            test->categories = TEST_CATEGORY_UNIT; \
            test->severity = TEST_SEVERITY_MEDIUM; \
            test->timeout_ms = DEFAULT_TEST_TIMEOUT; \
            test->enabled = true; \
            suite_name.test_count++; \
        } \
    } while(0)

/**
 * @brief End test suite definition
 */
#define TEST_SUITE_END(suite_name)

/* ============================================================================
 * Internal Implementation Functions
 * ============================================================================ */

/* Assertion implementation functions */
bool test_assert_true_impl(test_context_t* ctx, bool condition, const char* expr, 
                          const char* file, int line);
bool test_assert_false_impl(test_context_t* ctx, bool condition, const char* expr,
                           const char* file, int line);
bool test_assert_equal_impl(test_context_t* ctx, long expected, long actual,
                           const char* expected_expr, const char* actual_expr,
                           const char* file, int line);
bool test_assert_not_equal_impl(test_context_t* ctx, long expected, long actual,
                               const char* expected_expr, const char* actual_expr,
                               const char* file, int line);
bool test_assert_null_impl(test_context_t* ctx, const void* ptr, const char* expr,
                          const char* file, int line);
bool test_assert_not_null_impl(test_context_t* ctx, const void* ptr, const char* expr,
                              const char* file, int line);
bool test_assert_string_equal_impl(test_context_t* ctx, const char* expected, const char* actual,
                                  const char* expected_expr, const char* actual_expr,
                                  const char* file, int line);
bool test_assert_memory_equal_impl(test_context_t* ctx, const void* expected, const void* actual,
                                  size_t size, const char* expected_expr, const char* actual_expr,
                                  const char* file, int line);
void test_fail_impl(test_context_t* ctx, const char* message, const char* file, int line);

/* Performance testing implementation */
void test_perf_start_impl(test_context_t* ctx);
void test_perf_checkpoint_impl(test_context_t* ctx, const char* name);
bool test_perf_end_assert_impl(test_context_t* ctx, uint32_t max_time_ms);

/* ============================================================================
 * Module-Specific Test Categories
 * ============================================================================ */

/**
 * @brief Module loading tests
 */
#define TEST_CATEGORY_MODULE_LOADING    0x1000

/**
 * @brief Hardware detection tests
 */
#define TEST_CATEGORY_HARDWARE_DETECT   0x2000

/**
 * @brief Packet processing tests
 */
#define TEST_CATEGORY_PACKET_PROCESS    0x4000

/**
 * @brief Cache coherency tests
 */
#define TEST_CATEGORY_CACHE_COHERENCY   0x8000

#endif /* TEST_FRAMEWORK_H */