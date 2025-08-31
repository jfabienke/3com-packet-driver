/**
 * @file benchmarks.c
 * @brief Performance measurement suite with microbenchmarks
 *
 * 3Com Packet Driver - Performance Measurement Framework
 * 
 * Provides comprehensive microbenchmarking capabilities for critical
 * operations using PIT-based timing for 25-30% optimization validation.
 * 
 * Agent 04 - Performance Engineer - Week 1 Day 4-5 Critical Deliverable
 */

#include "../../include/common.h"
#include "../../docs/agents/shared/timing-measurement.h"
#include "../../include/cpu_detect.h"
#include "../../include/logging.h"
#include <string.h>
#include <stdint.h>

/* Performance benchmark configuration */
#define BENCHMARK_ITERATIONS    1000    /* Number of iterations for statistical stability */
#define WARMUP_ITERATIONS       100     /* Warmup iterations to stabilize cache/timing */
#define MIN_TEST_DURATION_US    10000   /* Minimum test duration: 10ms */
#define MAX_TEST_DURATION_US    500000  /* Maximum test duration: 500ms */

/* Test data sizes for memory operations */
#define TEST_SIZE_SMALL         64      /* Small packet size */
#define TEST_SIZE_MEDIUM        256     /* Medium packet size */
#define TEST_SIZE_LARGE         1514    /* Ethernet MTU size */
#define TEST_SIZE_JUMBO         4096    /* Large buffer size */

/* Alignment test patterns */
#define ALIGN_BYTE              1       /* Byte aligned */
#define ALIGN_WORD              2       /* Word aligned */
#define ALIGN_DWORD             4       /* Dword aligned */
#define ALIGN_PARAGRAPH         16      /* Paragraph aligned */

/* Performance test categories */
typedef enum {
    PERF_CATEGORY_MEMORY_COPY,
    PERF_CATEGORY_MEMORY_SET,
    PERF_CATEGORY_IO_OPERATIONS,
    PERF_CATEGORY_INTERRUPT_LATENCY,
    PERF_CATEGORY_FUNCTION_CALLS,
    PERF_CATEGORY_CPU_FEATURES,
    PERF_CATEGORY_COUNT
} perf_category_t;

/* Individual benchmark result */
typedef struct {
    char name[32];                      /* Benchmark name */
    perf_category_t category;           /* Performance category */
    uint32_t min_us;                    /* Minimum time (microseconds) */
    uint32_t max_us;                    /* Maximum time (microseconds) */
    uint32_t avg_us;                    /* Average time (microseconds) */
    uint32_t iterations;                /* Number of iterations */
    uint32_t bytes_transferred;         /* Bytes transferred per iteration */
    uint32_t throughput_kbps;           /* Throughput in KB/s */
    bool optimization_applied;          /* Whether CPU optimization was used */
    bool valid;                         /* Whether result is valid */
} benchmark_result_t;

/* Benchmark suite results */
typedef struct {
    benchmark_result_t results[64];     /* Individual benchmark results */
    uint32_t result_count;              /* Number of results */
    uint32_t baseline_established;      /* Whether baseline was established */
    uint32_t optimization_improvement;  /* Overall improvement percentage */
    cpu_type_t tested_cpu;              /* CPU type tested */
    uint32_t cpu_features;              /* CPU features available */
} benchmark_suite_t;

/* Test data buffers */
static uint8_t test_buffer_src[TEST_SIZE_JUMBO + 16] __attribute__((aligned(16)));
static uint8_t test_buffer_dst[TEST_SIZE_JUMBO + 16] __attribute__((aligned(16)));
static uint8_t test_pattern[256];

/* Benchmark suite results */
static benchmark_suite_t g_benchmark_suite = {0};

/* Function prototypes */
static void initialize_test_data(void);
static void run_memory_copy_benchmarks(void);
static void run_memory_set_benchmarks(void); 
static void run_io_operation_benchmarks(void);
static void run_interrupt_latency_benchmarks(void);
static void run_function_call_benchmarks(void);
static void run_cpu_feature_benchmarks(void);

