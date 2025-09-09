/**
 * @file statistical_analysis.c
 * @brief Statistical analysis engine with trend detection and thresholds
 * 
 * 3Com Packet Driver - Diagnostics Agent - Week 1
 * Implements comprehensive statistical analysis with trend detection
 */

#include "../../include/diagnostics.h"
#include "../../include/common.h"
#include "../../docs/agents/shared/error-codes.h"
#include <string.h>
#include <stdio.h>

/* Statistical analysis configuration */
#define MAX_TREND_SAMPLES       100
#define MIN_TREND_SAMPLES       5
#define TREND_ANALYSIS_WINDOW   60000   /* 60 seconds */
#define STATISTICAL_THRESHOLD   3       /* 3-sigma threshold for anomalies */

/* Statistical sample structure */
typedef struct stat_sample {
    uint32_t timestamp;
    uint32_t value;
    uint8_t  metric_type;
    struct stat_sample *next;
} stat_sample_t;

/* Trend detection engine */
typedef struct trend_engine {
    bool initialized;
    stat_sample_t *samples_head;
    stat_sample_t *samples_tail;
    uint16_t sample_count;
    uint32_t analysis_window_ms;
    uint32_t last_analysis_time;
    
    /* Statistical metrics */
    double mean;
    double variance;
    double std_deviation;
    double trend_slope;
    double correlation_coefficient;
    
    /* Threshold management */
    uint32_t upper_threshold;
    uint32_t lower_threshold;
    bool adaptive_thresholds;
    uint32_t threshold_violations;
    
} trend_engine_t;

/* Metric type definitions */
#define METRIC_TYPE_PACKET_RATE       0
#define METRIC_TYPE_ERROR_RATE        1
#define METRIC_TYPE_MEMORY_USAGE      2
#define METRIC_TYPE_CPU_UTILIZATION   3
#define METRIC_TYPE_NETWORK_HEALTH    4
#define METRIC_TYPE_ISR_TIMING        5
#define METRIC_TYPE_CLI_TIMING        6
#define METRIC_TYPE_NIC_HEALTH        7

static trend_engine_t g_trend_engines[8]; /* One per metric type */
static bool g_statistical_engine_initialized = false;

/* Basic statistical functions */
static double calculate_mean(const stat_sample_t *samples, uint16_t count) {
    if (count == 0 || !samples) return 0.0;
    
    double sum = 0.0;
    const stat_sample_t *current = samples;
    uint16_t processed = 0;
    
    while (current && processed < count) {
        sum += (double)current->value;
        current = current->next;
        processed++;
    }
    
    return sum / (double)count;
}

static double calculate_variance(const stat_sample_t *samples, uint16_t count, double mean) {
    if (count <= 1 || !samples) return 0.0;
    
    double sum_sq_diff = 0.0;
    const stat_sample_t *current = samples;
    uint16_t processed = 0;
    
    while (current && processed < count) {
        double diff = (double)current->value - mean;
        sum_sq_diff += diff * diff;
        current = current->next;
        processed++;
    }
    
    return sum_sq_diff / (double)(count - 1);
}

static double calculate_std_deviation(double variance) {
    /* Simple integer square root approximation for DOS */
    if (variance <= 0.0) return 0.0;
    
    double x = variance;
    double prev = 0.0;
    
    /* Newton's method for square root */
    for (int i = 0; i < 10 && x != prev; i++) {
        prev = x;
        x = (x + variance / x) / 2.0;
    }
    
    return x;
}

static double calculate_trend_slope(const stat_sample_t *samples, uint16_t count) {
    if (count < 2 || !samples) return 0.0;
    
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
    const stat_sample_t *current = samples;
    uint16_t processed = 0;
    
    while (current && processed < count) {
        double x = (double)processed; /* Time index */
        double y = (double)current->value;
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
        
        current = current->next;
        processed++;
    }
    
    double n = (double)count;
    double denominator = n * sum_x2 - sum_x * sum_x;
    
    if (denominator == 0.0) return 0.0;
    
    return (n * sum_xy - sum_x * sum_y) / denominator;
}

