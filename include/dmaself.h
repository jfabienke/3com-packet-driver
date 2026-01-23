/**
 * @file dma_self_test.h
 * @brief Self-test diagnostics for DMA safety framework - Header
 *
 * GPT-5 Critical: Production-grade self-test diagnostics
 */

#ifndef DMA_SELF_TEST_H
#define DMA_SELF_TEST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test result codes */
#define DMA_TEST_PASS               0
#define DMA_TEST_FAIL_ALIGNMENT    -1
#define DMA_TEST_FAIL_BOUNDARY     -2
#define DMA_TEST_FAIL_MEMORY       -3
#define DMA_TEST_FAIL_CACHE        -4
#define DMA_TEST_FAIL_VDS          -5
#define DMA_TEST_FAIL_CONSTRAINTS  -6
#define DMA_TEST_FAIL_ISR_SAFETY   -7
#define DMA_TEST_FAIL_CONTIGUITY   -8

/* Test suite control flags */
typedef enum {
    TEST_SUITE_QUICK    = 0x01,    /* Quick validation tests only */
    TEST_SUITE_FULL     = 0x02,    /* Complete test suite */
    TEST_SUITE_STRESS   = 0x04,    /* Include stress tests */
    TEST_SUITE_VERBOSE  = 0x08,    /* Verbose output */
    TEST_SUITE_CONTINUE = 0x10     /* Continue on failure */
} test_suite_flags_t;

/* Test results structure */
typedef struct {
    uint32_t tests_run;
    uint32_t tests_passed;
    uint32_t tests_failed;
    uint32_t tests_skipped;
    uint32_t time_elapsed_ms;
    bool production_ready;
    char failure_summary[256];
} test_results_t;

/* Main test functions */
int dma_run_self_tests(void);
int dma_run_quick_tests(void);
int dma_run_stress_tests(void);
test_results_t dma_run_test_suite(test_suite_flags_t flags);

/* Individual test functions (for debugging) */
int dma_test_64kb_boundaries(void);
int dma_test_isa_limits(void);
int dma_test_alignment(void);
int dma_test_bounce_buffers(void);
int dma_test_cache_coherency(void);
int dma_test_device_constraints(void);
int dma_test_vds_support(void);
int dma_test_isr_safety(void);
int dma_test_physical_contiguity(void);

/* Reporting functions */
void dma_print_self_test_report(void);
void dma_print_test_results(const test_results_t* results);
void dma_log_test_failure(const char* test_name, int error_code);

/* Validation helpers */
bool dma_validate_production_readiness(void);
bool dma_validate_device_readiness(const char* device_name);
bool dma_validate_environment(void);

/* Performance benchmarks */
typedef struct {
    uint32_t allocation_time_us;
    uint32_t sync_time_us;
    uint32_t cache_flush_time_us;
    uint32_t boundary_check_time_us;
    uint32_t total_overhead_us;
} performance_metrics_t;

performance_metrics_t dma_benchmark_operations(void);
void dma_print_performance_metrics(const performance_metrics_t* metrics);

#ifdef __cplusplus
}
#endif

#endif /* DMA_SELF_TEST_H */