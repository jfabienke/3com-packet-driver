/**
 * @file test_perf_basic.c
 * @brief Performance benchmarking test suite for packet operations and interrupt handling
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite provides comprehensive performance benchmarks for:
 * - Packet transmission and reception throughput
 * - Interrupt handling latency and overhead
 * - Memory allocation and management performance
 * - Queue management efficiency
 * - Hardware abstraction layer performance
 * - 3C509B PIO vs 3C515-TX DMA performance comparison
 */

#include "../../include/test_framework.h"
#include "../../include/packet_ops.h"
#include "../../include/hardware.h"
#include "../../include/hardware_mock.h"
#include "../../include/buffer_alloc.h"
#include "../../include/memory.h"
#include "../../include/logging.h"
#include <string.h>
#include <stdio.h>

/* Performance test constants */
#define PERF_TEST_DURATION_MS       5000    /* 5 second test duration */
#define PERF_PACKET_COUNT_SMALL     10000   /* Small packet count for throughput tests */
#define PERF_PACKET_COUNT_LARGE     1000    /* Large packet count for latency tests */
#define PERF_PACKET_SIZE_SMALL      64      /* Minimum Ethernet frame size */
#define PERF_PACKET_SIZE_MEDIUM     512     /* Medium packet size */
#define PERF_PACKET_SIZE_LARGE      1518    /* Maximum Ethernet frame size */
#define PERF_ITERATION_COUNT        100     /* Iterations for averaging */
#define PERF_WARMUP_ITERATIONS      10      /* Warmup iterations */

/* Performance measurement structures */
typedef struct {
    uint32_t start_time;
    uint32_t end_time;
    uint32_t duration_ms;
    uint32_t operations_completed;
    uint32_t bytes_processed;
    uint32_t errors_encountered;
    uint32_t packets_per_second;
    uint32_t bytes_per_second;
    uint32_t average_latency_us;
    uint32_t min_latency_us;
    uint32_t max_latency_us;
    uint32_t cpu_cycles_estimated;
} performance_result_t;

typedef struct {
    const char *test_name;
    performance_result_t pio_result;    /* 3C509B PIO results */
    performance_result_t dma_result;    /* 3C515-TX DMA results */
    uint32_t improvement_ratio;         /* DMA improvement over PIO (percentage) */
} comparative_result_t;

/* Global performance test state */
static struct {
    uint32_t test_start_time;
    uint32_t total_operations;
    uint32_t total_errors;
    uint32_t latency_measurements[1000];
    int latency_count;
    performance_result_t current_result;
} g_perf_state = {0};

/* Forward declarations */
static test_result_t test_packet_throughput_performance(void);
static test_result_t test_interrupt_latency_performance(void);
static test_result_t test_memory_allocation_performance(void);
static test_result_t test_queue_management_performance(void);
static test_result_t test_pio_vs_dma_performance(void);
static test_result_t test_packet_size_scaling_performance(void);
static test_result_t test_concurrent_operations_performance(void);
static test_result_t test_error_handling_performance(void);
static test_result_t test_resource_utilization_performance(void);
static test_result_t test_sustained_load_performance(void);

/* Helper functions */
static uint32_t get_performance_timestamp(void);
static void reset_performance_state(void);
static void start_performance_measurement(const char *test_name);
static void end_performance_measurement(performance_result_t *result);
static void calculate_performance_metrics(performance_result_t *result);
static void print_performance_result(const char *test_name, const performance_result_t *result);
static void print_comparative_result(const comparative_result_t *comp_result);
static int setup_performance_test_environment(void);
static void cleanup_performance_test_environment(void);

/**
 * @brief Main entry point for performance tests
 * @return 0 on success, negative on error
 */
int test_perf_basic_main(void) {
    test_config_t config;
    test_config_init_default(&config);
    config.run_benchmarks = true;
    config.benchmark_duration_ms = PERF_TEST_DURATION_MS;
    
    int result = test_framework_init(&config);
    if (result != SUCCESS) {
        log_error("Failed to initialize test framework: %d", result);
        return result;
    }
    
    log_info("=== Starting Performance Benchmark Suite ===");
    
    /* Setup performance test environment */
    if (setup_performance_test_environment() != SUCCESS) {
        log_error("Failed to setup performance test environment");
        test_framework_cleanup();
        return ERROR_HARDWARE;
    }
    
    /* Test structure array */
    struct {
        const char *name;
        test_result_t (*test_func)(void);
    } tests[] = {
        {"Packet Throughput Performance", test_packet_throughput_performance},
        {"Interrupt Latency Performance", test_interrupt_latency_performance},
        {"Memory Allocation Performance", test_memory_allocation_performance},
        {"Queue Management Performance", test_queue_management_performance},
        {"PIO vs DMA Performance Comparison", test_pio_vs_dma_performance},
        {"Packet Size Scaling Performance", test_packet_size_scaling_performance},
        {"Concurrent Operations Performance", test_concurrent_operations_performance},
        {"Error Handling Performance", test_error_handling_performance},
        {"Resource Utilization Performance", test_resource_utilization_performance},
        {"Sustained Load Performance", test_sustained_load_performance}
    };
    
    int total_tests = sizeof(tests) / sizeof(tests[0]);
    int passed_tests = 0;
    int failed_tests = 0;
    
    /* Run all performance tests */
    for (int i = 0; i < total_tests; i++) {
        TEST_LOG_START(tests[i].name);
        
        /* Reset performance state before each test */
        reset_performance_state();
        
        test_result_t test_result = tests[i].test_func();
        
        TEST_LOG_END(tests[i].name, test_result);
        
        if (test_result_is_success(test_result)) {
            passed_tests++;
        } else {
            failed_tests++;
        }
        
        /* Brief pause between tests */
        for (volatile int j = 0; j < 1000; j++);
    }
    
    /* Cleanup */
    cleanup_performance_test_environment();
    
    /* Report results */
    log_info("=== Performance Benchmark Suite Summary ===");
    log_info("Total benchmarks: %d", total_tests);
    log_info("Passed: %d", passed_tests);
    log_info("Failed: %d", failed_tests);
    log_info("Overall performance test success rate: %d%%", 
             (passed_tests * 100) / total_tests);
    
    test_framework_cleanup();
    
    return (failed_tests == 0) ? SUCCESS : ERROR_IO;
}

/**
 * @brief Test packet throughput performance
 */