/* Initialize statistical analysis engine */
int stat_analysis_init(void) {
    if (g_statistical_engine_initialized) {
        return SUCCESS;
    }
    
    /* Initialize trend engines for all metric types */
    for (int i = 0; i < 8; i++) {
        memset(&g_trend_engines[i], 0, sizeof(trend_engine_t));
        g_trend_engines[i].analysis_window_ms = TREND_ANALYSIS_WINDOW;
        g_trend_engines[i].adaptive_thresholds = true;
        g_trend_engines[i].initialized = true;
        
        /* Set initial thresholds based on metric type */
        switch (i) {
            case METRIC_TYPE_PACKET_RATE:
                g_trend_engines[i].upper_threshold = 10000; /* packets/sec */
                g_trend_engines[i].lower_threshold = 0;
                break;
            case METRIC_TYPE_ERROR_RATE:
                g_trend_engines[i].upper_threshold = 100; /* errors/sec */
                g_trend_engines[i].lower_threshold = 0;
                break;
            case METRIC_TYPE_MEMORY_USAGE:
                g_trend_engines[i].upper_threshold = 550000; /* 85% of 640KB */
                g_trend_engines[i].lower_threshold = 0;
                break;
            case METRIC_TYPE_CPU_UTILIZATION:
                g_trend_engines[i].upper_threshold = 95; /* 95% CPU */
                g_trend_engines[i].lower_threshold = 0;
                break;
            case METRIC_TYPE_NETWORK_HEALTH:
                g_trend_engines[i].upper_threshold = 100;
                g_trend_engines[i].lower_threshold = 50; /* Below 50% is critical */
                break;
            case METRIC_TYPE_ISR_TIMING:
                g_trend_engines[i].upper_threshold = 60; /* 60 microseconds */
                g_trend_engines[i].lower_threshold = 0;
                break;
            case METRIC_TYPE_CLI_TIMING:
                g_trend_engines[i].upper_threshold = 8; /* 8 microseconds */
                g_trend_engines[i].lower_threshold = 0;
                break;
            case METRIC_TYPE_NIC_HEALTH:
                g_trend_engines[i].upper_threshold = 100;
                g_trend_engines[i].lower_threshold = 70;
                break;
        }
    }
    
    g_statistical_engine_initialized = true;
    LOG_INFO("Statistical analysis engine initialized");
    return SUCCESS;
}

/* Add sample to trend engine */
int stat_analysis_add_sample(uint8_t metric_type, uint32_t value) {
    if (!g_statistical_engine_initialized || metric_type >= 8) {
        return ERROR_INVALID_PARAM;
    }
    
    trend_engine_t *engine = &g_trend_engines[metric_type];
    
    /* Allocate new sample */
    stat_sample_t *new_sample = (stat_sample_t*)malloc(sizeof(stat_sample_t));
    if (!new_sample) {
        LOG_ERROR("Failed to allocate memory for statistical sample");
        return ERROR_OUT_OF_MEMORY;
    }
    
    new_sample->timestamp = diag_get_timestamp();
    new_sample->value = value;
    new_sample->metric_type = metric_type;
    new_sample->next = NULL;
    
    /* Add to linked list */
    if (!engine->samples_head) {
        engine->samples_head = new_sample;
        engine->samples_tail = new_sample;
    } else {
        engine->samples_tail->next = new_sample;
        engine->samples_tail = new_sample;
    }
    
    engine->sample_count++;
    
    /* Remove old samples if we exceed maximum */
    while (engine->sample_count > MAX_TREND_SAMPLES) {
        stat_sample_t *old_sample = engine->samples_head;
        engine->samples_head = old_sample->next;
        free(old_sample);
        engine->sample_count--;
    }
    
    /* Check threshold violation */
    if (value > engine->upper_threshold || value < engine->lower_threshold) {
        engine->threshold_violations++;
        LOG_WARNING("Threshold violation for metric %d: value=%lu, upper=%lu, lower=%lu",
                   metric_type, value, engine->upper_threshold, engine->lower_threshold);
    }
    
    return SUCCESS;
}

/* Perform trend analysis */
int stat_analysis_perform_trend_analysis(uint8_t metric_type, trend_analysis_t *result) {
    if (!g_statistical_engine_initialized || metric_type >= 8 || !result) {
        return ERROR_INVALID_PARAM;
    }
    
    trend_engine_t *engine = &g_trend_engines[metric_type];
    
    if (engine->sample_count < MIN_TREND_SAMPLES) {
        LOG_WARNING("Insufficient samples for trend analysis: %d (need %d)", 
                   engine->sample_count, MIN_TREND_SAMPLES);
        return ERROR_INVALID_STATE;
    }
    
    /* Calculate statistical metrics */
    engine->mean = calculate_mean(engine->samples_head, engine->sample_count);
    engine->variance = calculate_variance(engine->samples_head, engine->sample_count, engine->mean);
    engine->std_deviation = calculate_std_deviation(engine->variance);
    engine->trend_slope = calculate_trend_slope(engine->samples_head, engine->sample_count);
    
    /* Fill result structure */
    result->analysis_window_ms = engine->analysis_window_ms;
    result->sample_count = engine->sample_count;
    
    /* Convert trend slope to trend classification */
    if (engine->trend_slope > (engine->std_deviation * 0.1)) {
        result->packet_trend = 1; /* Increasing trend */
    } else if (engine->trend_slope < -(engine->std_deviation * 0.1)) {
        result->packet_trend = -1; /* Decreasing trend */
    } else {
        result->packet_trend = 0; /* Stable trend */
    }
    
    /* Set metric-specific trend results */
    switch (metric_type) {
        case METRIC_TYPE_ERROR_RATE:
            result->error_trend = result->packet_trend;
            break;
        case METRIC_TYPE_NETWORK_HEALTH:
            result->health_trend = result->packet_trend;
            break;
        case METRIC_TYPE_MEMORY_USAGE:
            result->memory_trend = result->packet_trend;
            break;
        default:
            result->packet_trend = result->packet_trend;
            break;
    }
    
    engine->last_analysis_time = diag_get_timestamp();
    
    LOG_DEBUG("Trend analysis for metric %d: mean=%.2f, std_dev=%.2f, slope=%.6f, trend=%d",
              metric_type, engine->mean, engine->std_deviation, engine->trend_slope, 
              result->packet_trend);
    
    return SUCCESS;
}

