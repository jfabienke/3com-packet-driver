/**
 * @file perf_framework.c
 * @brief Performance testing framework implementation with DOS timer integration
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This implementation provides comprehensive performance testing capabilities
 * specifically designed for DOS environments with accurate timing and
 * statistical analysis.
 */

#include "perf_framework.h"
#include "../../include/logging.h"
#include "../../src/c/timestamp.c"  /* Include timestamp functions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dos.h>

/* Global framework state */
static perf_framework_state_t g_perf_framework = {0};

/* Timer calibration constants */
#define CALIBRATION_ITERATIONS      100
#define CALIBRATION_DURATION_MS     1000
#define DOS_TIMER_FREQ_HZ           18.2

/* Statistical constants */
#define OUTLIER_IQR_MULTIPLIER      1.5
#define MIN_SAMPLES_FOR_STATS       5
#define TREND_MIN_CORRELATION       0.7

/* Forward declarations of internal functions */
static int calibrate_timer_precision(void);
static int validate_framework_state(void);
static uint32_t interpolate_sub_tick_timing(uint32_t start_tick, uint32_t end_tick);
static double calculate_t_statistic(double sample_mean, double population_mean, 
                                   double sample_std, uint32_t sample_size);
static int detect_measurement_outliers(perf_sample_t *samples, uint32_t count);
static int sort_double_array(double *array, uint32_t count);

/**
 * @brief Initialize the performance framework
 */
int perf_framework_init(void) {
    if (g_perf_framework.framework_initialized) {
        return PERF_SUCCESS;  /* Already initialized */
    }
    
    log_info("Initializing performance testing framework...");
    
    /* Clear framework state */
    memset(&g_perf_framework, 0, sizeof(perf_framework_state_t));
    
    /* Initialize framework timestamp */
    g_perf_framework.framework_uptime_ms = get_system_timestamp_ms();
    
    /* Calibrate timer precision */
    int calibration_result = calibrate_timer_precision();
    if (calibration_result != PERF_SUCCESS) {
        log_error("Timer calibration failed: %d", calibration_result);
        return PERF_ERR_CALIBRATION_FAILED;
    }
    
    /* Set default configuration */
    g_perf_framework.cpu_frequency_estimate = PERF_CPU_FREQ_ESTIMATE;
    g_perf_framework.next_benchmark_id = 1;
    
    g_perf_framework.framework_initialized = true;
    
    log_info("Performance framework initialized successfully");
    log_info("Timer calibration factor: %lu", g_perf_framework.timer_calibration_factor);
    log_info("CPU frequency estimate: %lu Hz", g_perf_framework.cpu_frequency_estimate);
    
    return PERF_SUCCESS;
}

/**
 * @brief Clean up the performance framework
 */
int perf_framework_cleanup(void) {
    if (!g_perf_framework.framework_initialized) {
        return PERF_ERR_NOT_INITIALIZED;
    }
    
    log_info("Cleaning up performance framework...");
    
    /* Finalize any active benchmarks */
    for (uint32_t i = 0; i < g_perf_framework.active_benchmarks; i++) {
        if (g_perf_framework.benchmarks[i].test_active) {
            perf_benchmark_finalize(&g_perf_framework.benchmarks[i]);
        }
    }
    
    /* Print framework summary */
    perf_report_framework_summary();
    
    /* Clear framework state */
    g_perf_framework.framework_initialized = false;
    
    log_info("Performance framework cleanup completed");
    return PERF_SUCCESS;
}

/**
 * @brief Calibrate timer precision for accurate measurements
 */
int perf_framework_calibrate_timer(void) {
    return calibrate_timer_precision();
}

/**
 * @brief Check if framework is initialized
 */
bool perf_framework_is_initialized(void) {
    return g_perf_framework.framework_initialized;
}

/**
 * @brief Start a high-precision timer
 */