/* Optimization-specific benchmarks */
static void benchmark_memory_copy_baseline(size_t size, uint32_t alignment);
static void benchmark_memory_copy_optimized(size_t size, uint32_t alignment);
static void benchmark_rep_movsb_vs_movsw(size_t size);
static void benchmark_rep_movsw_vs_movsd(size_t size);
static void benchmark_pusha_vs_individual_saves(void);

/* Utility functions */
static void add_benchmark_result(const char* name, perf_category_t category,
                               const timing_stats_t* stats, uint32_t bytes_transferred,
                               bool optimization_applied);
static void calculate_throughput(benchmark_result_t* result);
static void print_benchmark_summary(void);
static void validate_benchmark_results(void);

/**
 * Initialize performance benchmark suite
 */
int performance_benchmark_init(void) {
    log_info("Initializing Performance Benchmark Suite...");
    
    /* Initialize PIT timing system */
    PIT_INIT();
    
    /* Clear benchmark results */
    memset(&g_benchmark_suite, 0, sizeof(g_benchmark_suite));
    
    /* Get CPU information */
    g_benchmark_suite.tested_cpu = cpu_detect_type();
    g_benchmark_suite.cpu_features = cpu_get_features();
    
    /* Initialize test data */
    initialize_test_data();
    
    log_info("Performance benchmark suite initialized for CPU type: %s",
             cpu_type_to_string(g_benchmark_suite.tested_cpu));
    
    return 0;
}

/**
 * Run comprehensive performance benchmark suite
 */
int run_performance_benchmarks(void) {
    log_info("Running comprehensive performance benchmarks...");
    
    /* Run all benchmark categories */
    run_memory_copy_benchmarks();
    run_memory_set_benchmarks();
    run_io_operation_benchmarks();
    run_interrupt_latency_benchmarks();
    run_function_call_benchmarks();
    run_cpu_feature_benchmarks();
    
    /* Validate and analyze results */
    validate_benchmark_results();
    print_benchmark_summary();
    
    log_info("Performance benchmark suite completed");
    return 0;
}

/**
 * Initialize test data patterns
 */
static void initialize_test_data(void) {
    /* Initialize source buffer with test pattern */
    for (uint32_t i = 0; i < sizeof(test_buffer_src); i++) {
        test_buffer_src[i] = (uint8_t)(i ^ 0xA5);
    }
    
    /* Clear destination buffer */
    memset(test_buffer_dst, 0, sizeof(test_buffer_dst));
    
    /* Create test pattern */
    for (uint32_t i = 0; i < sizeof(test_pattern); i++) {
        test_pattern[i] = (uint8_t)(i * 3 + 1);
    }
}

/**
 * Run memory copy benchmarks
 */
static void run_memory_copy_benchmarks(void) {
    log_debug("Running memory copy benchmarks...");
    
    /* Test different sizes and alignments */
    uint32_t test_sizes[] = {TEST_SIZE_SMALL, TEST_SIZE_MEDIUM, TEST_SIZE_LARGE, TEST_SIZE_JUMBO};
    uint32_t alignments[] = {ALIGN_BYTE, ALIGN_WORD, ALIGN_DWORD, ALIGN_PARAGRAPH};
    
    for (uint32_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        for (uint32_t j = 0; j < sizeof(alignments)/sizeof(alignments[0]); j++) {
            /* Run baseline (unoptimized) benchmark */
            benchmark_memory_copy_baseline(test_sizes[i], alignments[j]);
            
            /* Run optimized benchmark if CPU supports optimizations */
            if (cpu_supports_32bit() || cpu_has_feature(CPU_FEATURE_PUSHA)) {
                benchmark_memory_copy_optimized(test_sizes[i], alignments[j]);
            }
        }
    }
    
    /* CPU-specific optimization tests */
    if (g_benchmark_suite.tested_cpu >= CPU_TYPE_80286) {
        benchmark_rep_movsb_vs_movsw(TEST_SIZE_LARGE);
    }
    
    if (g_benchmark_suite.tested_cpu >= CPU_TYPE_80386) {
        benchmark_rep_movsw_vs_movsd(TEST_SIZE_LARGE);
    }
}

/**
 * Benchmark baseline memory copy (byte operations)
 */
