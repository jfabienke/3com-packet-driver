/**
 * @file dma_capability_test.h
 * @brief DMA capability testing and policy refinement
 * 
 * GPT-5 A+ Enhancement: Phase 2 DMA capability detection
 * Tests actual hardware behavior to optimize DMA strategy
 */

#ifndef _DMA_CAPABILITY_TEST_H_
#define _DMA_CAPABILITY_TEST_H_

#include <stdint.h>
#include <stdbool.h>
#include "../include/platform_probe.h"
#include "../include/hardware.h"

/* Test result codes */
typedef enum {
    DMA_TEST_SUCCESS = 0,
    DMA_TEST_FAILED = -1,
    DMA_TEST_SKIPPED = -2,
    DMA_TEST_NOT_SUPPORTED = -3,
    DMA_TEST_TIMEOUT = -4
} dma_test_result_t;

/* Cache modes */
typedef enum {
    CACHE_MODE_UNKNOWN = 0,
    CACHE_MODE_WRITE_THROUGH,
    CACHE_MODE_WRITE_BACK,
    CACHE_MODE_DISABLED
} cache_mode_t;

/* Individual test results */
typedef struct {
    bool cache_coherent;           /* DMA and CPU caches are coherent */
    bool bus_snooping;             /* Chipset snoops DMA transfers */
    bool can_cross_64k;            /* Can DMA across 64KB boundaries */
    bool supports_burst;           /* Supports burst DMA transfers */
    bool needs_alignment;          /* Requires specific alignment */
    uint16_t optimal_alignment;    /* Optimal buffer alignment */
    cache_mode_t cache_mode;       /* Detected cache mode */
    uint32_t max_dma_size;         /* Maximum single DMA transfer */
    uint32_t dma_latency_us;       /* DMA latency in microseconds */
    
    /* Additional test results for A+ grade */
    uint32_t cache_flush_overhead_us; /* Cache flush overhead per KB */
    int32_t dma_gain_256b;         /* DMA performance gain at 256B (%) */
    int32_t dma_gain_1514b;        /* DMA performance gain at 1514B (%) */
    uint16_t optimal_copybreak;    /* Optimal PIO/DMA threshold */
    uint16_t adjusted_copybreak;   /* Adjusted for cache overhead */
    bool misalignment_safe;        /* DMA safe with misaligned buffers */
} dma_test_results_t;

/* Refined DMA capabilities after testing */
typedef struct {
    /* Base policy from Phase 1 */
    dma_policy_t base_policy;
    
    /* Test results */
    dma_test_results_t test_results;
    
    /* Derived strategies */
    bool needs_cache_flush;        /* Must flush before DMA */
    bool needs_cache_invalidate;   /* Must invalidate after DMA */
    bool needs_bounce_64k;         /* Must use bounce for 64K crossing */
    bool needs_explicit_sync;      /* Requires manual sync operations */
    bool can_use_zero_copy;        /* All tests passed, optimal path */
    
    /* Performance hints */
    uint16_t recommended_buffer_size;  /* Optimal buffer size */
    uint16_t recommended_ring_size;    /* Optimal descriptor ring size */
    
    /* Fallback strategies */
    bool pio_fallback_available;   /* Can fall back to PIO */
    bool bounce_fallback_available; /* Can use bounce buffers */
    
    /* Confidence level */
    uint8_t confidence_percent;    /* 0-100% confidence in results */
} dma_capabilities_t;

/* Test control structure */
typedef struct {
    bool skip_destructive_tests;   /* Skip tests that modify memory */
    bool verbose_output;           /* Detailed test output */
    uint16_t test_iterations;      /* Number of iterations per test */
    uint32_t test_buffer_size;     /* Size of test buffers */
    uint32_t timeout_ms;           /* Test timeout in milliseconds */
} dma_test_config_t;

/* Global capability results */
extern dma_capabilities_t g_dma_caps;
extern bool g_dma_tests_complete;

/* Function prototypes */

/**
 * @brief Run comprehensive DMA capability tests
 * 
 * This should be called after NIC initialization but before
 * starting normal operations.
 * 
 * @param nic NIC to test with (NULL for memory-only tests)
 * @param config Test configuration
 * @return DMA_TEST_SUCCESS or error code
 */