int perf_timer_start(perf_timer_t *timer, uint32_t precision_level) {
    if (!timer) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    if (!g_perf_framework.framework_initialized) {
        return PERF_ERR_NOT_INITIALIZED;
    }
    
    memset(timer, 0, sizeof(perf_timer_t));
    timer->precision_level = precision_level;
    
    /* Get baseline DOS timer tick */
    timer->dos_tick_start = get_system_timestamp_ticks();
    
    /* For higher precision, add sub-tick interpolation */
    if (precision_level >= PERF_PRECISION_MEDIUM) {
        /* Use interpolation based on repeated timer reads */
        uint32_t tick1 = get_system_timestamp_ticks();
        uint32_t tick2 = get_system_timestamp_ticks();
        
        if (tick2 == tick1) {
            /* Same tick - we're in sub-tick precision range */
            timer->interpolation_factor = 0;
        } else {
            /* Different tick - estimate position within tick */
            timer->interpolation_factor = 1;
        }
    }
    
    /* For highest precision, estimate CPU cycles */
    if (precision_level >= PERF_PRECISION_HIGH) {
        /* Simple CPU cycle estimation start point */
        timer->cpu_cycle_estimate = 0;  /* Will be calculated in stop */
    }
    
    return PERF_SUCCESS;
}

/**
 * @brief Stop a high-precision timer
 */
int perf_timer_stop(perf_timer_t *timer) {
    if (!timer) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Get end timing */
    timer->dos_tick_end = get_system_timestamp_ticks();
    
    /* Calculate sub-tick interpolation for higher precision */
    if (timer->precision_level >= PERF_PRECISION_MEDIUM) {
        uint32_t interpolation = interpolate_sub_tick_timing(timer->dos_tick_start, 
                                                           timer->dos_tick_end);
        timer->interpolation_factor += interpolation;
    }
    
    /* Estimate CPU cycles for highest precision */
    if (timer->precision_level >= PERF_PRECISION_HIGH) {
        uint32_t elapsed_us = perf_timer_get_elapsed_us(timer);
        timer->cpu_cycle_estimate = perf_estimate_cpu_cycles(elapsed_us);
    }
    
    return PERF_SUCCESS;
}

/**
 * @brief Get elapsed time in microseconds
 */
uint32_t perf_timer_get_elapsed_us(const perf_timer_t *timer) {
    if (!timer) {
        return 0;
    }
    
    /* Calculate basic elapsed ticks */
    uint32_t elapsed_ticks;
    if (timer->dos_tick_end >= timer->dos_tick_start) {
        elapsed_ticks = timer->dos_tick_end - timer->dos_tick_start;
    } else {
        /* Handle timer rollover at midnight */
        elapsed_ticks = (0x1800B0L - timer->dos_tick_start) + timer->dos_tick_end;
    }
    
    /* Convert to microseconds */
    uint32_t elapsed_us = (elapsed_ticks * PERF_TIMER_RESOLUTION_US);
    
    /* Apply sub-tick interpolation for higher precision */
    if (timer->precision_level >= PERF_PRECISION_MEDIUM && timer->interpolation_factor > 0) {
        /* Adjust based on interpolation factor */
        elapsed_us += (timer->interpolation_factor * PERF_TIMER_RESOLUTION_US) / 10;
    }
    
    /* Apply calibration factor */
    if (g_perf_framework.timer_calibrated) {
        elapsed_us = (elapsed_us * g_perf_framework.timer_calibration_factor) / 1000;
    }
    
    return elapsed_us;
}

/**
 * @brief Get elapsed CPU cycles estimate
 */
uint32_t perf_timer_get_elapsed_cycles(const perf_timer_t *timer) {
    if (!timer || timer->precision_level < PERF_PRECISION_HIGH) {
        return 0;
    }
    
    return timer->cpu_cycle_estimate;
}

/**
 * @brief Create a new performance benchmark
 */
