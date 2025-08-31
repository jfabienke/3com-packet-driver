/**
 * @file runner_performance.c
 * @brief Performance Test Runner - Throughput, latency, and benchmarking
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test runner executes performance tests including:
 * - Throughput benchmarks (PPS, bandwidth)
 * - Latency measurements (min, max, average)
 * - Performance regression testing
 * - Comparative analysis (3C509B vs 3C515-TX)
 * - Memory performance impact
 * - CPU utilization analysis
 */

#include "../common/test_framework.h"
#include "../common/hardware_mock.h"
#include "../performance/perf_framework.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <stdio.h>
#include <string.h>

/* External performance test functions */
extern int test_perf_basic_main(void);
extern int test_perf_latency_main(void);
extern int test_perf_throughput_main(void);
extern int perf_regression_main(void);

/* Performance test configuration */
typedef struct {
    bool run_throughput_tests;
    bool run_latency_tests;
    bool run_regression_tests;
    bool run_comparative_tests;
    bool run_memory_impact_tests;
    bool run_cpu_utilization_tests;
    bool verbose_output;
    bool detailed_analysis;
    uint32_t test_duration_ms;
    uint32_t warmup_duration_ms;
    int packet_sizes[8];
    int num_packet_sizes;
} performance_test_config_t;

/* Performance test statistics */
typedef struct {
    int total_benchmarks_run;
    int benchmarks_passed;
    int benchmarks_failed;
    uint32_t total_duration_ms;
    uint32_t total_packets_tested;
    uint64_t total_bytes_tested;
    double best_throughput_pps;
    double worst_latency_us;
    const char *best_test_name;
    const char *worst_test_name;
} performance_test_stats_t;

/* Performance benchmark definition */
typedef struct {
    const char *name;
    const char *description;
    int (*benchmark_main)(void);
    bool *enabled_flag;
    bool is_baseline;
    uint32_t expected_min_pps;
    uint32_t expected_max_latency_us;
} performance_benchmark_t;

static performance_test_config_t g_perf_config = {
    .run_throughput_tests = true,
    .run_latency_tests = true,
    .run_regression_tests = true,
    .run_comparative_tests = true,
    .run_memory_impact_tests = true,
    .run_cpu_utilization_tests = true,
    .verbose_output = false,
    .detailed_analysis = false,
    .test_duration_ms = 10000,    /* 10 seconds default */
    .warmup_duration_ms = 2000,   /* 2 seconds warmup */
    .packet_sizes = {64, 128, 256, 512, 1024, 1500},
    .num_packet_sizes = 6
};

static performance_test_stats_t g_perf_stats = {0};

/* Forward declarations for performance test functions */
static int test_throughput_benchmarks(void);
static int test_latency_benchmarks(void);
static int test_comparative_analysis(void);
static int test_memory_impact_analysis(void);
static int test_cpu_utilization_analysis(void);

/**
 * @brief Parse command line arguments for performance test configuration
 */