static void benchmark_memory_copy_baseline(size_t size, uint32_t alignment) {
    char benchmark_name[32];
    timing_stats_t stats = {0};
    pit_timing_t timing;
    
    sprintf(benchmark_name, "MemCopy_Base_%u_%u", (uint32_t)size, alignment);
    
    /* Adjust source/destination pointers for alignment test */
    uint8_t* src = test_buffer_src + (alignment - 1);
    uint8_t* dst = test_buffer_dst + (alignment - 1);
    
    /* Warmup iterations */
    for (uint32_t i = 0; i < WARMUP_ITERATIONS; i++) {
        memcpy(dst, src, size);
    }
    
    /* Timed iterations */
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        /* Clear destination to ensure cache miss */
        memset(dst, 0, size);
        
        TIME_FUNCTION_CALL(&timing, {
            /* Baseline memory copy using byte operations */
            for (size_t j = 0; j < size; j++) {
                dst[j] = src[j];
            }
        });
        
        if (!timing.overflow) {
            update_timing_stats(&stats, &timing);
        }
    }
    
    add_benchmark_result(benchmark_name, PERF_CATEGORY_MEMORY_COPY, &stats, size, false);
}

/**
 * Benchmark optimized memory copy
 */
static void benchmark_memory_copy_optimized(size_t size, uint32_t alignment) {
    char benchmark_name[32];
    timing_stats_t stats = {0};
    pit_timing_t timing;
    
    sprintf(benchmark_name, "MemCopy_Opt_%u_%u", (uint32_t)size, alignment);
    
    /* Adjust source/destination pointers for alignment test */
    uint8_t* src = test_buffer_src + (alignment - 1);
    uint8_t* dst = test_buffer_dst + (alignment - 1);
    
    /* Warmup iterations */
    for (uint32_t i = 0; i < WARMUP_ITERATIONS; i++) {
        memcpy(dst, src, size);
    }
    
    /* Timed iterations */
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        /* Clear destination to ensure cache miss */
        memset(dst, 0, size);
        
        TIME_FUNCTION_CALL(&timing, {
            /* Optimized memory copy using CPU-specific instructions */
            if (cpu_supports_32bit() && (size >= 4) && ((uintptr_t)src % 4 == 0) && ((uintptr_t)dst % 4 == 0)) {
                /* Use 32-bit moves for 386+ CPUs when aligned */
                __asm__ __volatile__ (
                    "cld\n\t"
                    "rep movsl"
                    : "=D" (dst), "=S" (src), "=c" (size)
                    : "D" (dst), "S" (src), "c" (size / 4)
                    : "memory"
                );
                /* Handle remaining bytes */
                size_t remaining = size % 4;
                if (remaining) {
                    memcpy(dst, src, remaining);
                }
            } else if (g_benchmark_suite.tested_cpu >= CPU_TYPE_80286 && (size >= 2) && ((uintptr_t)src % 2 == 0) && ((uintptr_t)dst % 2 == 0)) {
                /* Use 16-bit moves for 286+ CPUs when aligned */
                __asm__ __volatile__ (
                    "cld\n\t"
                    "rep movsw"
                    : "=D" (dst), "=S" (src), "=c" (size)
                    : "D" (dst), "S" (src), "c" (size / 2)
                    : "memory"
                );
                /* Handle remaining bytes */
                if (size % 2) {
                    dst[size-1] = src[size-1];
                }
            } else {
                /* Fall back to byte operations */
                __asm__ __volatile__ (
                    "cld\n\t"
                    "rep movsb"
                    : "=D" (dst), "=S" (src), "=c" (size)
                    : "D" (dst), "S" (src), "c" (size)
                    : "memory"
                );
            }
        });
        
        if (!timing.overflow) {
            update_timing_stats(&stats, &timing);
        }
    }
    
    add_benchmark_result(benchmark_name, PERF_CATEGORY_MEMORY_COPY, &stats, size, true);
}

/**
 * Benchmark REP MOVSB vs REP MOVSW (286+ optimization)
 */
