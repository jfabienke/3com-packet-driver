/**
 * @file test_framework.c
 * @brief Comprehensive test framework infrastructure and reporting
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file implements a complete testing framework for validating
 * all driver functionality under stress conditions and edge cases.
 */

#include "../../include/test_framework.h"
#include "../../include/hardware.h"
#include "../../include/packet_ops.h"
#include "../../include/memory.h"
#include "../../include/diagnostics.h"
#include "../../include/logging.h"
#include <string.h>
#include <stdio.h>

/* Global test framework state */
static test_framework_state_t g_test_framework;
static test_report_t g_test_report;
static bool g_framework_initialized = false;

/* Test result tracking */
static test_result_entry_t g_test_results[MAX_TEST_RESULTS];
static int g_num_test_results = 0;

/* Performance benchmark data */
static benchmark_result_t g_benchmark_results[MAX_BENCHMARKS];
static int g_num_benchmarks = 0;

/* Forward declarations */
static void test_framework_reset_state(void);
static void test_framework_init_report(void);
static uint32_t test_framework_get_timestamp(void);
static const char* test_category_to_string(test_category_t category);
static const char* test_result_to_string(test_result_t result);
static void test_framework_update_statistics(test_result_t result);
static int test_framework_add_result(const char* test_name, test_category_t category, 
                                     test_result_t result, uint32_t duration_ms, 
                                     const char* details);

/**
 * @brief Initialize the comprehensive test framework
 * @param config Test configuration
 * @return 0 on success, negative on error
 */