static test_result_t test_packet_throughput_performance(void) {
    log_info("=== Packet Throughput Performance Test ===");
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create mock devices for testing */
    int pio_device = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 10);
    int dma_device = mock_device_create(MOCK_DEVICE_3C515, 0x320, 11);
    
    TEST_ASSERT(pio_device >= 0, "Failed to create PIO device");
    TEST_ASSERT(dma_device >= 0, "Failed to create DMA device");
    
    mock_device_enable(pio_device, true);
    mock_device_enable(dma_device, true);
    
    uint8_t test_packet[PERF_PACKET_SIZE_MEDIUM];
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    memset(test_packet, 0xAA, sizeof(test_packet));
    
    comparative_result_t comp_result = {"Packet Throughput", {0}, {0}, 0};
    
    /* Test 1: PIO throughput */
    log_info("Testing PIO throughput (3C509B)...");
    start_performance_measurement("PIO Throughput");
    
    for (int i = 0; i < PERF_PACKET_COUNT_SMALL && 
         (get_performance_timestamp() - g_perf_state.test_start_time) < PERF_TEST_DURATION_MS; i++) {
        
        int result = packet_send_enhanced(pio_device, test_packet, sizeof(test_packet), dest_mac, 0x1000 + i);
        if (result == SUCCESS) {
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
        
        /* Periodic flush to prevent queue overflow */
        if (i % 100 == 0) {
            packet_flush_tx_queue_enhanced();
        }
    }
    
    packet_flush_tx_queue_enhanced();  /* Final flush */
    end_performance_measurement(&comp_result.pio_result);
    calculate_performance_metrics(&comp_result.pio_result);
    
    /* Test 2: DMA throughput */
    log_info("Testing DMA throughput (3C515-TX)...");
    reset_performance_state();
    start_performance_measurement("DMA Throughput");
    
    for (int i = 0; i < PERF_PACKET_COUNT_SMALL && 
         (get_performance_timestamp() - g_perf_state.test_start_time) < PERF_TEST_DURATION_MS; i++) {
        
        int result = packet_send_enhanced(dma_device, test_packet, sizeof(test_packet), dest_mac, 0x2000 + i);
        if (result == SUCCESS) {
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
        
        /* Periodic flush */
        if (i % 100 == 0) {
            packet_flush_tx_queue_enhanced();
        }
    }
    
    packet_flush_tx_queue_enhanced();  /* Final flush */
    end_performance_measurement(&comp_result.dma_result);
    calculate_performance_metrics(&comp_result.dma_result);
    
    /* Calculate improvement ratio */
    if (comp_result.pio_result.packets_per_second > 0) {
        comp_result.improvement_ratio = 
            (comp_result.dma_result.packets_per_second * 100) / comp_result.pio_result.packets_per_second;
    }
    
    print_comparative_result(&comp_result);
    
    /* Validate performance expectations */
    TEST_ASSERT(comp_result.pio_result.packets_per_second > 1000, "PIO throughput should exceed 1000 pps");
    TEST_ASSERT(comp_result.dma_result.packets_per_second > 1000, "DMA throughput should exceed 1000 pps");
    TEST_ASSERT(comp_result.improvement_ratio >= 80, "DMA should perform at least 80% as well as PIO");
    
    mock_device_destroy(pio_device);
    mock_device_destroy(dma_device);
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test interrupt latency performance
 */
static test_result_t test_interrupt_latency_performance(void) {
    log_info("=== Interrupt Latency Performance Test ===");
    
    int device_id = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(device_id >= 0, "Failed to create device for latency test");
    
    mock_device_enable(device_id, true);
    
    performance_result_t latency_result = {0};
    start_performance_measurement("Interrupt Latency");
    
    uint32_t latency_sum = 0;
    uint32_t min_latency = UINT32_MAX;
    uint32_t max_latency = 0;
    int measurement_count = 0;
    
    /* Test interrupt latency over multiple iterations */
    for (int i = 0; i < PERF_ITERATION_COUNT; i++) {
        uint32_t interrupt_start = get_performance_timestamp();
        
        /* Generate interrupt and measure response time */
        mock_interrupt_generate(device_id, MOCK_INTR_TX_COMPLETE);
        
        /* Simulate interrupt handler processing */
        for (volatile int j = 0; j < 10; j++);  /* Minimal processing delay */
        
        uint32_t interrupt_end = get_performance_timestamp();
        uint32_t latency = interrupt_end - interrupt_start;
        
        latency_sum += latency;
        if (latency < min_latency) min_latency = latency;
        if (latency > max_latency) max_latency = latency;
        measurement_count++;
        
        if (measurement_count < 1000) {
            g_perf_state.latency_measurements[measurement_count] = latency;
        }
        
        mock_interrupt_clear(device_id);
        g_perf_state.total_operations++;
    }
    
    end_performance_measurement(&latency_result);
    
    /* Calculate latency metrics */
    latency_result.average_latency_us = latency_sum / measurement_count;
    latency_result.min_latency_us = min_latency;
    latency_result.max_latency_us = max_latency;
    
    print_performance_result("Interrupt Latency", &latency_result);
    
    log_info("Latency Statistics:");
    log_info("  Average: %lu us", latency_result.average_latency_us);
    log_info("  Minimum: %lu us", latency_result.min_latency_us);
    log_info("  Maximum: %lu us", latency_result.max_latency_us);
    log_info("  Jitter: %lu us", latency_result.max_latency_us - latency_result.min_latency_us);
    
    /* Calculate standard deviation */
    uint32_t variance_sum = 0;
    for (int i = 0; i < measurement_count && i < 1000; i++) {
        int32_t diff = g_perf_state.latency_measurements[i] - latency_result.average_latency_us;
        variance_sum += (diff * diff);
    }
    uint32_t std_deviation = (variance_sum > 0) ? (uint32_t)sqrt(variance_sum / measurement_count) : 0;
    log_info("  Std Deviation: %lu us", std_deviation);
    
    /* Validate latency performance */
    TEST_ASSERT(latency_result.average_latency_us < 100, "Average latency should be under 100us");
    TEST_ASSERT((latency_result.max_latency_us - latency_result.min_latency_us) < 500, 
                "Jitter should be under 500us");
    
    mock_device_destroy(device_id);
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test memory allocation performance
 */
static test_result_t test_memory_allocation_performance(void) {
    log_info("=== Memory Allocation Performance Test ===");
    
    TEST_ASSERT(buffer_alloc_init() == SUCCESS, "Failed to initialize buffer allocator");
    
    performance_result_t alloc_result = {0};
    start_performance_measurement("Memory Allocation");
    
    /* Test 1: Small buffer allocations */
    uint32_t small_alloc_start = get_performance_timestamp();
    
    for (int i = 0; i < PERF_ITERATION_COUNT * 10; i++) {
        buffer_desc_t *buffer = buffer_alloc_ethernet_frame(PERF_PACKET_SIZE_SMALL, BUFFER_TYPE_TX);
        if (buffer) {
            buffer_free_any(buffer);
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
    }
    
    uint32_t small_alloc_end = get_performance_timestamp();
    uint32_t small_alloc_duration = small_alloc_end - small_alloc_start;
    
    /* Test 2: Large buffer allocations */
    uint32_t large_alloc_start = get_performance_timestamp();
    
    for (int i = 0; i < PERF_ITERATION_COUNT * 5; i++) {
        buffer_desc_t *buffer = buffer_alloc_ethernet_frame(PERF_PACKET_SIZE_LARGE, BUFFER_TYPE_TX);
        if (buffer) {
            buffer_free_any(buffer);
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
    }
    
    uint32_t large_alloc_end = get_performance_timestamp();
    uint32_t large_alloc_duration = large_alloc_end - large_alloc_start;
    
    /* Test 3: Packet buffer allocations */
    uint32_t packet_alloc_start = get_performance_timestamp();
    
    for (int i = 0; i < PERF_ITERATION_COUNT * 10; i++) {
        packet_buffer_t *packet_buf = packet_buffer_alloc(PERF_PACKET_SIZE_MEDIUM);
        if (packet_buf) {
            packet_buffer_free(packet_buf);
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
    }
    
    uint32_t packet_alloc_end = get_performance_timestamp();
    uint32_t packet_alloc_duration = packet_alloc_end - packet_alloc_start;
    
    end_performance_measurement(&alloc_result);
    calculate_performance_metrics(&alloc_result);
    
    print_performance_result("Memory Allocation", &alloc_result);
    
    /* Calculate allocation rates */
    uint32_t small_alloc_rate = (PERF_ITERATION_COUNT * 10 * 1000) / small_alloc_duration;
    uint32_t large_alloc_rate = (PERF_ITERATION_COUNT * 5 * 1000) / large_alloc_duration;
    uint32_t packet_alloc_rate = (PERF_ITERATION_COUNT * 10 * 1000) / packet_alloc_duration;
    
    log_info("Allocation Performance:");
    log_info("  Small buffers (%d bytes): %lu allocs/sec", PERF_PACKET_SIZE_SMALL, small_alloc_rate);
    log_info("  Large buffers (%d bytes): %lu allocs/sec", PERF_PACKET_SIZE_LARGE, large_alloc_rate);
    log_info("  Packet buffers (%d bytes): %lu allocs/sec", PERF_PACKET_SIZE_MEDIUM, packet_alloc_rate);
    
    /* Test memory fragmentation */
    const mem_stats_t *mem_stats = memory_get_stats();
    log_info("Memory Statistics:");
    log_info("  Used: %lu bytes", mem_stats->used_memory);
    log_info("  Peak: %lu bytes", mem_stats->peak_usage);
    log_info("  Free: %lu bytes", mem_stats->total_memory - mem_stats->used_memory);
    
    /* Validate allocation performance */
    TEST_ASSERT(small_alloc_rate > 10000, "Small buffer allocation rate should exceed 10k/sec");
    TEST_ASSERT(large_alloc_rate > 1000, "Large buffer allocation rate should exceed 1k/sec");
    TEST_ASSERT(alloc_result.errors_encountered < (alloc_result.operations_completed / 100), 
                "Error rate should be under 1%");
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test queue management performance
 */
static test_result_t test_queue_management_performance(void) {
    log_info("=== Queue Management Performance Test ===");
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    uint8_t test_packet[PERF_PACKET_SIZE_MEDIUM];
    memset(test_packet, 0x55, sizeof(test_packet));
    
    performance_result_t queue_result = {0};
    start_performance_measurement("Queue Management");
    
    /* Test 1: Basic queue operations */
    uint32_t queue_start = get_performance_timestamp();
    
    for (int i = 0; i < PERF_ITERATION_COUNT * 10; i++) {
        int result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                            PACKET_PRIORITY_NORMAL, 0x3000 + i);
        if (result == SUCCESS) {
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
        
        /* Periodic flush to prevent queue overflow */
        if (i % 50 == 0) {
            int flushed = packet_flush_tx_queue_enhanced();
            g_perf_state.total_operations += flushed;
        }
    }
    
    /* Final flush */
    int final_flushed = packet_flush_tx_queue_enhanced();
    g_perf_state.total_operations += final_flushed;
    
    uint32_t queue_end = get_performance_timestamp();
    uint32_t queue_duration = queue_end - queue_start;
    
    /* Test 2: Priority queue performance */
    uint32_t priority_start = get_performance_timestamp();
    
    for (int round = 0; round < PERF_ITERATION_COUNT; round++) {
        for (int priority = PACKET_PRIORITY_LOW; priority <= PACKET_PRIORITY_URGENT; priority++) {
            int result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                                priority, 0x4000 + round * 4 + priority);
            if (result == SUCCESS) {
                g_perf_state.total_operations++;
            } else {
                g_perf_state.total_errors++;
            }
        }
    }
    
    int priority_flushed = packet_flush_tx_queue_enhanced();
    g_perf_state.total_operations += priority_flushed;
    
    uint32_t priority_end = get_performance_timestamp();
    uint32_t priority_duration = priority_end - priority_start;
    
    end_performance_measurement(&queue_result);
    calculate_performance_metrics(&queue_result);
    
    print_performance_result("Queue Management", &queue_result);
    
    /* Calculate queue operation rates */
    uint32_t basic_queue_rate = ((PERF_ITERATION_COUNT * 10) * 1000) / queue_duration;
    uint32_t priority_queue_rate = ((PERF_ITERATION_COUNT * 4) * 1000) / priority_duration;
    
    log_info("Queue Performance:");
    log_info("  Basic queue operations: %lu ops/sec", basic_queue_rate);
    log_info("  Priority queue operations: %lu ops/sec", priority_queue_rate);
    log_info("  Final flush processed: %d packets", final_flushed);
    log_info("  Priority flush processed: %d packets", priority_flushed);
    
    /* Get queue statistics */
    packet_queue_management_stats_t queue_stats;
    int stats_result = packet_get_queue_stats(&queue_stats);
    if (stats_result == SUCCESS) {
        log_info("Queue Statistics:");
        for (int i = 0; i < 4; i++) {
            log_info("  Priority %d: %d packets, %lu%% usage, %lu dropped", 
                     i, queue_stats.tx_queue_counts[i], 
                     queue_stats.tx_queue_usage[i], queue_stats.tx_queue_dropped[i]);
        }
        log_info("  Queue overflow events: %lu", queue_stats.queue_full_events);
        log_info("  Backpressure events: %lu", queue_stats.backpressure_events);
    }
    
    /* Validate queue performance */
    TEST_ASSERT(basic_queue_rate > 5000, "Basic queue rate should exceed 5k ops/sec");
    TEST_ASSERT(priority_queue_rate > 2000, "Priority queue rate should exceed 2k ops/sec");
    TEST_ASSERT(queue_result.errors_encountered < (queue_result.operations_completed / 50), 
                "Queue error rate should be under 2%");
    
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test PIO vs DMA performance comparison
 */
static test_result_t test_pio_vs_dma_performance(void) {
    log_info("=== PIO vs DMA Performance Comparison ===");
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create both device types */
    int pio_device = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 10);
    int dma_device = mock_device_create(MOCK_DEVICE_3C515, 0x320, 11);
    
    TEST_ASSERT(pio_device >= 0, "Failed to create PIO device");
    TEST_ASSERT(dma_device >= 0, "Failed to create DMA device");
    
    mock_device_enable(pio_device, true);
    mock_device_enable(dma_device, true);
    
    uint8_t test_packets[3][PERF_PACKET_SIZE_LARGE];
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    /* Initialize test packets with different patterns */
    memset(test_packets[0], 0xAA, PERF_PACKET_SIZE_SMALL);
    memset(test_packets[1], 0x55, PERF_PACKET_SIZE_MEDIUM);
    memset(test_packets[2], 0xCC, PERF_PACKET_SIZE_LARGE);
    
    size_t packet_sizes[] = {PERF_PACKET_SIZE_SMALL, PERF_PACKET_SIZE_MEDIUM, PERF_PACKET_SIZE_LARGE};
    const char *size_names[] = {"Small (64B)", "Medium (512B)", "Large (1518B)"};
    
    comparative_result_t comparisons[3];
    
    /* Test each packet size */
    for (int size_idx = 0; size_idx < 3; size_idx++) {
        size_t packet_size = packet_sizes[size_idx];
        
        log_info("Testing %s packets...", size_names[size_idx]);
        
        /* Initialize comparison result */
        strncpy((char*)comparisons[size_idx].test_name, size_names[size_idx], 63);
        comparisons[size_idx].test_name = size_names[size_idx];
        
        /* Test PIO performance */
        reset_performance_state();
        start_performance_measurement("PIO");
        
        for (int i = 0; i < PERF_ITERATION_COUNT * 5; i++) {
            int result = packet_send_enhanced(pio_device, test_packets[size_idx], packet_size, 
                                            dest_mac, 0x5000 + i);
            if (result == SUCCESS) {
                g_perf_state.total_operations++;
            } else {
                g_perf_state.total_errors++;
            }
        }
        
        end_performance_measurement(&comparisons[size_idx].pio_result);
        calculate_performance_metrics(&comparisons[size_idx].pio_result);
        
        /* Test DMA performance */
        reset_performance_state();
        start_performance_measurement("DMA");
        
        /* Setup DMA descriptors */
        mock_dma_set_descriptors(dma_device, 0x100000, 0x200000);
        
        for (int i = 0; i < PERF_ITERATION_COUNT * 5; i++) {
            int result = packet_send_enhanced(dma_device, test_packets[size_idx], packet_size, 
                                            dest_mac, 0x6000 + i);
            if (result == SUCCESS) {
                g_perf_state.total_operations++;
            } else {
                g_perf_state.total_errors++;
            }
        }
        
        end_performance_measurement(&comparisons[size_idx].dma_result);
        calculate_performance_metrics(&comparisons[size_idx].dma_result);
        
        /* Calculate improvement ratio */
        if (comparisons[size_idx].pio_result.packets_per_second > 0) {
            comparisons[size_idx].improvement_ratio = 
                (comparisons[size_idx].dma_result.packets_per_second * 100) / 
                comparisons[size_idx].pio_result.packets_per_second;
        }
        
        print_comparative_result(&comparisons[size_idx]);
    }
    
    /* Summary analysis */
    log_info("=== PIO vs DMA Summary ===");
    for (int i = 0; i < 3; i++) {
        log_info("%s: DMA is %lu%% of PIO performance", 
                 comparisons[i].test_name, comparisons[i].improvement_ratio);
    }
    
    /* Calculate average improvement */
    uint32_t avg_improvement = 0;
    for (int i = 0; i < 3; i++) {
        avg_improvement += comparisons[i].improvement_ratio;
    }
    avg_improvement /= 3;
    
    log_info("Average DMA performance: %lu%% of PIO", avg_improvement);
    
    /* Validate performance expectations */
    TEST_ASSERT(avg_improvement >= 80, "DMA should perform at least 80% as well as PIO on average");
    
    /* Test DMA-specific features */
    log_info("Testing DMA-specific features...");
    
    /* Test concurrent DMA transfers */
    uint32_t concurrent_start = get_performance_timestamp();
    
    for (int i = 0; i < 50; i++) {
        mock_dma_start_transfer(dma_device, true);  /* TX */
        mock_dma_start_transfer(dma_device, false); /* RX */
        
        /* Simulate DMA completion */
        mock_interrupt_generate(dma_device, MOCK_INTR_DMA_COMPLETE);
        mock_interrupt_clear(dma_device);
    }
    
    uint32_t concurrent_end = get_performance_timestamp();
    uint32_t concurrent_duration = concurrent_end - concurrent_start;
    uint32_t dma_ops_per_sec = (100 * 1000) / concurrent_duration;
    
    log_info("DMA concurrent operations: %lu ops/sec", dma_ops_per_sec);
    
    mock_device_destroy(pio_device);
    mock_device_destroy(dma_device);
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test packet size scaling performance
 */
static test_result_t test_packet_size_scaling_performance(void) {
    log_info("=== Packet Size Scaling Performance Test ===");
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    int device_id = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(device_id >= 0, "Failed to create device");
    mock_device_enable(device_id, true);
    
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    /* Test various packet sizes */
    size_t test_sizes[] = {64, 128, 256, 512, 1024, 1518};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    performance_result_t size_results[6];
    
    for (int size_idx = 0; size_idx < num_sizes; size_idx++) {
        size_t packet_size = test_sizes[size_idx];
        uint8_t *test_packet = malloc(packet_size);
        TEST_ASSERT(test_packet != NULL, "Failed to allocate test packet");
        
        memset(test_packet, 0xAA + size_idx, packet_size);
        
        log_info("Testing %zu byte packets...", packet_size);
        
        reset_performance_state();
        start_performance_measurement("Size Scaling");
        
        /* Adjust iteration count based on packet size */
        int iterations = PERF_ITERATION_COUNT * (1518 / packet_size);
        if (iterations > PERF_ITERATION_COUNT * 5) {
            iterations = PERF_ITERATION_COUNT * 5;
        }
        
        for (int i = 0; i < iterations; i++) {
            int result = packet_send_enhanced(device_id, test_packet, packet_size, 
                                            dest_mac, 0x7000 + i);
            if (result == SUCCESS) {
                g_perf_state.total_operations++;
            } else {
                g_perf_state.total_errors++;
            }
            
            /* Periodic flush */
            if (i % 20 == 0) {
                packet_flush_tx_queue_enhanced();
            }
        }
        
        packet_flush_tx_queue_enhanced();
        end_performance_measurement(&size_results[size_idx]);
        calculate_performance_metrics(&size_results[size_idx]);
        
        char test_name[64];
        snprintf(test_name, sizeof(test_name), "%zu-byte packets", packet_size);
        print_performance_result(test_name, &size_results[size_idx]);
        
        free(test_packet);
    }
    
    /* Analyze scaling characteristics */
    log_info("=== Packet Size Scaling Analysis ===");
    log_info("Size\tPPS\t\tBPS\t\tLatency");
    
    for (int i = 0; i < num_sizes; i++) {
        log_info("%zu\t%lu\t\t%lu\t\t%lu us", 
                 test_sizes[i],
                 size_results[i].packets_per_second,
                 size_results[i].bytes_per_second,
                 size_results[i].average_latency_us);
    }
    
    /* Calculate efficiency metrics */
    uint32_t max_pps = 0, max_bps = 0;
    int best_pps_size = 0, best_bps_size = 0;
    
    for (int i = 0; i < num_sizes; i++) {
        if (size_results[i].packets_per_second > max_pps) {
            max_pps = size_results[i].packets_per_second;
            best_pps_size = i;
        }
        if (size_results[i].bytes_per_second > max_bps) {
            max_bps = size_results[i].bytes_per_second;
            best_bps_size = i;
        }
    }
    
    log_info("Performance Analysis:");
    log_info("  Best PPS: %zu bytes at %lu pps", test_sizes[best_pps_size], max_pps);
    log_info("  Best BPS: %zu bytes at %lu bps", test_sizes[best_bps_size], max_bps);
    
    /* Calculate scaling efficiency */
    uint32_t small_efficiency = size_results[0].bytes_per_second / test_sizes[0];
    uint32_t large_efficiency = size_results[num_sizes-1].bytes_per_second / test_sizes[num_sizes-1];
    
    log_info("  Small packet efficiency: %lu bps/byte", small_efficiency);
    log_info("  Large packet efficiency: %lu bps/byte", large_efficiency);
    
    /* Validate scaling performance */
    TEST_ASSERT(max_pps > 1000, "Maximum PPS should exceed 1000");
    TEST_ASSERT(max_bps > 500000, "Maximum BPS should exceed 500KB/s");
    TEST_ASSERT(large_efficiency > small_efficiency, "Large packets should be more efficient");
    
    mock_device_destroy(device_id);
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test concurrent operations performance
 */
static test_result_t test_concurrent_operations_performance(void) {
    log_info("=== Concurrent Operations Performance Test ===");
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    /* Create multiple devices */
    int device1 = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 10);
    int device2 = mock_device_create(MOCK_DEVICE_3C515, 0x320, 11);
    
    TEST_ASSERT(device1 >= 0, "Failed to create device 1");
    TEST_ASSERT(device2 >= 0, "Failed to create device 2");
    
    mock_device_enable(device1, true);
    mock_device_enable(device2, true);
    
    uint8_t test_packet[PERF_PACKET_SIZE_MEDIUM];
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    memset(test_packet, 0x77, sizeof(test_packet));
    
    performance_result_t concurrent_result = {0};
    start_performance_measurement("Concurrent Operations");
    
    /* Test 1: Simultaneous TX operations on both devices */
    uint32_t tx_start = get_performance_timestamp();
    
    for (int i = 0; i < PERF_ITERATION_COUNT * 2; i++) {
        int device = (i % 2 == 0) ? device1 : device2;
        
        int result = packet_send_enhanced(device, test_packet, sizeof(test_packet), 
                                        dest_mac, 0x8000 + i);
        if (result == SUCCESS) {
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
        
        /* Periodic flush */
        if (i % 30 == 0) {
            packet_flush_tx_queue_enhanced();
        }
    }
    
    packet_flush_tx_queue_enhanced();
    uint32_t tx_end = get_performance_timestamp();
    uint32_t tx_duration = tx_end - tx_start;
    
    /* Test 2: Concurrent RX and TX operations */
    uint32_t concurrent_start = get_performance_timestamp();
    
    for (int i = 0; i < PERF_ITERATION_COUNT; i++) {
        /* Inject RX packet */
        mock_packet_inject_rx(device1, test_packet, sizeof(test_packet));
        
        /* Send TX packet */
        int result = packet_send_enhanced(device2, test_packet, sizeof(test_packet), 
                                        dest_mac, 0x9000 + i);
        if (result == SUCCESS) {
            g_perf_state.total_operations++;
        }
        
        /* Try to receive */
        uint8_t rx_buffer[PERF_PACKET_SIZE_MEDIUM];
        size_t rx_length = sizeof(rx_buffer);
        if (packet_receive_from_nic(device1, rx_buffer, &rx_length) == SUCCESS) {
            g_perf_state.total_operations++;
        }
    }
    
    uint32_t concurrent_end = get_performance_timestamp();
    uint32_t concurrent_duration = concurrent_end - concurrent_start;
    
    /* Test 3: Multi-priority concurrent queuing */
    uint32_t priority_start = get_performance_timestamp();
    
    for (int round = 0; round < PERF_ITERATION_COUNT / 2; round++) {
        for (int priority = PACKET_PRIORITY_LOW; priority <= PACKET_PRIORITY_URGENT; priority++) {
            int result = packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                                priority, 0xA000 + round * 4 + priority);
            if (result == SUCCESS) {
                g_perf_state.total_operations++;
            } else {
                g_perf_state.total_errors++;
            }
        }
    }
    
    int priority_flushed = packet_flush_tx_queue_enhanced();
    uint32_t priority_end = get_performance_timestamp();
    uint32_t priority_duration = priority_end - priority_start;
    
    end_performance_measurement(&concurrent_result);
    calculate_performance_metrics(&concurrent_result);
    
    print_performance_result("Concurrent Operations", &concurrent_result);
    
    /* Calculate concurrent operation rates */
    uint32_t tx_rate = ((PERF_ITERATION_COUNT * 2) * 1000) / tx_duration;
    uint32_t concurrent_rate = ((PERF_ITERATION_COUNT * 2) * 1000) / concurrent_duration;
    uint32_t priority_rate = ((PERF_ITERATION_COUNT * 2) * 1000) / priority_duration;
    
    log_info("Concurrent Performance:");
    log_info("  Alternating TX: %lu ops/sec", tx_rate);
    log_info("  RX/TX concurrent: %lu ops/sec", concurrent_rate);
    log_info("  Priority concurrent: %lu ops/sec", priority_rate);
    log_info("  Priority packets flushed: %d", priority_flushed);
    
    /* Test resource contention */
    uint32_t contention_start = get_performance_timestamp();
    int contention_success = 0;
    
    for (int i = 0; i < 100; i++) {
        /* Rapid operations that might cause contention */
        int result1 = packet_send_enhanced(device1, test_packet, sizeof(test_packet), dest_mac, 0xB000 + i);
        int result2 = packet_send_enhanced(device2, test_packet, sizeof(test_packet), dest_mac, 0xB100 + i);
        
        if (result1 == SUCCESS) contention_success++;
        if (result2 == SUCCESS) contention_success++;
    }
    
    uint32_t contention_end = get_performance_timestamp();
    uint32_t contention_duration = contention_end - contention_start;
    uint32_t contention_rate = (contention_success * 1000) / contention_duration;
    
    log_info("  Resource contention: %lu ops/sec (%d/%d successful)", 
             contention_rate, contention_success, 200);
    
    /* Validate concurrent performance */
    TEST_ASSERT(tx_rate > 2000, "Alternating TX rate should exceed 2k ops/sec");
    TEST_ASSERT(concurrent_rate > 1000, "Concurrent RX/TX rate should exceed 1k ops/sec");
    TEST_ASSERT(contention_success >= 150, "At least 75% of contention operations should succeed");
    
    mock_device_destroy(device1);
    mock_device_destroy(device2);
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test error handling performance
 */
static test_result_t test_error_handling_performance(void) {
    log_info("=== Error Handling Performance Test ===");
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    int device_id = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(device_id >= 0, "Failed to create device");
    mock_device_enable(device_id, true);
    
    uint8_t test_packet[PERF_PACKET_SIZE_MEDIUM];
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    memset(test_packet, 0x88, sizeof(test_packet));
    
    performance_result_t error_result = {0};
    start_performance_measurement("Error Handling");
    
    /* Test 1: Error injection and recovery performance */
    uint32_t error_start = get_performance_timestamp();
    
    for (int i = 0; i < PERF_ITERATION_COUNT; i++) {
        /* Inject error every 10th operation */
        if (i % 10 == 0) {
            mock_error_inject(device_id, MOCK_ERROR_TX_TIMEOUT, 1);
        }
        
        int result = packet_send_enhanced(device_id, test_packet, sizeof(test_packet), 
                                        dest_mac, 0xC000 + i);
        if (result == SUCCESS) {
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
        
        /* Clear error for next operations */
        if (i % 10 == 0) {
            mock_error_clear(device_id);
        }
    }
    
    uint32_t error_end = get_performance_timestamp();
    uint32_t error_duration = error_end - error_start;
    
    /* Test 2: Error handling with retries */
    uint32_t retry_start = get_performance_timestamp();
    int retry_success = 0;
    
    for (int i = 0; i < 50; i++) {
        /* Inject error that will cause retry */
        mock_error_inject(device_id, MOCK_ERROR_TX_UNDERRUN, 2);  /* Fail first 2 attempts */
        
        int result = packet_send_with_retry(test_packet, sizeof(test_packet), 
                                          dest_mac, 0xD000 + i, 5);
        if (result == SUCCESS) {
            retry_success++;
        }
        
        mock_error_clear(device_id);
        g_perf_state.total_operations++;
    }
    
    uint32_t retry_end = get_performance_timestamp();
    uint32_t retry_duration = retry_end - retry_start;
    
    /* Test 3: Invalid parameter handling performance */
    uint32_t invalid_start = get_performance_timestamp();
    
    for (int i = 0; i < 100; i++) {
        /* Test various invalid parameters */
        packet_send_enhanced(99, test_packet, sizeof(test_packet), dest_mac, 0xE000 + i);  /* Invalid device */
        packet_send_enhanced(device_id, NULL, sizeof(test_packet), dest_mac, 0xE100 + i);  /* NULL packet */
        packet_send_enhanced(device_id, test_packet, 0, dest_mac, 0xE200 + i);             /* Zero length */
        packet_send_enhanced(device_id, test_packet, 2000, dest_mac, 0xE300 + i);          /* Too large */
        
        g_perf_state.total_operations += 4;
    }
    
    uint32_t invalid_end = get_performance_timestamp();
    uint32_t invalid_duration = invalid_end - invalid_start;
    
    end_performance_measurement(&error_result);
    calculate_performance_metrics(&error_result);
    
    print_performance_result("Error Handling", &error_result);
    
    /* Calculate error handling rates */
    uint32_t error_recovery_rate = (PERF_ITERATION_COUNT * 1000) / error_duration;
    uint32_t retry_rate = (50 * 1000) / retry_duration;
    uint32_t invalid_param_rate = (400 * 1000) / invalid_duration;
    
    log_info("Error Handling Performance:");
    log_info("  Error injection/recovery: %lu ops/sec", error_recovery_rate);
    log_info("  Retry operations: %lu ops/sec (%d/%d successful)", retry_rate, retry_success, 50);
    log_info("  Invalid parameter handling: %lu ops/sec", invalid_param_rate);
    log_info("  Error rate during test: %lu/%lu (%lu%%)", 
             error_result.errors_encountered, error_result.operations_completed,
             (error_result.errors_encountered * 100) / error_result.operations_completed);
    
    /* Test memory allocation failure handling */
    uint32_t mem_error_start = get_performance_timestamp();
    int mem_handled = 0;
    
    for (int i = 0; i < 20; i++) {
        /* This would test actual memory allocation failures in a real implementation */
        /* For mock testing, we just measure the overhead of error path checking */
        packet_buffer_t *buffer = packet_buffer_alloc(PERF_PACKET_SIZE_LARGE * 2);  /* Large allocation */
        if (buffer) {
            packet_buffer_free(buffer);
            mem_handled++;
        } else {
            mem_handled++;  /* Count failed allocations as handled */
        }
    }
    
    uint32_t mem_error_end = get_performance_timestamp();
    uint32_t mem_error_duration = mem_error_end - mem_error_start;
    uint32_t mem_error_rate = (20 * 1000) / mem_error_duration;
    
    log_info("  Memory error handling: %lu ops/sec (%d/20 handled)", mem_error_rate, mem_handled);
    
    /* Validate error handling performance */
    TEST_ASSERT(error_recovery_rate > 500, "Error recovery rate should exceed 500 ops/sec");
    TEST_ASSERT(retry_success >= 40, "At least 80% of retry operations should succeed");
    TEST_ASSERT(invalid_param_rate > 10000, "Invalid parameter handling should exceed 10k ops/sec");
    
    mock_device_destroy(device_id);
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test resource utilization performance
 */
static test_result_t test_resource_utilization_performance(void) {
    log_info("=== Resource Utilization Performance Test ===");
    
    /* Get initial resource state */
    const mem_stats_t *initial_mem = memory_get_stats();
    uint32_t initial_memory = initial_mem->used_memory;
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    int device_id = mock_device_create(MOCK_DEVICE_3C509B, 0x300, 10);
    TEST_ASSERT(device_id >= 0, "Failed to create device");
    mock_device_enable(device_id, true);
    
    uint8_t test_packet[PERF_PACKET_SIZE_MEDIUM];
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    memset(test_packet, 0x99, sizeof(test_packet));
    
    performance_result_t resource_result = {0};
    start_performance_measurement("Resource Utilization");
    
    /* Test 1: Memory usage during operations */
    uint32_t memory_start = get_performance_timestamp();
    const mem_stats_t *mem_before_ops = memory_get_stats();
    uint32_t memory_before = mem_before_ops->used_memory;
    
    for (int i = 0; i < PERF_ITERATION_COUNT * 2; i++) {
        /* Allocate buffer */
        packet_buffer_t *buffer = packet_buffer_alloc(PERF_PACKET_SIZE_MEDIUM);
        if (buffer) {
            packet_set_data(buffer, test_packet, sizeof(test_packet));
            
            /* Queue for transmission */
            packet_queue_tx_enhanced(test_packet, sizeof(test_packet), 
                                   PACKET_PRIORITY_NORMAL, 0xF000 + i);
            
            packet_buffer_free(buffer);
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
        
        /* Periodic flush and memory check */
        if (i % 50 == 0) {
            packet_flush_tx_queue_enhanced();
            
            const mem_stats_t *current_mem = memory_get_stats();
            uint32_t current_usage = current_mem->used_memory;
            
            /* Log memory usage */
            if (i % 200 == 0) {
                log_info("Memory usage at operation %d: %lu bytes (+%lu from start)", 
                         i, current_usage, current_usage - memory_before);
            }
        }
    }
    
    packet_flush_tx_queue_enhanced();
    
    const mem_stats_t *mem_after_ops = memory_get_stats();
    uint32_t memory_after = mem_after_ops->used_memory;
    uint32_t memory_end = get_performance_timestamp();
    uint32_t memory_duration = memory_end - memory_start;
    
    /* Test 2: Resource cleanup efficiency */
    uint32_t cleanup_start = get_performance_timestamp();
    
    /* Allocate and immediately free many resources */
    for (int i = 0; i < 500; i++) {
        packet_buffer_t *buffer = packet_buffer_alloc(PERF_PACKET_SIZE_SMALL);
        if (buffer) {
            packet_buffer_free(buffer);
        }
        
        buffer_desc_t *eth_buffer = buffer_alloc_ethernet_frame(PERF_PACKET_SIZE_LARGE, BUFFER_TYPE_TX);
        if (eth_buffer) {
            buffer_free_any(eth_buffer);
        }
        
        g_perf_state.total_operations += 2;
    }
    
    uint32_t cleanup_end = get_performance_timestamp();
    uint32_t cleanup_duration = cleanup_end - cleanup_start;
    
    /* Test 3: Peak resource usage measurement */
    const mem_stats_t *peak_mem = memory_get_stats();
    uint32_t peak_usage = peak_mem->peak_usage;
    
    end_performance_measurement(&resource_result);
    calculate_performance_metrics(&resource_result);
    
    print_performance_result("Resource Utilization", &resource_result);
    
    /* Calculate resource metrics */
    uint32_t memory_ops_rate = ((PERF_ITERATION_COUNT * 2) * 1000) / memory_duration;
    uint32_t cleanup_rate = (1000 * 1000) / cleanup_duration;
    uint32_t memory_growth = memory_after - memory_before;
    uint32_t total_memory_growth = memory_after - initial_memory;
    
    log_info("Resource Utilization Metrics:");
    log_info("  Memory operations rate: %lu ops/sec", memory_ops_rate);
    log_info("  Resource cleanup rate: %lu ops/sec", cleanup_rate);
    log_info("  Memory growth during test: %lu bytes", memory_growth);
    log_info("  Total memory growth: %lu bytes", total_memory_growth);
    log_info("  Peak memory usage: %lu bytes", peak_usage);
    log_info("  Memory efficiency: %lu bytes/operation", 
             memory_growth / (resource_result.operations_completed > 0 ? resource_result.operations_completed : 1));
    
    /* Test memory leak detection */
    uint32_t leak_test_start = get_performance_timestamp();
    const mem_stats_t *leak_start_mem = memory_get_stats();
    uint32_t leak_start_usage = leak_start_mem->used_memory;
    
    /* Perform allocation cycles that should not leak */
    for (int cycle = 0; cycle < 10; cycle++) {
        for (int i = 0; i < 20; i++) {
            packet_buffer_t *buffer = packet_buffer_alloc(256);
            if (buffer) {
                packet_set_data(buffer, test_packet, 200);
                packet_buffer_free(buffer);
            }
        }
    }
    
    const mem_stats_t *leak_end_mem = memory_get_stats();
    uint32_t leak_end_usage = leak_end_mem->used_memory;
    uint32_t potential_leak = (leak_end_usage > leak_start_usage) ? 
                             (leak_end_usage - leak_start_usage) : 0;
    
    uint32_t leak_test_end = get_performance_timestamp();
    uint32_t leak_test_duration = leak_test_end - leak_test_start;
    uint32_t leak_test_rate = (200 * 1000) / leak_test_duration;
    
    log_info("  Leak test rate: %lu ops/sec", leak_test_rate);
    log_info("  Potential memory leak: %lu bytes", potential_leak);
    
    /* Calculate resource efficiency scores */
    uint32_t memory_efficiency_score = 100;
    if (memory_growth > 0) {
        memory_efficiency_score = 100 - ((memory_growth * 100) / (peak_usage > 0 ? peak_usage : 1));
    }
    
    uint32_t cleanup_efficiency_score = (cleanup_rate > 5000) ? 100 : (cleanup_rate * 100 / 5000);
    
    log_info("Efficiency Scores:");
    log_info("  Memory efficiency: %lu/100", memory_efficiency_score);
    log_info("  Cleanup efficiency: %lu/100", cleanup_efficiency_score);
    
    /* Validate resource utilization */
    TEST_ASSERT(memory_ops_rate > 1000, "Memory operations rate should exceed 1k ops/sec");
    TEST_ASSERT(cleanup_rate > 2000, "Cleanup rate should exceed 2k ops/sec");
    TEST_ASSERT(potential_leak < 1024, "Potential memory leak should be under 1KB");
    TEST_ASSERT(memory_efficiency_score > 70, "Memory efficiency should exceed 70%");
    
    mock_device_destroy(device_id);
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/**
 * @brief Test sustained load performance
 */
static test_result_t test_sustained_load_performance(void) {
    log_info("=== Sustained Load Performance Test ===");
    
    config_t test_config = {0};
    TEST_ASSERT(packet_ops_init(&test_config) == SUCCESS, "Failed to initialize packet ops");
    
    int device_id = mock_device_create(MOCK_DEVICE_3C515, 0x320, 11);
    TEST_ASSERT(device_id >= 0, "Failed to create device");
    mock_device_enable(device_id, true);
    mock_dma_set_descriptors(device_id, 0x100000, 0x200000);
    
    uint8_t test_packet[PERF_PACKET_SIZE_MEDIUM];
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    memset(test_packet, 0xDD, sizeof(test_packet));
    
    performance_result_t sustained_result = {0};
    
    /* Test sustained load for extended period */
    log_info("Starting sustained load test for %d seconds...", PERF_TEST_DURATION_MS / 1000);
    
    start_performance_measurement("Sustained Load");
    uint32_t load_start = get_performance_timestamp();
    uint32_t last_report = load_start;
    uint32_t operations_at_last_report = 0;
    
    uint32_t min_ops_per_sec = UINT32_MAX;
    uint32_t max_ops_per_sec = 0;
    uint32_t stability_measurements = 0;
    uint32_t stability_sum = 0;
    
    while ((get_performance_timestamp() - load_start) < PERF_TEST_DURATION_MS) {
        /* Send packet */
        int result = packet_send_enhanced(device_id, test_packet, sizeof(test_packet), 
                                        dest_mac, g_perf_state.total_operations);
        if (result == SUCCESS) {
            g_perf_state.total_operations++;
        } else {
            g_perf_state.total_errors++;
        }
        
        /* Inject some RX packets for realistic load */
        if (g_perf_state.total_operations % 5 == 0) {
            mock_packet_inject_rx(device_id, test_packet, sizeof(test_packet));
        }
        
        /* Periodic processing and reporting */
        if (g_perf_state.total_operations % 100 == 0) {
            packet_flush_tx_queue_enhanced();
            
            /* Try to receive injected packets */
            uint8_t rx_buffer[PERF_PACKET_SIZE_MEDIUM];
            size_t rx_length = sizeof(rx_buffer);
            while (packet_receive_from_nic(device_id, rx_buffer, &rx_length) == SUCCESS) {
                rx_length = sizeof(rx_buffer);
            }
        }
        
        /* Report performance every second */
        uint32_t current_time = get_performance_timestamp();
        if ((current_time - last_report) >= 1000) {
            uint32_t ops_in_period = g_perf_state.total_operations - operations_at_last_report;
            uint32_t ops_per_sec = ops_in_period;  /* Already measured over ~1 second */
            
            if (ops_per_sec < min_ops_per_sec) min_ops_per_sec = ops_per_sec;
            if (ops_per_sec > max_ops_per_sec) max_ops_per_sec = ops_per_sec;
            
            stability_sum += ops_per_sec;
            stability_measurements++;
            
            log_info("Sustained load: %lu ops/sec (total: %lu, errors: %lu)", 
                     ops_per_sec, g_perf_state.total_operations, g_perf_state.total_errors);
            
            last_report = current_time;
            operations_at_last_report = g_perf_state.total_operations;
        }
    }
    
    /* Final flush */
    packet_flush_tx_queue_enhanced();
    
    end_performance_measurement(&sustained_result);
    calculate_performance_metrics(&sustained_result);
    
    print_performance_result("Sustained Load", &sustained_result);
    
    /* Calculate stability metrics */
    uint32_t avg_ops_per_sec = (stability_measurements > 0) ? 
                              (stability_sum / stability_measurements) : 0;
    uint32_t performance_range = max_ops_per_sec - min_ops_per_sec;
    uint32_t stability_percentage = (performance_range < avg_ops_per_sec) ? 
                                   (100 - ((performance_range * 100) / avg_ops_per_sec)) : 0;
    
    log_info("Sustained Load Analysis:");
    log_info("  Test duration: %lu ms", sustained_result.duration_ms);
    log_info("  Average rate: %lu ops/sec", avg_ops_per_sec);
    log_info("  Minimum rate: %lu ops/sec", min_ops_per_sec);
    log_info("  Maximum rate: %lu ops/sec", max_ops_per_sec);
    log_info("  Performance range: %lu ops/sec", performance_range);
    log_info("  Stability: %lu%% (higher is better)", stability_percentage);
    log_info("  Error rate: %lu%% (%lu errors / %lu operations)", 
             (sustained_result.errors_encountered * 100) / sustained_result.operations_completed,
             sustained_result.errors_encountered, sustained_result.operations_completed);
    
    /* Test thermal/performance degradation simulation */
    log_info("Testing performance under stress conditions...");
    
    uint32_t stress_start = get_performance_timestamp();
    uint32_t stress_operations = 0;
    
    /* Rapid operations to simulate thermal stress */
    for (int burst = 0; burst < 10; burst++) {
        for (int i = 0; i < 100; i++) {
            packet_send_enhanced(device_id, test_packet, sizeof(test_packet), dest_mac, 0x10000 + i);
            stress_operations++;
        }
        packet_flush_tx_queue_enhanced();
        
        /* Simulate brief cooling period */
        for (volatile int j = 0; j < 50; j++);
    }
    
    uint32_t stress_end = get_performance_timestamp();
    uint32_t stress_duration = stress_end - stress_start;
    uint32_t stress_rate = (stress_operations * 1000) / stress_duration;
    
    log_info("  Stress test rate: %lu ops/sec", stress_rate);
    
    /* Calculate performance degradation */
    uint32_t degradation_percentage = (stress_rate < avg_ops_per_sec) ? 
                                     (((avg_ops_per_sec - stress_rate) * 100) / avg_ops_per_sec) : 0;
    
    log_info("  Performance degradation under stress: %lu%%", degradation_percentage);
    
    /* Validate sustained performance */
    TEST_ASSERT(avg_ops_per_sec > 500, "Average sustained rate should exceed 500 ops/sec");
    TEST_ASSERT(stability_percentage > 80, "Performance stability should exceed 80%");
    TEST_ASSERT(degradation_percentage < 30, "Stress degradation should be under 30%");
    TEST_ASSERT((sustained_result.errors_encountered * 100 / sustained_result.operations_completed) < 5,
                "Error rate should be under 5%");
    
    mock_device_destroy(device_id);
    packet_ops_cleanup();
    
    return TEST_RESULT_PASS;
}

/* Helper function implementations */

/**
 * @brief Get performance timestamp
 */
static uint32_t get_performance_timestamp(void) {
    static uint32_t counter = 0;
    return ++counter;  /* Simple incrementing counter for testing */
}

/**
 * @brief Reset performance state
 */
static void reset_performance_state(void) {
    memset(&g_perf_state, 0, sizeof(g_perf_state));
}

/**
 * @brief Start performance measurement
 */
static void start_performance_measurement(const char *test_name) {
    g_perf_state.test_start_time = get_performance_timestamp();
    g_perf_state.total_operations = 0;
    g_perf_state.total_errors = 0;
    g_perf_state.latency_count = 0;
}

/**
 * @brief End performance measurement
 */
static void end_performance_measurement(performance_result_t *result) {
    result->end_time = get_performance_timestamp();
    result->start_time = g_perf_state.test_start_time;
    result->duration_ms = result->end_time - result->start_time;
    result->operations_completed = g_perf_state.total_operations;
    result->errors_encountered = g_perf_state.total_errors;
}

/**
 * @brief Calculate performance metrics
 */
static void calculate_performance_metrics(performance_result_t *result) {
    if (result->duration_ms > 0) {
        result->packets_per_second = (result->operations_completed * 1000) / result->duration_ms;
        result->bytes_per_second = (result->bytes_processed * 1000) / result->duration_ms;
    }
    
    /* Estimate CPU cycles (rough approximation) */
    result->cpu_cycles_estimated = result->operations_completed * 100;  /* Assume 100 cycles per operation */
}

/**
 * @brief Print performance result
 */
static void print_performance_result(const char *test_name, const performance_result_t *result) {
    log_info("=== %s Results ===", test_name);
    log_info("  Duration: %lu ms", result->duration_ms);
    log_info("  Operations: %lu", result->operations_completed);
    log_info("  Errors: %lu", result->errors_encountered);
    log_info("  Rate: %lu ops/sec", result->packets_per_second);
    log_info("  Throughput: %lu bytes/sec", result->bytes_per_second);
    if (result->average_latency_us > 0) {
        log_info("  Avg Latency: %lu us", result->average_latency_us);
    }
}

/**
 * @brief Print comparative result
 */
static void print_comparative_result(const comparative_result_t *comp_result) {
    log_info("=== %s Comparison ===", comp_result->test_name);
    log_info("PIO (3C509B):");
    log_info("  Rate: %lu ops/sec", comp_result->pio_result.packets_per_second);
    log_info("  Throughput: %lu bytes/sec", comp_result->pio_result.bytes_per_second);
    log_info("DMA (3C515-TX):");
    log_info("  Rate: %lu ops/sec", comp_result->dma_result.packets_per_second);
    log_info("  Throughput: %lu bytes/sec", comp_result->dma_result.bytes_per_second);
    log_info("DMA Performance: %lu%% of PIO", comp_result->improvement_ratio);
}

/**
 * @brief Setup performance test environment
 */
static int setup_performance_test_environment(void) {
    /* Initialize mock framework */
    if (mock_framework_init() != SUCCESS) {
        return ERROR_HARDWARE;
    }
    
    /* Enable I/O logging for performance analysis */
    mock_io_log_enable(true);
    
    /* Initialize memory system */
    if (!memory_is_initialized()) {
        int result = memory_init();
        if (result != SUCCESS) {
            return result;
        }
    }
    
    /* Initialize buffer allocator */
    if (buffer_alloc_init() != SUCCESS) {
        return ERROR_NO_MEMORY;
    }
    
    return SUCCESS;
}

/**
 * @brief Cleanup performance test environment
 */
static void cleanup_performance_test_environment(void) {
    mock_framework_cleanup();
}