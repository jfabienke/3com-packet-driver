/**
 * @file perf_framework.h
 * @brief Performance testing framework with DOS timer integration
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This header provides a comprehensive performance testing framework including:
 * - High-precision DOS timer integration
 * - Statistical analysis and regression detection
 * - Performance baseline management
 * - Multi-level benchmarking support
 * - Automated performance reporting
 * - Cross-NIC performance comparison
 */

#ifndef PERF_FRAMEWORK_H
#define PERF_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Performance framework constants */
#define PERF_MAX_SAMPLES            1000    /* Maximum performance samples */
#define PERF_MAX_BENCHMARKS         50      /* Maximum concurrent benchmarks */
#define PERF_MAX_BASELINE_SAMPLES   100     /* Baseline sample count */
#define PERF_TIMER_RESOLUTION_US    55      /* DOS timer resolution (microseconds) */
#define PERF_CPU_FREQ_ESTIMATE      25000000 /* 25MHz CPU frequency estimate */

/* Performance test types */
#define PERF_TEST_TYPE_THROUGHPUT   0       /* Throughput testing */
#define PERF_TEST_TYPE_LATENCY      1       /* Latency testing */
#define PERF_TEST_TYPE_CPU          2       /* CPU utilization testing */
#define PERF_TEST_TYPE_MEMORY       3       /* Memory performance testing */
#define PERF_TEST_TYPE_STRESS       4       /* Stress testing */
#define PERF_TEST_TYPE_STABILITY    5       /* Stability testing */

/* Performance measurement precision levels */
#define PERF_PRECISION_LOW          0       /* Standard DOS timer */
#define PERF_PRECISION_MEDIUM       1       /* Enhanced timer interpolation */
#define PERF_PRECISION_HIGH         2       /* CPU cycle estimation */

/* Statistical confidence levels */
#define PERF_CONFIDENCE_90          90      /* 90% confidence */
#define PERF_CONFIDENCE_95          95      /* 95% confidence */
#define PERF_CONFIDENCE_99          99      /* 99% confidence */

/* Performance comparison results */
#define PERF_COMPARE_BETTER         1       /* Performance improved */
#define PERF_COMPARE_SAME           0       /* Performance unchanged */
#define PERF_COMPARE_WORSE          -1      /* Performance degraded */

/* Regression detection sensitivity */
#define PERF_REGRESSION_THRESHOLD_5 5       /* 5% regression threshold */
#define PERF_REGRESSION_THRESHOLD_10 10     /* 10% regression threshold */
#define PERF_REGRESSION_THRESHOLD_15 15     /* 15% regression threshold */

/* High-precision timer structure */
typedef struct {
    uint32_t dos_tick_start;        /* DOS timer tick at start */
    uint32_t dos_tick_end;          /* DOS timer tick at end */
    uint32_t cpu_cycle_estimate;    /* Estimated CPU cycles */
    uint32_t interpolation_factor;  /* Sub-tick interpolation */
    uint32_t precision_level;       /* Measurement precision level */
} perf_timer_t;

/* Performance sample structure */
typedef struct {
    uint32_t sample_id;             /* Unique sample identifier */
    uint32_t timestamp;             /* Sample timestamp */
    
    /* Measurement data */
    perf_timer_t timer;             /* Timer measurement */
    uint32_t value;                 /* Primary measurement value */
    uint32_t secondary_value;       /* Secondary measurement (optional) */
    uint32_t operations_count;      /* Number of operations measured */
    
    /* Context information */
    uint32_t cpu_load_percent;      /* CPU load during measurement */
    uint32_t memory_usage_bytes;    /* Memory usage during measurement */
    uint32_t system_load_factor;    /* Overall system load factor */
    
    /* Quality indicators */
    bool measurement_valid;         /* Whether measurement is valid */
    bool outlier_detected;          /* Whether sample is an outlier */
    uint32_t confidence_level;      /* Confidence in measurement */
    
    /* Performance metrics */
    uint32_t throughput_pps;        /* Throughput (packets per second) */
    uint32_t latency_us;            /* Latency (microseconds) */
    uint32_t cpu_utilization;       /* CPU utilization percentage */
    uint32_t memory_bandwidth;      /* Memory bandwidth estimate */
} perf_sample_t;

