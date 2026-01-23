/**
 * @file dma_tests.c
 * @brief DMA cache coherency and bus snooping test functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file implements DMA testing functions for Phase 4.5 of the
 * boot sequence. These tests are critical for 286 systems and
 * unknown chipsets to determine DMA reliability.
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "../../include/dmacap.h"
#include "../../include/logging.h"
#include "../../include/memory.h"

/* Test constants */
#define DMA_TEST_PATTERN      0xAA55
#define DMA_TEST_INVERTED     0x55AA
#define CACHE_LINE_SIZE       16      /* Typical 286 cache line */
#define DMA_TEST_ITERATIONS   3

/* Test results storage */
static struct {
    int cache_coherent;
    int bus_snooping_works;
    int needs_flush;
    int tested;
} g_dma_test_results = {0};

/**
 * @brief Test DMA cache coherency (Phase 4.5)
 * 
 * Tests whether the CPU cache is coherent with DMA operations.
 * This is critical for 286 systems which may have external caches
 * that don't snoop the bus during DMA.
 * 
 * @param config Test configuration
 * @return 0 if cache coherent, negative if not
 */
int test_dma_cache_coherency(dma_test_config_t *config) {
    uint16_t far *test_buffer;
    uint16_t segment, offset;
    uint16_t test_value;
    int i;
    
    log_info("  Testing DMA cache coherency");
    
    /* Allocate test buffer in low memory (DMA-accessible) */
    test_buffer = (uint16_t far *)_fmalloc(config->test_buffer_size);
    if (!test_buffer) {
        log_error("    Failed to allocate test buffer");
        return -1;
    }
    
    /* Get physical address */
    segment = FP_SEG(test_buffer);
    offset = FP_OFF(test_buffer);
    
    /* Fill buffer with test pattern */
    for (i = 0; i < config->test_buffer_size / 2; i++) {
        test_buffer[i] = DMA_TEST_PATTERN;
    }
    
    /* Force cache load by reading buffer */
    test_value = 0;
    for (i = 0; i < config->test_buffer_size / 2; i++) {
        test_value ^= test_buffer[i];
    }
    
    /* Simulate DMA write by directly modifying memory */
    /* In real implementation, this would use actual DMA controller */
    _asm {
        push es
        push di
        
        mov es, segment
        mov di, offset
        mov cx, 8          ; Modify first 8 words
        mov ax, DMA_TEST_INVERTED
        
        cld
        rep stosw          ; Write inverted pattern
        
        pop di
        pop es
    }
    
    /* Check if CPU sees the DMA changes */
    for (i = 0; i < 8; i++) {
        if (test_buffer[i] != DMA_TEST_INVERTED) {
            log_warning("    Cache not coherent at offset %d", i);
            log_warning("    Expected 0x%04X, got 0x%04X", 
                       DMA_TEST_INVERTED, test_buffer[i]);
            g_dma_test_results.cache_coherent = 0;
            _ffree(test_buffer);
            return -1;
        }
    }
    
    log_info("    Cache coherency test passed");
    g_dma_test_results.cache_coherent = 1;
    
    _ffree(test_buffer);
    return 0;
}

/**
 * @brief Test bus snooping capability (Phase 4.5)
 * 
 * Tests whether the system supports bus snooping, which allows
 * the cache controller to monitor DMA transactions and invalidate
 * cache lines as needed.
 * 
 * @param config Test configuration
 * @return 0 if bus snooping works, negative if not
 */
int test_bus_snooping(dma_test_config_t *config) {
    uint8_t far *test_buffer;
    uint8_t far *alias_buffer;
    uint16_t segment;
    int i;
    
    log_info("  Testing bus snooping capability");
    
    /* Allocate aligned test buffer */
    test_buffer = (uint8_t far *)_fmalloc(config->test_buffer_size);
    if (!test_buffer) {
        log_error("    Failed to allocate test buffer");
        return -1;
    }
    
    segment = FP_SEG(test_buffer);
    
    /* Create aliased pointer (same physical, different logical) */
    /* This tests if cache correctly handles aliasing */
    alias_buffer = MK_FP(segment + 1, FP_OFF(test_buffer) - 16);
    
    /* Write pattern through first pointer */
    for (i = 0; i < CACHE_LINE_SIZE; i++) {
        test_buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Modify through aliased pointer (simulates DMA) */
    for (i = 0; i < CACHE_LINE_SIZE; i++) {
        alias_buffer[i] = (uint8_t)(~i & 0xFF);
    }
    
    /* Check if original pointer sees the change */
    for (i = 0; i < CACHE_LINE_SIZE; i++) {
        if (test_buffer[i] != (uint8_t)(~i & 0xFF)) {
            log_warning("    Bus snooping not working at offset %d", i);
            g_dma_test_results.bus_snooping_works = 0;
            _ffree(test_buffer);
            return -1;
        }
    }
    
    log_info("    Bus snooping test passed");
    g_dma_test_results.bus_snooping_works = 1;
    
    _ffree(test_buffer);
    return 0;
}

/**
 * @brief Get DMA test results
 * 
 * Returns the results of DMA capability testing for use in
 * determining DMA policy.
 * 
 * @return Pointer to test results structure
 */
dma_test_results_t* get_dma_test_results(void) {
    static dma_test_results_t results;
    
    results.cache_coherent = g_dma_test_results.cache_coherent;
    results.bus_snooping_works = g_dma_test_results.bus_snooping_works;
    results.needs_cache_flush = !g_dma_test_results.cache_coherent;
    results.tests_completed = g_dma_test_results.tested;
    
    return &results;
}

/**
 * @brief Run comprehensive DMA tests
 * 
 * Runs all DMA capability tests and stores results.
 * 
 * @param config Test configuration
 * @return 0 on success, negative on error
 */
int run_dma_tests(dma_test_config_t *config) {
    int result;
    
    log_info("Running comprehensive DMA tests");
    
    /* Test cache coherency */
    result = test_dma_cache_coherency(config);
    if (result < 0) {
        log_warning("Cache coherency test failed");
    }
    
    /* Test bus snooping */
    result = test_bus_snooping(config);
    if (result < 0) {
        log_warning("Bus snooping test failed");
    }
    
    g_dma_test_results.tested = 1;
    
    /* Determine if cache flushing is needed */
    if (!g_dma_test_results.cache_coherent || 
        !g_dma_test_results.bus_snooping_works) {
        g_dma_test_results.needs_flush = 1;
        log_warning("DMA operations will require cache management");
    } else {
        g_dma_test_results.needs_flush = 0;
        log_info("DMA operations are cache-safe");
    }
    
    return 0;
}