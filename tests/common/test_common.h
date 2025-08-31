#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Common test framework definitions */

/* Test result codes */
typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_SKIP = 2,
    TEST_ERROR = 3
} test_result_t;

/* Test function pointer type */
typedef test_result_t (*test_func_t)(void);

/* Test case structure */
typedef struct {
    const char* name;
    test_func_t func;
    const char* description;
} test_case_t;

/* Test suite structure */
typedef struct {
    const char* suite_name;
    test_case_t* tests;
    size_t test_count;
    int setup_result;
    int teardown_result;
} test_suite_t;

/* Global test statistics */
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    int error_tests;
} test_stats_t;

/* Common test functions */
void test_init(void);
void test_cleanup(void);
void test_print_stats(const test_stats_t* stats);
test_result_t run_test_suite(const test_suite_t* suite);
test_result_t run_single_test(const test_case_t* test);

/* Test output functions */
void test_log(const char* format, ...);
void test_error(const char* format, ...);
void test_info(const char* format, ...);

/* Memory testing utilities */
void* test_malloc(size_t size);
void test_free(void* ptr);
void test_memory_init(void);
void test_memory_cleanup(void);
bool test_memory_leaks_detected(void);

/* Timing utilities */
typedef struct {
    uint64_t start_time;
    uint64_t end_time;
} test_timer_t;

void test_timer_start(test_timer_t* timer);
void test_timer_stop(test_timer_t* timer);
uint64_t test_timer_elapsed_ms(const test_timer_t* timer);

/* Constants */
#define MAX_TEST_NAME_LENGTH 64
#define MAX_TEST_DESCRIPTION_LENGTH 256
#define TEST_TIMEOUT_MS 5000

/* Colors for test output (if supported) */
#define TEST_COLOR_RESET   "\033[0m"
#define TEST_COLOR_RED     "\033[31m"
#define TEST_COLOR_GREEN   "\033[32m"
#define TEST_COLOR_YELLOW  "\033[33m"
#define TEST_COLOR_BLUE    "\033[34m"
#define TEST_COLOR_MAGENTA "\033[35m"
#define TEST_COLOR_CYAN    "\033[36m"

#endif /* TEST_COMMON_H */