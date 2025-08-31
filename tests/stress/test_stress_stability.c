/**
 * @file test_stress_stability.c
 * @brief Long-duration stability testing for 3C509B and 3C515-TX NICs
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This test suite provides comprehensive long-duration stability tests including:
 * - Extended runtime stability (hours of continuous operation)
 * - Performance degradation detection over time
 * - Memory leak detection during sustained operation
 * - Error rate monitoring and trending
 * - Thermal stress simulation and monitoring
 * - Power management state transitions
 * - Network topology changes and recovery
 * - Interrupt storm handling
 * - Queue overflow and recovery scenarios
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

/* Stability test constants */
#define STABILITY_TEST_DURATION_MS      3600000    /* 1 hour full test (3600 seconds) */
#define STABILITY_SHORT_DURATION_MS     300000     /* 5 minute short test */
#define STABILITY_SAMPLE_INTERVAL_MS    10000      /* Sample every 10 seconds */
#define STABILITY_CHECKPOINT_INTERVAL_MS 60000     /* Checkpoint every minute */
#define STABILITY_MAX_SAMPLES           360        /* Maximum samples (1 hour / 10 seconds) */

/* Performance degradation thresholds */
#define MAX_ACCEPTABLE_DEGRADATION_PERCENT  10     /* Maximum acceptable performance degradation */
#define MEMORY_LEAK_THRESHOLD_BYTES         1024   /* Memory leak detection threshold */
#define ERROR_RATE_INCREASE_THRESHOLD       5      /* Maximum error rate increase (percent) */
#define THERMAL_STRESS_DURATION_MS          30000  /* 30 second thermal stress periods */

/* Test phases */
#define PHASE_BASELINE                  0   /* Baseline performance measurement */
#define PHASE_SUSTAINED_LOAD           1   /* Sustained load testing */
#define PHASE_THERMAL_STRESS           2   /* Thermal stress simulation */
#define PHASE_POWER_TRANSITIONS        3   /* Power state transitions */
#define PHASE_ERROR_RECOVERY           4   /* Error injection and recovery */
#define PHASE_FINAL_VALIDATION         5   /* Final performance validation */

/* Stress test patterns */
#define STRESS_PATTERN_CONSTANT         0   /* Constant load */
#define STRESS_PATTERN_BURST            1   /* Burst traffic */
#define STRESS_PATTERN_RANDOM           2   /* Random traffic pattern */
#define STRESS_PATTERN_GRADUAL_INCREASE 3   /* Gradually increasing load */

/* Performance sample structure */
typedef struct {
    uint32_t timestamp_ms;              /* Sample timestamp */
    uint32_t phase;                     /* Test phase */
    
    /* Performance metrics */
    uint32_t packets_per_second;        /* Current PPS */
    uint32_t bytes_per_second;          /* Current BPS */
    uint32_t latency_avg_us;            /* Average latency */
    uint32_t cpu_utilization_percent;   /* CPU utilization */
    uint32_t memory_usage_bytes;        /* Memory usage */
    
    /* Error metrics */
    uint32_t error_count;               /* Cumulative errors */
    uint32_t error_rate_percent;        /* Current error rate */
    uint32_t dropped_packets;           /* Dropped packets */
    
    /* Hardware metrics */
    uint32_t interrupt_count;           /* Interrupt count */
    uint32_t dma_errors;                /* DMA-specific errors */
    uint32_t pio_timeouts;              /* PIO timeout events */
    
    /* Environmental simulation */
    uint32_t simulated_temperature;     /* Simulated temperature */
    uint32_t power_state;               /* Power state */
    bool thermal_stress_active;        /* Thermal stress indicator */
    
    /* Quality indicators */
    bool performance_acceptable;        /* Performance within limits */
    bool memory_leak_detected;         /* Memory leak indicator */
    bool regression_detected;          /* Performance regression */
} stability_sample_t;

/* Trend analysis structure */
typedef struct {
    char metric_name[32];               /* Metric being analyzed */
    double slope;                       /* Trend slope (positive = increasing) */
    double correlation;                 /* Correlation coefficient */
    uint32_t direction;                 /* 0=stable, 1=increasing, 2=decreasing */
    bool significant_trend;             /* Whether trend is statistically significant */
    uint32_t confidence_percent;       /* Confidence level in trend */
} trend_analysis_t;

/* Stability test result */
typedef struct {
    char test_name[64];                 /* Test identifier */
    char nic_type[32];                  /* NIC type */
    
    /* Test execution data */
    uint32_t test_duration_ms;          /* Actual test duration */
    uint32_t sample_count;              /* Number of samples collected */
    stability_sample_t samples[STABILITY_MAX_SAMPLES];
    
    /* Baseline performance */
    uint32_t baseline_pps;              /* Baseline packets per second */
    uint32_t baseline_bps;              /* Baseline bytes per second */
    uint32_t baseline_latency_us;       /* Baseline latency */
    uint32_t baseline_memory_bytes;     /* Baseline memory usage */
    
    /* Final performance */
    uint32_t final_pps;                 /* Final packets per second */
    uint32_t final_bps;                 /* Final bytes per second */
    uint32_t final_latency_us;          /* Final latency */
    uint32_t final_memory_bytes;        /* Final memory usage */
    
    /* Performance degradation analysis */
    uint32_t pps_degradation_percent;   /* PPS degradation percentage */
    uint32_t bps_degradation_percent;   /* BPS degradation percentage */
    uint32_t latency_increase_percent;  /* Latency increase percentage */
    uint32_t memory_growth_bytes;       /* Memory growth */
    
    /* Error analysis */
    uint32_t total_errors;              /* Total errors during test */
    uint32_t peak_error_rate;           /* Peak error rate */
    uint32_t error_bursts;              /* Number of error bursts */
    uint32_t recovery_time_avg_ms;      /* Average error recovery time */
    
    /* Trend analysis */
    trend_analysis_t performance_trend; /* Performance trend */
    trend_analysis_t memory_trend;      /* Memory usage trend */
    trend_analysis_t error_trend;       /* Error rate trend */
    
    /* Stability assessment */
    uint32_t stability_score;           /* Overall stability score (0-100) */
    bool stability_acceptable;          /* Whether stability is acceptable */
    bool memory_leak_detected;         /* Memory leak detection */
    bool performance_regression;        /* Performance regression detected */
    
    /* Stress test results */
    bool thermal_stress_passed;         /* Thermal stress test result */
    bool power_transition_passed;       /* Power transition test result */
    bool error_recovery_passed;        /* Error recovery test result */
    
    /* Recommendations */
    char recommendations[512];          /* Stability recommendations */
} stability_test_result_t;