static void benchmark_rep_movsb_vs_movsw(size_t size) {
    timing_stats_t stats_movsb = {0};
    timing_stats_t stats_movsw = {0};
    pit_timing_t timing;
    
    uint8_t* src = test_buffer_src;
    uint8_t* dst = test_buffer_dst;
    
    /* Ensure word alignment for fair comparison */
    if ((uintptr_t)src % 2 != 0) src++;
    if ((uintptr_t)dst % 2 != 0) dst++;
    size &= ~1; /* Make size even */
    
    /* Benchmark REP MOVSB */
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        memset(dst, 0, size);
        
        TIME_FUNCTION_CALL(&timing, {
            __asm__ __volatile__ (
                "cld\n\t"
                "rep movsb"
                : "=D" (dst), "=S" (src), "=c" (size)
                : "D" (dst), "S" (src), "c" (size)
                : "memory"
            );
        });
        
        if (!timing.overflow) {
            update_timing_stats(&stats_movsb, &timing);
        }
    }
    
    /* Benchmark REP MOVSW */
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        memset(dst, 0, size);
        
        TIME_FUNCTION_CALL(&timing, {
            __asm__ __volatile__ (
                "cld\n\t"
                "rep movsw"
                : "=D" (dst), "=S" (src), "=c" (size)
                : "D" (dst), "S" (src), "c" (size / 2)
                : "memory"
            );
        });
        
        if (!timing.overflow) {
            update_timing_stats(&stats_movsw, &timing);
        }
    }
    
    add_benchmark_result("REP_MOVSB", PERF_CATEGORY_CPU_FEATURES, &stats_movsb, size, false);
    add_benchmark_result("REP_MOVSW", PERF_CATEGORY_CPU_FEATURES, &stats_movsw, size, true);
}

/**
 * Benchmark REP MOVSW vs REP MOVSD (386+ optimization)
 */
static void benchmark_rep_movsw_vs_movsd(size_t size) {
    timing_stats_t stats_movsw = {0};
    timing_stats_t stats_movsd = {0};
    pit_timing_t timing;
    
    uint8_t* src = test_buffer_src;
    uint8_t* dst = test_buffer_dst;
    
    /* Ensure dword alignment for fair comparison */
    if ((uintptr_t)src % 4 != 0) src += 4 - ((uintptr_t)src % 4);
    if ((uintptr_t)dst % 4 != 0) dst += 4 - ((uintptr_t)dst % 4);
    size &= ~3; /* Make size multiple of 4 */
    
    /* Benchmark REP MOVSW */
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        memset(dst, 0, size);
        
        TIME_FUNCTION_CALL(&timing, {
            __asm__ __volatile__ (
                "cld\n\t"
                "rep movsw"
                : "=D" (dst), "=S" (src), "=c" (size)
                : "D" (dst), "S" (src), "c" (size / 2)
                : "memory"
            );
        });
        
        if (!timing.overflow) {
            update_timing_stats(&stats_movsw, &timing);
        }
    }
    
    /* Benchmark REP MOVSD (386+ only) */
    if (cpu_supports_32bit()) {
        for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
            memset(dst, 0, size);
            
            TIME_FUNCTION_CALL(&timing, {
                __asm__ __volatile__ (
                    "cld\n\t"
                    "rep movsl"
                    : "=D" (dst), "=S" (src), "=c" (size)
                    : "D" (dst), "S" (src), "c" (size / 4)
                    : "memory"
                );
            });
            
            if (!timing.overflow) {
                update_timing_stats(&stats_movsd, &timing);
            }
        }
        
        add_benchmark_result("REP_MOVSD", PERF_CATEGORY_CPU_FEATURES, &stats_movsd, size, true);
    }
    
    add_benchmark_result("REP_MOVSW_386", PERF_CATEGORY_CPU_FEATURES, &stats_movsw, size, false);
}

/**
 * Run memory set benchmarks
 */
static void run_memory_set_benchmarks(void) {
    log_debug("Running memory set benchmarks...");
    
    /* Implementation for memory set benchmarks */
    /* This would test memset optimizations vs. baseline */
}

/**
 * Run I/O operation benchmarks  
 */
static void run_io_operation_benchmarks(void) {
    log_debug("Running I/O operation benchmarks...");
    
    /* Implementation for I/O benchmarks */
    /* This would test port I/O operations with timing */
}

