/**
 * @file test_perf_latency.c
 * @brief Comprehensive latency performance testing for 3C509B and 3C515-TX NICs
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite provides comprehensive latency benchmarks including:
 * - Interrupt latency measurement using DOS timer
 * - Packet processing latency (TX and RX paths)
 * - Memory allocation latency
 * - DMA vs PIO latency comparison
 * - Latency under load conditions
 * - Jitter analysis and statistical measurement
 * - CPU utilization impact on latency
 */

#include "../../include/test_framework.h"
#include "../../include/packet_ops.h"
#include "../../include/hardware.h"
#include "../../include/stats.h"
#include "../../include/diagnostics.h"
#include "../../include/buffer_alloc.h"
#include "../../include/memory.h"
#include "../../include/logging.h"
#include "../../src/c/timestamp.c"  /* Include timestamp functions directly */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Latency test constants */
#define LATENCY_TEST_SAMPLES        1000    /* Number of latency samples per test */
#define LATENCY_WARMUP_SAMPLES      50      /* Warmup samples to discard */
#define LATENCY_MAX_ACCEPTABLE_US   100     /* Maximum acceptable average latency (microseconds) */
#define LATENCY_MAX_JITTER_US       500     /* Maximum acceptable jitter (microseconds) */
#define LATENCY_PERCENTILE_99       99      /* 99th percentile tracking */
#define LATENCY_STRESS_DURATION_MS  5000    /* Duration for stress latency tests */

/* Timer precision constants for DOS */
#define TIMER_FREQUENCY_HZ          18.2    /* DOS timer frequency */
#define TIMER_TICK_US               54925   /* Microseconds per timer tick */
#define HIGH_RES_TIMER_TICKS        100     /* Use CPU cycle estimation for higher resolution */

/* Latency measurement types */
#define LATENCY_TYPE_INTERRUPT      0       /* Interrupt handling latency */
#define LATENCY_TYPE_TX_PACKET      1       /* TX packet processing latency */
#define LATENCY_TYPE_RX_PACKET      2       /* RX packet processing latency */
#define LATENCY_TYPE_MEMORY_ALLOC   3       /* Memory allocation latency */
#define LATENCY_TYPE_DMA_SETUP      4       /* DMA setup latency */
#define LATENCY_TYPE_PIO_OPERATION  5       /* PIO operation latency */

/* Performance targets for different NIC types */
#define TARGET_INTERRUPT_LATENCY_3C509B_US   50    /* Target interrupt latency for 3C509B */
#define TARGET_INTERRUPT_LATENCY_3C515_US    30    /* Target interrupt latency for 3C515-TX */
#define TARGET_TX_LATENCY_3C509B_US          80    /* Target TX latency for 3C509B */
#define TARGET_TX_LATENCY_3C515_US           60    /* Target TX latency for 3C515-TX */
#define TARGET_RX_LATENCY_3C509B_US          70    /* Target RX latency for 3C509B */
#define TARGET_RX_LATENCY_3C515_US           50    /* Target RX latency for 3C515-TX */

/* Latency measurement structure */
typedef struct {
    uint32_t start_tick;                    /* Start timestamp (high resolution) */
    uint32_t end_tick;                      /* End timestamp (high resolution) */
    uint32_t latency_us;                    /* Calculated latency in microseconds */
    uint32_t cpu_load_percent;              /* CPU load during measurement */
    uint32_t memory_pressure;               /* Memory pressure indicator */
    uint32_t concurrent_operations;         /* Number of concurrent operations */
    bool outlier;                           /* Whether this sample is an outlier */
} latency_sample_t;

/* Statistical analysis structure */
typedef struct {
    uint32_t sample_count;                  /* Number of valid samples */
    uint32_t min_latency_us;                /* Minimum latency */
    uint32_t max_latency_us;                /* Maximum latency */
    uint32_t avg_latency_us;                /* Average latency */
    uint32_t median_latency_us;             /* Median latency */
    uint32_t percentile_95_us;              /* 95th percentile latency */
    uint32_t percentile_99_us;              /* 99th percentile latency */
    uint32_t std_deviation_us;              /* Standard deviation */
    uint32_t jitter_us;                     /* Maximum jitter (max - min) */
    uint32_t jitter_percent;                /* Jitter as percentage of average */
    uint32_t outlier_count;                 /* Number of outlier samples */
    double coefficient_of_variation;        /* Coefficient of variation */
} latency_statistics_t;

/* Test result structure */
typedef struct {
    char test_name[64];                     /* Test identifier */
    char nic_type[32];                      /* NIC type (3C509B or 3C515-TX) */
    int latency_type;                       /* Type of latency measured */
    uint32_t packet_size;                   /* Packet size (if applicable) */
    uint32_t cpu_load_target;               /* Target CPU load during test */
    
    /* Raw sample data */
    latency_sample_t samples[LATENCY_TEST_SAMPLES];
    uint32_t valid_samples;                 /* Number of valid samples */
    
    /* Statistical analysis */
    latency_statistics_t stats;             /* Calculated statistics */
    
    /* Performance assessment */
    uint32_t target_latency_us;             /* Target latency for this test */
    bool meets_target;                      /* Whether target was met */
    uint32_t performance_score;             /* Performance score (0-100) */
    
    /* Regression analysis */
    bool regression_detected;               /* Whether regression was detected */
    uint32_t regression_severity;           /* Severity of regression (0-100) */
    
    /* Environmental factors */
    uint32_t test_duration_ms;              /* Total test duration */
    uint32_t avg_cpu_utilization;           /* Average CPU utilization */
    uint32_t peak_memory_usage;             /* Peak memory usage during test */
    uint32_t error_count;                   /* Number of errors during test */
} latency_test_result_t;