int perf_benchmark_create(const char *name, uint32_t test_type, perf_benchmark_t **benchmark) {
    if (!name || !benchmark) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    if (!g_perf_framework.framework_initialized) {
        return PERF_ERR_NOT_INITIALIZED;
    }
    
    if (g_perf_framework.active_benchmarks >= PERF_MAX_BENCHMARKS) {
        return PERF_ERR_OUT_OF_MEMORY;
    }
    
    /* Find free benchmark slot */
    perf_benchmark_t *new_benchmark = NULL;
    for (uint32_t i = 0; i < PERF_MAX_BENCHMARKS; i++) {
        if (!g_perf_framework.benchmarks[i].test_active) {
            new_benchmark = &g_perf_framework.benchmarks[i];
            break;
        }
    }
    
    if (!new_benchmark) {
        return PERF_ERR_OUT_OF_MEMORY;
    }
    
    /* Initialize benchmark */
    memset(new_benchmark, 0, sizeof(perf_benchmark_t));
    strncpy(new_benchmark->benchmark_name, name, sizeof(new_benchmark->benchmark_name) - 1);
    new_benchmark->test_type = test_type;
    new_benchmark->precision_level = PERF_PRECISION_MEDIUM;  /* Default precision */
    new_benchmark->target_sample_count = 100;               /* Default sample count */
    new_benchmark->max_duration_ms = 30000;                 /* Default 30 second limit */
    new_benchmark->warmup_duration_ms = 1000;               /* Default 1 second warmup */
    new_benchmark->cooldown_duration_ms = 500;              /* Default 0.5 second cooldown */
    
    g_perf_framework.active_benchmarks++;
    *benchmark = new_benchmark;
    
    log_info("Created performance benchmark: %s (type: %lu)", name, test_type);
    return PERF_SUCCESS;
}

/**
 * @brief Destroy a performance benchmark
 */
int perf_benchmark_destroy(perf_benchmark_t *benchmark) {
    if (!benchmark) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    if (benchmark->test_active) {
        perf_benchmark_finalize(benchmark);
    }
    
    memset(benchmark, 0, sizeof(perf_benchmark_t));
    g_perf_framework.active_benchmarks--;
    
    return PERF_SUCCESS;
}

/**
 * @brief Start a performance benchmark
 */
int perf_benchmark_start(perf_benchmark_t *benchmark) {
    if (!benchmark) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    if (benchmark->test_active) {
        return PERF_ERR_INVALID_PARAM;  /* Already active */
    }
    
    benchmark->test_start_time = get_system_timestamp_ms();
    benchmark->test_active = true;
    benchmark->samples_collected = 0;
    benchmark->invalid_samples = 0;
    benchmark->outlier_samples = 0;
    
    log_info("Started benchmark: %s", benchmark->benchmark_name);
    return PERF_SUCCESS;
}

/**
 * @brief Stop a performance benchmark
 */
int perf_benchmark_stop(perf_benchmark_t *benchmark) {
    if (!benchmark || !benchmark->test_active) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    benchmark->test_active = false;
    
    log_info("Stopped benchmark: %s (%lu samples collected)", 
             benchmark->benchmark_name, benchmark->samples_collected);
    
    return PERF_SUCCESS;
}

/**
 * @brief Add a sample to a performance benchmark
 */