/* Detect anomalies using statistical analysis */
int stat_analysis_detect_anomalies(uint8_t metric_type, uint32_t current_value, bool *anomaly_detected) {
    if (!g_statistical_engine_initialized || metric_type >= 8 || !anomaly_detected) {
        return ERROR_INVALID_PARAM;
    }
    
    trend_engine_t *engine = &g_trend_engines[metric_type];
    *anomaly_detected = false;
    
    if (engine->sample_count < MIN_TREND_SAMPLES) {
        return SUCCESS; /* Not enough data for anomaly detection */
    }
    
    /* Calculate z-score (standard deviations from mean) */
    if (engine->std_deviation > 0.0) {
        double z_score = ((double)current_value - engine->mean) / engine->std_deviation;
        
        /* Anomaly if beyond STATISTICAL_THRESHOLD standard deviations */
        if (z_score > STATISTICAL_THRESHOLD || z_score < -STATISTICAL_THRESHOLD) {
            *anomaly_detected = true;
            LOG_WARNING("Statistical anomaly detected for metric %d: value=%lu, z-score=%.2f",
                       metric_type, current_value, z_score);
        }
    }
    
    return SUCCESS;
}

/* Update adaptive thresholds based on statistical analysis */
int stat_analysis_update_adaptive_thresholds(uint8_t metric_type) {
    if (!g_statistical_engine_initialized || metric_type >= 8) {
        return ERROR_INVALID_PARAM;
    }
    
    trend_engine_t *engine = &g_trend_engines[metric_type];
    
    if (!engine->adaptive_thresholds || engine->sample_count < MIN_TREND_SAMPLES) {
        return SUCCESS; /* Adaptive thresholds disabled or insufficient data */
    }
    
    /* Update thresholds based on mean ± 2 * standard deviation */
    if (engine->std_deviation > 0.0) {
        uint32_t new_upper = (uint32_t)(engine->mean + 2.0 * engine->std_deviation);
        uint32_t new_lower = 0;
        
        if (engine->mean > 2.0 * engine->std_deviation) {
            new_lower = (uint32_t)(engine->mean - 2.0 * engine->std_deviation);
        }
        
        /* Only update if the new thresholds are reasonable */
        if (new_upper > engine->upper_threshold / 2 && new_upper < engine->upper_threshold * 2) {
            engine->upper_threshold = new_upper;
        }
        
        if (new_lower < engine->lower_threshold * 2) {
            engine->lower_threshold = new_lower;
        }
        
        LOG_DEBUG("Updated adaptive thresholds for metric %d: upper=%lu, lower=%lu",
                 metric_type, engine->upper_threshold, engine->lower_threshold);
    }
    
    return SUCCESS;
}