int run_dma_capability_tests(nic_info_t *nic, dma_test_config_t *config);

/**
 * @brief Test cache coherency between CPU and DMA
 * 
 * @param nic NIC to test with
 * @param results Output test results
 * @return true if coherent, false otherwise
 */
bool test_cache_coherency(nic_info_t *nic, dma_test_results_t *results);

/**
 * @brief Test if chipset performs bus snooping
 * 
 * @param nic NIC to test with
 * @param results Output test results
 * @return true if snooping active, false otherwise
 */
bool test_bus_snooping(nic_info_t *nic, dma_test_results_t *results);

/**
 * @brief Test DMA across 64KB boundaries
 * 
 * @param nic NIC to test with
 * @param results Output test results
 * @return true if can cross boundaries, false otherwise
 */
bool test_64kb_boundary(nic_info_t *nic, dma_test_results_t *results);

/**
 * @brief Detect cache mode (write-back vs write-through)
 * 
 * @param results Output test results
 * @return Detected cache mode
 */
cache_mode_t test_cache_mode(dma_test_results_t *results);

/**
 * @brief Test DMA alignment requirements
 * 
 * @param nic NIC to test with
 * @param results Output test results
 * @return Optimal alignment in bytes
 */
uint16_t test_dma_alignment(nic_info_t *nic, dma_test_results_t *results);

/**
 * @brief Test burst DMA capability
 * 
 * @param nic NIC to test with
 * @param results Output test results
 * @return true if burst mode supported
 */
bool test_burst_mode(nic_info_t *nic, dma_test_results_t *results);

/**
 * @brief Refine DMA policy based on test results
 * 
 * Takes the base policy from Phase 1 and test results from Phase 2
 * to create an optimized DMA strategy.
 * 
 * @param base_policy Policy from early platform probe
 * @param test_results Results from capability tests
 * @return Refined DMA capabilities
 */
dma_capabilities_t refine_dma_policy(
    dma_policy_t base_policy,
    dma_test_results_t *test_results
);

/**
 * @brief Apply refined DMA capabilities globally
 * 
 * Updates global DMA subsystem with refined capabilities
 * 
 * @param caps Refined capabilities to apply
 * @return 0 on success, negative on error
 */
int apply_dma_capabilities(dma_capabilities_t *caps);

/**
 * @brief Get human-readable description of capabilities
 * 
 * @param caps Capabilities to describe
 * @param buffer Output buffer
 * @param size Buffer size
 */
void describe_dma_capabilities(
    dma_capabilities_t *caps,
    char *buffer,
    size_t size
);

/**
 * @brief Print detailed test results
 * 
 * @param results Test results to print
 */
void print_dma_test_results(dma_test_results_t *results);

/* Phase 4.5 DMA test functions (early boot, no NIC required) */
int test_dma_cache_coherency(dma_test_config_t *config);
int test_bus_snooping(dma_test_config_t *config);
int run_dma_tests(dma_test_config_t *config);
dma_test_results_t* get_dma_test_results(void);

/**
 * @brief Get recommended DMA strategy
 * 
 * @return Pointer to global refined capabilities
 */
dma_capabilities_t* get_dma_capabilities(void);

/**
 * @brief Check if DMA tests have been run
 * 
 * @return true if tests complete, false otherwise
 */
bool dma_tests_completed(void);

/* Cache management helpers based on test results */

/**
 * @brief Flush caches if needed based on capabilities
 * 
 * @param addr Buffer address
 * @param size Buffer size
 */
void dma_flush_if_needed(void *addr, size_t size);

/**
 * @brief Invalidate caches if needed based on capabilities
 * 
 * @param addr Buffer address
 * @param size Buffer size
 */
void dma_invalidate_if_needed(void *addr, size_t size);

/**
 * @brief Check if buffer needs bounce due to 64K boundary
 * 
 * @param addr Buffer address
 * @param size Buffer size
 * @return true if bounce buffer needed
 */
bool dma_needs_bounce_buffer(void *addr, size_t size);

#endif /* _DMA_CAPABILITY_TEST_H_ */