int perf_benchmark_add_sample(perf_benchmark_t *benchmark, const perf_sample_t *sample) {
    if (!benchmark || !sample) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    if (!benchmark->test_active) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    if (benchmark->samples_collected >= PERF_MAX_SAMPLES) {
        return PERF_ERR_OUT_OF_MEMORY;
    }
    
    /* Validate sample */
    if (perf_sample_validate(sample) != PERF_SUCCESS) {
        benchmark->invalid_samples++;
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Add sample to benchmark */
    benchmark->samples[benchmark->samples_collected] = *sample;
    benchmark->samples[benchmark->samples_collected].sample_id = benchmark->samples_collected;
    benchmark->samples_collected++;
    
    g_perf_framework.total_samples_collected++;
    
    /* Check if benchmark should be automatically finalized */
    if (benchmark->samples_collected >= benchmark->target_sample_count) {
        uint32_t elapsed_ms = get_system_timestamp_ms() - benchmark->test_start_time;
        if (elapsed_ms >= benchmark->max_duration_ms) {
            log_info("Auto-finalizing benchmark due to duration limit");
            perf_benchmark_finalize(benchmark);
        }
    }
    
    return PERF_SUCCESS;
}

/**
 * @brief Finalize a performance benchmark and calculate statistics
 */
int perf_benchmark_finalize(perf_benchmark_t *benchmark) {
    if (!benchmark) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    if (benchmark->samples_collected < MIN_SAMPLES_FOR_STATS) {
        log_warning("Insufficient samples for statistical analysis: %lu", 
                   benchmark->samples_collected);
        return PERF_ERR_INSUFFICIENT_DATA;
    }
    
    log_info("Finalizing benchmark: %s", benchmark->benchmark_name);
    
    /* Detect and mark outliers */
    detect_measurement_outliers(benchmark->samples, benchmark->samples_collected);
    
    /* Calculate statistics */
    int stats_result = perf_statistics_calculate(benchmark->samples, 
                                               benchmark->samples_collected,
                                               &benchmark->statistics);
    if (stats_result != PERF_SUCCESS) {
        log_error("Statistical calculation failed: %d", stats_result);
        return stats_result;
    }
    
    /* Calculate measurement stability */
    if (benchmark->statistics.std_deviation > 0 && benchmark->statistics.mean > 0) {
        benchmark->measurement_stability = 
            1.0 - (benchmark->statistics.coefficient_variation / 100.0);
    } else {
        benchmark->measurement_stability = 1.0;
    }
    
    /* Perform regression analysis if baseline exists */
    if (benchmark->baseline) {
        int regression_result = perf_baseline_compare(benchmark->baseline,
                                                    &benchmark->statistics,
                                                    &benchmark->regression);
        if (regression_result == PERF_ERR_REGRESSION_DETECTED) {
            g_perf_framework.total_regressions_detected++;
            log_warning("Performance regression detected in benchmark: %s", 
                       benchmark->benchmark_name);
        }
    }
    
    benchmark->test_active = false;
    
    log_info("Benchmark finalized: %s (stability: %.2f)", 
             benchmark->benchmark_name, benchmark->measurement_stability);
    
    return PERF_SUCCESS;
}

/**
 * @brief Create a performance sample
 */
int perf_sample_create(perf_sample_t *sample, uint32_t value, const perf_timer_t *timer) {
    if (!sample || !timer) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    memset(sample, 0, sizeof(perf_sample_t));
    
    sample->timestamp = get_system_timestamp_ms();
    sample->value = value;
    sample->timer = *timer;
    sample->operations_count = 1;  /* Default to single operation */
    
    /* Calculate derived metrics */
    sample->latency_us = perf_timer_get_elapsed_us(timer);
    
    if (sample->latency_us > 0) {
        sample->throughput_pps = (sample->operations_count * 1000000) / sample->latency_us;
    }
    
    /* Default quality indicators */
    sample->measurement_valid = true;
    sample->outlier_detected = false;
    sample->confidence_level = 95;  /* Default confidence */
    
    /* Enhance sample with additional context */
    perf_sample_enhance(sample);
    
    return PERF_SUCCESS;
}

/**
 * @brief Validate a performance sample
 */
int perf_sample_validate(const perf_sample_t *sample) {
    if (!sample) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Check for basic validity */
    if (!sample->measurement_valid) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Check for reasonable latency values */
    if (sample->latency_us > 10000000) {  /* > 10 seconds seems unreasonable */
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Check timer consistency */
    if (sample->timer.dos_tick_end < sample->timer.dos_tick_start) {
        /* Allow for timer rollover, but check magnitude */
        uint32_t rollover_diff = (0x1800B0L - sample->timer.dos_tick_start) + 
                                sample->timer.dos_tick_end;
        if (rollover_diff > 0x1800B0L / 2) {  /* > 12 hours seems wrong */
            return PERF_ERR_INVALID_PARAM;
        }
    }
    
    return PERF_SUCCESS;
}

/**
 * @brief Enhance a sample with additional context information
 */
int perf_sample_enhance(perf_sample_t *sample) {
    if (!sample) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Estimate CPU utilization based on operation timing */
    if (sample->latency_us > 0) {
        /* Simple heuristic - higher latency suggests higher CPU load */
        sample->cpu_utilization = (sample->latency_us / 1000) * 2;  /* Rough estimate */
        if (sample->cpu_utilization > 100) sample->cpu_utilization = 100;
    }
    
    /* Estimate system load factor */
    sample->system_load_factor = sample->cpu_utilization / 10;  /* 0-10 scale */
    
    /* Estimate memory bandwidth */
    if (sample->operations_count > 0 && sample->latency_us > 0) {
        /* Assume average operation touches 1KB of memory */
        uint32_t memory_bytes = sample->operations_count * 1024;
        sample->memory_bandwidth = (memory_bytes * 1000000) / sample->latency_us;  /* Bytes/sec */
    }
    
    return PERF_SUCCESS;
}

/**
 * @brief Calculate comprehensive statistics for performance samples
 */
int perf_statistics_calculate(const perf_sample_t *samples, uint32_t count, 
                             perf_statistics_t *stats) {
    if (!samples || !stats || count < MIN_SAMPLES_FOR_STATS) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    memset(stats, 0, sizeof(perf_statistics_t));
    stats->sample_count = count;
    
    /* Extract values for statistical calculation */
    double *values = malloc(count * sizeof(double));
    if (!values) {
        return PERF_ERR_OUT_OF_MEMORY;
    }
    
    uint32_t valid_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (samples[i].measurement_valid && !samples[i].outlier_detected) {
            values[valid_count] = (double)samples[i].value;
            valid_count++;
        }
    }
    
    if (valid_count < MIN_SAMPLES_FOR_STATS) {
        free(values);
        return PERF_ERR_INSUFFICIENT_DATA;
    }
    
    /* Calculate basic statistics */
    stats->mean = perf_calculate_mean(values, valid_count);
    stats->std_deviation = perf_calculate_std_deviation(values, valid_count, stats->mean);
    stats->variance = stats->std_deviation * stats->std_deviation;
    
    /* Find min/max */
    stats->min_value = values[0];
    stats->max_value = values[0];
    for (uint32_t i = 1; i < valid_count; i++) {
        if (values[i] < stats->min_value) stats->min_value = values[i];
        if (values[i] > stats->max_value) stats->max_value = values[i];
    }
    stats->range = stats->max_value - stats->min_value;
    
    /* Calculate coefficient of variation */
    if (stats->mean > 0) {
        stats->coefficient_variation = (stats->std_deviation / stats->mean) * 100.0;
    }
    
    /* Sort values for percentile calculations */
    sort_double_array(values, valid_count);
    
    /* Calculate percentiles */
    stats->median = perf_calculate_percentile(values, valid_count, 50);
    stats->percentile_25 = perf_calculate_percentile(values, valid_count, 25);
    stats->percentile_75 = perf_calculate_percentile(values, valid_count, 75);
    stats->percentile_90 = perf_calculate_percentile(values, valid_count, 90);
    stats->percentile_95 = perf_calculate_percentile(values, valid_count, 95);
    stats->percentile_99 = perf_calculate_percentile(values, valid_count, 99);
    
    /* Calculate outlier thresholds using IQR method */
    double iqr = stats->percentile_75 - stats->percentile_25;
    stats->outlier_threshold_low = stats->percentile_25 - (OUTLIER_IQR_MULTIPLIER * iqr);
    stats->outlier_threshold_high = stats->percentile_75 + (OUTLIER_IQR_MULTIPLIER * iqr);
    
    /* Count outliers */
    stats->outlier_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (samples[i].outlier_detected) {
            stats->outlier_count++;
        }
    }
    
    /* Calculate confidence intervals (using t-distribution approximation) */
    if (valid_count > 1) {
        double standard_error = stats->std_deviation / sqrt(valid_count);
        double t_90 = 1.645;  /* Approximate t-value for 90% confidence */
        double t_95 = 1.96;   /* Approximate t-value for 95% confidence */
        
        stats->confidence_interval_90_low = stats->mean - (t_90 * standard_error);
        stats->confidence_interval_90_high = stats->mean + (t_90 * standard_error);
        stats->confidence_interval_95_low = stats->mean - (t_95 * standard_error);
        stats->confidence_interval_95_high = stats->mean + (t_95 * standard_error);
    }
    
    /* Analyze trend */
    perf_statistics_analyze_trend(samples, count, &stats->trend_slope, &stats->trend_correlation);
    stats->trend_significant = (fabs(stats->trend_correlation) >= TREND_MIN_CORRELATION);
    
    free(values);
    return PERF_SUCCESS;
}