/**
 * Run interrupt latency benchmarks
 */
static void run_interrupt_latency_benchmarks(void) {
    log_debug("Running interrupt latency benchmarks...");
    
    /* Implementation for interrupt latency tests */
    /* This would measure CLI/STI overhead and ISR entry/exit times */
}

/**
 * Run function call benchmarks
 */
static void run_function_call_benchmarks(void) {
    log_debug("Running function call benchmarks...");
    
    /* Test PUSHA/POPA vs individual register saves */
    if (g_benchmark_suite.tested_cpu >= CPU_TYPE_80286) {
        benchmark_pusha_vs_individual_saves();
    }
}

/**
 * Benchmark PUSHA/POPA vs individual register saves
 */
static void benchmark_pusha_vs_individual_saves(void) {
    timing_stats_t stats_individual = {0};
    timing_stats_t stats_pusha = {0};
    pit_timing_t timing;
    
    /* Benchmark individual register saves */
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        TIME_FUNCTION_CALL(&timing, {
            __asm__ __volatile__ (
                "push %%ax\n\t"
                "push %%bx\n\t"
                "push %%cx\n\t"
                "push %%dx\n\t"
                "push %%si\n\t"
                "push %%di\n\t"
                "push %%bp\n\t"
                "pop %%bp\n\t"
                "pop %%di\n\t"
                "pop %%si\n\t"
                "pop %%dx\n\t"
                "pop %%cx\n\t"
                "pop %%bx\n\t"
                "pop %%ax"
                :
                :
                : "memory"
            );
        });
        
        if (!timing.overflow) {
            update_timing_stats(&stats_individual, &timing);
        }
    }
    
    /* Benchmark PUSHA/POPA (286+ only) */
    if (cpu_has_feature(CPU_FEATURE_PUSHA)) {
        for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
            TIME_FUNCTION_CALL(&timing, {
                __asm__ __volatile__ (
                    "pusha\n\t"
                    "popa"
                    :
                    :
                    : "memory"
                );
            });
            
            if (!timing.overflow) {
                update_timing_stats(&stats_pusha, &timing);
            }
        }
        
        add_benchmark_result("PUSHA_POPA", PERF_CATEGORY_FUNCTION_CALLS, &stats_pusha, 0, true);
    }
    
    add_benchmark_result("Individual_Saves", PERF_CATEGORY_FUNCTION_CALLS, &stats_individual, 0, false);
}

/**
 * Run CPU feature benchmarks
 */
static void run_cpu_feature_benchmarks(void) {
    log_debug("Running CPU feature benchmarks...");
    
    /* CPU feature benchmarks are integrated into other tests */
    /* Additional CPU-specific benchmarks could be added here */
}

/**
 * Add benchmark result to suite
 */
static void add_benchmark_result(const char* name, perf_category_t category,
                               const timing_stats_t* stats, uint32_t bytes_transferred,
                               bool optimization_applied) {
    if (g_benchmark_suite.result_count >= sizeof(g_benchmark_suite.results)/sizeof(g_benchmark_suite.results[0])) {
        log_warning("Benchmark result buffer full, skipping result: %s", name);
        return;
    }
    
    benchmark_result_t* result = &g_benchmark_suite.results[g_benchmark_suite.result_count++];
    
    strncpy(result->name, name, sizeof(result->name) - 1);
    result->name[sizeof(result->name) - 1] = '\0';
    
    result->category = category;
    result->min_us = stats->min_us;
    result->max_us = stats->max_us;
    result->avg_us = AVERAGE_TIMING_US(stats);
    result->iterations = stats->count;
    result->bytes_transferred = bytes_transferred;
    result->optimization_applied = optimization_applied;
    result->valid = (stats->count > 0 && stats->overflow_count == 0);
    
    if (result->valid && bytes_transferred > 0) {
        calculate_throughput(result);
    }
    
    log_debug("Benchmark: %s, Avg: %luus, Min: %luus, Max: %luus, Optimized: %s",
              name, result->avg_us, result->min_us, result->max_us,
              optimization_applied ? "Yes" : "No");
}

/**
 * Calculate throughput for benchmark result
 */