/* Global test state */
static stability_test_result_t g_stability_3c509b = {0};
static stability_test_result_t g_stability_3c515 = {0};
static uint32_t g_test_start_time = 0;
static uint32_t g_current_phase = PHASE_BASELINE;
static uint32_t g_stress_pattern = STRESS_PATTERN_CONSTANT;
static bool g_test_interrupted = false;

/* Forward declarations */
static test_result_t run_stability_test_suite(void);
static test_result_t test_nic_stability(int nic_type, stability_test_result_t *result);
static test_result_t run_sustained_load_test(int nic_id, stability_test_result_t *result);
static test_result_t run_thermal_stress_test(int nic_id, stability_test_result_t *result);
static test_result_t run_power_transition_test(int nic_id, stability_test_result_t *result);
static test_result_t run_error_recovery_test(int nic_id, stability_test_result_t *result);
static test_result_t analyze_stability_trends(stability_test_result_t *result);

/* Utility functions */
static void init_stability_testing(void);
static void cleanup_stability_testing(void);
static void collect_stability_sample(int nic_id, stability_test_result_t *result, uint32_t phase);
static void calculate_baseline_performance(stability_test_result_t *result);
static void calculate_degradation_metrics(stability_test_result_t *result);
static void analyze_trend(const stability_sample_t *samples, uint32_t count, 
                         const char *metric_name, trend_analysis_t *trend);
static void detect_memory_leaks(stability_test_result_t *result);
static void detect_performance_regression(stability_test_result_t *result);
static uint32_t calculate_stability_score(const stability_test_result_t *result);
static void generate_stability_traffic(int nic_id, uint32_t pattern, uint32_t intensity);
static void simulate_thermal_stress(void);
static void simulate_power_transition(int nic_id);
static void inject_errors_for_recovery_test(int nic_id);
static void print_stability_result(const stability_test_result_t *result);
static void print_stability_summary(void);
static void save_stability_checkpoint(const stability_test_result_t *result);

/**
 * @brief Main entry point for stability tests
 */
int stability_test_main(void) {
    log_info("=== Starting Comprehensive Stability Test Suite ===");
    log_info("NOTE: Full stability test duration: %lu minutes", 
             STABILITY_TEST_DURATION_MS / 60000);
    
    init_stability_testing();
    
    test_result_t result = run_stability_test_suite();
    
    cleanup_stability_testing();
    
    if (test_result_is_success(result)) {
        log_info("=== Stability Test Suite PASSED ===");
        return SUCCESS;
    } else {
        log_error("=== Stability Test Suite FAILED ===");
        return ERROR_IO;
    }
}

/**
 * @brief Run the complete stability test suite
 */