/* Statistical analysis results */
typedef struct {
    uint32_t sample_count;          /* Number of samples analyzed */
    
    /* Basic statistics */
    double mean;                    /* Arithmetic mean */
    double median;                  /* Median value */
    double mode;                    /* Mode (most frequent value) */
    double std_deviation;           /* Standard deviation */
    double variance;                /* Variance */
    
    /* Distribution analysis */
    double min_value;               /* Minimum value */
    double max_value;               /* Maximum value */
    double range;                   /* Range (max - min) */
    double coefficient_variation;   /* Coefficient of variation */
    
    /* Percentile analysis */
    double percentile_25;           /* 25th percentile */
    double percentile_75;           /* 75th percentile */
    double percentile_90;           /* 90th percentile */
    double percentile_95;           /* 95th percentile */
    double percentile_99;           /* 99th percentile */
    
    /* Outlier analysis */
    uint32_t outlier_count;         /* Number of outliers detected */
    double outlier_threshold_low;   /* Lower outlier threshold */
    double outlier_threshold_high;  /* Upper outlier threshold */
    
    /* Trend analysis */
    double trend_slope;             /* Linear trend slope */
    double trend_correlation;       /* Trend correlation coefficient */
    bool trend_significant;         /* Whether trend is statistically significant */
    
    /* Confidence intervals */
    double confidence_interval_90_low;  /* 90% CI lower bound */
    double confidence_interval_90_high; /* 90% CI upper bound */
    double confidence_interval_95_low;  /* 95% CI lower bound */
    double confidence_interval_95_high; /* 95% CI upper bound */
} perf_statistics_t;

/* Performance baseline */
typedef struct {
    char baseline_name[64];         /* Baseline identifier */
    uint32_t creation_timestamp;    /* When baseline was created */
    uint32_t test_type;             /* Type of performance test */
    
    /* Baseline performance data */
    perf_statistics_t stats;        /* Statistical analysis of baseline */
    perf_sample_t samples[PERF_MAX_BASELINE_SAMPLES]; /* Baseline samples */
    uint32_t sample_count;          /* Number of baseline samples */
    
    /* System configuration */
    char system_config[256];        /* System configuration description */
    char nic_config[128];           /* NIC configuration */
    uint32_t driver_version;        /* Driver version */
    
    /* Validity indicators */
    bool baseline_valid;            /* Whether baseline is valid */
    uint32_t confidence_level;      /* Confidence in baseline */
    uint32_t baseline_quality_score; /* Quality score (0-100) */
} perf_baseline_t;

/* Regression analysis result */
typedef struct {
    char test_name[64];             /* Test being analyzed */
    uint32_t analysis_timestamp;    /* When analysis was performed */
    
    /* Comparison data */
    perf_baseline_t *baseline;      /* Baseline for comparison */
    perf_statistics_t current_stats; /* Current performance statistics */
    
    /* Regression detection */
    bool regression_detected;       /* Whether regression was detected */
    double regression_magnitude;    /* Magnitude of regression (percentage) */
    uint32_t regression_confidence; /* Confidence in regression detection */
    
    /* Performance comparison */
    int comparison_result;          /* PERF_COMPARE_* result */
    double performance_ratio;       /* Current/baseline performance ratio */
    
    /* Statistical significance */
    bool statistically_significant; /* Whether difference is statistically significant */
    double p_value;                 /* Statistical p-value */
    double effect_size;             /* Effect size measure */
    
    /* Root cause analysis */
    uint32_t suspected_causes;      /* Bit mask of suspected causes */
    char analysis_notes[512];       /* Detailed analysis notes */
    
    /* Recommendations */
    char recommendations[256];      /* Performance improvement recommendations */
} perf_regression_analysis_t;