static int parse_performance_test_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_perf_config.verbose_output = true;
        } else if (strcmp(argv[i], "--detailed") == 0) {
            g_perf_config.detailed_analysis = true;
        } else if (strcmp(argv[i], "--throughput-only") == 0) {
            g_perf_config.run_throughput_tests = true;
            g_perf_config.run_latency_tests = false;
            g_perf_config.run_regression_tests = false;
            g_perf_config.run_comparative_tests = false;
            g_perf_config.run_memory_impact_tests = false;
            g_perf_config.run_cpu_utilization_tests = false;
        } else if (strcmp(argv[i], "--latency-only") == 0) {
            g_perf_config.run_throughput_tests = false;
            g_perf_config.run_latency_tests = true;
            g_perf_config.run_regression_tests = false;
            g_perf_config.run_comparative_tests = false;
            g_perf_config.run_memory_impact_tests = false;
            g_perf_config.run_cpu_utilization_tests = false;
        } else if (strcmp(argv[i], "--quick") == 0) {
            g_perf_config.test_duration_ms = 5000;     /* 5 seconds */
            g_perf_config.warmup_duration_ms = 1000;   /* 1 second */
        } else if (strcmp(argv[i], "--extended") == 0) {
            g_perf_config.test_duration_ms = 30000;    /* 30 seconds */
            g_perf_config.warmup_duration_ms = 5000;   /* 5 seconds */
        } else if (strcmp(argv[i], "--duration") == 0) {
            if (i + 1 < argc) {
                g_perf_config.test_duration_ms = atoi(argv[++i]) * 1000; /* Convert seconds to ms */
            } else {
                log_error("--duration requires a value in seconds");
                return -1;
            }
        } else if (strcmp(argv[i], "--packet-size") == 0) {
            if (i + 1 < argc) {
                int size = atoi(argv[++i]);
                if (size > 0 && size <= 9000) {
                    g_perf_config.packet_sizes[0] = size;
                    g_perf_config.num_packet_sizes = 1;
                } else {
                    log_error("Invalid packet size: %d", size);
                    return -1;
                }
            } else {
                log_error("--packet-size requires a value");
                return -1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Performance Test Runner - 3Com Packet Driver\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -v, --verbose           Enable verbose output\n");
            printf("  --detailed              Enable detailed analysis\n");
            printf("  --throughput-only       Run only throughput tests\n");
            printf("  --latency-only          Run only latency tests\n");
            printf("  --quick                 Quick test mode (5 seconds)\n");
            printf("  --extended              Extended test mode (30 seconds)\n");
            printf("  --duration <seconds>    Set test duration\n");
            printf("  --packet-size <bytes>   Test specific packet size only\n");
            printf("  -h, --help              Show this help\n");
            printf("\nPerformance test categories:\n");
            printf("  Throughput              - Packets per second and bandwidth tests\n");
            printf("  Latency                 - Round-trip time and response latency\n");
            printf("  Regression              - Performance regression detection\n");
            printf("  Comparative             - 3C509B vs 3C515-TX comparison\n");
            printf("  Memory Impact           - Memory usage vs performance analysis\n");
            printf("  CPU Utilization         - CPU usage efficiency analysis\n");
            return 1;
        }
    }
    
    return 0;
}

/**
 * @brief Initialize performance test environment
 */
static int initialize_performance_test_environment(void) {
    log_info("Initializing performance test environment");
    
    /* Initialize logging with appropriate level */
    int result = logging_init();
    if (result != 0) {
        printf("Failed to initialize logging system\n");
        return -1;
    }
    
    if (g_perf_config.verbose_output) {
        log_set_level(LOG_LEVEL_DEBUG);
    } else {
        log_set_level(LOG_LEVEL_INFO);
    }
    
    /* Initialize memory management */
    result = memory_init();
    if (result != 0) {
        log_error("Failed to initialize memory management");
        return -2;
    }
    
    /* Initialize hardware mock framework with performance monitoring */
    result = mock_framework_init();
    if (result != 0) {
        log_error("Failed to initialize hardware mock framework");
        return -3;
    }
    
    /* Enable performance monitoring features */
    mock_enable_performance_monitoring(true);
    mock_enable_timing_simulation(true);
    mock_set_latency_simulation(10, 50); /* 10-50 microseconds */
    
    /* Initialize performance framework */
    perf_config_t perf_config;
    perf_config.test_duration_ms = g_perf_config.test_duration_ms;
    perf_config.warmup_duration_ms = g_perf_config.warmup_duration_ms;
    perf_config.enable_detailed_stats = g_perf_config.detailed_analysis;
    perf_config.enable_cpu_monitoring = g_perf_config.run_cpu_utilization_tests;
    perf_config.enable_memory_monitoring = g_perf_config.run_memory_impact_tests;
    
    result = perf_framework_init(&perf_config);
    if (result != 0) {
        log_error("Failed to initialize performance framework");
        return -4;
    }
    
    /* Initialize test framework with performance test configuration */
    test_config_t test_config;
    test_config_init_default(&test_config);
    test_config.test_hardware = true;
    test_config.test_memory = true;
    test_config.test_packet_ops = true;
    test_config.run_benchmarks = true;
    test_config.run_stress_tests = false;
    test_config.verbose_output = g_perf_config.verbose_output;
    test_config.benchmark_duration_ms = g_perf_config.test_duration_ms;
    test_config.init_hardware = true;
    test_config.init_memory = true;
    test_config.init_diagnostics = true;
    
    result = test_framework_init(&test_config);
    if (result != 0) {
        log_error("Failed to initialize test framework");
        return -5;
    }
    
    log_info("Performance test environment initialized successfully");
    log_info("Test duration: %d ms, Warmup: %d ms", 
             g_perf_config.test_duration_ms, g_perf_config.warmup_duration_ms);
    
    return 0;
}