/**
 * @brief Detect outliers in performance samples
 */
int perf_statistics_detect_outliers(perf_sample_t *samples, uint32_t count, double threshold) {
    if (!samples || count < MIN_SAMPLES_FOR_STATS) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    return detect_measurement_outliers(samples, count);
}

/**
 * @brief Analyze trend in performance samples
 */
int perf_statistics_analyze_trend(const perf_sample_t *samples, uint32_t count,
                                 double *slope, double *correlation) {
    if (!samples || !slope || !correlation || count < MIN_SAMPLES_FOR_STATS) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Simple linear regression analysis */
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    uint32_t valid_count = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        if (samples[i].measurement_valid && !samples[i].outlier_detected) {
            double x = (double)valid_count;  /* Sample index as x */
            double y = (double)samples[i].value;  /* Sample value as y */
            
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_x2 += x * x;
            valid_count++;
        }
    }
    
    if (valid_count < 2) {
        *slope = 0.0;
        *correlation = 0.0;
        return PERF_ERR_INSUFFICIENT_DATA;
    }
    
    /* Calculate slope */
    double denominator = valid_count * sum_x2 - sum_x * sum_x;
    if (fabs(denominator) < 1e-10) {
        *slope = 0.0;
        *correlation = 0.0;
    } else {
        *slope = (valid_count * sum_xy - sum_x * sum_y) / denominator;
        
        /* Calculate correlation coefficient */
        double mean_x = sum_x / valid_count;
        double mean_y = sum_y / valid_count;
        double numerator = 0, denom_x = 0, denom_y = 0;
        
        uint32_t idx = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (samples[i].measurement_valid && !samples[i].outlier_detected) {
                double x = (double)idx;
                double y = (double)samples[i].value;
                
                numerator += (x - mean_x) * (y - mean_y);
                denom_x += (x - mean_x) * (x - mean_x);
                denom_y += (y - mean_y) * (y - mean_y);
                idx++;
            }
        }
        
        if (denom_x > 0 && denom_y > 0) {
            *correlation = numerator / sqrt(denom_x * denom_y);
        } else {
            *correlation = 0.0;
        }
    }
    
    return PERF_SUCCESS;
}