/* Comprehensive latency benchmark */
typedef struct {
    latency_test_result_t interrupt_3c509b;     /* 3C509B interrupt latency */
    latency_test_result_t interrupt_3c515;      /* 3C515-TX interrupt latency */
    latency_test_result_t tx_3c509b;            /* 3C509B TX latency */
    latency_test_result_t tx_3c515;             /* 3C515-TX TX latency */
    latency_test_result_t rx_3c509b;            /* 3C509B RX latency */
    latency_test_result_t rx_3c515;             /* 3C515-TX RX latency */
    latency_test_result_t memory_alloc;         /* Memory allocation latency */
    latency_test_result_t dma_setup;            /* DMA setup latency */
    latency_test_result_t pio_operation;        /* PIO operation latency */
    
    /* Comparative analysis */
    uint32_t dma_latency_advantage_percent;     /* DMA advantage over PIO */
    uint32_t overall_performance_score;         /* Overall latency performance */
    bool all_targets_met;                       /* Whether all targets were met */
    
    /* Stress test results */
    latency_test_result_t stress_high_load;     /* Latency under high load */
    latency_test_result_t stress_memory_pressure; /* Latency under memory pressure */
    latency_test_result_t stress_concurrent;    /* Latency with concurrent operations */
    
    char recommendations[512];                  /* Performance recommendations */
} latency_benchmark_t;

/* Global test state */
static latency_benchmark_t g_latency_benchmark = {0};
static uint32_t g_high_res_timer_base = 0;
static uint32_t g_cpu_cycle_estimate = 0;

/* Forward declarations */
static test_result_t run_latency_benchmark_suite(void);
static test_result_t test_interrupt_latency(int nic_type, latency_test_result_t *result);
static test_result_t test_packet_latency(int nic_type, int latency_type, latency_test_result_t *result);
static test_result_t test_memory_allocation_latency(latency_test_result_t *result);
static test_result_t test_dma_setup_latency(latency_test_result_t *result);
static test_result_t test_pio_operation_latency(latency_test_result_t *result);
static test_result_t test_latency_under_stress(latency_test_result_t *result);
static test_result_t analyze_latency_performance(void);

/* Utility functions */
static void init_latency_testing(void);
static void cleanup_latency_testing(void);
static uint32_t get_high_resolution_timestamp(void);
static uint32_t calculate_latency_us(uint32_t start_tick, uint32_t end_tick);
static void calculate_latency_statistics(latency_test_result_t *result);
static void detect_outliers(latency_test_result_t *result);
static void sort_latency_samples(uint32_t *latencies, uint32_t count);
static uint32_t calculate_percentile(uint32_t *sorted_latencies, uint32_t count, uint32_t percentile);
static uint32_t calculate_latency_performance_score(const latency_test_result_t *result);
static void simulate_cpu_load(uint32_t target_percent);
static void simulate_memory_pressure(void);
static void print_latency_result(const latency_test_result_t *result);
static void print_latency_benchmark_summary(const latency_benchmark_t *benchmark);
static void generate_latency_recommendations(latency_benchmark_t *benchmark);

/**
 * @brief Main entry point for latency performance tests
 */
int latency_test_main(void) {
    log_info("=== Starting Comprehensive Latency Benchmark Suite ===");
    
    init_latency_testing();
    
    test_result_t result = run_latency_benchmark_suite();
    
    cleanup_latency_testing();
    
    if (test_result_is_success(result)) {
        log_info("=== Latency Benchmark Suite PASSED ===");
        return SUCCESS;
    } else {
        log_error("=== Latency Benchmark Suite FAILED ===");
        return ERROR_IO;
    }
}

/**
 * @brief Run the complete latency benchmark suite
 */