/**
 * @brief Cleanup performance test environment
 */
static void cleanup_performance_test_environment(void) {
    log_info("Cleaning up performance test environment");
    
    /* Generate performance report */
    perf_framework_generate_report();
    
    /* Cleanup frameworks */
    perf_framework_cleanup();
    test_framework_cleanup();
    mock_framework_cleanup();
    memory_cleanup();
    logging_cleanup();
    
    log_info("Performance test environment cleanup completed");
}

/**
 * @brief Test throughput benchmarks across different packet sizes
 */
static int test_throughput_benchmarks(void) {
    log_info("Testing throughput benchmarks");
    
    perf_results_t results;
    int overall_result = 0;
    
    for (int i = 0; i < g_perf_config.num_packet_sizes; i++) {
        int packet_size = g_perf_config.packet_sizes[i];
        
        log_info("Running throughput test for %d byte packets", packet_size);
        
        /* Configure performance test */
        perf_test_config_t test_config;
        test_config.packet_size = packet_size;
        test_config.test_duration_ms = g_perf_config.test_duration_ms;
        test_config.warmup_duration_ms = g_perf_config.warmup_duration_ms;
        test_config.target_pps = 0; /* Maximum throughput */
        
        /* Run throughput benchmark */
        int result = perf_run_throughput_test(&test_config, &results);
        if (result != 0) {
            log_error("Throughput test failed for %d byte packets", packet_size);
            overall_result = -1;
            continue;
        }
        
        /* Track best performance */
        if (results.throughput_pps > g_perf_stats.best_throughput_pps) {
            g_perf_stats.best_throughput_pps = results.throughput_pps;
            static char best_name[64];
            snprintf(best_name, sizeof(best_name), "Throughput %dB", packet_size);
            g_perf_stats.best_test_name = best_name;
        }
        
        /* Update statistics */
        g_perf_stats.total_packets_tested += results.packets_sent;
        g_perf_stats.total_bytes_tested += results.bytes_sent;
        
        log_info("Throughput results for %d byte packets:", packet_size);
        log_info("  Packets/sec: %.2f", results.throughput_pps);
        log_info("  Bandwidth: %.2f Mbps", results.bandwidth_mbps);
        log_info("  Efficiency: %.1f%%", results.efficiency_percent);
        
        if (g_perf_config.detailed_analysis) {
            log_info("  Min PPS: %.2f", results.min_pps);
            log_info("  Max PPS: %.2f", results.max_pps);
            log_info("  Jitter: %.2f%%", results.jitter_percent);
        }
    }
    
    /* Run basic performance test module */
    int basic_result = test_perf_basic_main();
    if (basic_result != 0) {
        log_error("Basic performance test failed");
        overall_result = -1;
    }
    
    /* Run dedicated throughput test module */
    int throughput_result = test_perf_throughput_main();
    if (throughput_result != 0) {
        log_error("Throughput performance test failed");
        overall_result = -1;
    }
    
    if (overall_result == 0) {
        log_info("Throughput benchmarks PASSED");
    } else {
        log_error("Throughput benchmarks FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test latency benchmarks across different conditions
 */
static int test_latency_benchmarks(void) {
    log_info("Testing latency benchmarks");
    
    perf_results_t results;
    int overall_result = 0;
    
    /* Test latency at different loads */
    uint32_t load_levels[] = {10, 50, 90}; /* Percentage of maximum throughput */
    int num_loads = sizeof(load_levels) / sizeof(load_levels[0]);
    
    for (int i = 0; i < num_loads; i++) {
        uint32_t load_percent = load_levels[i];
        
        log_info("Running latency test at %d%% load", load_percent);
        
        /* Configure latency test */
        perf_test_config_t test_config;
        test_config.packet_size = 64; /* Small packets for latency testing */
        test_config.test_duration_ms = g_perf_config.test_duration_ms;
        test_config.warmup_duration_ms = g_perf_config.warmup_duration_ms;
        test_config.load_percent = load_percent;
        
        /* Run latency benchmark */
        int result = perf_run_latency_test(&test_config, &results);
        if (result != 0) {
            log_error("Latency test failed at %d%% load", load_percent);
            overall_result = -1;
            continue;
        }
        
        /* Track worst latency */
        if (results.max_latency_us > g_perf_stats.worst_latency_us) {
            g_perf_stats.worst_latency_us = results.max_latency_us;
            static char worst_name[64];
            snprintf(worst_name, sizeof(worst_name), "Latency %d%% load", load_percent);
            g_perf_stats.worst_test_name = worst_name;
        }
        
        log_info("Latency results at %d%% load:", load_percent);
        log_info("  Average latency: %.2f µs", results.avg_latency_us);
        log_info("  Min latency: %.2f µs", results.min_latency_us);
        log_info("  Max latency: %.2f µs", results.max_latency_us);
        log_info("  99th percentile: %.2f µs", results.p99_latency_us);
        
        if (g_perf_config.detailed_analysis) {
            log_info("  95th percentile: %.2f µs", results.p95_latency_us);
            log_info("  Std deviation: %.2f µs", results.latency_stddev_us);
            log_info("  Latency jitter: %.2f µs", results.latency_jitter_us);
        }
    }
    
    /* Run dedicated latency test module */
    int latency_result = test_perf_latency_main();
    if (latency_result != 0) {
        log_error("Latency performance test failed");
        overall_result = -1;
    }
    
    if (overall_result == 0) {
        log_info("Latency benchmarks PASSED");
    } else {
        log_error("Latency benchmarks FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test comparative analysis between different drivers
 */
static int test_comparative_analysis(void) {
    log_info("Testing comparative analysis (3C509B vs 3C515-TX)");
    
    /* Create test NICs for comparison */
    mock_create_specific_nic(NIC_TYPE_3C509B, 0);
    mock_create_specific_nic(NIC_TYPE_3C515TX, 1);
    
    perf_results_t results_3c509b, results_3c515tx;
    int overall_result = 0;
    
    /* Test 3C509B performance */
    log_info("Testing 3C509B performance (PIO mode)");
    
    perf_test_config_t test_config_3c509b;
    test_config_3c509b.packet_size = 1500; /* Large packets to show difference */
    test_config_3c509b.test_duration_ms = g_perf_config.test_duration_ms;
    test_config_3c509b.warmup_duration_ms = g_perf_config.warmup_duration_ms;
    test_config_3c509b.target_nic = 0; /* 3C509B */
    
    int result = perf_run_comparative_test(&test_config_3c509b, &results_3c509b);
    if (result != 0) {
        log_error("3C509B performance test failed");
        overall_result = -1;
    }
    
    /* Test 3C515-TX performance */
    log_info("Testing 3C515-TX performance (DMA mode)");
    
    perf_test_config_t test_config_3c515tx;
    test_config_3c515tx.packet_size = 1500;
    test_config_3c515tx.test_duration_ms = g_perf_config.test_duration_ms;
    test_config_3c515tx.warmup_duration_ms = g_perf_config.warmup_duration_ms;
    test_config_3c515tx.target_nic = 1; /* 3C515-TX */
    
    result = perf_run_comparative_test(&test_config_3c515tx, &results_3c515tx);
    if (result != 0) {
        log_error("3C515-TX performance test failed");
        overall_result = -1;
    }
    
    if (overall_result == 0) {
        /* Print comparative analysis */
        log_info("Comparative Analysis Results:");
        log_info("=============================");
        
        log_info("3C509B (PIO Mode):");
        log_info("  Throughput: %.2f PPS", results_3c509b.throughput_pps);
        log_info("  Bandwidth: %.2f Mbps", results_3c509b.bandwidth_mbps);
        log_info("  Avg Latency: %.2f µs", results_3c509b.avg_latency_us);
        log_info("  CPU Usage: %.1f%%", results_3c509b.cpu_usage_percent);
        
        log_info("3C515-TX (DMA Mode):");
        log_info("  Throughput: %.2f PPS", results_3c515tx.throughput_pps);
        log_info("  Bandwidth: %.2f Mbps", results_3c515tx.bandwidth_mbps);
        log_info("  Avg Latency: %.2f µs", results_3c515tx.avg_latency_us);
        log_info("  CPU Usage: %.1f%%", results_3c515tx.cpu_usage_percent);
        
        /* Calculate performance differences */
        double throughput_ratio = results_3c515tx.throughput_pps / results_3c509b.throughput_pps;
        double latency_ratio = results_3c509b.avg_latency_us / results_3c515tx.avg_latency_us;
        double cpu_efficiency = results_3c509b.cpu_usage_percent / results_3c515tx.cpu_usage_percent;
        
        log_info("Performance Ratios:");
        log_info("  3C515-TX throughput advantage: %.2fx", throughput_ratio);
        log_info("  3C515-TX latency improvement: %.2fx", latency_ratio);
        log_info("  3C515-TX CPU efficiency: %.2fx", cpu_efficiency);
        
        if (throughput_ratio < 1.5) {
            log_warning("Expected higher throughput advantage for DMA vs PIO");
        }
        
        if (cpu_efficiency < 1.2) {
            log_warning("Expected better CPU efficiency for DMA vs PIO");
        }
    }
    
    if (overall_result == 0) {
        log_info("Comparative analysis PASSED");
    } else {
        log_error("Comparative analysis FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test memory impact on performance
 */
static int test_memory_impact_analysis(void) {
    log_info("Testing memory impact on performance");
    
    const mem_stats_t *initial_stats = memory_get_stats();
    perf_results_t baseline_results, loaded_results;
    int overall_result = 0;
    
    /* Run baseline performance test */
    log_info("Running baseline performance test");
    
    perf_test_config_t baseline_config;
    baseline_config.packet_size = 1500;
    baseline_config.test_duration_ms = g_perf_config.test_duration_ms / 2; /* Shorter for baseline */
    baseline_config.warmup_duration_ms = g_perf_config.warmup_duration_ms;
    
    int result = perf_run_throughput_test(&baseline_config, &baseline_results);
    if (result != 0) {
        log_error("Baseline performance test failed");
        return -1;
    }
    
    /* Allocate significant memory to test impact */
    log_info("Allocating memory to test performance impact");
    
    const int NUM_LARGE_BUFFERS = 100;
    const int BUFFER_SIZE = 64 * 1024; /* 64KB buffers */
    void *large_buffers[NUM_LARGE_BUFFERS];
    
    for (int i = 0; i < NUM_LARGE_BUFFERS; i++) {
        large_buffers[i] = malloc(BUFFER_SIZE);
        if (!large_buffers[i]) {
            log_error("Failed to allocate large buffer %d", i);
            /* Clean up already allocated buffers */
            for (int j = 0; j < i; j++) {
                free(large_buffers[j]);
            }
            return -1;
        }
        
        /* Touch the memory to ensure it's actually allocated */
        memset(large_buffers[i], 0x42, BUFFER_SIZE);
    }
    
    const mem_stats_t *loaded_stats = memory_get_stats();
    uint32_t memory_overhead = loaded_stats->used_memory - initial_stats->used_memory;
    
    log_info("Memory overhead: %u bytes (%.2f MB)", 
             memory_overhead, memory_overhead / (1024.0 * 1024.0));
    
    /* Run performance test with memory pressure */
    log_info("Running performance test under memory pressure");
    
    result = perf_run_throughput_test(&baseline_config, &loaded_results);
    if (result != 0) {
        log_error("Memory-loaded performance test failed");
        overall_result = -1;
    } else {
        /* Analyze memory impact */
        double throughput_impact = (baseline_results.throughput_pps - loaded_results.throughput_pps) / 
                                  baseline_results.throughput_pps * 100.0;
        double latency_impact = (loaded_results.avg_latency_us - baseline_results.avg_latency_us) / 
                               baseline_results.avg_latency_us * 100.0;
        
        log_info("Memory Impact Analysis:");
        log_info("  Baseline throughput: %.2f PPS", baseline_results.throughput_pps);
        log_info("  Loaded throughput: %.2f PPS", loaded_results.throughput_pps);
        log_info("  Throughput impact: %.2f%%", throughput_impact);
        log_info("  Baseline latency: %.2f µs", baseline_results.avg_latency_us);
        log_info("  Loaded latency: %.2f µs", loaded_results.avg_latency_us);
        log_info("  Latency impact: %.2f%%", latency_impact);
        
        if (throughput_impact > 10.0) {
            log_warning("High memory impact on throughput: %.2f%%", throughput_impact);
        }
        
        if (latency_impact > 20.0) {
            log_warning("High memory impact on latency: %.2f%%", latency_impact);
        }
    }
    
    /* Clean up allocated memory */
    for (int i = 0; i < NUM_LARGE_BUFFERS; i++) {
        free(large_buffers[i]);
    }
    
    if (overall_result == 0) {
        log_info("Memory impact analysis PASSED");
    } else {
        log_error("Memory impact analysis FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Test CPU utilization efficiency
 */
static int test_cpu_utilization_analysis(void) {
    log_info("Testing CPU utilization efficiency");
    
    /* This would typically require OS-specific CPU monitoring */
    /* For the sake of this test, we'll simulate CPU usage measurements */
    
    perf_results_t results;
    int overall_result = 0;
    
    /* Test CPU efficiency at different packet rates */
    uint32_t packet_rates[] = {1000, 10000, 50000}; /* PPS */
    int num_rates = sizeof(packet_rates) / sizeof(packet_rates[0]);
    
    for (int i = 0; i < num_rates; i++) {
        uint32_t target_pps = packet_rates[i];
        
        log_info("Testing CPU efficiency at %u PPS", target_pps);
        
        perf_test_config_t test_config;
        test_config.packet_size = 64; /* Small packets stress CPU more */
        test_config.test_duration_ms = g_perf_config.test_duration_ms / 2;
        test_config.warmup_duration_ms = g_perf_config.warmup_duration_ms;
        test_config.target_pps = target_pps;
        
        int result = perf_run_cpu_efficiency_test(&test_config, &results);
        if (result != 0) {
            log_error("CPU efficiency test failed at %u PPS", target_pps);
            overall_result = -1;
            continue;
        }
        
        /* Calculate efficiency metrics */
        double packets_per_cpu_percent = results.throughput_pps / results.cpu_usage_percent;
        double cpu_efficiency_score = (results.throughput_pps / target_pps) / (results.cpu_usage_percent / 100.0);
        
        log_info("CPU Efficiency Results at %u PPS:", target_pps);
        log_info("  Actual throughput: %.2f PPS", results.throughput_pps);
        log_info("  CPU usage: %.1f%%", results.cpu_usage_percent);
        log_info("  Packets per CPU%%: %.0f", packets_per_cpu_percent);
        log_info("  Efficiency score: %.2f", cpu_efficiency_score);
        
        if (results.cpu_usage_percent > 80.0 && results.throughput_pps < target_pps * 0.9) {
            log_warning("High CPU usage with low throughput efficiency");
        }
        
        if (cpu_efficiency_score < 0.8) {
            log_warning("Low CPU efficiency score: %.2f", cpu_efficiency_score);
        }
    }
    
    if (overall_result == 0) {
        log_info("CPU utilization analysis PASSED");
    } else {
        log_error("CPU utilization analysis FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Run a specific performance benchmark
 */
static int run_performance_benchmark(const performance_benchmark_t *benchmark) {
    if (!benchmark || !benchmark->benchmark_main) {
        log_error("Invalid performance benchmark");
        return -1;
    }
    
    log_info("=== Running Performance Benchmark: %s ===", benchmark->name);
    log_info("Description: %s", benchmark->description);
    
    if (benchmark->expected_min_pps > 0) {
        log_info("Expected minimum PPS: %u", benchmark->expected_min_pps);
    }
    if (benchmark->expected_max_latency_us > 0) {
        log_info("Expected maximum latency: %u µs", benchmark->expected_max_latency_us);
    }
    
    uint32_t start_time = get_system_timestamp_ms();
    
    int result = benchmark->benchmark_main();
    
    uint32_t end_time = get_system_timestamp_ms();
    uint32_t duration = end_time - start_time;
    
    g_perf_stats.total_benchmarks_run++;
    
    if (result == 0) {
        g_perf_stats.benchmarks_passed++;
        log_info("✓ Performance Benchmark PASSED: %s (duration: %lu ms)", benchmark->name, duration);
    } else {
        g_perf_stats.benchmarks_failed++;
        log_error("✗ Performance Benchmark FAILED: %s (duration: %lu ms, code: %d)", 
                  benchmark->name, duration, result);
    }
    
    return result;
}

/**
 * @brief Print performance test summary
 */
static void print_performance_test_summary(void) {
    log_info("");
    log_info("===================================================================");
    log_info("                PERFORMANCE TEST SUITE SUMMARY");
    log_info("===================================================================");
    log_info("Benchmarks Executed:");
    log_info("  Total Benchmarks: %d", g_perf_stats.total_benchmarks_run);
    log_info("  Passed: %d", g_perf_stats.benchmarks_passed);
    log_info("  Failed: %d", g_perf_stats.benchmarks_failed);
    log_info("");
    log_info("Test Data:");
    log_info("  Total Packets Tested: %u", g_perf_stats.total_packets_tested);
    log_info("  Total Bytes Tested: %llu (%.2f MB)", 
             g_perf_stats.total_bytes_tested, g_perf_stats.total_bytes_tested / (1024.0 * 1024.0));
    log_info("");
    log_info("Performance Highlights:");
    log_info("  Best Throughput: %.2f PPS (%s)", 
             g_perf_stats.best_throughput_pps, 
             g_perf_stats.best_test_name ? g_perf_stats.best_test_name : "N/A");
    log_info("  Worst Latency: %.2f µs (%s)", 
             g_perf_stats.worst_latency_us,
             g_perf_stats.worst_test_name ? g_perf_stats.worst_test_name : "N/A");
    log_info("");
    log_info("Execution Time:");
    log_info("  Total Duration: %lu ms (%.2f seconds)", 
             g_perf_stats.total_duration_ms, g_perf_stats.total_duration_ms / 1000.0);
    log_info("");
    
    if (g_perf_stats.benchmarks_failed == 0) {
        log_info("Success Rate: 100%% - ALL PERFORMANCE BENCHMARKS PASSED! ✓");
    } else {
        float success_rate = (float)g_perf_stats.benchmarks_passed / g_perf_stats.total_benchmarks_run * 100.0;
        log_info("Success Rate: %.1f%% (%d/%d benchmarks passed)", 
                 success_rate, g_perf_stats.benchmarks_passed, g_perf_stats.total_benchmarks_run);
        
        if (success_rate >= 80.0) {
            log_info("Result: GOOD - Most performance benchmarks passed");
        } else if (success_rate >= 60.0) {
            log_warning("Result: ACCEPTABLE - Some performance benchmarks failed");
        } else {
            log_error("Result: POOR - Many performance benchmarks failed");
        }
    }
    
    log_info("===================================================================");
}

/**
 * @brief Main performance test runner entry point (called from master runner)
 */
int run_performance_tests(int argc, char *argv[]) {
    log_info("Starting Performance Test Suite Runner");
    log_info("======================================");
    
    /* Parse performance test specific arguments */
    int parse_result = parse_performance_test_arguments(argc, argv);
    if (parse_result == 1) {
        return 0;  /* Help was shown */
    } else if (parse_result < 0) {
        return 1;  /* Error in arguments */
    }
    
    /* Initialize performance test environment */
    int init_result = initialize_performance_test_environment();
    if (init_result != 0) {
        log_error("Failed to initialize performance test environment");
        return 1;
    }
    
    uint32_t overall_start_time = get_system_timestamp_ms();
    
    /* Define all performance benchmarks */
    performance_benchmark_t benchmarks[] = {
        {
            .name = "Throughput Benchmarks",
            .description = "Packet throughput testing across various packet sizes",
            .benchmark_main = test_throughput_benchmarks,
            .enabled_flag = &g_perf_config.run_throughput_tests,
            .is_baseline = true,
            .expected_min_pps = 10000,
            .expected_max_latency_us = 0
        },
        {
            .name = "Latency Benchmarks",
            .description = "Network latency testing under various load conditions",
            .benchmark_main = test_latency_benchmarks,
            .enabled_flag = &g_perf_config.run_latency_tests,
            .is_baseline = true,
            .expected_min_pps = 0,
            .expected_max_latency_us = 1000
        },
        {
            .name = "Performance Regression",
            .description = "Performance regression detection and analysis",
            .benchmark_main = perf_regression_main,
            .enabled_flag = &g_perf_config.run_regression_tests,
            .is_baseline = false,
            .expected_min_pps = 0,
            .expected_max_latency_us = 0
        },
        {
            .name = "Comparative Analysis",
            .description = "Performance comparison between 3C509B and 3C515-TX",
            .benchmark_main = test_comparative_analysis,
            .enabled_flag = &g_perf_config.run_comparative_tests,
            .is_baseline = false,
            .expected_min_pps = 0,
            .expected_max_latency_us = 0
        },
        {
            .name = "Memory Impact Analysis",
            .description = "Performance impact under memory pressure",
            .benchmark_main = test_memory_impact_analysis,
            .enabled_flag = &g_perf_config.run_memory_impact_tests,
            .is_baseline = false,
            .expected_min_pps = 0,
            .expected_max_latency_us = 0
        },
        {
            .name = "CPU Utilization Analysis",
            .description = "CPU efficiency and utilization analysis",
            .benchmark_main = test_cpu_utilization_analysis,
            .enabled_flag = &g_perf_config.run_cpu_utilization_tests,
            .is_baseline = false,
            .expected_min_pps = 0,
            .expected_max_latency_us = 0
        }
    };
    
    int num_benchmarks = sizeof(benchmarks) / sizeof(benchmarks[0]);
    int overall_result = 0;
    
    /* Run all enabled performance benchmarks */
    for (int i = 0; i < num_benchmarks; i++) {
        if (!(*benchmarks[i].enabled_flag)) {
            log_info("Skipping disabled performance benchmark: %s", benchmarks[i].name);
            continue;
        }
        
        int result = run_performance_benchmark(&benchmarks[i]);
        if (result != 0) {
            overall_result = 1;
            
            if (benchmarks[i].is_baseline) {
                log_error("Baseline benchmark failed: %s", benchmarks[i].name);
            }
        }
    }
    
    uint32_t overall_end_time = get_system_timestamp_ms();
    g_perf_stats.total_duration_ms = overall_end_time - overall_start_time;
    
    /* Print comprehensive summary */
    print_performance_test_summary();
    
    /* Cleanup */
    cleanup_performance_test_environment();
    
    if (overall_result == 0) {
        log_info("Performance Test Suite: ALL BENCHMARKS COMPLETED SUCCESSFULLY");
    } else {
        log_error("Performance Test Suite: SOME BENCHMARKS FAILED");
    }
    
    return overall_result;
}

/**
 * @brief Standalone entry point (when run directly)
 */
int main(int argc, char *argv[]) {
    printf("3Com Packet Driver - Performance Test Suite Runner\n");
    printf("=================================================\n\n");
    
    return run_performance_tests(argc, argv);
}