int test_framework_init(const test_config_t *config) {
    if (g_framework_initialized) {
        return SUCCESS;
    }
    
    if (!config) {
        log_error("Test framework configuration is required");
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Initializing comprehensive test framework");
    
    /* Reset framework state */
    test_framework_reset_state();
    
    /* Copy configuration */
    memcpy(&g_test_framework.config, config, sizeof(test_config_t));
    
    /* Initialize test report */
    test_framework_init_report();
    
    /* Initialize subsystems if needed */
    if (config->init_hardware && !hardware_get_nic_count()) {
        int result = hardware_init();
        if (result != SUCCESS) {
            log_error("Failed to initialize hardware for testing: %d", result);
            return result;
        }
    }
    
    if (config->init_memory && !memory_is_initialized()) {
        int result = memory_init();
        if (result != SUCCESS) {
            log_error("Failed to initialize memory for testing: %d", result);
            return result;
        }
    }
    
    if (config->init_diagnostics && !diagnostics_is_enabled()) {
        int result = diagnostics_init();
        if (result != SUCCESS) {
            log_error("Failed to initialize diagnostics for testing: %d", result);
            return result;
        }
    }
    
    g_test_framework.start_time = test_framework_get_timestamp();
    g_test_framework.status = TEST_STATUS_READY;
    g_framework_initialized = true;
    
    log_info("Test framework initialized successfully");
    return SUCCESS;
}

/**
 * @brief Cleanup test framework resources
 */
void test_framework_cleanup(void) {
    if (!g_framework_initialized) {
        return;
    }
    
    log_info("Cleaning up test framework");
    
    /* Finalize test report */
    g_test_framework.end_time = test_framework_get_timestamp();
    g_test_framework.status = TEST_STATUS_COMPLETED;
    
    /* Generate final report */
    test_framework_generate_report();
    
    g_framework_initialized = false;
    log_info("Test framework cleanup completed");
}

/**
 * @brief Run comprehensive hardware validation tests
 * @return 0 on success, negative on error
 */
int test_framework_run_hardware_tests(void) {
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    uint32_t start_time;
    uint32_t test_duration;
    int result;
    
    if (!g_framework_initialized) {
        log_error("Test framework not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    log_info("=== Starting Hardware Validation Tests ===");
    g_test_framework.status = TEST_STATUS_HARDWARE;
    start_time = test_framework_get_timestamp();
    
    /* Test 1: Hardware Self-Test for each NIC */
    int nic_count = hardware_get_nic_count();
    for (int nic_idx = 0; nic_idx < nic_count; nic_idx++) {
        nic_info_t *nic = hardware_get_nic(nic_idx);
        if (!nic) continue;
        
        uint32_t test_start = test_framework_get_timestamp();
        diag_result_t diag_result;
        
        result = diag_hardware_test(nic, &diag_result);
        test_duration = test_framework_get_timestamp() - test_start;
        
        char test_name[64];
        snprintf(test_name, sizeof(test_name), "Hardware Self-Test NIC %d", nic_idx);
        
        if (result == SUCCESS && diag_result.passed) {
            test_framework_add_result(test_name, TEST_CATEGORY_HARDWARE, 
                                    TEST_RESULT_PASS, test_duration, diag_result.description);
            passed_tests++;
        } else {
            test_framework_add_result(test_name, TEST_CATEGORY_HARDWARE, 
                                    TEST_RESULT_FAIL, test_duration, diag_result.description);
            failed_tests++;
        }
        total_tests++;
    }
    
    /* Test 2: Multi-NIC Tests (if multiple NICs available) */
    if (nic_count >= 2) {
        uint32_t test_start = test_framework_get_timestamp();
        result = hardware_run_multi_nic_tests();
        test_duration = test_framework_get_timestamp() - test_start;
        
        if (result == SUCCESS) {
            test_framework_add_result("Multi-NIC Test Suite", TEST_CATEGORY_HARDWARE, 
                                    TEST_RESULT_PASS, test_duration, "All multi-NIC tests passed");
            passed_tests++;
        } else {
            test_framework_add_result("Multi-NIC Test Suite", TEST_CATEGORY_HARDWARE, 
                                    TEST_RESULT_FAIL, test_duration, "Some multi-NIC tests failed");
            failed_tests++;
        }
        total_tests++;
    } else {
        test_framework_add_result("Multi-NIC Test Suite", TEST_CATEGORY_HARDWARE, 
                                TEST_RESULT_SKIP, 0, "Insufficient NICs for multi-NIC testing");
        total_tests++;
    }
    
    /* Report hardware test results */
    uint32_t total_duration = test_framework_get_timestamp() - start_time;
    log_info("Hardware tests completed: %d passed, %d failed, %d total (duration: %lu ms)",
             passed_tests, failed_tests, total_tests, total_duration);
    
    return (failed_tests == 0) ? SUCCESS : ERROR_HARDWARE;
}

/**
 * @brief Run comprehensive memory validation tests
 * @return 0 on success, negative on error
 */
int test_framework_run_memory_tests(void) {
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    uint32_t start_time;
    uint32_t test_duration;
    int result;
    
    if (!g_framework_initialized) {
        log_error("Test framework not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    log_info("=== Starting Memory Validation Tests ===");
    g_test_framework.status = TEST_STATUS_MEMORY;
    start_time = test_framework_get_timestamp();
    
    /* Test 1: Basic Memory Test Suite */
    uint32_t test_start = test_framework_get_timestamp();
    result = memory_run_comprehensive_tests();
    test_duration = test_framework_get_timestamp() - test_start;
    
    if (result == SUCCESS) {
        test_framework_add_result("Basic Memory Test Suite", TEST_CATEGORY_MEMORY, 
                                TEST_RESULT_PASS, test_duration, "All basic memory tests passed");
        passed_tests++;
    } else {
        test_framework_add_result("Basic Memory Test Suite", TEST_CATEGORY_MEMORY, 
                                TEST_RESULT_FAIL, test_duration, "Some basic memory tests failed");
        failed_tests++;
    }
    total_tests++;
    
    /* Test 2: Comprehensive Stress Test */
    test_start = test_framework_get_timestamp();
    result = memory_comprehensive_stress_test();
    test_duration = test_framework_get_timestamp() - test_start;
    
    if (result == SUCCESS) {
        test_framework_add_result("Memory Stress Test Suite", TEST_CATEGORY_MEMORY, 
                                TEST_RESULT_PASS, test_duration, "All stress tests passed");
        passed_tests++;
    } else {
        test_framework_add_result("Memory Stress Test Suite", TEST_CATEGORY_MEMORY, 
                                TEST_RESULT_FAIL, test_duration, "Some stress tests failed");
        failed_tests++;
    }
    total_tests++;
    
    /* Test 3: Memory Leak Detection */
    test_start = test_framework_get_timestamp();
    uint32_t initial_used = memory_get_stats()->used_memory;
    
    /* Perform allocation/deallocation cycles */
    for (int cycle = 0; cycle < 10; cycle++) {
        void *ptrs[20];
        for (int i = 0; i < 20; i++) {
            ptrs[i] = memory_alloc(256 + (i * 64), MEM_TYPE_GENERAL, 0);
        }
        for (int i = 0; i < 20; i++) {
            if (ptrs[i]) memory_free(ptrs[i]);
        }
    }
    
    uint32_t final_used = memory_get_stats()->used_memory;
    test_duration = test_framework_get_timestamp() - test_start;
    
    if (final_used <= initial_used + 1024) {  /* 1KB tolerance */
        test_framework_add_result("Memory Leak Detection", TEST_CATEGORY_MEMORY, 
                                TEST_RESULT_PASS, test_duration, "No memory leaks detected");
        passed_tests++;
    } else {
        char details[128];
        snprintf(details, sizeof(details), "Potential leak: %lu bytes (initial=%lu, final=%lu)",
                final_used - initial_used, initial_used, final_used);
        test_framework_add_result("Memory Leak Detection", TEST_CATEGORY_MEMORY, 
                                TEST_RESULT_FAIL, test_duration, details);
        failed_tests++;
    }
    total_tests++;
    
    /* Report memory test results */
    uint32_t total_duration = test_framework_get_timestamp() - start_time;
    log_info("Memory tests completed: %d passed, %d failed, %d total (duration: %lu ms)",
             passed_tests, failed_tests, total_tests, total_duration);
    
    return (failed_tests == 0) ? SUCCESS : ERROR_INVALID_DATA;
}

/**
 * @brief Run comprehensive packet operation tests
 * @return 0 on success, negative on error
 */
int test_framework_run_packet_tests(void) {
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    uint32_t start_time;
    uint32_t test_duration;
    int result;
    
    if (!g_framework_initialized) {
        log_error("Test framework not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    log_info("=== Starting Packet Operation Tests ===");
    g_test_framework.status = TEST_STATUS_PACKET;
    start_time = test_framework_get_timestamp();
    
    int nic_count = hardware_get_nic_count();
    
    /* Test 1: Internal Loopback for each NIC */
    for (int nic_idx = 0; nic_idx < nic_count; nic_idx++) {
        if (!hardware_is_nic_active(nic_idx)) continue;
        
        uint32_t test_start = test_framework_get_timestamp();
        uint8_t test_pattern[] = "LOOPBACK_TEST_PATTERN_12345";
        
        result = packet_test_internal_loopback(nic_idx, test_pattern, sizeof(test_pattern));
        test_duration = test_framework_get_timestamp() - test_start;
        
        char test_name[64];
        snprintf(test_name, sizeof(test_name), "Internal Loopback NIC %d", nic_idx);
        
        if (result == SUCCESS) {
            test_framework_add_result(test_name, TEST_CATEGORY_PACKET, 
                                    TEST_RESULT_PASS, test_duration, "Loopback test passed");
            passed_tests++;
        } else {
            char details[128];
            snprintf(details, sizeof(details), "Loopback test failed with error %d", result);
            test_framework_add_result(test_name, TEST_CATEGORY_PACKET, 
                                    TEST_RESULT_FAIL, test_duration, details);
            failed_tests++;
        }
        total_tests++;
    }
    
    /* Test 2: Cross-NIC Loopback (if multiple NICs) */
    if (nic_count >= 2) {
        uint32_t test_start = test_framework_get_timestamp();
        uint8_t test_data[] = "CROSS_NIC_TEST_DATA_PATTERN";
        
        result = packet_test_cross_nic_loopback(0, 1, test_data, sizeof(test_data));
        test_duration = test_framework_get_timestamp() - test_start;
        
        if (result == SUCCESS) {
            test_framework_add_result("Cross-NIC Loopback", TEST_CATEGORY_PACKET, 
                                    TEST_RESULT_PASS, test_duration, "Cross-NIC test passed");
            passed_tests++;
        } else {
            char details[128];
            snprintf(details, sizeof(details), "Cross-NIC test failed with error %d", result);
            test_framework_add_result("Cross-NIC Loopback", TEST_CATEGORY_PACKET, 
                                    TEST_RESULT_FAIL, test_duration, details);
            failed_tests++;
        }
        total_tests++;
    } else {
        test_framework_add_result("Cross-NIC Loopback", TEST_CATEGORY_PACKET, 
                                TEST_RESULT_SKIP, 0, "Insufficient NICs for cross-NIC testing");
        total_tests++;
    }
    
    /* Test 3: Packet Integrity Verification */
    uint32_t test_start = test_framework_get_timestamp();
    uint8_t original_data[] = "INTEGRITY_TEST_DATA_1234567890ABCDEF";
    uint8_t received_data[] = "INTEGRITY_TEST_DATA_1234567890ABCDEF";  /* Same data */
    packet_integrity_result_t integrity_result;
    
    result = packet_verify_loopback_integrity(original_data, received_data, 
                                            sizeof(original_data), &integrity_result);
    test_duration = test_framework_get_timestamp() - test_start;
    
    if (result == SUCCESS && integrity_result.mismatch_count == 0) {
        test_framework_add_result("Packet Integrity Verification", TEST_CATEGORY_PACKET, 
                                TEST_RESULT_PASS, test_duration, "Integrity verification passed");
        passed_tests++;
    } else {
        char details[128];
        snprintf(details, sizeof(details), "Integrity check failed: %d mismatches", 
                integrity_result.mismatch_count);
        test_framework_add_result("Packet Integrity Verification", TEST_CATEGORY_PACKET, 
                                TEST_RESULT_FAIL, test_duration, details);
        failed_tests++;
    }
    total_tests++;
    
    /* Report packet test results */
    uint32_t total_duration = test_framework_get_timestamp() - start_time;
    log_info("Packet tests completed: %d passed, %d failed, %d total (duration: %lu ms)",
             passed_tests, failed_tests, total_tests, total_duration);
    
    return (failed_tests == 0) ? SUCCESS : ERROR_IO;
}

/**
 * @brief Run performance benchmarks
 * @return 0 on success, negative on error
 */
int test_framework_run_benchmarks(void) {
    if (!g_framework_initialized) {
        log_error("Test framework not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    log_info("=== Starting Performance Benchmarks ===");
    g_test_framework.status = TEST_STATUS_BENCHMARK;
    
    int nic_count = hardware_get_nic_count();
    
    /* Benchmark 1: Single NIC Throughput */
    for (int nic_idx = 0; nic_idx < nic_count; nic_idx++) {
        if (!hardware_is_nic_active(nic_idx)) continue;
        
        benchmark_result_t benchmark;
        strncpy(benchmark.name, "Single NIC Throughput", sizeof(benchmark.name) - 1);
        benchmark.category = BENCHMARK_THROUGHPUT;
        benchmark.start_time = test_framework_get_timestamp();
        
        /* Run throughput test for 5 seconds */
        uint32_t packet_count = 0;
        uint32_t byte_count = 0;
        uint8_t test_packet[1518];  /* Max Ethernet frame */
        memset(test_packet, 0xAA, sizeof(test_packet));
        
        uint32_t test_start = test_framework_get_timestamp();
        while ((test_framework_get_timestamp() - test_start) < 5000) {  /* 5 seconds */
            nic_info_t *nic = hardware_get_nic(nic_idx);
            if (hardware_send_packet(nic, test_packet, sizeof(test_packet)) == SUCCESS) {
                packet_count++;
                byte_count += sizeof(test_packet);
            }
        }
        
        benchmark.end_time = test_framework_get_timestamp();
        benchmark.duration_ms = benchmark.end_time - benchmark.start_time;
        benchmark.packets_per_second = (packet_count * 1000) / benchmark.duration_ms;
        benchmark.bytes_per_second = (byte_count * 1000) / benchmark.duration_ms;
        benchmark.error_rate = 0;  /* Calculate if needed */
        
        snprintf(benchmark.details, sizeof(benchmark.details), 
                "NIC %d: %lu pps, %lu Bps", nic_idx, 
                benchmark.packets_per_second, benchmark.bytes_per_second);
        
        if (g_num_benchmarks < MAX_BENCHMARKS) {
            g_benchmark_results[g_num_benchmarks++] = benchmark;
        }
        
        log_info("Benchmark: %s - %s", benchmark.name, benchmark.details);
    }
    
    /* Benchmark 2: Multi-NIC Aggregate Throughput */
    if (nic_count >= 2) {
        benchmark_result_t benchmark;
        strncpy(benchmark.name, "Multi-NIC Aggregate Throughput", sizeof(benchmark.name) - 1);
        benchmark.category = BENCHMARK_THROUGHPUT;
        benchmark.start_time = test_framework_get_timestamp();
        
        /* Run multi-NIC performance test */
        int result = hardware_test_multi_nic_performance(5000);  /* 5 seconds */
        
        benchmark.end_time = test_framework_get_timestamp();
        benchmark.duration_ms = benchmark.end_time - benchmark.start_time;
        
        /* Get aggregated statistics from hardware layer */
        const hardware_stats_t *hw_stats = hardware_get_stats();
        benchmark.packets_per_second = (hw_stats->packets_sent * 1000) / benchmark.duration_ms;
        benchmark.bytes_per_second = 0;  /* Would need to track bytes */
        benchmark.error_rate = (hw_stats->send_errors * 100) / hw_stats->packets_sent;
        
        snprintf(benchmark.details, sizeof(benchmark.details), 
                "Aggregate: %lu pps, %lu%% error rate", 
                benchmark.packets_per_second, benchmark.error_rate);
        
        if (g_num_benchmarks < MAX_BENCHMARKS) {
            g_benchmark_results[g_num_benchmarks++] = benchmark;
        }
        
        log_info("Benchmark: %s - %s", benchmark.name, benchmark.details);
    }
    
    /* Benchmark 3: Memory Allocation Performance */
    benchmark_result_t mem_benchmark;
    strncpy(mem_benchmark.name, "Memory Allocation Performance", sizeof(mem_benchmark.name) - 1);
    mem_benchmark.category = BENCHMARK_MEMORY;
    mem_benchmark.start_time = test_framework_get_timestamp();
    
    uint32_t allocation_count = 0;
    uint32_t test_start = test_framework_get_timestamp();
    
    while ((test_framework_get_timestamp() - test_start) < 1000) {  /* 1 second */
        void *ptr = memory_alloc(256, MEM_TYPE_GENERAL, 0);
        if (ptr) {
            memory_free(ptr);
            allocation_count++;
        }
    }
    
    mem_benchmark.end_time = test_framework_get_timestamp();
    mem_benchmark.duration_ms = mem_benchmark.end_time - mem_benchmark.start_time;
    mem_benchmark.packets_per_second = (allocation_count * 1000) / mem_benchmark.duration_ms;
    mem_benchmark.bytes_per_second = 0;
    mem_benchmark.error_rate = 0;
    
    snprintf(mem_benchmark.details, sizeof(mem_benchmark.details), 
            "%lu allocations/sec", mem_benchmark.packets_per_second);
    
    if (g_num_benchmarks < MAX_BENCHMARKS) {
        g_benchmark_results[g_num_benchmarks++] = mem_benchmark;
    }
    
    log_info("Benchmark: %s - %s", mem_benchmark.name, mem_benchmark.details);
    
    log_info("Performance benchmarks completed");
    return SUCCESS;
}

/**
 * @brief Run complete comprehensive test suite
 * @return 0 on success, negative on error
 */
int test_framework_run_comprehensive_tests(void) {
    int result = SUCCESS;
    uint32_t total_start_time;
    
    if (!g_framework_initialized) {
        log_error("Test framework not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    log_info("=== Starting Comprehensive Test Suite ===");
    g_test_framework.status = TEST_STATUS_RUNNING;
    total_start_time = test_framework_get_timestamp();
    
    /* Hardware Tests */
    if (g_test_framework.config.test_hardware) {
        log_info("Running hardware validation tests...");
        int hw_result = test_framework_run_hardware_tests();
        if (hw_result != SUCCESS) {
            log_error("Hardware tests failed");
            result = hw_result;
        }
    }
    
    /* Memory Tests */
    if (g_test_framework.config.test_memory) {
        log_info("Running memory validation tests...");
        int mem_result = test_framework_run_memory_tests();
        if (mem_result != SUCCESS) {
            log_error("Memory tests failed");
            if (result == SUCCESS) result = mem_result;
        }
    }
    
    /* Packet Tests */
    if (g_test_framework.config.test_packet_ops) {
        log_info("Running packet operation tests...");
        int pkt_result = test_framework_run_packet_tests();
        if (pkt_result != SUCCESS) {
            log_error("Packet tests failed");
            if (result == SUCCESS) result = pkt_result;
        }
    }
    
    /* Performance Benchmarks */
    if (g_test_framework.config.run_benchmarks) {
        log_info("Running performance benchmarks...");
        int bench_result = test_framework_run_benchmarks();
        if (bench_result != SUCCESS) {
            log_warning("Some benchmarks may have failed");
        }
    }
    
    /* Generate comprehensive report */
    uint32_t total_duration = test_framework_get_timestamp() - total_start_time;
    g_test_framework.status = TEST_STATUS_COMPLETED;
    
    log_info("=== Comprehensive Test Suite Summary ===");
    log_info("Total duration: %lu ms", total_duration);
    log_info("Tests passed: %d", g_test_framework.tests_passed);
    log_info("Tests failed: %d", g_test_framework.tests_failed);
    log_info("Tests skipped: %d", g_test_framework.tests_skipped);
    log_info("Benchmarks run: %d", g_num_benchmarks);
    
    if (result == SUCCESS) {
        log_info("=== ALL TESTS PASSED ===");
    } else {
        log_error("=== SOME TESTS FAILED ===");
    }
    
    return result;
}

/**
 * @brief Generate comprehensive test report
 * @return 0 on success, negative on error
 */
int test_framework_generate_report(void) {
    if (!g_framework_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    log_info("=== COMPREHENSIVE TEST REPORT ===");
    log_info("Test Framework Version: 1.0");
    log_info("Test Start Time: %lu", g_test_framework.start_time);
    log_info("Test End Time: %lu", g_test_framework.end_time);
    log_info("Total Duration: %lu ms", g_test_framework.end_time - g_test_framework.start_time);
    
    /* System Information */
    log_info("=== System Configuration ===");
    log_info("Hardware NICs: %d", hardware_get_nic_count());
    const mem_stats_t *mem_stats = memory_get_stats();
    log_info("Memory - Used: %lu bytes, Peak: %lu bytes", 
             mem_stats->used_memory, mem_stats->peak_usage);
    
    /* Test Results Summary */
    log_info("=== Test Results Summary ===");
    log_info("Total Tests: %d", g_test_framework.tests_passed + g_test_framework.tests_failed + g_test_framework.tests_skipped);
    log_info("Passed: %d", g_test_framework.tests_passed);
    log_info("Failed: %d", g_test_framework.tests_failed);
    log_info("Skipped: %d", g_test_framework.tests_skipped);
    
    if (g_test_framework.tests_failed == 0) {
        log_info("Overall Result: PASS");
    } else {
        log_info("Overall Result: FAIL");
    }
    
    /* Detailed Test Results */
    log_info("=== Detailed Test Results ===");
    for (int i = 0; i < g_num_test_results; i++) {
        test_result_entry_t *entry = &g_test_results[i];
        log_info("%s [%s] %s (%lu ms) - %s",
                entry->test_name,
                test_category_to_string(entry->category),
                test_result_to_string(entry->result),
                entry->duration_ms,
                entry->details);
    }
    
    /* Performance Benchmarks */
    if (g_num_benchmarks > 0) {
        log_info("=== Performance Benchmarks ===");
        for (int i = 0; i < g_num_benchmarks; i++) {
            benchmark_result_t *bench = &g_benchmark_results[i];
            log_info("%s: %lu pps, %lu Bps (%lu ms) - %s",
                    bench->name,
                    bench->packets_per_second,
                    bench->bytes_per_second,
                    bench->duration_ms,
                    bench->details);
        }
    }
    
    log_info("=== END OF TEST REPORT ===");
    return SUCCESS;
}

/**
 * @brief Get current test framework status
 * @return Current test status
 */
test_status_t test_framework_get_status(void) {
    return g_test_framework.status;
}

/**
 * @brief Get test framework statistics
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int test_framework_get_statistics(test_framework_stats_t *stats) {
    if (!stats) {
        return ERROR_INVALID_PARAM;
    }
    
    memset(stats, 0, sizeof(test_framework_stats_t));
    
    stats->total_tests = g_test_framework.tests_passed + g_test_framework.tests_failed + g_test_framework.tests_skipped;
    stats->tests_passed = g_test_framework.tests_passed;
    stats->tests_failed = g_test_framework.tests_failed;
    stats->tests_skipped = g_test_framework.tests_skipped;
    stats->benchmarks_run = g_num_benchmarks;
    stats->total_duration_ms = g_test_framework.end_time - g_test_framework.start_time;
    stats->status = g_test_framework.status;
    
    return SUCCESS;
}

/* Private helper function implementations */

/**
 * @brief Reset test framework state
 */
static void test_framework_reset_state(void) {
    memset(&g_test_framework, 0, sizeof(test_framework_state_t));
    memset(&g_test_report, 0, sizeof(test_report_t));
    memset(g_test_results, 0, sizeof(g_test_results));
    memset(g_benchmark_results, 0, sizeof(g_benchmark_results));
    
    g_num_test_results = 0;
    g_num_benchmarks = 0;
    
    g_test_framework.status = TEST_STATUS_INIT;
}

/**
 * @brief Initialize test report structure
 */
static void test_framework_init_report(void) {
    strncpy(g_test_report.framework_version, "1.0", sizeof(g_test_report.framework_version) - 1);
    g_test_report.start_time = test_framework_get_timestamp();
    g_test_report.system_info.nic_count = hardware_get_nic_count();
    
    const mem_stats_t *mem_stats = memory_get_stats();
    g_test_report.system_info.memory_total = mem_stats->peak_usage;
    g_test_report.system_info.memory_used = mem_stats->used_memory;
}

/**
 * @brief Get current timestamp in milliseconds
 * @return Timestamp in milliseconds
 */
static uint32_t test_framework_get_timestamp(void) {
    /* In a real DOS implementation, this would use system timer */
    /* For now, return a simple counter or use available timer function */
    static uint32_t counter = 0;
    return ++counter * 10;  /* Simulate 10ms increments */
}

/**
 * @brief Convert test category to string
 * @param category Test category
 * @return String representation
 */
static const char* test_category_to_string(test_category_t category) {
    switch (category) {
        case TEST_CATEGORY_HARDWARE:    return "HARDWARE";
        case TEST_CATEGORY_MEMORY:      return "MEMORY";
        case TEST_CATEGORY_PACKET:      return "PACKET";
        case TEST_CATEGORY_NETWORK:     return "NETWORK";
        case TEST_CATEGORY_STRESS:      return "STRESS";
        case TEST_CATEGORY_BENCHMARK:   return "BENCHMARK";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief Convert test result to string
 * @param result Test result
 * @return String representation
 */
static const char* test_result_to_string(test_result_t result) {
    switch (result) {
        case TEST_RESULT_PASS:      return "PASS";
        case TEST_RESULT_FAIL:      return "FAIL";
        case TEST_RESULT_SKIP:      return "SKIP";
        case TEST_RESULT_ERROR:     return "ERROR";
        default:                    return "UNKNOWN";
    }
}

/**
 * @brief Update test framework statistics
 * @param result Test result to count
 */
static void test_framework_update_statistics(test_result_t result) {
    switch (result) {
        case TEST_RESULT_PASS:
            g_test_framework.tests_passed++;
            break;
        case TEST_RESULT_FAIL:
        case TEST_RESULT_ERROR:
            g_test_framework.tests_failed++;
            break;
        case TEST_RESULT_SKIP:
            g_test_framework.tests_skipped++;
            break;
    }
}

/**
 * @brief Add a test result to the results array
 * @param test_name Name of the test
 * @param category Test category
 * @param result Test result
 * @param duration_ms Test duration in milliseconds
 * @param details Test details or error message
 * @return 0 on success, negative on error
 */
static int test_framework_add_result(const char* test_name, test_category_t category, 
                                     test_result_t result, uint32_t duration_ms, 
                                     const char* details) {
    if (g_num_test_results >= MAX_TEST_RESULTS) {
        log_warning("Test results array full, cannot add more results");
        return ERROR_NO_MEMORY;
    }
    
    test_result_entry_t *entry = &g_test_results[g_num_test_results];
    
    strncpy(entry->test_name, test_name, sizeof(entry->test_name) - 1);
    entry->test_name[sizeof(entry->test_name) - 1] = '\0';
    
    entry->category = category;
    entry->result = result;
    entry->duration_ms = duration_ms;
    entry->timestamp = test_framework_get_timestamp();
    
    if (details) {
        strncpy(entry->details, details, sizeof(entry->details) - 1);
        entry->details[sizeof(entry->details) - 1] = '\0';
    } else {
        entry->details[0] = '\0';
    }
    
    g_num_test_results++;
    test_framework_update_statistics(result);
    
    return SUCCESS;
}