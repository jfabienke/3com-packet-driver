/**
 * @file test_framework.h
 * @brief Comprehensive test framework infrastructure and reporting
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file defines the interface for a complete testing framework
 * that validates all driver functionality under stress conditions.
 */

#ifndef _TEST_FRAMEWORK_H_
#define _TEST_FRAMEWORK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "hardware.h"

/* Test framework constants */
#define MAX_TEST_RESULTS        256     /* Maximum test results to track */
#define MAX_BENCHMARKS          32      /* Maximum benchmark results */
#define MAX_TEST_NAME_LEN       64      /* Maximum test name length */
#define MAX_TEST_DETAILS_LEN    256     /* Maximum test details length */

/* Test categories */
typedef enum {
    TEST_CATEGORY_HARDWARE,             /* 0: Hardware validation tests */
    TEST_CATEGORY_MEMORY,               /* 1: Memory system tests */
    TEST_CATEGORY_PACKET,               /* 2: Packet operation tests */
    TEST_CATEGORY_NETWORK,              /* 3: Network protocol tests */
    TEST_CATEGORY_STRESS,               /* 4: Stress and load tests */
    TEST_CATEGORY_BENCHMARK,            /* 5: Performance benchmarks */
    TEST_CATEGORY_MAX                   /* 6: Category count */
} test_category_t;

/* Test results */
typedef enum {
    TEST_RESULT_PASS,                   /* 0: Test passed */
    TEST_RESULT_FAIL,                   /* 1: Test failed */
    TEST_RESULT_SKIP,                   /* 2: Test skipped */
    TEST_RESULT_ERROR,                  /* 3: Test error/exception */
    TEST_RESULT_MAX                     /* 4: Result count */
} test_result_t;

/* Test framework status */
typedef enum {
    TEST_STATUS_INIT,                   /* 0: Initializing */
    TEST_STATUS_READY,                  /* 1: Ready to run tests */
    TEST_STATUS_RUNNING,                /* 2: Tests in progress */
    TEST_STATUS_HARDWARE,               /* 3: Running hardware tests */
    TEST_STATUS_MEMORY,                 /* 4: Running memory tests */
    TEST_STATUS_PACKET,                 /* 5: Running packet tests */
    TEST_STATUS_BENCHMARK,              /* 6: Running benchmarks */
    TEST_STATUS_COMPLETED,              /* 7: All tests completed */
    TEST_STATUS_FAILED,                 /* 8: Tests failed */
    TEST_STATUS_MAX                     /* 9: Status count */
} test_status_t;

/* Benchmark categories */
typedef enum {
    BENCHMARK_THROUGHPUT,               /* 0: Throughput benchmarks */
    BENCHMARK_LATENCY,                  /* 1: Latency benchmarks */
    BENCHMARK_MEMORY,                   /* 2: Memory performance */
    BENCHMARK_CPU,                      /* 3: CPU utilization */
    BENCHMARK_MAX                       /* 4: Category count */
} benchmark_category_t;

/* Test configuration */
typedef struct {
    bool test_hardware;                 /* Run hardware tests */
    bool test_memory;                   /* Run memory tests */
    bool test_packet_ops;               /* Run packet operation tests */
    bool test_network;                  /* Run network protocol tests */
    bool run_stress_tests;              /* Run stress tests */
    bool run_benchmarks;                /* Run performance benchmarks */
    bool init_hardware;                 /* Initialize hardware for testing */
    bool init_memory;                   /* Initialize memory for testing */
    bool init_diagnostics;              /* Initialize diagnostics for testing */
    bool verbose_output;                /* Enable verbose test output */
    uint32_t stress_duration_ms;        /* Stress test duration */
    uint32_t benchmark_duration_ms;     /* Benchmark duration */
} test_config_t;

/* Test result entry */
typedef struct {
    char test_name[MAX_TEST_NAME_LEN];  /* Test name */
    test_category_t category;           /* Test category */
    test_result_t result;               /* Test result */
    uint32_t duration_ms;               /* Test duration in milliseconds */
    uint32_t timestamp;                 /* Test completion timestamp */
    char details[MAX_TEST_DETAILS_LEN]; /* Test details or error message */
} test_result_entry_t;

/* Benchmark result */
typedef struct {
    char name[MAX_TEST_NAME_LEN];       /* Benchmark name */
    benchmark_category_t category;      /* Benchmark category */
    uint32_t start_time;                /* Start timestamp */
    uint32_t end_time;                  /* End timestamp */
    uint32_t duration_ms;               /* Duration in milliseconds */
    uint32_t packets_per_second;        /* Packets per second */
    uint32_t bytes_per_second;          /* Bytes per second */
    uint32_t error_rate;                /* Error rate percentage */
    char details[MAX_TEST_DETAILS_LEN]; /* Benchmark details */
} benchmark_result_t;

/* System information for test report */
typedef struct {
    uint8_t nic_count;                  /* Number of NICs */
    uint32_t memory_total;              /* Total memory available */
    uint32_t memory_used;               /* Memory currently used */
    uint8_t cpu_type;                   /* CPU type */
    bool xms_available;                 /* XMS memory available */
    bool umb_available;                 /* UMB memory available */
} test_system_info_t;