/**
 * @brief Calculate confidence interval for statistics
 */
int perf_statistics_calculate_confidence_interval(const perf_statistics_t *stats,
                                                 uint32_t confidence_level,
                                                 double *lower, double *upper) {
    if (!stats || !lower || !upper) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    switch (confidence_level) {
        case 90:
            *lower = stats->confidence_interval_90_low;
            *upper = stats->confidence_interval_90_high;
            break;
        case 95:
            *lower = stats->confidence_interval_95_low;
            *upper = stats->confidence_interval_95_high;
            break;
        default:
            return PERF_ERR_INVALID_PARAM;
    }
    
    return PERF_SUCCESS;
}

/* Utility function implementations */

/**
 * @brief Calculate percentile value from sorted array
 */
double perf_calculate_percentile(const double *sorted_values, uint32_t count, uint32_t percentile) {
    if (!sorted_values || count == 0 || percentile > 100) {
        return 0.0;
    }
    
    if (percentile == 0) return sorted_values[0];
    if (percentile == 100) return sorted_values[count - 1];
    
    double index = (percentile / 100.0) * (count - 1);
    uint32_t lower_index = (uint32_t)floor(index);
    uint32_t upper_index = (uint32_t)ceil(index);
    
    if (lower_index == upper_index) {
        return sorted_values[lower_index];
    } else {
        double weight = index - lower_index;
        return sorted_values[lower_index] * (1.0 - weight) + sorted_values[upper_index] * weight;
    }
}