/* Get statistical summary for a metric */
int stat_analysis_get_summary(uint8_t metric_type, char *buffer, uint32_t buffer_size) {
    if (!g_statistical_engine_initialized || metric_type >= 8 || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    trend_engine_t *engine = &g_trend_engines[metric_type];
    
    snprintf(buffer, buffer_size,
            "Metric %d: Samples=%d, Mean=%.2f, StdDev=%.2f, Slope=%.6f, "
            "Thresholds=[%lu,%lu], Violations=%lu",
            metric_type, engine->sample_count, engine->mean, engine->std_deviation,
            engine->trend_slope, engine->lower_threshold, engine->upper_threshold,
            engine->threshold_violations);
    
    return SUCCESS;
}

/* Perform comprehensive statistical analysis across all metrics */
int stat_analysis_comprehensive_analysis(void) {
    if (!g_statistical_engine_initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== STATISTICAL ANALYSIS DASHBOARD ===\n");
    
    for (int i = 0; i < 8; i++) {
        trend_engine_t *engine = &g_trend_engines[i];
        
        if (engine->sample_count >= MIN_TREND_SAMPLES) {
            char summary[256];
            stat_analysis_get_summary(i, summary, sizeof(summary));
            printf("%s\n", summary);
            
            /* Update adaptive thresholds */
            stat_analysis_update_adaptive_thresholds(i);
            
            /* Perform trend analysis */
            trend_analysis_t trend;
            if (stat_analysis_perform_trend_analysis(i, &trend) == SUCCESS) {
                const char *trend_desc = (trend.packet_trend > 0) ? "INCREASING" :
                                       (trend.packet_trend < 0) ? "DECREASING" : "STABLE";
                printf("  Trend: %s\n", trend_desc);
            }
        }
    }
    
    return SUCCESS;
}

/* Export statistical data for external analysis */
int stat_analysis_export_data(char *buffer, uint32_t buffer_size) {
    if (!g_statistical_engine_initialized || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t written = 0;
    written += snprintf(buffer + written, buffer_size - written,
                       "# Statistical Analysis Export\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "# Timestamp: %lu\n", diag_get_timestamp());
    
    for (int i = 0; i < 8; i++) {
        trend_engine_t *engine = &g_trend_engines[i];
        
        if (engine->sample_count > 0) {
            written += snprintf(buffer + written, buffer_size - written,
                               "\n[METRIC_%d]\n", i);
            written += snprintf(buffer + written, buffer_size - written,
                               "samples=%d\n", engine->sample_count);
            written += snprintf(buffer + written, buffer_size - written,
                               "mean=%.2f\n", engine->mean);
            written += snprintf(buffer + written, buffer_size - written,
                               "variance=%.2f\n", engine->variance);
            written += snprintf(buffer + written, buffer_size - written,
                               "std_deviation=%.2f\n", engine->std_deviation);
            written += snprintf(buffer + written, buffer_size - written,
                               "trend_slope=%.6f\n", engine->trend_slope);
            written += snprintf(buffer + written, buffer_size - written,
                               "upper_threshold=%lu\n", engine->upper_threshold);
            written += snprintf(buffer + written, buffer_size - written,
                               "lower_threshold=%lu\n", engine->lower_threshold);
            written += snprintf(buffer + written, buffer_size - written,
                               "violations=%lu\n", engine->threshold_violations);
            
            if (written >= buffer_size - 100) {
                break; /* Prevent buffer overflow */
            }
        }
    }
    
    return SUCCESS;
}

/* Cleanup statistical analysis engine */
void stat_analysis_cleanup(void) {
    if (!g_statistical_engine_initialized) {
        return;
    }
    
    /* Free all sample data */
    for (int i = 0; i < 8; i++) {
        trend_engine_t *engine = &g_trend_engines[i];
        
        stat_sample_t *current = engine->samples_head;
        while (current) {
            stat_sample_t *next = current->next;
            free(current);
            current = next;
        }
        
        memset(engine, 0, sizeof(trend_engine_t));
    }
    
    g_statistical_engine_initialized = false;
    LOG_INFO("Statistical analysis engine cleaned up");
}

/* Week 1 specific: Statistical validation for NE2000 emulation testing */
int stat_analysis_validate_ne2000_emulation(void) {
    if (!g_statistical_engine_initialized) {
        return ERROR_INVALID_STATE;
    }
    
    LOG_INFO("Performing statistical validation for NE2000 emulation testing...");
    
    /* Check packet rate statistics for NE2000 compatibility */
    trend_engine_t *packet_engine = &g_trend_engines[METRIC_TYPE_PACKET_RATE];
    if (packet_engine->sample_count >= MIN_TREND_SAMPLES) {
        if (packet_engine->mean < 100.0) { /* Minimum expected packet rate */
            LOG_WARNING("NE2000 emulation packet rate below expected: %.2f pps", packet_engine->mean);
            return ERROR_PERFORMANCE;
        }
    }
    
    /* Check error rate statistics */
    trend_engine_t *error_engine = &g_trend_engines[METRIC_TYPE_ERROR_RATE];
    if (error_engine->sample_count >= MIN_TREND_SAMPLES) {
        if (error_engine->mean > 10.0) { /* Maximum acceptable error rate */
            LOG_WARNING("NE2000 emulation error rate too high: %.2f errors/sec", error_engine->mean);
            return ERROR_HARDWARE_IO_ERROR;
        }
    }
    
    LOG_INFO("NE2000 emulation statistical validation passed");
    return SUCCESS;
}