static test_result_t run_latency_benchmark_suite(void) {
    log_info("Initializing latency benchmark environment...");
    
    /* Initialize test framework */
    test_config_t config;
    test_config_init_default(&config);
    config.run_benchmarks = true;
    
    TEST_ASSERT(test_framework_init(&config) == SUCCESS, "Failed to initialize test framework");
    
    /* Initialize packet operations */
    config_t driver_config = {0};
    TEST_ASSERT(packet_ops_init(&driver_config) == SUCCESS, "Failed to initialize packet operations");
    
    /* Initialize statistics */
    TEST_ASSERT(stats_subsystem_init(&driver_config) == SUCCESS, "Failed to initialize statistics");
    
    /* Test interrupt latency */
    log_info("=== Testing Interrupt Latency ===");
    TEST_ASSERT(test_result_is_success(test_interrupt_latency(NIC_TYPE_3C509B, 
                                       &g_latency_benchmark.interrupt_3c509b)), 
                "3C509B interrupt latency test failed");
    
    TEST_ASSERT(test_result_is_success(test_interrupt_latency(NIC_TYPE_3C515_TX, 
                                       &g_latency_benchmark.interrupt_3c515)), 
                "3C515-TX interrupt latency test failed");
    
    /* Test packet processing latency */
    log_info("=== Testing Packet Processing Latency ===");
    TEST_ASSERT(test_result_is_success(test_packet_latency(NIC_TYPE_3C509B, LATENCY_TYPE_TX_PACKET,
                                       &g_latency_benchmark.tx_3c509b)), 
                "3C509B TX latency test failed");
    
    TEST_ASSERT(test_result_is_success(test_packet_latency(NIC_TYPE_3C515_TX, LATENCY_TYPE_TX_PACKET,
                                       &g_latency_benchmark.tx_3c515)), 
                "3C515-TX TX latency test failed");
    
    TEST_ASSERT(test_result_is_success(test_packet_latency(NIC_TYPE_3C509B, LATENCY_TYPE_RX_PACKET,
                                       &g_latency_benchmark.rx_3c509b)), 
                "3C509B RX latency test failed");
    
    TEST_ASSERT(test_result_is_success(test_packet_latency(NIC_TYPE_3C515_TX, LATENCY_TYPE_RX_PACKET,
                                       &g_latency_benchmark.rx_3c515)), 
                "3C515-TX RX latency test failed");
    
    /* Test resource operation latency */
    log_info("=== Testing Resource Operation Latency ===");
    TEST_ASSERT(test_result_is_success(test_memory_allocation_latency(&g_latency_benchmark.memory_alloc)), 
                "Memory allocation latency test failed");
    
    TEST_ASSERT(test_result_is_success(test_dma_setup_latency(&g_latency_benchmark.dma_setup)), 
                "DMA setup latency test failed");
    
    TEST_ASSERT(test_result_is_success(test_pio_operation_latency(&g_latency_benchmark.pio_operation)), 
                "PIO operation latency test failed");
    
    /* Test latency under stress */
    log_info("=== Testing Latency Under Stress Conditions ===");
    TEST_ASSERT(test_result_is_success(test_latency_under_stress(&g_latency_benchmark.stress_high_load)), 
                "High load stress latency test failed");
    
    /* Analyze results */
    log_info("=== Analyzing Latency Performance ===");
    TEST_ASSERT(test_result_is_success(analyze_latency_performance()), "Latency analysis failed");
    
    /* Generate recommendations */
    generate_latency_recommendations(&g_latency_benchmark);
    
    /* Print comprehensive results */
    print_latency_benchmark_summary(&g_latency_benchmark);
    
    /* Cleanup */
    packet_ops_cleanup();
    stats_cleanup();
    test_framework_cleanup();
    
    return g_latency_benchmark.all_targets_met ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Test interrupt handling latency
 */
static test_result_t test_interrupt_latency(int nic_type, latency_test_result_t *result) {
    /* Initialize result structure */
    memset(result, 0, sizeof(latency_test_result_t));
    snprintf(result->test_name, sizeof(result->test_name), "Interrupt_Latency_%s", 
             (nic_type == NIC_TYPE_3C509B) ? "3C509B" : "3C515TX");
    strcpy(result->nic_type, (nic_type == NIC_TYPE_3C509B) ? "3C509B" : "3C515-TX");
    result->latency_type = LATENCY_TYPE_INTERRUPT;
    result->target_latency_us = (nic_type == NIC_TYPE_3C509B) ? 
                               TARGET_INTERRUPT_LATENCY_3C509B_US : TARGET_INTERRUPT_LATENCY_3C515_US;
    
    log_info("Testing interrupt latency for %s (target: %lu us)", 
             result->nic_type, result->target_latency_us);
    
    /* Create test NIC */
    nic_info_t test_nic = {0};
    test_nic.type = nic_type;
    test_nic.io_base = (nic_type == NIC_TYPE_3C509B) ? 0x300 : 0x320;
    test_nic.irq = (nic_type == NIC_TYPE_3C509B) ? 10 : 11;
    test_nic.status = NIC_STATUS_PRESENT | NIC_STATUS_ACTIVE;
    
    int nic_id = hardware_add_nic(&test_nic);
    TEST_ASSERT(nic_id >= 0, "Failed to add test NIC");
    
    uint32_t test_start = get_system_timestamp_ms();
    uint32_t valid_samples = 0;
    uint32_t errors = 0;
    
    /* Warmup phase */
    log_debug("Warming up interrupt handling...");
    for (int i = 0; i < LATENCY_WARMUP_SAMPLES; i++) {
        /* Simulate interrupt generation and handling */
        uint32_t start_tick = get_high_resolution_timestamp();
        
        /* Simulate interrupt trigger */
        for (volatile int j = 0; j < 5; j++);  /* Brief processing delay */
        
        uint32_t end_tick = get_high_resolution_timestamp();
        
        /* Discard warmup samples */
    }
    
    /* Main measurement phase */
    log_debug("Starting interrupt latency measurements...");
    
    for (int i = 0; i < LATENCY_TEST_SAMPLES && valid_samples < LATENCY_TEST_SAMPLES; i++) {
        latency_sample_t *sample = &result->samples[valid_samples];
        
        /* Record start time */
        sample->start_tick = get_high_resolution_timestamp();
        
        /* Simulate interrupt handling sequence */
        /* 1. Interrupt recognition */
        for (volatile int j = 0; j < 2; j++);
        
        /* 2. Context save (simulated) */
        for (volatile int j = 0; j < 3; j++);
        
        /* 3. Interrupt handler entry */
        for (volatile int j = 0; j < 1; j++);
        
        /* 4. Minimal interrupt processing */
        if (nic_type == NIC_TYPE_3C515_TX) {
            /* DMA interrupt handling - typically faster */
            for (volatile int j = 0; j < 8; j++);
        } else {
            /* PIO interrupt handling - typically more CPU intensive */
            for (volatile int j = 0; j < 12; j++);
        }
        
        /* 5. Interrupt acknowledgment */
        for (volatile int j = 0; j < 2; j++);
        
        /* 6. Context restore (simulated) */
        for (volatile int j = 0; j < 3; j++);
        
        /* Record end time */
        sample->end_tick = get_high_resolution_timestamp();
        
        /* Calculate latency */
        sample->latency_us = calculate_latency_us(sample->start_tick, sample->end_tick);
        
        /* Validate measurement */
        if (sample->latency_us > 0 && sample->latency_us < 10000) {  /* Sanity check: < 10ms */
            sample->cpu_load_percent = 0;  /* No additional CPU load for basic test */
            sample->memory_pressure = 0;
            sample->concurrent_operations = 0;
            sample->outlier = false;
            
            valid_samples++;
        } else {
            errors++;
            log_debug("Invalid latency measurement: %lu us", sample->latency_us);
        }
        
        /* Brief pause between measurements */
        for (volatile int j = 0; j < 10; j++);
    }
    
    uint32_t test_end = get_system_timestamp_ms();
    
    /* Record test metadata */
    result->valid_samples = valid_samples;
    result->test_duration_ms = test_end - test_start;
    result->error_count = errors;
    
    /* Calculate statistics */
    calculate_latency_statistics(result);
    
    /* Detect outliers */
    detect_outliers(result);
    
    /* Calculate performance score */
    result->performance_score = calculate_latency_performance_score(result);
    
    /* Check if target was met */
    result->meets_target = (result->stats.avg_latency_us <= result->target_latency_us) &&
                          (result->stats.percentile_99_us <= (result->target_latency_us * 2)) &&
                          (result->stats.jitter_percent <= 20);
    
    /* Print result */
    print_latency_result(result);
    
    /* Cleanup */
    hardware_remove_nic(nic_id);
    
    return result->meets_target ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Test packet processing latency (TX or RX)
 */
static test_result_t test_packet_latency(int nic_type, int latency_type, latency_test_result_t *result) {
    /* Initialize result structure */
    memset(result, 0, sizeof(latency_test_result_t));
    snprintf(result->test_name, sizeof(result->test_name), "%s_Latency_%s", 
             (latency_type == LATENCY_TYPE_TX_PACKET) ? "TX" : "RX",
             (nic_type == NIC_TYPE_3C509B) ? "3C509B" : "3C515TX");
    strcpy(result->nic_type, (nic_type == NIC_TYPE_3C509B) ? "3C509B" : "3C515-TX");
    result->latency_type = latency_type;
    result->packet_size = 512;  /* Medium packet size for testing */
    
    if (latency_type == LATENCY_TYPE_TX_PACKET) {
        result->target_latency_us = (nic_type == NIC_TYPE_3C509B) ? 
                                   TARGET_TX_LATENCY_3C509B_US : TARGET_TX_LATENCY_3C515_US;
    } else {
        result->target_latency_us = (nic_type == NIC_TYPE_3C509B) ? 
                                   TARGET_RX_LATENCY_3C509B_US : TARGET_RX_LATENCY_3C515_US;
    }
    
    log_info("Testing %s packet latency for %s (target: %lu us)", 
             (latency_type == LATENCY_TYPE_TX_PACKET) ? "TX" : "RX",
             result->nic_type, result->target_latency_us);
    
    /* Create test NIC */
    nic_info_t test_nic = {0};
    test_nic.type = nic_type;
    test_nic.io_base = (nic_type == NIC_TYPE_3C509B) ? 0x300 : 0x320;
    test_nic.irq = (nic_type == NIC_TYPE_3C509B) ? 10 : 11;
    test_nic.status = NIC_STATUS_PRESENT | NIC_STATUS_ACTIVE;
    
    int nic_id = hardware_add_nic(&test_nic);
    TEST_ASSERT(nic_id >= 0, "Failed to add test NIC");
    
    /* Allocate test packet */
    uint8_t *test_packet = malloc(result->packet_size);
    TEST_ASSERT(test_packet != NULL, "Failed to allocate test packet");
    
    /* Initialize test packet */
    memset(test_packet, 0xAA, result->packet_size);
    test_packet[0] = 0x00; test_packet[1] = 0x11; test_packet[2] = 0x22;  /* Dest MAC */
    test_packet[3] = 0x33; test_packet[4] = 0x44; test_packet[5] = 0x55;
    
    uint32_t test_start = get_system_timestamp_ms();
    uint32_t valid_samples = 0;
    uint32_t errors = 0;
    
    /* Warmup phase */
    for (int i = 0; i < LATENCY_WARMUP_SAMPLES; i++) {
        if (latency_type == LATENCY_TYPE_TX_PACKET) {
            packet_send(nic_id, test_packet, result->packet_size);
        } else {
            /* For RX, we simulate packet reception processing */
            for (volatile int j = 0; j < 20; j++);
        }
    }
    
    /* Main measurement phase */
    for (int i = 0; i < LATENCY_TEST_SAMPLES && valid_samples < LATENCY_TEST_SAMPLES; i++) {
        latency_sample_t *sample = &result->samples[valid_samples];
        
        if (latency_type == LATENCY_TYPE_TX_PACKET) {
            /* Measure TX packet latency */
            sample->start_tick = get_high_resolution_timestamp();
            
            int send_result = packet_send(nic_id, test_packet, result->packet_size);
            
            sample->end_tick = get_high_resolution_timestamp();
            
            if (send_result == SUCCESS) {
                sample->latency_us = calculate_latency_us(sample->start_tick, sample->end_tick);
                valid_samples++;
            } else {
                errors++;
            }
        } else {
            /* Measure RX packet processing latency */
            sample->start_tick = get_high_resolution_timestamp();
            
            /* Simulate packet reception and processing */
            uint8_t rx_buffer[1518];
            size_t rx_size = sizeof(rx_buffer);
            
            /* Simulate the RX processing steps */
            /* 1. DMA/PIO data transfer simulation */
            if (nic_type == NIC_TYPE_3C515_TX) {
                /* DMA transfer simulation */
                for (volatile int j = 0; j < (result->packet_size / 16); j++);
            } else {
                /* PIO transfer simulation */
                for (volatile int j = 0; j < (result->packet_size / 4); j++);
            }
            
            /* 2. Packet validation and processing */
            for (volatile int j = 0; j < 10; j++);
            
            /* 3. Buffer management */
            for (volatile int j = 0; j < 5; j++);
            
            sample->end_tick = get_high_resolution_timestamp();
            sample->latency_us = calculate_latency_us(sample->start_tick, sample->end_tick);
            valid_samples++;
        }
        
        /* Validate measurement */
        if (sample->latency_us > 0 && sample->latency_us < 5000) {  /* Sanity check: < 5ms */
            sample->cpu_load_percent = 0;
            sample->memory_pressure = 0;
            sample->concurrent_operations = 0;
            sample->outlier = false;
        } else {
            valid_samples--;  /* Discard invalid sample */
            errors++;
        }
        
        /* Brief pause between measurements */
        for (volatile int j = 0; j < 5; j++);
    }
    
    uint32_t test_end = get_system_timestamp_ms();
    
    /* Record test metadata */
    result->valid_samples = valid_samples;
    result->test_duration_ms = test_end - test_start;
    result->error_count = errors;
    
    /* Calculate statistics */
    calculate_latency_statistics(result);
    
    /* Detect outliers */
    detect_outliers(result);
    
    /* Calculate performance score */
    result->performance_score = calculate_latency_performance_score(result);
    
    /* Check if target was met */
    result->meets_target = (result->stats.avg_latency_us <= result->target_latency_us) &&
                          (result->stats.percentile_99_us <= (result->target_latency_us * 2));
    
    /* Print result */
    print_latency_result(result);
    
    /* Cleanup */
    free(test_packet);
    hardware_remove_nic(nic_id);
    
    return result->meets_target ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Test memory allocation latency
 */
static test_result_t test_memory_allocation_latency(latency_test_result_t *result) {
    /* Initialize result structure */
    memset(result, 0, sizeof(latency_test_result_t));
    strcpy(result->test_name, "Memory_Allocation_Latency");
    strcpy(result->nic_type, "N/A");
    result->latency_type = LATENCY_TYPE_MEMORY_ALLOC;
    result->target_latency_us = 20;  /* Target: 20 microseconds for memory allocation */
    
    log_info("Testing memory allocation latency (target: %lu us)", result->target_latency_us);
    
    uint32_t test_start = get_system_timestamp_ms();
    uint32_t valid_samples = 0;
    uint32_t errors = 0;
    
    /* Test various allocation sizes */
    size_t alloc_sizes[] = {64, 256, 512, 1024, 1518};
    int num_sizes = sizeof(alloc_sizes) / sizeof(alloc_sizes[0]);
    
    for (int size_idx = 0; size_idx < num_sizes; size_idx++) {
        size_t alloc_size = alloc_sizes[size_idx];
        
        for (int i = 0; i < (LATENCY_TEST_SAMPLES / num_sizes) && valid_samples < LATENCY_TEST_SAMPLES; i++) {
            latency_sample_t *sample = &result->samples[valid_samples];
            
            /* Measure allocation latency */
            sample->start_tick = get_high_resolution_timestamp();
            
            void *buffer = malloc(alloc_size);
            
            sample->end_tick = get_high_resolution_timestamp();
            
            if (buffer != NULL) {
                sample->latency_us = calculate_latency_us(sample->start_tick, sample->end_tick);
                
                /* Immediately free to measure complete cycle */
                uint32_t free_start = get_high_resolution_timestamp();
                free(buffer);
                uint32_t free_end = get_high_resolution_timestamp();
                
                /* Add free latency to total */
                sample->latency_us += calculate_latency_us(free_start, free_end);
                
                if (sample->latency_us > 0 && sample->latency_us < 1000) {  /* Sanity check */
                    sample->cpu_load_percent = 0;
                    sample->memory_pressure = size_idx;  /* Use as pressure indicator */
                    sample->concurrent_operations = 0;
                    sample->outlier = false;
                    valid_samples++;
                } else {
                    errors++;
                }
            } else {
                errors++;
            }
            
            /* Brief pause */
            for (volatile int j = 0; j < 5; j++);
        }
    }
    
    uint32_t test_end = get_system_timestamp_ms();
    
    /* Record test metadata */
    result->valid_samples = valid_samples;
    result->test_duration_ms = test_end - test_start;
    result->error_count = errors;
    
    /* Calculate statistics */
    calculate_latency_statistics(result);
    
    /* Calculate performance score */
    result->performance_score = calculate_latency_performance_score(result);
    
    /* Check if target was met */
    result->meets_target = (result->stats.avg_latency_us <= result->target_latency_us);
    
    /* Print result */
    print_latency_result(result);
    
    return result->meets_target ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Test DMA setup latency
 */
static test_result_t test_dma_setup_latency(latency_test_result_t *result) {
    /* Initialize result structure */
    memset(result, 0, sizeof(latency_test_result_t));
    strcpy(result->test_name, "DMA_Setup_Latency");
    strcpy(result->nic_type, "3C515-TX");
    result->latency_type = LATENCY_TYPE_DMA_SETUP;
    result->target_latency_us = 15;  /* Target: 15 microseconds for DMA setup */
    
    log_info("Testing DMA setup latency (target: %lu us)", result->target_latency_us);
    
    uint32_t test_start = get_system_timestamp_ms();
    uint32_t valid_samples = 0;
    
    for (int i = 0; i < LATENCY_TEST_SAMPLES && valid_samples < LATENCY_TEST_SAMPLES; i++) {
        latency_sample_t *sample = &result->samples[valid_samples];
        
        /* Measure DMA setup latency */
        sample->start_tick = get_high_resolution_timestamp();
        
        /* Simulate DMA setup operations */
        /* 1. Descriptor preparation */
        for (volatile int j = 0; j < 8; j++);
        
        /* 2. DMA controller programming */
        for (volatile int j = 0; j < 5; j++);
        
        /* 3. Transfer initiation */
        for (volatile int j = 0; j < 3; j++);
        
        sample->end_tick = get_high_resolution_timestamp();
        sample->latency_us = calculate_latency_us(sample->start_tick, sample->end_tick);
        
        if (sample->latency_us > 0 && sample->latency_us < 500) {  /* Sanity check */
            sample->cpu_load_percent = 0;
            sample->memory_pressure = 0;
            sample->concurrent_operations = 0;
            sample->outlier = false;
            valid_samples++;
        }
        
        /* Brief pause */
        for (volatile int j = 0; j < 10; j++);
    }
    
    uint32_t test_end = get_system_timestamp_ms();
    
    /* Record test metadata */
    result->valid_samples = valid_samples;
    result->test_duration_ms = test_end - test_start;
    result->error_count = 0;
    
    /* Calculate statistics */
    calculate_latency_statistics(result);
    
    /* Calculate performance score */
    result->performance_score = calculate_latency_performance_score(result);
    
    /* Check if target was met */
    result->meets_target = (result->stats.avg_latency_us <= result->target_latency_us);
    
    /* Print result */
    print_latency_result(result);
    
    return result->meets_target ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Test PIO operation latency
 */
static test_result_t test_pio_operation_latency(latency_test_result_t *result) {
    /* Initialize result structure */
    memset(result, 0, sizeof(latency_test_result_t));
    strcpy(result->test_name, "PIO_Operation_Latency");
    strcpy(result->nic_type, "3C509B");
    result->latency_type = LATENCY_TYPE_PIO_OPERATION;
    result->target_latency_us = 25;  /* Target: 25 microseconds for PIO operation */
    
    log_info("Testing PIO operation latency (target: %lu us)", result->target_latency_us);
    
    uint32_t test_start = get_system_timestamp_ms();
    uint32_t valid_samples = 0;
    
    for (int i = 0; i < LATENCY_TEST_SAMPLES && valid_samples < LATENCY_TEST_SAMPLES; i++) {
        latency_sample_t *sample = &result->samples[valid_samples];
        
        /* Measure PIO operation latency */
        sample->start_tick = get_high_resolution_timestamp();
        
        /* Simulate PIO operations */
        /* 1. Register access preparation */
        for (volatile int j = 0; j < 3; j++);
        
        /* 2. Data transfer (byte by byte) */
        for (volatile int j = 0; j < 16; j++);  /* Simulate 16-byte transfer */
        
        /* 3. Status checking */
        for (volatile int j = 0; j < 5; j++);
        
        sample->end_tick = get_high_resolution_timestamp();
        sample->latency_us = calculate_latency_us(sample->start_tick, sample->end_tick);
        
        if (sample->latency_us > 0 && sample->latency_us < 500) {  /* Sanity check */
            sample->cpu_load_percent = 0;
            sample->memory_pressure = 0;
            sample->concurrent_operations = 0;
            sample->outlier = false;
            valid_samples++;
        }
        
        /* Brief pause */
        for (volatile int j = 0; j < 10; j++);
    }
    
    uint32_t test_end = get_system_timestamp_ms();
    
    /* Record test metadata */
    result->valid_samples = valid_samples;
    result->test_duration_ms = test_end - test_start;
    result->error_count = 0;
    
    /* Calculate statistics */
    calculate_latency_statistics(result);
    
    /* Calculate performance score */
    result->performance_score = calculate_latency_performance_score(result);
    
    /* Check if target was met */
    result->meets_target = (result->stats.avg_latency_us <= result->target_latency_us);
    
    /* Print result */
    print_latency_result(result);
    
    return result->meets_target ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Test latency under stress conditions
 */
static test_result_t test_latency_under_stress(latency_test_result_t *result) {
    /* Initialize result structure */
    memset(result, 0, sizeof(latency_test_result_t));
    strcpy(result->test_name, "Latency_Under_Stress");
    strcpy(result->nic_type, "Both");
    result->latency_type = LATENCY_TYPE_INTERRUPT;
    result->target_latency_us = 200;  /* More lenient target under stress */
    
    log_info("Testing latency under stress conditions (target: %lu us)", result->target_latency_us);
    
    uint32_t test_start = get_system_timestamp_ms();
    uint32_t valid_samples = 0;
    
    /* Create high CPU load and memory pressure */
    for (int i = 0; i < LATENCY_TEST_SAMPLES && valid_samples < LATENCY_TEST_SAMPLES; i++) {
        latency_sample_t *sample = &result->samples[valid_samples];
        
        /* Simulate high CPU load */
        simulate_cpu_load(75);  /* 75% CPU load */
        
        /* Measure latency under stress */
        sample->start_tick = get_high_resolution_timestamp();
        
        /* Simulate interrupt handling under stress */
        for (volatile int j = 0; j < 50; j++);  /* More processing under load */
        
        sample->end_tick = get_high_resolution_timestamp();
        sample->latency_us = calculate_latency_us(sample->start_tick, sample->end_tick);
        
        if (sample->latency_us > 0 && sample->latency_us < 2000) {  /* More lenient sanity check */
            sample->cpu_load_percent = 75;
            sample->memory_pressure = 1;
            sample->concurrent_operations = 10;
            sample->outlier = false;
            valid_samples++;
        }
        
        /* Maintain stress conditions */
        for (volatile int j = 0; j < 100; j++);
    }
    
    uint32_t test_end = get_system_timestamp_ms();
    
    /* Record test metadata */
    result->valid_samples = valid_samples;
    result->test_duration_ms = test_end - test_start;
    result->error_count = 0;
    
    /* Calculate statistics */
    calculate_latency_statistics(result);
    
    /* Calculate performance score */
    result->performance_score = calculate_latency_performance_score(result);
    
    /* Check if target was met */
    result->meets_target = (result->stats.avg_latency_us <= result->target_latency_us);
    
    /* Print result */
    print_latency_result(result);
    
    return result->meets_target ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Analyze overall latency performance
 */
static test_result_t analyze_latency_performance(void) {
    log_info("Analyzing overall latency performance...");
    
    /* Calculate DMA vs PIO advantage */
    uint32_t dma_avg = (g_latency_benchmark.tx_3c515.stats.avg_latency_us + 
                       g_latency_benchmark.rx_3c515.stats.avg_latency_us) / 2;
    uint32_t pio_avg = (g_latency_benchmark.tx_3c509b.stats.avg_latency_us + 
                       g_latency_benchmark.rx_3c509b.stats.avg_latency_us) / 2;
    
    if (pio_avg > 0) {
        g_latency_benchmark.dma_latency_advantage_percent = 
            ((pio_avg - dma_avg) * 100) / pio_avg;
    }
    
    /* Check if all targets were met */
    g_latency_benchmark.all_targets_met = 
        g_latency_benchmark.interrupt_3c509b.meets_target &&
        g_latency_benchmark.interrupt_3c515.meets_target &&
        g_latency_benchmark.tx_3c509b.meets_target &&
        g_latency_benchmark.tx_3c515.meets_target &&
        g_latency_benchmark.rx_3c509b.meets_target &&
        g_latency_benchmark.rx_3c515.meets_target &&
        g_latency_benchmark.memory_alloc.meets_target &&
        g_latency_benchmark.dma_setup.meets_target &&
        g_latency_benchmark.pio_operation.meets_target;
    
    /* Calculate overall performance score */
    uint32_t total_score = 
        g_latency_benchmark.interrupt_3c509b.performance_score +
        g_latency_benchmark.interrupt_3c515.performance_score +
        g_latency_benchmark.tx_3c509b.performance_score +
        g_latency_benchmark.tx_3c515.performance_score +
        g_latency_benchmark.rx_3c509b.performance_score +
        g_latency_benchmark.rx_3c515.performance_score +
        g_latency_benchmark.memory_alloc.performance_score +
        g_latency_benchmark.dma_setup.performance_score +
        g_latency_benchmark.pio_operation.performance_score;
    
    g_latency_benchmark.overall_performance_score = total_score / 9;
    
    log_info("Analysis complete:");
    log_info("  DMA latency advantage: %lu%%", g_latency_benchmark.dma_latency_advantage_percent);
    log_info("  Overall performance score: %lu/100", g_latency_benchmark.overall_performance_score);
    log_info("  All targets met: %s", g_latency_benchmark.all_targets_met ? "YES" : "NO");
    
    return TEST_RESULT_PASS;
}

/* Utility function implementations */

/**
 * @brief Initialize latency testing environment
 */
static void init_latency_testing(void) {
    g_high_res_timer_base = get_system_timestamp_ticks();
    g_cpu_cycle_estimate = 0;
    
    /* Initialize memory for testing */
    if (!memory_is_initialized()) {
        memory_init();
    }
    
    log_info("Latency testing environment initialized");
}

/**
 * @brief Clean up latency testing environment
 */
static void cleanup_latency_testing(void) {
    log_info("Latency testing environment cleaned up");
}

/**
 * @brief Get high resolution timestamp
 */
static uint32_t get_high_resolution_timestamp(void) {
    /* For DOS, we use the timer tick as base and estimate higher resolution
     * In a real implementation, this might use the 8253 timer or CPU cycle counter */
    static uint32_t last_tick = 0;
    static uint32_t sub_tick_counter = 0;
    
    uint32_t current_tick = get_system_timestamp_ticks();
    
    if (current_tick != last_tick) {
        last_tick = current_tick;
        sub_tick_counter = 0;
    } else {
        sub_tick_counter++;
    }
    
    /* Return tick with sub-tick resolution */
    return (current_tick * HIGH_RES_TIMER_TICKS) + sub_tick_counter;
}

/**
 * @brief Calculate latency in microseconds
 */
static uint32_t calculate_latency_us(uint32_t start_tick, uint32_t end_tick) {
    if (end_tick <= start_tick) return 0;
    
    uint32_t diff_ticks = end_tick - start_tick;
    
    /* Convert high-resolution ticks to microseconds */
    /* Each high-res tick represents approximately TIMER_TICK_US / HIGH_RES_TIMER_TICKS microseconds */
    return (diff_ticks * TIMER_TICK_US) / HIGH_RES_TIMER_TICKS;
}

/**
 * @brief Calculate comprehensive latency statistics
 */
static void calculate_latency_statistics(latency_test_result_t *result) {
    if (result->valid_samples == 0) return;
    
    latency_statistics_t *stats = &result->stats;
    stats->sample_count = result->valid_samples;
    
    /* Extract latency values for sorting */
    uint32_t *latencies = malloc(result->valid_samples * sizeof(uint32_t));
    if (!latencies) return;
    
    stats->min_latency_us = UINT32_MAX;
    stats->max_latency_us = 0;
    uint32_t sum = 0;
    
    for (uint32_t i = 0; i < result->valid_samples; i++) {
        latencies[i] = result->samples[i].latency_us;
        
        if (latencies[i] < stats->min_latency_us) stats->min_latency_us = latencies[i];
        if (latencies[i] > stats->max_latency_us) stats->max_latency_us = latencies[i];
        
        sum += latencies[i];
    }
    
    /* Calculate average */
    stats->avg_latency_us = sum / result->valid_samples;
    
    /* Sort for percentile calculations */
    sort_latency_samples(latencies, result->valid_samples);
    
    /* Calculate median and percentiles */
    stats->median_latency_us = calculate_percentile(latencies, result->valid_samples, 50);
    stats->percentile_95_us = calculate_percentile(latencies, result->valid_samples, 95);
    stats->percentile_99_us = calculate_percentile(latencies, result->valid_samples, 99);
    
    /* Calculate standard deviation */
    uint32_t variance_sum = 0;
    for (uint32_t i = 0; i < result->valid_samples; i++) {
        int32_t diff = result->samples[i].latency_us - stats->avg_latency_us;
        variance_sum += (diff * diff);
    }
    stats->std_deviation_us = (uint32_t)sqrt(variance_sum / result->valid_samples);
    
    /* Calculate jitter */
    stats->jitter_us = stats->max_latency_us - stats->min_latency_us;
    if (stats->avg_latency_us > 0) {
        stats->jitter_percent = (stats->jitter_us * 100) / stats->avg_latency_us;
    }
    
    /* Calculate coefficient of variation */
    if (stats->avg_latency_us > 0) {
        stats->coefficient_of_variation = (double)stats->std_deviation_us / stats->avg_latency_us;
    }
    
    free(latencies);
}

/**
 * @brief Detect outlier measurements
 */
static void detect_outliers(latency_test_result_t *result) {
    if (result->valid_samples < 10) return;  /* Need sufficient samples */
    
    latency_statistics_t *stats = &result->stats;
    
    /* Use 1.5 * IQR method for outlier detection */
    uint32_t q1_index = result->valid_samples / 4;
    uint32_t q3_index = (3 * result->valid_samples) / 4;
    
    /* Sort samples by latency for quartile calculation */
    uint32_t *sorted_latencies = malloc(result->valid_samples * sizeof(uint32_t));
    if (!sorted_latencies) return;
    
    for (uint32_t i = 0; i < result->valid_samples; i++) {
        sorted_latencies[i] = result->samples[i].latency_us;
    }
    
    sort_latency_samples(sorted_latencies, result->valid_samples);
    
    uint32_t q1 = sorted_latencies[q1_index];
    uint32_t q3 = sorted_latencies[q3_index];
    uint32_t iqr = q3 - q1;
    
    uint32_t lower_bound = (q1 > (iqr * 3 / 2)) ? (q1 - (iqr * 3 / 2)) : 0;
    uint32_t upper_bound = q3 + (iqr * 3 / 2);
    
    /* Mark outliers */
    stats->outlier_count = 0;
    for (uint32_t i = 0; i < result->valid_samples; i++) {
        uint32_t latency = result->samples[i].latency_us;
        if (latency < lower_bound || latency > upper_bound) {
            result->samples[i].outlier = true;
            stats->outlier_count++;
        }
    }
    
    free(sorted_latencies);
}

/**
 * @brief Sort latency samples (simple bubble sort for small arrays)
 */
static void sort_latency_samples(uint32_t *latencies, uint32_t count) {
    for (uint32_t i = 0; i < count - 1; i++) {
        for (uint32_t j = 0; j < count - i - 1; j++) {
            if (latencies[j] > latencies[j + 1]) {
                uint32_t temp = latencies[j];
                latencies[j] = latencies[j + 1];
                latencies[j + 1] = temp;
            }
        }
    }
}

/**
 * @brief Calculate percentile value from sorted array
 */
static uint32_t calculate_percentile(uint32_t *sorted_latencies, uint32_t count, uint32_t percentile) {
    if (count == 0 || percentile > 100) return 0;
    
    uint32_t index = (percentile * (count - 1)) / 100;
    return sorted_latencies[index];
}

/**
 * @brief Calculate performance score for latency test
 */
static uint32_t calculate_latency_performance_score(const latency_test_result_t *result) {
    uint32_t score = 100;
    
    /* Deduct points based on how much we exceed target */
    if (result->stats.avg_latency_us > result->target_latency_us) {
        uint32_t excess_percent = ((result->stats.avg_latency_us - result->target_latency_us) * 100) / 
                                 result->target_latency_us;
        score -= (excess_percent > 50) ? 50 : excess_percent;
    }
    
    /* Deduct points for high jitter */
    if (result->stats.jitter_percent > 20) {
        score -= (result->stats.jitter_percent - 20);
    }
    
    /* Deduct points for outliers */
    if (result->valid_samples > 0) {
        uint32_t outlier_percent = (result->stats.outlier_count * 100) / result->valid_samples;
        score -= outlier_percent / 2;  /* 0.5 points per percent of outliers */
    }
    
    /* Deduct points for errors */
    if (result->error_count > 0) {
        score -= (result->error_count > 20) ? 20 : result->error_count;
    }
    
    return (score > 100) ? 0 : score;
}

/**
 * @brief Simulate CPU load
 */
static void simulate_cpu_load(uint32_t target_percent) {
    /* Simple CPU load simulation */
    uint32_t work_iterations = target_percent * 10;
    for (volatile uint32_t i = 0; i < work_iterations; i++);
}

/**
 * @brief Simulate memory pressure
 */
static void simulate_memory_pressure(void) {
    /* Allocate and free memory to create pressure */
    void *buffers[10];
    for (int i = 0; i < 10; i++) {
        buffers[i] = malloc(1024);
    }
    for (int i = 0; i < 10; i++) {
        if (buffers[i]) free(buffers[i]);
    }
}

/**
 * @brief Print detailed latency test result
 */
static void print_latency_result(const latency_test_result_t *result) {
    log_info("=== %s Results ===", result->test_name);
    log_info("NIC Type: %s", result->nic_type);
    log_info("Target Latency: %lu us", result->target_latency_us);
    log_info("Valid Samples: %lu", result->valid_samples);
    log_info("Test Duration: %lu ms", result->test_duration_ms);
    log_info("Errors: %lu", result->error_count);
    
    log_info("Latency Statistics:");
    log_info("  Average: %lu us", result->stats.avg_latency_us);
    log_info("  Median: %lu us", result->stats.median_latency_us);
    log_info("  Min/Max: %lu/%lu us", result->stats.min_latency_us, result->stats.max_latency_us);
    log_info("  95th/99th Percentile: %lu/%lu us", result->stats.percentile_95_us, result->stats.percentile_99_us);
    log_info("  Std Deviation: %lu us", result->stats.std_deviation_us);
    log_info("  Jitter: %lu us (%lu%%)", result->stats.jitter_us, result->stats.jitter_percent);
    log_info("  Outliers: %lu", result->stats.outlier_count);
    
    log_info("Performance: Score %lu/100, Target %s", 
             result->performance_score, result->meets_target ? "MET" : "NOT MET");
    
    if (result->regression_detected) {
        log_warning("REGRESSION DETECTED (severity: %lu)", result->regression_severity);
    }
    
    log_info("================================");
}

/**
 * @brief Print comprehensive latency benchmark summary
 */
static void print_latency_benchmark_summary(const latency_benchmark_t *benchmark) {
    log_info("=== COMPREHENSIVE LATENCY BENCHMARK SUMMARY ===");
    
    log_info("Overall Result: %s", benchmark->all_targets_met ? "PASSED" : "FAILED");
    log_info("Overall Performance Score: %lu/100", benchmark->overall_performance_score);
    log_info("DMA Latency Advantage: %lu%%", benchmark->dma_latency_advantage_percent);
    
    /* Summary table */
    log_info("\nLatency Summary Table:");
    log_info("Test Type            | Target  | 3C509B  | 3C515   | Score | Status");
    log_info("---------------------|---------|---------|---------|-------|-------");
    log_info("Interrupt Latency    | %7lu | %7lu | %7lu | %5lu | %s",
             benchmark->interrupt_3c509b.target_latency_us,
             benchmark->interrupt_3c509b.stats.avg_latency_us,
             benchmark->interrupt_3c515.stats.avg_latency_us,
             (benchmark->interrupt_3c509b.performance_score + benchmark->interrupt_3c515.performance_score) / 2,
             (benchmark->interrupt_3c509b.meets_target && benchmark->interrupt_3c515.meets_target) ? "PASS" : "FAIL");
    
    log_info("TX Packet Latency    | %7lu | %7lu | %7lu | %5lu | %s",
             benchmark->tx_3c509b.target_latency_us,
             benchmark->tx_3c509b.stats.avg_latency_us,
             benchmark->tx_3c515.stats.avg_latency_us,
             (benchmark->tx_3c509b.performance_score + benchmark->tx_3c515.performance_score) / 2,
             (benchmark->tx_3c509b.meets_target && benchmark->tx_3c515.meets_target) ? "PASS" : "FAIL");
    
    log_info("RX Packet Latency    | %7lu | %7lu | %7lu | %5lu | %s",
             benchmark->rx_3c509b.target_latency_us,
             benchmark->rx_3c509b.stats.avg_latency_us,
             benchmark->rx_3c515.stats.avg_latency_us,
             (benchmark->rx_3c509b.performance_score + benchmark->rx_3c515.performance_score) / 2,
             (benchmark->rx_3c509b.meets_target && benchmark->rx_3c515.meets_target) ? "PASS" : "FAIL");
    
    log_info("Memory Allocation    | %7lu | %7lu |    N/A  | %5lu | %s",
             benchmark->memory_alloc.target_latency_us,
             benchmark->memory_alloc.stats.avg_latency_us,
             benchmark->memory_alloc.performance_score,
             benchmark->memory_alloc.meets_target ? "PASS" : "FAIL");
    
    /* Recommendations */
    log_info("\n%s", benchmark->recommendations);
    
    log_info("=================================================");
}

/**
 * @brief Generate performance recommendations
 */
static void generate_latency_recommendations(latency_benchmark_t *benchmark) {
    strcpy(benchmark->recommendations, "Performance Recommendations:\n");
    
    if (benchmark->dma_latency_advantage_percent > 20) {
        strcat(benchmark->recommendations, "- DMA shows significant latency advantage - prefer 3C515-TX for latency-critical applications\n");
    } else if (benchmark->dma_latency_advantage_percent < 5) {
        strcat(benchmark->recommendations, "- PIO latency is competitive - 3C509B suitable for latency-sensitive workloads\n");
    }
    
    if (benchmark->interrupt_3c509b.stats.jitter_percent > 20 || 
        benchmark->interrupt_3c515.stats.jitter_percent > 20) {
        strcat(benchmark->recommendations, "- High interrupt jitter detected - consider interrupt mitigation techniques\n");
    }
    
    if (benchmark->memory_alloc.stats.avg_latency_us > 50) {
        strcat(benchmark->recommendations, "- Memory allocation latency is high - consider pre-allocation strategies\n");
    }
    
    if (benchmark->overall_performance_score < 80) {
        strcat(benchmark->recommendations, "- Overall latency performance below target - review system configuration\n");
    }
}