/**
 * @brief Calculate arithmetic mean
 */
double perf_calculate_mean(const double *values, uint32_t count) {
    if (!values || count == 0) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        sum += values[i];
    }
    
    return sum / count;
}

/**
 * @brief Calculate standard deviation
 */
double perf_calculate_std_deviation(const double *values, uint32_t count, double mean) {
    if (!values || count < 2) {
        return 0.0;
    }
    
    double sum_squared_diffs = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        double diff = values[i] - mean;
        sum_squared_diffs += diff * diff;
    }
    
    return sqrt(sum_squared_diffs / (count - 1));
}

/**
 * @brief Estimate CPU cycles from elapsed microseconds
 */
uint32_t perf_estimate_cpu_cycles(uint32_t elapsed_us) {
    /* Simple estimation based on framework's CPU frequency estimate */
    return (elapsed_us * g_perf_framework.cpu_frequency_estimate) / 1000000;
}

/**
 * @brief Generate a simple performance report for a benchmark
 */
int perf_report_benchmark(const perf_benchmark_t *benchmark) {
    if (!benchmark) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    log_info("=== Performance Benchmark Report: %s ===", benchmark->benchmark_name);
    log_info("Test Type: %lu", benchmark->test_type);
    log_info("Samples Collected: %lu", benchmark->samples_collected);
    log_info("Invalid Samples: %lu", benchmark->invalid_samples);
    log_info("Outlier Samples: %lu", benchmark->outlier_samples);
    log_info("Measurement Stability: %.3f", benchmark->measurement_stability);
    
    if (benchmark->samples_collected >= MIN_SAMPLES_FOR_STATS) {
        const perf_statistics_t *stats = &benchmark->statistics;
        log_info("Statistical Analysis:");
        log_info("  Mean: %.2f", stats->mean);
        log_info("  Std Dev: %.2f", stats->std_deviation);
        log_info("  Min/Max: %.2f/%.2f", stats->min_value, stats->max_value);
        log_info("  Median: %.2f", stats->median);
        log_info("  95th Percentile: %.2f", stats->percentile_95);
        log_info("  Coefficient of Variation: %.2f%%", stats->coefficient_variation);
        log_info("  Outliers: %lu", stats->outlier_count);
        
        if (stats->trend_significant) {
            log_info("  Trend: %.3f (correlation: %.3f)", stats->trend_slope, stats->trend_correlation);
        }
    }
    
    if (benchmark->regression.regression_detected) {
        log_warning("Performance Regression Detected:");
        log_warning("  Magnitude: %.2f%%", benchmark->regression.regression_magnitude);
        log_warning("  Confidence: %lu%%", benchmark->regression.regression_confidence);
    }
    
    log_info("===============================================");
    
    return PERF_SUCCESS;
}

/**
 * @brief Print framework summary
 */
int perf_report_framework_summary(void) {
    log_info("=== Performance Framework Summary ===");
    log_info("Framework Uptime: %lu ms", 
             get_system_timestamp_ms() - g_perf_framework.framework_uptime_ms);
    log_info("Active Benchmarks: %lu", g_perf_framework.active_benchmarks);
    log_info("Total Samples Collected: %lu", g_perf_framework.total_samples_collected);
    log_info("Total Regressions Detected: %lu", g_perf_framework.total_regressions_detected);
    log_info("Timer Calibrated: %s", g_perf_framework.timer_calibrated ? "Yes" : "No");
    log_info("Baseline Count: %lu", g_perf_framework.baseline_count);
    log_info("=====================================");
    
    return PERF_SUCCESS;
}