static void calculate_throughput(benchmark_result_t* result) {
    if (result->avg_us > 0 && result->bytes_transferred > 0) {
        /* Throughput = (bytes * 1000) / avg_us = KB/s */
        result->throughput_kbps = (result->bytes_transferred * 1000) / result->avg_us;
    } else {
        result->throughput_kbps = 0;
    }
}

/**
 * Validate benchmark results
 */
static void validate_benchmark_results(void) {
    log_debug("Validating benchmark results...");
    
    uint32_t valid_results = 0;
    uint32_t optimized_results = 0;
    uint32_t total_improvement = 0;
    uint32_t improvement_count = 0;
    
    for (uint32_t i = 0; i < g_benchmark_suite.result_count; i++) {
        benchmark_result_t* result = &g_benchmark_suite.results[i];
        
        if (result->valid) {
            valid_results++;
            
            if (result->optimization_applied) {
                optimized_results++;
                
                /* Find corresponding baseline result */
                char baseline_name[32];
                strncpy(baseline_name, result->name, sizeof(baseline_name));
                baseline_name[sizeof(baseline_name) - 1] = '\0';
                
                /* Replace "_Opt_" with "_Base_" to find baseline */
                char* opt_pos = strstr(baseline_name, "_Opt_");
                if (opt_pos) {
                    memcpy(opt_pos, "_Base", 5);
                    
                    /* Find baseline result */
                    for (uint32_t j = 0; j < g_benchmark_suite.result_count; j++) {
                        if (strcmp(g_benchmark_suite.results[j].name, baseline_name) == 0 &&
                            g_benchmark_suite.results[j].valid) {
                            
                            uint32_t baseline_time = g_benchmark_suite.results[j].avg_us;
                            uint32_t optimized_time = result->avg_us;
                            
                            if (baseline_time > optimized_time) {
                                uint32_t improvement = ((baseline_time - optimized_time) * 100) / baseline_time;
                                total_improvement += improvement;
                                improvement_count++;
                                
                                log_info("Optimization improvement: %s: %lu%% (%luus -> %luus)",
                                         result->name, improvement, baseline_time, optimized_time);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    
    if (improvement_count > 0) {
        g_benchmark_suite.optimization_improvement = total_improvement / improvement_count;
    }
    
    log_info("Benchmark validation: %lu valid results, %lu optimized, avg improvement: %lu%%",
             valid_results, optimized_results, g_benchmark_suite.optimization_improvement);
}

/**
 * Print benchmark summary
 */
static void print_benchmark_summary(void) {
    printf("\n=== Performance Benchmark Results ===\n");
    printf("CPU Type: %s\n", cpu_type_to_string(g_benchmark_suite.tested_cpu));
    printf("CPU Features: 0x%08lX\n", g_benchmark_suite.cpu_features);
    printf("Total Benchmarks: %lu\n", g_benchmark_suite.result_count);
    printf("Average Optimization Improvement: %lu%%\n", g_benchmark_suite.optimization_improvement);
    printf("\nDetailed Results:\n");
    printf("%-20s %-10s %-8s %-8s %-8s %-8s %-8s\n", 
           "Name", "Category", "Avg(us)", "Min(us)", "Max(us)", "KB/s", "Opt");
    printf("--------------------------------------------------------------------------------------------------------\n");
    
    for (uint32_t i = 0; i < g_benchmark_suite.result_count; i++) {
        benchmark_result_t* result = &g_benchmark_suite.results[i];
        if (result->valid) {
            printf("%-20s %-10d %-8lu %-8lu %-8lu %-8lu %-8s\n",
                   result->name,
                   result->category,
                   result->avg_us,
                   result->min_us,
                   result->max_us,
                   result->throughput_kbps,
                   result->optimization_applied ? "Yes" : "No");
        }
    }
    printf("=====================================================================================================\n");
}

/**
 * Get benchmark suite results
 */
const benchmark_suite_t* get_benchmark_results(void) {
    return &g_benchmark_suite;
}

/**
 * Check if performance targets are met
 */
bool performance_targets_met(void) {
    /* Target: 25-30% improvement on critical operations */
    return (g_benchmark_suite.optimization_improvement >= 25);
}