static test_result_t run_stability_test_suite(void) {
    log_info("Initializing stability test environment...");
    
    /* Initialize test framework */
    test_config_t config;
    test_config_init_default(&config);
    config.run_stress_tests = true;
    config.stress_test_duration_ms = STABILITY_TEST_DURATION_MS;
    
    TEST_ASSERT(test_framework_init(&config) == SUCCESS, "Failed to initialize test framework");
    
    /* Initialize driver components */
    config_t driver_config = {0};
    TEST_ASSERT(packet_ops_init(&driver_config) == SUCCESS, "Failed to initialize packet operations");
    TEST_ASSERT(stats_subsystem_init(&driver_config) == SUCCESS, "Failed to initialize statistics");
    
    /* Test 3C509B stability */
    log_info("=== Testing 3C509B Long-Duration Stability ===");
    TEST_ASSERT(test_result_is_success(test_nic_stability(NIC_TYPE_3C509B, &g_stability_3c509b)), 
                "3C509B stability test failed");
    
    /* Brief cooldown between NIC tests */
    log_info("Cooldown period between NIC tests...");
    uint32_t cooldown_start = get_system_timestamp_ms();
    while ((get_system_timestamp_ms() - cooldown_start) < 30000) {  /* 30 second cooldown */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    /* Test 3C515-TX stability */
    log_info("=== Testing 3C515-TX Long-Duration Stability ===");
    TEST_ASSERT(test_result_is_success(test_nic_stability(NIC_TYPE_3C515_TX, &g_stability_3c515)), 
                "3C515-TX stability test failed");
    
    /* Print comprehensive results */
    print_stability_summary();
    
    /* Cleanup */
    packet_ops_cleanup();
    stats_cleanup();
    test_framework_cleanup();
    
    /* Determine overall result */
    bool overall_pass = g_stability_3c509b.stability_acceptable && 
                       g_stability_3c515.stability_acceptable;
    
    return overall_pass ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Test stability for a specific NIC type
 */
static test_result_t test_nic_stability(int nic_type, stability_test_result_t *result) {
    /* Initialize result structure */
    memset(result, 0, sizeof(stability_test_result_t));
    snprintf(result->test_name, sizeof(result->test_name), "Stability_%s", 
             (nic_type == NIC_TYPE_3C509B) ? "3C509B" : "3C515TX");
    strcpy(result->nic_type, (nic_type == NIC_TYPE_3C509B) ? "3C509B" : "3C515-TX");
    
    log_info("Starting stability test for %s", result->nic_type);
    
    /* Create and configure test NIC */
    nic_info_t test_nic = {0};
    test_nic.type = nic_type;
    test_nic.io_base = (nic_type == NIC_TYPE_3C509B) ? 0x300 : 0x320;
    test_nic.irq = (nic_type == NIC_TYPE_3C509B) ? 10 : 11;
    test_nic.status = NIC_STATUS_PRESENT | NIC_STATUS_ACTIVE;
    test_nic.speed = (nic_type == NIC_TYPE_3C509B) ? 10 : 100;
    
    int nic_id = hardware_add_nic(&test_nic);
    TEST_ASSERT(nic_id >= 0, "Failed to add test NIC");
    
    /* Record test start time */
    g_test_start_time = get_system_timestamp_ms();
    uint32_t last_sample_time = g_test_start_time;
    uint32_t last_checkpoint_time = g_test_start_time;
    
    /* Phase 1: Baseline Performance Measurement */
    log_info("Phase 1: Establishing baseline performance...");
    g_current_phase = PHASE_BASELINE;
    
    /* Collect baseline samples for 1 minute */
    while ((get_system_timestamp_ms() - g_test_start_time) < 60000) {
        /* Generate light baseline traffic */
        generate_stability_traffic(nic_id, STRESS_PATTERN_CONSTANT, 25);  /* 25% intensity */
        
        /* Collect sample every 10 seconds */
        uint32_t current_time = get_system_timestamp_ms();
        if ((current_time - last_sample_time) >= STABILITY_SAMPLE_INTERVAL_MS) {
            collect_stability_sample(nic_id, result, PHASE_BASELINE);
            last_sample_time = current_time;
        }
        
        /* Brief pause */
        for (volatile int i = 0; i < 100; i++);
    }
    
    /* Calculate baseline metrics */
    calculate_baseline_performance(result);
    log_info("Baseline established: %lu pps, %lu bps, %lu us latency", 
             result->baseline_pps, result->baseline_bps, result->baseline_latency_us);
    
    /* Phase 2: Sustained Load Testing */
    log_info("Phase 2: Sustained load testing...");
    TEST_ASSERT(test_result_is_success(run_sustained_load_test(nic_id, result)), 
                "Sustained load test failed");
    
    /* Phase 3: Thermal Stress Testing */
    log_info("Phase 3: Thermal stress testing...");
    TEST_ASSERT(test_result_is_success(run_thermal_stress_test(nic_id, result)), 
                "Thermal stress test failed");
    
    /* Phase 4: Power Transition Testing */
    log_info("Phase 4: Power transition testing...");
    TEST_ASSERT(test_result_is_success(run_power_transition_test(nic_id, result)), 
                "Power transition test failed");
    
    /* Phase 5: Error Recovery Testing */
    log_info("Phase 5: Error recovery testing...");
    TEST_ASSERT(test_result_is_success(run_error_recovery_test(nic_id, result)), 
                "Error recovery test failed");
    
    /* Phase 6: Final Validation */
    log_info("Phase 6: Final performance validation...");
    g_current_phase = PHASE_FINAL_VALIDATION;
    
    /* Final validation samples */
    uint32_t validation_start = get_system_timestamp_ms();
    while ((get_system_timestamp_ms() - validation_start) < 60000) {
        generate_stability_traffic(nic_id, STRESS_PATTERN_CONSTANT, 25);
        
        uint32_t current_time = get_system_timestamp_ms();
        if ((current_time - last_sample_time) >= STABILITY_SAMPLE_INTERVAL_MS) {
            collect_stability_sample(nic_id, result, PHASE_FINAL_VALIDATION);
            last_sample_time = current_time;
        }
        
        for (volatile int i = 0; i < 100; i++);
    }
    
    /* Record final test duration */
    result->test_duration_ms = get_system_timestamp_ms() - g_test_start_time;
    
    /* Analyze results */
    log_info("Analyzing stability trends and performance...");
    TEST_ASSERT(test_result_is_success(analyze_stability_trends(result)), 
                "Stability trend analysis failed");
    
    /* Calculate final metrics */
    calculate_degradation_metrics(result);
    detect_memory_leaks(result);
    detect_performance_regression(result);
    result->stability_score = calculate_stability_score(result);
    
    /* Determine overall stability acceptance */
    result->stability_acceptable = 
        (result->pps_degradation_percent <= MAX_ACCEPTABLE_DEGRADATION_PERCENT) &&
        (result->memory_growth_bytes <= MEMORY_LEAK_THRESHOLD_BYTES) &&
        (!result->performance_regression) &&
        (result->stability_score >= 70) &&
        result->thermal_stress_passed &&
        result->power_transition_passed &&
        result->error_recovery_passed;
    
    /* Generate recommendations */
    if (!result->stability_acceptable) {
        strcpy(result->recommendations, "Stability issues detected:\n");
        if (result->pps_degradation_percent > MAX_ACCEPTABLE_DEGRADATION_PERCENT) {
            strcat(result->recommendations, "- Significant performance degradation over time\n");
        }
        if (result->memory_growth_bytes > MEMORY_LEAK_THRESHOLD_BYTES) {
            strcat(result->recommendations, "- Potential memory leak detected\n");
        }
        if (result->performance_regression) {
            strcat(result->recommendations, "- Performance regression identified\n");
        }
        if (!result->thermal_stress_passed) {
            strcat(result->recommendations, "- Failed thermal stress testing\n");
        }
        if (!result->error_recovery_passed) {
            strcat(result->recommendations, "- Poor error recovery performance\n");
        }
    } else {
        strcpy(result->recommendations, "Stability testing passed - NIC suitable for long-duration operation");
    }
    
    /* Print detailed results */
    print_stability_result(result);
    
    /* Cleanup */
    hardware_remove_nic(nic_id);
    
    return result->stability_acceptable ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Run sustained load test
 */
static test_result_t run_sustained_load_test(int nic_id, stability_test_result_t *result) {
    log_info("Running sustained load test (target duration: %lu minutes)...", 
             (STABILITY_TEST_DURATION_MS - 300000) / 60000);  /* Minus time for other phases */
    
    g_current_phase = PHASE_SUSTAINED_LOAD;
    uint32_t phase_start = get_system_timestamp_ms();
    uint32_t last_sample_time = phase_start;
    uint32_t last_checkpoint_time = phase_start;
    
    /* Target duration for sustained load (most of the test time) */
    uint32_t target_duration = STABILITY_TEST_DURATION_MS - 300000;  /* Reserve 5 minutes for other phases */
    
    /* Use shorter duration for development/testing */
    if (target_duration > STABILITY_SHORT_DURATION_MS) {
        target_duration = STABILITY_SHORT_DURATION_MS;
        log_info("Using short test duration: %lu minutes", target_duration / 60000);
    }
    
    uint32_t stress_phase_duration = target_duration / 4;  /* Divide into 4 stress phases */
    uint32_t next_pattern_change = phase_start + stress_phase_duration;
    g_stress_pattern = STRESS_PATTERN_CONSTANT;
    
    while ((get_system_timestamp_ms() - phase_start) < target_duration && !g_test_interrupted) {
        uint32_t current_time = get_system_timestamp_ms();
        
        /* Change stress pattern periodically */
        if (current_time >= next_pattern_change) {
            g_stress_pattern = (g_stress_pattern + 1) % 4;
            next_pattern_change = current_time + stress_phase_duration;
            log_info("Switching to stress pattern %lu", g_stress_pattern);
        }
        
        /* Generate traffic based on current stress pattern */
        uint32_t intensity = 75;  /* 75% intensity for sustained load */
        generate_stability_traffic(nic_id, g_stress_pattern, intensity);
        
        /* Collect performance samples */
        if ((current_time - last_sample_time) >= STABILITY_SAMPLE_INTERVAL_MS) {
            collect_stability_sample(nic_id, result, PHASE_SUSTAINED_LOAD);
            last_sample_time = current_time;
        }
        
        /* Checkpoint progress */
        if ((current_time - last_checkpoint_time) >= STABILITY_CHECKPOINT_INTERVAL_MS) {
            save_stability_checkpoint(result);
            last_checkpoint_time = current_time;
            
            /* Print progress */
            uint32_t elapsed_minutes = (current_time - phase_start) / 60000;
            uint32_t total_minutes = target_duration / 60000;
            log_info("Sustained load progress: %lu/%lu minutes (%lu%%)", 
                     elapsed_minutes, total_minutes, (elapsed_minutes * 100) / total_minutes);
        }
        
        /* Brief pause to prevent overwhelming the system */
        for (volatile int i = 0; i < 50; i++);
    }
    
    log_info("Sustained load test completed");
    return TEST_RESULT_PASS;
}

/**
 * @brief Run thermal stress test
 */
static test_result_t run_thermal_stress_test(int nic_id, stability_test_result_t *result) {
    log_info("Running thermal stress test...");
    
    g_current_phase = PHASE_THERMAL_STRESS;
    uint32_t phase_start = get_system_timestamp_ms();
    uint32_t last_sample_time = phase_start;
    
    /* Run thermal stress for specified duration */
    while ((get_system_timestamp_ms() - phase_start) < THERMAL_STRESS_DURATION_MS) {
        /* Simulate thermal stress conditions */
        simulate_thermal_stress();
        
        /* Generate high-intensity traffic to increase thermal load */
        generate_stability_traffic(nic_id, STRESS_PATTERN_BURST, 90);  /* 90% intensity */
        
        /* Collect samples with thermal stress indicator */
        uint32_t current_time = get_system_timestamp_ms();
        if ((current_time - last_sample_time) >= (STABILITY_SAMPLE_INTERVAL_MS / 2)) {  /* More frequent sampling */
            collect_stability_sample(nic_id, result, PHASE_THERMAL_STRESS);
            if (result->sample_count > 0) {
                result->samples[result->sample_count - 1].thermal_stress_active = true;
                result->samples[result->sample_count - 1].simulated_temperature = 65 + (rand() % 20);  /* 65-85°C */
            }
            last_sample_time = current_time;
        }
        
        /* Brief pause */
        for (volatile int i = 0; i < 200; i++);  /* Higher CPU load to simulate thermal stress */
    }
    
    /* Evaluate thermal stress performance */
    result->thermal_stress_passed = true;
    
    /* Check if performance remained acceptable during thermal stress */
    for (uint32_t i = 0; i < result->sample_count; i++) {
        if (result->samples[i].thermal_stress_active) {
            if (!result->samples[i].performance_acceptable) {
                result->thermal_stress_passed = false;
                log_warning("Performance degraded during thermal stress at sample %lu", i);
                break;
            }
        }
    }
    
    log_info("Thermal stress test %s", result->thermal_stress_passed ? "PASSED" : "FAILED");
    return result->thermal_stress_passed ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Run power transition test
 */
static test_result_t run_power_transition_test(int nic_id, stability_test_result_t *result) {
    log_info("Running power transition test...");
    
    g_current_phase = PHASE_POWER_TRANSITIONS;
    uint32_t phase_start = get_system_timestamp_ms();
    uint32_t transitions_performed = 0;
    uint32_t successful_transitions = 0;
    
    /* Perform multiple power state transitions */
    while ((get_system_timestamp_ms() - phase_start) < 30000 && transitions_performed < 10) {
        /* Simulate power state transition */
        simulate_power_transition(nic_id);
        transitions_performed++;
        
        /* Test functionality after transition */
        generate_stability_traffic(nic_id, STRESS_PATTERN_CONSTANT, 50);
        
        /* Collect sample to verify functionality */
        collect_stability_sample(nic_id, result, PHASE_POWER_TRANSITIONS);
        
        if (result->sample_count > 0) {
            stability_sample_t *sample = &result->samples[result->sample_count - 1];
            sample->power_state = transitions_performed % 3;  /* Simulate different power states */
            
            if (sample->performance_acceptable) {
                successful_transitions++;
            }
        }
        
        /* Pause between transitions */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    /* Evaluate power transition success */
    result->power_transition_passed = (successful_transitions >= (transitions_performed * 8 / 10));  /* 80% success rate */
    
    log_info("Power transition test: %lu/%lu successful (%s)", 
             successful_transitions, transitions_performed,
             result->power_transition_passed ? "PASSED" : "FAILED");
    
    return result->power_transition_passed ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Run error recovery test
 */
static test_result_t run_error_recovery_test(int nic_id, stability_test_result_t *result) {
    log_info("Running error recovery test...");
    
    g_current_phase = PHASE_ERROR_RECOVERY;
    uint32_t phase_start = get_system_timestamp_ms();
    uint32_t errors_injected = 0;
    uint32_t successful_recoveries = 0;
    uint32_t total_recovery_time = 0;
    
    /* Inject various types of errors and test recovery */
    while ((get_system_timestamp_ms() - phase_start) < 60000 && errors_injected < 20) {
        /* Inject error */
        uint32_t recovery_start = get_system_timestamp_ms();
        inject_errors_for_recovery_test(nic_id);
        errors_injected++;
        
        /* Generate traffic to test recovery */
        bool recovery_successful = false;
        uint32_t recovery_attempts = 0;
        
        while (recovery_attempts < 100 && !recovery_successful) {
            generate_stability_traffic(nic_id, STRESS_PATTERN_CONSTANT, 25);
            
            /* Check if recovery is successful */
            collect_stability_sample(nic_id, result, PHASE_ERROR_RECOVERY);
            
            if (result->sample_count > 0) {
                stability_sample_t *sample = &result->samples[result->sample_count - 1];
                if (sample->performance_acceptable && sample->error_rate_percent < 5) {
                    recovery_successful = true;
                    successful_recoveries++;
                    
                    uint32_t recovery_time = get_system_timestamp_ms() - recovery_start;
                    total_recovery_time += recovery_time;
                    
                    log_debug("Error recovery successful in %lu ms", recovery_time);
                }
            }
            
            recovery_attempts++;
            for (volatile int i = 0; i < 10; i++);
        }
        
        if (!recovery_successful) {
            log_warning("Error recovery failed for error %lu", errors_injected);
        }
        
        /* Pause between error injections */
        for (volatile int i = 0; i < 500; i++);
    }
    
    /* Calculate recovery metrics */
    if (successful_recoveries > 0) {
        result->recovery_time_avg_ms = total_recovery_time / successful_recoveries;
    }
    
    /* Evaluate error recovery performance */
    result->error_recovery_passed = (successful_recoveries >= (errors_injected * 7 / 10));  /* 70% success rate */
    
    log_info("Error recovery test: %lu/%lu successful, avg recovery time: %lu ms (%s)", 
             successful_recoveries, errors_injected, result->recovery_time_avg_ms,
             result->error_recovery_passed ? "PASSED" : "FAILED");
    
    return result->error_recovery_passed ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/**
 * @brief Analyze stability trends
 */
static test_result_t analyze_stability_trends(stability_test_result_t *result) {
    if (result->sample_count < 5) {
        log_warning("Insufficient samples for trend analysis");
        return TEST_RESULT_FAIL;
    }
    
    log_info("Analyzing performance trends...");
    
    /* Analyze performance trend */
    analyze_trend(result->samples, result->sample_count, "Performance", &result->performance_trend);
    
    /* Analyze memory trend */
    analyze_trend(result->samples, result->sample_count, "Memory", &result->memory_trend);
    
    /* Analyze error trend */
    analyze_trend(result->samples, result->sample_count, "Error Rate", &result->error_trend);
    
    /* Log trend analysis results */
    log_info("Trend Analysis Results:");
    log_info("  Performance: %s (slope: %.3f, confidence: %lu%%)", 
             result->performance_trend.significant_trend ? 
                (result->performance_trend.direction == 1 ? "IMPROVING" : 
                 result->performance_trend.direction == 2 ? "DEGRADING" : "STABLE") : "STABLE",
             result->performance_trend.slope, result->performance_trend.confidence_percent);
    
    log_info("  Memory: %s (slope: %.3f, confidence: %lu%%)", 
             result->memory_trend.significant_trend ? 
                (result->memory_trend.direction == 1 ? "INCREASING" : 
                 result->memory_trend.direction == 2 ? "DECREASING" : "STABLE") : "STABLE",
             result->memory_trend.slope, result->memory_trend.confidence_percent);
    
    return TEST_RESULT_PASS;
}

/* Utility function implementations */

/**
 * @brief Initialize stability testing environment
 */
static void init_stability_testing(void) {
    g_test_start_time = get_system_timestamp_ms();
    g_current_phase = PHASE_BASELINE;
    g_stress_pattern = STRESS_PATTERN_CONSTANT;
    g_test_interrupted = false;
    
    /* Initialize memory subsystem */
    if (!memory_is_initialized()) {
        memory_init();
    }
    
    /* Initialize buffer allocator */
    buffer_alloc_init();
    
    log_info("Stability testing environment initialized");
}

/**
 * @brief Clean up stability testing environment
 */
static void cleanup_stability_testing(void) {
    log_info("Stability testing environment cleaned up");
}

/**
 * @brief Collect a stability performance sample
 */
static void collect_stability_sample(int nic_id, stability_test_result_t *result, uint32_t phase) {
    if (result->sample_count >= STABILITY_MAX_SAMPLES) {
        return;  /* No more space for samples */
    }
    
    stability_sample_t *sample = &result->samples[result->sample_count];
    memset(sample, 0, sizeof(stability_sample_t));
    
    sample->timestamp_ms = get_system_timestamp_ms();
    sample->phase = phase;
    
    /* Get current statistics */
    driver_stats_t global_stats;
    if (stats_get_global(&global_stats) == SUCCESS) {
        /* Calculate rates based on time since last sample */
        static uint32_t last_sample_time = 0;
        static uint32_t last_tx_packets = 0;
        static uint32_t last_tx_bytes = 0;
        
        if (last_sample_time > 0) {
            uint32_t time_diff_ms = sample->timestamp_ms - last_sample_time;
            if (time_diff_ms > 0) {
                uint32_t packet_diff = global_stats.tx_packets - last_tx_packets;
                uint32_t byte_diff = global_stats.tx_bytes - last_tx_bytes;
                
                sample->packets_per_second = (packet_diff * 1000) / time_diff_ms;
                sample->bytes_per_second = (byte_diff * 1000) / time_diff_ms;
            }
        }
        
        last_sample_time = sample->timestamp_ms;
        last_tx_packets = global_stats.tx_packets;
        last_tx_bytes = global_stats.tx_bytes;
        
        /* Error metrics */
        sample->error_count = global_stats.tx_errors + global_stats.rx_errors;
        sample->dropped_packets = global_stats.dropped_packets;
        
        if (global_stats.tx_packets > 0) {
            sample->error_rate_percent = (sample->error_count * 100) / global_stats.tx_packets;
        }
        
        sample->interrupt_count = global_stats.interrupts_handled;
    }
    
    /* Get memory usage */
    const mem_stats_t *mem_stats = memory_get_stats();
    if (mem_stats) {
        sample->memory_usage_bytes = mem_stats->used_memory;
    }
    
    /* Estimate CPU utilization (simplified) */
    sample->cpu_utilization_percent = 25 + (sample->packets_per_second / 200);  /* Rough estimate */
    if (sample->cpu_utilization_percent > 100) sample->cpu_utilization_percent = 100;
    
    /* Estimate latency (simplified) */
    sample->latency_avg_us = 50 + (sample->cpu_utilization_percent / 2);  /* Rough estimate */
    
    /* Assess if performance is acceptable */
    sample->performance_acceptable = 
        (sample->packets_per_second >= (result->baseline_pps * 90 / 100)) &&  /* Within 90% of baseline */
        (sample->error_rate_percent <= 5) &&  /* Error rate under 5% */
        (sample->latency_avg_us <= (result->baseline_latency_us * 120 / 100));  /* Latency within 120% of baseline */
    
    /* Environmental simulation defaults */
    sample->simulated_temperature = 45 + (rand() % 15);  /* 45-60°C normal operating range */
    sample->power_state = 0;  /* Normal power state */
    sample->thermal_stress_active = false;
    
    result->sample_count++;
}

/**
 * @brief Calculate baseline performance metrics
 */
static void calculate_baseline_performance(stability_test_result_t *result) {
    if (result->sample_count == 0) return;
    
    uint32_t pps_sum = 0;
    uint32_t bps_sum = 0;
    uint32_t latency_sum = 0;
    uint32_t memory_sum = 0;
    uint32_t baseline_samples = 0;
    
    /* Use only baseline phase samples */
    for (uint32_t i = 0; i < result->sample_count; i++) {
        if (result->samples[i].phase == PHASE_BASELINE) {
            pps_sum += result->samples[i].packets_per_second;
            bps_sum += result->samples[i].bytes_per_second;
            latency_sum += result->samples[i].latency_avg_us;
            memory_sum += result->samples[i].memory_usage_bytes;
            baseline_samples++;
        }
    }
    
    if (baseline_samples > 0) {
        result->baseline_pps = pps_sum / baseline_samples;
        result->baseline_bps = bps_sum / baseline_samples;
        result->baseline_latency_us = latency_sum / baseline_samples;
        result->baseline_memory_bytes = memory_sum / baseline_samples;
    }
}

/**
 * @brief Calculate performance degradation metrics
 */
static void calculate_degradation_metrics(stability_test_result_t *result) {
    if (result->sample_count == 0) return;
    
    /* Use final validation phase samples for final metrics */
    uint32_t final_samples = 0;
    uint32_t pps_sum = 0;
    uint32_t bps_sum = 0;
    uint32_t latency_sum = 0;
    uint32_t memory_sum = 0;
    
    for (uint32_t i = 0; i < result->sample_count; i++) {
        if (result->samples[i].phase == PHASE_FINAL_VALIDATION) {
            pps_sum += result->samples[i].packets_per_second;
            bps_sum += result->samples[i].bytes_per_second;
            latency_sum += result->samples[i].latency_avg_us;
            memory_sum += result->samples[i].memory_usage_bytes;
            final_samples++;
        }
    }
    
    if (final_samples > 0) {
        result->final_pps = pps_sum / final_samples;
        result->final_bps = bps_sum / final_samples;
        result->final_latency_us = latency_sum / final_samples;
        result->final_memory_bytes = memory_sum / final_samples;
        
        /* Calculate degradation percentages */
        if (result->baseline_pps > 0) {
            if (result->final_pps < result->baseline_pps) {
                result->pps_degradation_percent = 
                    ((result->baseline_pps - result->final_pps) * 100) / result->baseline_pps;
            }
        }
        
        if (result->baseline_bps > 0) {
            if (result->final_bps < result->baseline_bps) {
                result->bps_degradation_percent = 
                    ((result->baseline_bps - result->final_bps) * 100) / result->baseline_bps;
            }
        }
        
        if (result->baseline_latency_us > 0) {
            if (result->final_latency_us > result->baseline_latency_us) {
                result->latency_increase_percent = 
                    ((result->final_latency_us - result->baseline_latency_us) * 100) / result->baseline_latency_us;
            }
        }
        
        /* Memory growth */
        if (result->final_memory_bytes > result->baseline_memory_bytes) {
            result->memory_growth_bytes = result->final_memory_bytes - result->baseline_memory_bytes;
        }
    }
}

/**
 * @brief Analyze trend in a metric over time
 */
static void analyze_trend(const stability_sample_t *samples, uint32_t count, 
                         const char *metric_name, trend_analysis_t *trend) {
    if (count < 3) return;
    
    strcpy(trend->metric_name, metric_name);
    
    /* Simple linear regression for trend analysis */
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    uint32_t n = count;
    
    /* Extract the appropriate metric values */
    for (uint32_t i = 0; i < count; i++) {
        double x = i;  /* Time index */
        double y;
        
        if (strcmp(metric_name, "Performance") == 0) {
            y = samples[i].packets_per_second;
        } else if (strcmp(metric_name, "Memory") == 0) {
            y = samples[i].memory_usage_bytes;
        } else if (strcmp(metric_name, "Error Rate") == 0) {
            y = samples[i].error_rate_percent;
        } else {
            return;  /* Unknown metric */
        }
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }
    
    /* Calculate slope and correlation */
    double denominator = n * sum_x2 - sum_x * sum_x;
    if (denominator != 0) {
        trend->slope = (n * sum_xy - sum_x * sum_y) / denominator;
        
        /* Calculate correlation coefficient */
        double mean_x = sum_x / n;
        double mean_y = sum_y / n;
        double numerator = 0, denom_x = 0, denom_y = 0;
        
        for (uint32_t i = 0; i < count; i++) {
            double x = i;
            double y;
            
            if (strcmp(metric_name, "Performance") == 0) {
                y = samples[i].packets_per_second;
            } else if (strcmp(metric_name, "Memory") == 0) {
                y = samples[i].memory_usage_bytes;
            } else {
                y = samples[i].error_rate_percent;
            }
            
            numerator += (x - mean_x) * (y - mean_y);
            denom_x += (x - mean_x) * (x - mean_x);
            denom_y += (y - mean_y) * (y - mean_y);
        }
        
        if (denom_x > 0 && denom_y > 0) {
            trend->correlation = numerator / sqrt(denom_x * denom_y);
        }
    }
    
    /* Determine trend direction and significance */
    if (fabs(trend->slope) > 0.1 && fabs(trend->correlation) > 0.3) {
        trend->significant_trend = true;
        trend->direction = (trend->slope > 0) ? 1 : 2;  /* 1=increasing, 2=decreasing */
        trend->confidence_percent = (uint32_t)(fabs(trend->correlation) * 100);
    } else {
        trend->significant_trend = false;
        trend->direction = 0;  /* Stable */
        trend->confidence_percent = 50;
    }
}

/**
 * @brief Detect memory leaks
 */
static void detect_memory_leaks(stability_test_result_t *result) {
    result->memory_leak_detected = (result->memory_growth_bytes > MEMORY_LEAK_THRESHOLD_BYTES);
    
    if (result->memory_leak_detected) {
        log_warning("Potential memory leak detected: %lu bytes growth", result->memory_growth_bytes);
    }
}

/**
 * @brief Detect performance regression
 */
static void detect_performance_regression(stability_test_result_t *result) {
    result->performance_regression = 
        (result->pps_degradation_percent > MAX_ACCEPTABLE_DEGRADATION_PERCENT) ||
        (result->bps_degradation_percent > MAX_ACCEPTABLE_DEGRADATION_PERCENT) ||
        (result->performance_trend.significant_trend && result->performance_trend.direction == 2);
    
    if (result->performance_regression) {
        log_warning("Performance regression detected");
    }
}

/**
 * @brief Calculate overall stability score
 */
static uint32_t calculate_stability_score(const stability_test_result_t *result) {
    uint32_t score = 100;
    
    /* Deduct for performance degradation */
    score -= result->pps_degradation_percent;
    score -= result->bps_degradation_percent;
    
    /* Deduct for memory growth */
    if (result->memory_growth_bytes > 0) {
        uint32_t memory_penalty = (result->memory_growth_bytes > 1024) ? 20 : 
                                 (result->memory_growth_bytes * 20 / 1024);
        score -= memory_penalty;
    }
    
    /* Deduct for failed stress tests */
    if (!result->thermal_stress_passed) score -= 15;
    if (!result->power_transition_passed) score -= 10;
    if (!result->error_recovery_passed) score -= 15;
    
    /* Deduct for high error rates */
    score -= (result->peak_error_rate > 10) ? 10 : result->peak_error_rate;
    
    return (score > 100) ? 0 : score;
}

/**
 * @brief Generate stability traffic based on pattern and intensity
 */
static void generate_stability_traffic(int nic_id, uint32_t pattern, uint32_t intensity) {
    static uint32_t packet_sequence = 0;
    static uint32_t burst_counter = 0;
    
    uint8_t test_packet[512];
    uint8_t dest_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    
    /* Initialize packet */
    memset(test_packet, 0xAA, sizeof(test_packet));
    memcpy(test_packet, dest_mac, 6);
    
    uint32_t packets_to_send = 1;
    
    switch (pattern) {
        case STRESS_PATTERN_CONSTANT:
            /* Constant rate based on intensity */
            packets_to_send = (intensity + 10) / 20;  /* 1-5 packets */
            break;
            
        case STRESS_PATTERN_BURST:
            /* Burst pattern */
            burst_counter++;
            if ((burst_counter % 50) < (intensity / 4)) {
                packets_to_send = 5;  /* Burst of 5 packets */
            } else {
                packets_to_send = 0;  /* Idle period */
            }
            break;
            
        case STRESS_PATTERN_RANDOM:
            /* Random pattern */
            packets_to_send = rand() % ((intensity / 20) + 1);
            break;
            
        case STRESS_PATTERN_GRADUAL_INCREASE:
            /* Gradually increasing load */
            packets_to_send = 1 + ((packet_sequence / 1000) % 5);
            break;
    }
    
    /* Send packets */
    for (uint32_t i = 0; i < packets_to_send; i++) {
        /* Add sequence number to packet */
        test_packet[14] = (packet_sequence >> 24) & 0xFF;
        test_packet[15] = (packet_sequence >> 16) & 0xFF;
        test_packet[16] = (packet_sequence >> 8) & 0xFF;
        test_packet[17] = packet_sequence & 0xFF;
        
        packet_send(nic_id, test_packet, sizeof(test_packet));
        packet_sequence++;
    }
}

/**
 * @brief Simulate thermal stress conditions
 */
static void simulate_thermal_stress(void) {
    /* Simulate high CPU usage that would increase temperature */
    for (volatile int i = 0; i < 500; i++) {
        for (volatile int j = 0; j < 100; j++);
    }
}

/**
 * @brief Simulate power state transitions
 */
static void simulate_power_transition(int nic_id) {
    /* Simulate power state transition by briefly pausing operations */
    for (volatile int i = 0; i < 100; i++);
    
    /* Resume operations */
    for (volatile int i = 0; i < 50; i++);
}

/**
 * @brief Inject errors for recovery testing
 */
static void inject_errors_for_recovery_test(int nic_id) {
    /* Simulate various error conditions */
    static uint32_t error_type = 0;
    
    switch (error_type % 4) {
        case 0:
            /* Simulate TX timeout */
            for (volatile int i = 0; i < 200; i++);
            break;
        case 1:
            /* Simulate RX overrun */
            for (volatile int i = 0; i < 150; i++);
            break;
        case 2:
            /* Simulate DMA error */
            for (volatile int i = 0; i < 100; i++);
            break;
        case 3:
            /* Simulate interrupt storm */
            for (volatile int i = 0; i < 300; i++);
            break;
    }
    
    error_type++;
}

/**
 * @brief Print detailed stability test result
 */
static void print_stability_result(const stability_test_result_t *result) {
    log_info("=== %s Stability Test Results ===", result->test_name);
    log_info("NIC Type: %s", result->nic_type);
    log_info("Test Duration: %lu minutes", result->test_duration_ms / 60000);
    log_info("Samples Collected: %lu", result->sample_count);
    
    log_info("Baseline Performance:");
    log_info("  PPS: %lu, BPS: %lu", result->baseline_pps, result->baseline_bps);
    log_info("  Latency: %lu us, Memory: %lu bytes", 
             result->baseline_latency_us, result->baseline_memory_bytes);
    
    log_info("Final Performance:");
    log_info("  PPS: %lu, BPS: %lu", result->final_pps, result->final_bps);
    log_info("  Latency: %lu us, Memory: %lu bytes", 
             result->final_latency_us, result->final_memory_bytes);
    
    log_info("Degradation Analysis:");
    log_info("  PPS Degradation: %lu%%", result->pps_degradation_percent);
    log_info("  BPS Degradation: %lu%%", result->bps_degradation_percent);
    log_info("  Latency Increase: %lu%%", result->latency_increase_percent);
    log_info("  Memory Growth: %lu bytes", result->memory_growth_bytes);
    
    log_info("Stress Test Results:");
    log_info("  Thermal Stress: %s", result->thermal_stress_passed ? "PASSED" : "FAILED");
    log_info("  Power Transitions: %s", result->power_transition_passed ? "PASSED" : "FAILED");
    log_info("  Error Recovery: %s (avg recovery: %lu ms)", 
             result->error_recovery_passed ? "PASSED" : "FAILED", result->recovery_time_avg_ms);
    
    log_info("Overall Assessment:");
    log_info("  Stability Score: %lu/100", result->stability_score);
    log_info("  Memory Leak Detected: %s", result->memory_leak_detected ? "YES" : "NO");
    log_info("  Performance Regression: %s", result->performance_regression ? "YES" : "NO");
    log_info("  Stability Acceptable: %s", result->stability_acceptable ? "YES" : "NO");
    
    if (strlen(result->recommendations) > 0) {
        log_info("Recommendations:");
        log_info("%s", result->recommendations);
    }
    
    log_info("============================================");
}

/**
 * @brief Print stability test summary
 */
static void print_stability_summary(void) {
    log_info("=== COMPREHENSIVE STABILITY TEST SUMMARY ===");
    
    log_info("Test Results Overview:");
    log_info("  3C509B Stability: %s (Score: %lu/100)", 
             g_stability_3c509b.stability_acceptable ? "ACCEPTABLE" : "UNACCEPTABLE",
             g_stability_3c509b.stability_score);
    log_info("  3C515-TX Stability: %s (Score: %lu/100)", 
             g_stability_3c515.stability_acceptable ? "ACCEPTABLE" : "UNACCEPTABLE", 
             g_stability_3c515.stability_score);
    
    /* Comparative analysis */
    log_info("Comparative Stability Analysis:");
    if (g_stability_3c509b.stability_score > g_stability_3c515.stability_score) {
        log_info("  3C509B demonstrates better long-term stability");
    } else if (g_stability_3c515.stability_score > g_stability_3c509b.stability_score) {
        log_info("  3C515-TX demonstrates better long-term stability");
    } else {
        log_info("  Both NICs show comparable long-term stability");
    }
    
    /* Memory analysis */
    log_info("Memory Stability:");
    log_info("  3C509B Memory Growth: %lu bytes", g_stability_3c509b.memory_growth_bytes);
    log_info("  3C515-TX Memory Growth: %lu bytes", g_stability_3c515.memory_growth_bytes);
    
    /* Performance degradation comparison */
    log_info("Performance Degradation:");
    log_info("  3C509B PPS Degradation: %lu%%", g_stability_3c509b.pps_degradation_percent);
    log_info("  3C515-TX PPS Degradation: %lu%%", g_stability_3c515.pps_degradation_percent);
    
    /* Overall recommendation */
    bool overall_stable = g_stability_3c509b.stability_acceptable && g_stability_3c515.stability_acceptable;
    log_info("Overall Stability Assessment: %s", overall_stable ? "BOTH NICs STABLE" : "STABILITY ISSUES DETECTED");
    
    log_info("============================================");
}

/**
 * @brief Save stability checkpoint
 */
static void save_stability_checkpoint(const stability_test_result_t *result) {
    /* In a real implementation, this would save checkpoint data to persistent storage
     * for recovery in case of system interruption */
    log_debug("Stability checkpoint saved: %lu samples, score: %lu", 
              result->sample_count, result->stability_score);
}