/* Internal function implementations */

/**
 * @brief Calibrate timer precision
 */
static int calibrate_timer_precision(void) {
    log_info("Calibrating timer precision...");
    
    uint32_t calibration_start = get_system_timestamp_ms();
    uint32_t tick_start = get_system_timestamp_ticks();
    
    /* Wait for calibration duration */
    while ((get_system_timestamp_ms() - calibration_start) < CALIBRATION_DURATION_MS) {
        for (volatile int i = 0; i < 1000; i++);  /* Light CPU load */
    }
    
    uint32_t calibration_end = get_system_timestamp_ms();
    uint32_t tick_end = get_system_timestamp_ticks();
    
    uint32_t elapsed_ms = calibration_end - calibration_start;
    uint32_t elapsed_ticks = tick_end - tick_start;
    
    if (elapsed_ticks == 0) {
        log_warning("Timer calibration failed - no tick change detected");
        g_perf_framework.timer_calibration_factor = 1000;  /* Default */
        return PERF_ERR_CALIBRATION_FAILED;
    }
    
    /* Calculate calibration factor */
    double expected_ticks = (elapsed_ms * DOS_TIMER_FREQ_HZ) / 1000.0;
    g_perf_framework.timer_calibration_factor = (uint32_t)((expected_ticks * 1000) / elapsed_ticks);
    
    g_perf_framework.timer_calibrated = true;
    
    log_info("Timer calibration completed: factor = %lu", 
             g_perf_framework.timer_calibration_factor);
    
    return PERF_SUCCESS;
}

/**
 * @brief Interpolate sub-tick timing for better precision
 */
static uint32_t interpolate_sub_tick_timing(uint32_t start_tick, uint32_t end_tick) {
    /* Simple interpolation based on multiple timer reads */
    uint32_t interpolation = 0;
    
    if (end_tick == start_tick) {
        /* Same tick - estimate position within tick */
        for (int i = 0; i < 10; i++) {
            uint32_t current_tick = get_system_timestamp_ticks();
            if (current_tick != start_tick) {
                interpolation = i;
                break;
            }
        }
    }
    
    return interpolation;
}

/**
 * @brief Detect outliers in measurement samples
 */
static int detect_measurement_outliers(perf_sample_t *samples, uint32_t count) {
    if (!samples || count < MIN_SAMPLES_FOR_STATS) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    /* Extract values for outlier detection */
    double *values = malloc(count * sizeof(double));
    if (!values) {
        return PERF_ERR_OUT_OF_MEMORY;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        values[i] = (double)samples[i].value;
    }
    
    /* Sort values */
    sort_double_array(values, count);
    
    /* Calculate quartiles */
    double q1 = perf_calculate_percentile(values, count, 25);
    double q3 = perf_calculate_percentile(values, count, 75);
    double iqr = q3 - q1;
    
    double lower_bound = q1 - (OUTLIER_IQR_MULTIPLIER * iqr);
    double upper_bound = q3 + (OUTLIER_IQR_MULTIPLIER * iqr);
    
    /* Mark outliers */
    uint32_t outlier_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        double value = (double)samples[i].value;
        if (value < lower_bound || value > upper_bound) {
            samples[i].outlier_detected = true;
            outlier_count++;
        }
    }
    
    free(values);
    
    log_debug("Detected %lu outliers out of %lu samples", outlier_count, count);
    return PERF_SUCCESS;
}

/**
 * @brief Sort double array (simple bubble sort for small arrays)
 */
static int sort_double_array(double *array, uint32_t count) {
    if (!array || count == 0) {
        return PERF_ERR_INVALID_PARAM;
    }
    
    for (uint32_t i = 0; i < count - 1; i++) {
        for (uint32_t j = 0; j < count - i - 1; j++) {
            if (array[j] > array[j + 1]) {
                double temp = array[j];
                array[j] = array[j + 1];
                array[j + 1] = temp;
            }
        }
    }
    
    return PERF_SUCCESS;
}