/* Test framework state */
typedef struct {
    test_config_t config;               /* Test configuration */
    test_status_t status;               /* Current status */
    uint32_t start_time;                /* Framework start time */
    uint32_t end_time;                  /* Framework end time */
    uint16_t tests_passed;              /* Number of tests passed */
    uint16_t tests_failed;              /* Number of tests failed */
    uint16_t tests_skipped;             /* Number of tests skipped */
    uint16_t benchmarks_run;            /* Number of benchmarks run */
} test_framework_state_t;

/* Test report structure */
typedef struct {
    char framework_version[16];         /* Framework version */
    uint32_t start_time;                /* Test start time */
    uint32_t end_time;                  /* Test end time */
    test_system_info_t system_info;     /* System information */
    uint16_t total_tests;               /* Total number of tests */
    uint16_t tests_passed;              /* Tests passed */
    uint16_t tests_failed;              /* Tests failed */
    uint16_t tests_skipped;             /* Tests skipped */
    uint16_t benchmarks_run;            /* Benchmarks executed */
    test_result_t overall_result;       /* Overall test result */
} test_report_t;

/* Test framework statistics */
typedef struct {
    uint16_t total_tests;               /* Total tests run */
    uint16_t tests_passed;              /* Tests passed */
    uint16_t tests_failed;              /* Tests failed */
    uint16_t tests_skipped;             /* Tests skipped */
    uint16_t benchmarks_run;            /* Benchmarks run */
    uint32_t total_duration_ms;         /* Total test duration */
    test_status_t status;               /* Current status */
} test_framework_stats_t;

/* Diagnostic test result (from diagnostics.h integration) */
typedef struct {
    uint8_t test_type;                  /* Type of diagnostic test */
    bool passed;                        /* Test result */
    uint16_t error_code;                /* Error code if failed */
    uint32_t duration_ms;               /* Test duration */
    uint32_t timestamp;                 /* Test timestamp */
    char description[128];              /* Test description */
} diag_result_t;

/* Function prototypes */

/* Framework initialization and cleanup */
int test_framework_init(const test_config_t *config);
void test_framework_cleanup(void);

/* Test execution functions */
int test_framework_run_hardware_tests(void);
int test_framework_run_memory_tests(void);
int test_framework_run_packet_tests(void);
int test_framework_run_benchmarks(void);
int test_framework_run_comprehensive_tests(void);

/* Reporting and status functions */
int test_framework_generate_report(void);
test_status_t test_framework_get_status(void);
int test_framework_get_statistics(test_framework_stats_t *stats);

/* Test configuration helpers */
static inline void test_config_init_default(test_config_t *config) {
    if (config) {
        memset(config, 0, sizeof(test_config_t));
        config->test_hardware = true;
        config->test_memory = true;
        config->test_packet_ops = true;
        config->run_benchmarks = true;
        config->init_hardware = true;
        config->init_memory = true;
        config->init_diagnostics = true;
        config->stress_duration_ms = 10000;     /* 10 seconds */
        config->benchmark_duration_ms = 5000;   /* 5 seconds */
    }
}

static inline void test_config_minimal(test_config_t *config) {
    if (config) {
        memset(config, 0, sizeof(test_config_t));
        config->test_hardware = true;
        config->test_memory = true;
        config->init_hardware = true;
        config->init_memory = true;
        config->stress_duration_ms = 1000;      /* 1 second */
        config->benchmark_duration_ms = 1000;   /* 1 second */
    }
}

static inline void test_config_stress(test_config_t *config) {
    if (config) {
        test_config_init_default(config);
        config->run_stress_tests = true;
        config->stress_duration_ms = 60000;     /* 60 seconds */
        config->benchmark_duration_ms = 30000;  /* 30 seconds */
    }
}

/* Test result validation helpers */
static inline bool test_result_is_success(test_result_t result) {
    return (result == TEST_RESULT_PASS);
}

static inline bool test_result_is_failure(test_result_t result) {
    return (result == TEST_RESULT_FAIL || result == TEST_RESULT_ERROR);
}

static inline bool test_framework_is_running(test_status_t status) {
    return (status >= TEST_STATUS_RUNNING && status < TEST_STATUS_COMPLETED);
}

/* Test macros for convenience */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            log_error("TEST ASSERTION FAILED: %s", message); \
            return TEST_RESULT_FAIL; \
        } \
    } while(0)

#define TEST_EXPECT(condition, message) \
    do { \
        if (!(condition)) { \
            log_warning("TEST EXPECTATION FAILED: %s", message); \
        } \
    } while(0)

#define TEST_LOG_START(test_name) \
    log_info("=== Starting test: %s ===", test_name)

#define TEST_LOG_END(test_name, result) \
    log_info("=== Test %s: %s ===", test_name, \
             test_result_is_success(result) ? "PASSED" : "FAILED")

#ifdef __cplusplus
}
#endif

#endif /* _TEST_FRAMEWORK_H_ */