/* Performance benchmark context */
typedef struct {
    char benchmark_name[64];        /* Benchmark identifier */
    uint32_t test_type;             /* Type of performance test */
    uint32_t precision_level;       /* Timer precision level */
    
    /* Test configuration */
    uint32_t target_sample_count;   /* Target number of samples */
    uint32_t max_duration_ms;       /* Maximum test duration */
    uint32_t warmup_duration_ms;    /* Warmup duration */
    uint32_t cooldown_duration_ms;  /* Cooldown duration */
    
    /* Current test state */
    uint32_t samples_collected;     /* Samples collected so far */
    uint32_t test_start_time;       /* Test start timestamp */
    bool test_active;               /* Whether test is currently active */
    
    /* Sample storage */
    perf_sample_t samples[PERF_MAX_SAMPLES]; /* Performance samples */
    perf_statistics_t statistics;   /* Statistical analysis */
    
    /* Quality control */
    uint32_t invalid_samples;       /* Number of invalid samples */
    uint32_t outlier_samples;       /* Number of outlier samples */
    double measurement_stability;   /* Measurement stability indicator */
    
    /* Comparison and regression */
    perf_baseline_t *baseline;      /* Associated baseline */
    perf_regression_analysis_t regression; /* Regression analysis */
} perf_benchmark_t;

/* Performance framework state */
typedef struct {
    bool framework_initialized;     /* Framework initialization status */
    uint32_t active_benchmarks;     /* Number of active benchmarks */
    
    /* Timer calibration */
    uint32_t timer_calibration_factor; /* Timer calibration factor */
    uint32_t cpu_frequency_estimate;   /* Estimated CPU frequency */
    bool timer_calibrated;          /* Whether timer is calibrated */
    
    /* Benchmark management */
    perf_benchmark_t benchmarks[PERF_MAX_BENCHMARKS]; /* Active benchmarks */
    uint32_t next_benchmark_id;     /* Next benchmark ID */
    
    /* Baseline management */
    perf_baseline_t baselines[PERF_MAX_BENCHMARKS]; /* Stored baselines */
    uint32_t baseline_count;        /* Number of stored baselines */
    
    /* Global statistics */
    uint32_t total_samples_collected; /* Total samples across all benchmarks */
    uint32_t total_regressions_detected; /* Total regressions detected */
    uint32_t framework_uptime_ms;   /* Framework uptime */
} perf_framework_state_t;

/* Function prototypes */

/* Framework initialization and management */
int perf_framework_init(void);
int perf_framework_cleanup(void);
int perf_framework_calibrate_timer(void);
bool perf_framework_is_initialized(void);

/* Timer functions */
int perf_timer_start(perf_timer_t *timer, uint32_t precision_level);
int perf_timer_stop(perf_timer_t *timer);
uint32_t perf_timer_get_elapsed_us(const perf_timer_t *timer);
uint32_t perf_timer_get_elapsed_cycles(const perf_timer_t *timer);

/* Benchmark management */
int perf_benchmark_create(const char *name, uint32_t test_type, perf_benchmark_t **benchmark);
int perf_benchmark_destroy(perf_benchmark_t *benchmark);
int perf_benchmark_start(perf_benchmark_t *benchmark);
int perf_benchmark_stop(perf_benchmark_t *benchmark);
int perf_benchmark_add_sample(perf_benchmark_t *benchmark, const perf_sample_t *sample);
int perf_benchmark_finalize(perf_benchmark_t *benchmark);

/* Sample creation and validation */
int perf_sample_create(perf_sample_t *sample, uint32_t value, const perf_timer_t *timer);
int perf_sample_validate(const perf_sample_t *sample);
int perf_sample_enhance(perf_sample_t *sample);

/* Statistical analysis */
int perf_statistics_calculate(const perf_sample_t *samples, uint32_t count, perf_statistics_t *stats);
int perf_statistics_detect_outliers(perf_sample_t *samples, uint32_t count, double threshold);
int perf_statistics_analyze_trend(const perf_sample_t *samples, uint32_t count, 
                                 double *slope, double *correlation);
int perf_statistics_calculate_confidence_interval(const perf_statistics_t *stats, 
                                                 uint32_t confidence_level,
                                                 double *lower, double *upper);

/* Baseline management */
int perf_baseline_create(const char *name, uint32_t test_type, const perf_sample_t *samples,
                        uint32_t count, perf_baseline_t *baseline);
int perf_baseline_save(const perf_baseline_t *baseline, const char *filename);
int perf_baseline_load(const char *filename, perf_baseline_t *baseline);
int perf_baseline_validate(const perf_baseline_t *baseline);
int perf_baseline_compare(const perf_baseline_t *baseline, const perf_statistics_t *current_stats,
                         perf_regression_analysis_t *analysis);

/* Regression detection */
int perf_regression_detect(const perf_baseline_t *baseline, const perf_statistics_t *current_stats,
                          uint32_t threshold_percent, perf_regression_analysis_t *result);
int perf_regression_analyze_causes(const perf_regression_analysis_t *regression);
int perf_regression_generate_recommendations(perf_regression_analysis_t *regression);

/* Reporting and output */
int perf_report_benchmark(const perf_benchmark_t *benchmark);
int perf_report_statistics(const perf_statistics_t *stats);
int perf_report_regression(const perf_regression_analysis_t *regression);
int perf_report_comparison(const perf_baseline_t *baseline, const perf_statistics_t *current);
int perf_report_framework_summary(void);

/* Utility functions */
double perf_calculate_percentile(const double *sorted_values, uint32_t count, uint32_t percentile);
double perf_calculate_mean(const double *values, uint32_t count);
double perf_calculate_std_deviation(const double *values, uint32_t count, double mean);
int perf_sort_samples_by_value(perf_sample_t *samples, uint32_t count);
uint32_t perf_estimate_cpu_cycles(uint32_t elapsed_us);

/* Advanced analysis functions */
int perf_analyze_performance_profile(const perf_benchmark_t *benchmark, char *profile_report);
int perf_detect_performance_anomalies(const perf_sample_t *samples, uint32_t count);
int perf_compare_benchmarks(const perf_benchmark_t *benchmark1, const perf_benchmark_t *benchmark2);
int perf_generate_optimization_suggestions(const perf_benchmark_t *benchmark, char *suggestions);

/* Framework configuration */
int perf_framework_set_precision_level(uint32_t level);
int perf_framework_set_regression_threshold(uint32_t threshold_percent);
int perf_framework_enable_automatic_baseline_update(bool enable);
int perf_framework_set_outlier_detection_sensitivity(double sensitivity);

/* Error codes */
#define PERF_SUCCESS                0
#define PERF_ERR_NOT_INITIALIZED   -1
#define PERF_ERR_INVALID_PARAM     -2
#define PERF_ERR_OUT_OF_MEMORY     -3
#define PERF_ERR_TIMER_FAILED      -4
#define PERF_ERR_INSUFFICIENT_DATA -5
#define PERF_ERR_REGRESSION_DETECTED -6
#define PERF_ERR_BASELINE_INVALID  -7
#define PERF_ERR_STAT_CALCULATION  -8
#define PERF_ERR_FILE_IO           -9
#define PERF_ERR_CALIBRATION_FAILED -10

/* Macros for convenient performance measurement */
#define PERF_MEASURE_START(timer) \
    do { \
        if (perf_timer_start(&(timer), PERF_PRECISION_MEDIUM) != PERF_SUCCESS) { \
            log_warning("Performance timer start failed"); \
        } \
    } while(0)

#define PERF_MEASURE_END(timer) \
    do { \
        if (perf_timer_stop(&(timer)) != PERF_SUCCESS) { \
            log_warning("Performance timer stop failed"); \
        } \
    } while(0)

#define PERF_MEASURE_OPERATION(operation, timer, result_var) \
    do { \
        PERF_MEASURE_START(timer); \
        operation; \
        PERF_MEASURE_END(timer); \
        result_var = perf_timer_get_elapsed_us(&(timer)); \
    } while(0)

#define PERF_ADD_SAMPLE_SIMPLE(benchmark, value, timer) \
    do { \
        perf_sample_t sample; \
        if (perf_sample_create(&sample, (value), &(timer)) == PERF_SUCCESS) { \
            perf_benchmark_add_sample((benchmark), &sample); \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* PERF_FRAMEWORK_H */