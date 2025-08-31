/**
 * @file performance_monitor.c
 * @brief Phase 3 Performance Monitoring System
 *
 * 3Com Packet Driver - Performance Monitoring and Analysis
 *
 * This module provides comprehensive performance monitoring for Phase 3
 * optimizations, including ISR execution time tracking, throughput analysis,
 * and optimization effectiveness measurement.
 *
 * Key Features:
 * - Real-time ISR execution time monitoring (target: <100µs)
 * - Interrupt coalescing and batching statistics
 * - CPU-specific optimization effectiveness tracking
 * - Memory operation performance analysis
 * - LFSR generation optimization metrics
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/common.h"
#include "../include/performance_enabler.h"
#include "../include/cpu_detect.h"
#include "../include/logging.h"
#include "../include/stats.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Performance monitoring constants */
#define PERF_MONITOR_HISTORY_SIZE       1000    /* History buffer size */
#define PERF_ISR_TARGET_TIME_US         100     /* Target ISR execution time */
#define PERF_ANALYSIS_WINDOW_SIZE       100     /* Analysis window size */
#define PERF_OPTIMIZATION_THRESHOLD     10      /* 10% improvement threshold */

/* Performance data structures */
typedef struct {
    uint16_t isr_execution_time_us;             /* ISR execution time in microseconds */
    uint32_t timestamp;                         /* Timestamp of measurement */
    uint8_t  interrupt_type;                    /* Type of interrupt processed */
    uint8_t  batch_size;                        /* Number of interrupts batched */
    uint16_t cpu_usage_percent;                 /* CPU usage during processing */
} performance_sample_t;

typedef struct {
    uint32_t total_interrupts;                  /* Total interrupts processed */
    uint32_t batched_interrupts;                /* Interrupts processed in batches */
    uint32_t coalesced_interrupts;              /* Interrupts coalesced */
    uint32_t optimized_memory_ops;              /* Optimized memory operations */
    uint32_t lfsr_generations;                  /* LFSR values generated */
    uint64_t cpu_cycles_saved;                  /* Estimated CPU cycles saved */
    float    average_isr_time_us;               /* Average ISR execution time */
    float    peak_isr_time_us;                  /* Peak ISR execution time */
    float    optimization_efficiency;           /* Overall optimization efficiency */
    uint16_t performance_index;                 /* Composite performance index */
} performance_metrics_t;

typedef struct {
    bool     monitoring_enabled;                /* Monitoring state */
    bool     optimization_active;               /* Optimization state */
    uint16_t cpu_capabilities;                  /* CPU optimization capabilities */
    uint8_t  current_cpu_type;                  /* Current CPU type */
    uint16_t cpu_speed_mhz;                     /* CPU speed in MHz */
    performance_sample_t history[PERF_MONITOR_HISTORY_SIZE];
    uint16_t history_index;                     /* Current history buffer index */
    uint16_t history_count;                     /* Number of valid samples */
    performance_metrics_t current_metrics;      /* Current performance metrics */
    performance_metrics_t baseline_metrics;     /* Baseline metrics (before optimization) */
} performance_monitor_state_t;

/* Global performance monitor state */
static performance_monitor_state_t perf_monitor = {0};
static bool performance_monitor_initialized = false;

/* Forward declarations */
static void update_performance_metrics(void);
static void analyze_performance_trends(void);
static float calculate_optimization_efficiency(void);
static uint16_t calculate_performance_index(void);
static void detect_performance_anomalies(void);
static void log_performance_summary(void);

/* External assembly function prototypes */
extern uint16_t perf_measure_isr_execution_time(void);
extern void perf_get_performance_metrics(void *buffer);
extern uint16_t asm_get_cpu_flags(void);
extern uint8_t asm_detect_cpu_type(void);

/**
 * Initialize the performance monitoring system
 */
bool performance_monitor_init(void) {
    if (performance_monitor_initialized) {
        return true;
    }

    log_info("Initializing performance monitoring system...");

    /* Clear monitoring state */
    memset(&perf_monitor, 0, sizeof(performance_monitor_state_t));

    /* Detect CPU capabilities */
    perf_monitor.current_cpu_type = asm_detect_cpu_type();
    perf_monitor.cpu_capabilities = asm_get_cpu_flags();
    perf_monitor.cpu_speed_mhz = 25; /* Default 25MHz - would be detected */

    /* Initialize performance metrics */
    perf_monitor.current_metrics.average_isr_time_us = 0.0f;
    perf_monitor.current_metrics.peak_isr_time_us = 0.0f;
    perf_monitor.current_metrics.optimization_efficiency = 0.0f;
    perf_monitor.current_metrics.performance_index = 100; /* Baseline index */

    /* Set up monitoring parameters based on CPU type */
    switch (perf_monitor.current_cpu_type) {
        case 1: /* 80286 */
            /* More relaxed monitoring for slower CPUs */
            break;
        case 2: /* 80386 */
            /* Balanced monitoring */
            break;
        case 3: /* 80486 */
            /* Aggressive monitoring for faster CPUs */
            break;
        default:
            log_warning("Unknown CPU type %d, using default monitoring parameters",
                       perf_monitor.current_cpu_type);
            break;
    }

    /* Enable monitoring */
    perf_monitor.monitoring_enabled = true;
    performance_monitor_initialized = true;

    log_info("Performance monitoring initialized for CPU type %d (%dMHz)",
             perf_monitor.current_cpu_type, perf_monitor.cpu_speed_mhz);

    return true;
}

/**
 * Record a performance sample
 */
void performance_monitor_record_sample(uint8_t interrupt_type, uint8_t batch_size) {
    if (!perf_monitor.monitoring_enabled) {
        return;
    }

    /* Get current ISR execution time */
    uint16_t isr_time = perf_measure_isr_execution_time();

    /* Create performance sample */
    performance_sample_t *sample = &perf_monitor.history[perf_monitor.history_index];
    sample->isr_execution_time_us = isr_time;
    sample->timestamp = get_current_tick_count(); /* System tick count */
    sample->interrupt_type = interrupt_type;
    sample->batch_size = batch_size;
    sample->cpu_usage_percent = 50; /* Simplified - would calculate actual usage */

    /* Update history buffer */
    perf_monitor.history_index = (perf_monitor.history_index + 1) % PERF_MONITOR_HISTORY_SIZE;
    if (perf_monitor.history_count < PERF_MONITOR_HISTORY_SIZE) {
        perf_monitor.history_count++;
    }

    /* Update metrics periodically */
    if (perf_monitor.history_index % 10 == 0) {
        update_performance_metrics();
    }

    /* Check for target compliance */
    if (isr_time > PERF_ISR_TARGET_TIME_US) {
        log_warning("ISR execution time (%d µs) exceeds target (%d µs)",
                   isr_time, PERF_ISR_TARGET_TIME_US);
    }
}

/**
 * Update current performance metrics from history
 */
static void update_performance_metrics(void) {
    if (perf_monitor.history_count == 0) {
        return;
    }

    /* Get current assembly metrics */
    uint32_t asm_metrics[16]; /* Buffer for assembly metrics */
    perf_get_performance_metrics(asm_metrics);

    /* Update from assembly metrics */
    perf_monitor.current_metrics.total_interrupts = asm_metrics[1]; /* total_interrupts */
    perf_monitor.current_metrics.batched_interrupts = asm_metrics[2]; /* batched_interrupts */
    perf_monitor.current_metrics.optimized_memory_ops = asm_metrics[3]; /* memory_ops_optimized */
    perf_monitor.current_metrics.lfsr_generations = asm_metrics[4]; /* lfsr_generations */
    
    /* Calculate average ISR execution time */
    uint32_t total_time = 0;
    uint16_t max_time = 0;
    uint16_t samples_to_analyze = (perf_monitor.history_count < PERF_ANALYSIS_WINDOW_SIZE) ?
                                  perf_monitor.history_count : PERF_ANALYSIS_WINDOW_SIZE;

    for (uint16_t i = 0; i < samples_to_analyze; i++) {
        uint16_t idx = (perf_monitor.history_index + PERF_MONITOR_HISTORY_SIZE - 1 - i) % 
                       PERF_MONITOR_HISTORY_SIZE;
        uint16_t time = perf_monitor.history[idx].isr_execution_time_us;
        
        total_time += time;
        if (time > max_time) {
            max_time = time;
        }
    }

    /* Update calculated metrics */
    perf_monitor.current_metrics.average_isr_time_us = (float)total_time / samples_to_analyze;
    perf_monitor.current_metrics.peak_isr_time_us = (float)max_time;

    /* Calculate optimization efficiency */
    perf_monitor.current_metrics.optimization_efficiency = calculate_optimization_efficiency();

    /* Calculate performance index */
    perf_monitor.current_metrics.performance_index = calculate_performance_index();

    /* Analyze performance trends */
    analyze_performance_trends();
}

/**
 * Calculate optimization efficiency percentage
 */
static float calculate_optimization_efficiency(void) {
    if (perf_monitor.current_metrics.total_interrupts == 0) {
        return 0.0f;
    }

    float efficiency = 0.0f;

    /* Factor 1: Interrupt batching efficiency */
    if (perf_monitor.current_metrics.batched_interrupts > 0) {
        float batch_ratio = (float)perf_monitor.current_metrics.batched_interrupts / 
                           perf_monitor.current_metrics.total_interrupts;
        efficiency += batch_ratio * 25.0f; /* 25% weight */
    }

    /* Factor 2: ISR execution time performance */
    if (perf_monitor.current_metrics.average_isr_time_us > 0) {
        float time_efficiency = (PERF_ISR_TARGET_TIME_US - perf_monitor.current_metrics.average_isr_time_us) /
                               PERF_ISR_TARGET_TIME_US;
        if (time_efficiency < 0) time_efficiency = 0;
        efficiency += time_efficiency * 35.0f; /* 35% weight */
    }

    /* Factor 3: Memory operation optimization */
    if (perf_monitor.current_metrics.optimized_memory_ops > 0) {
        /* Assume optimization rate of 80% is ideal */
        float mem_efficiency = (float)perf_monitor.current_metrics.optimized_memory_ops / 
                              (perf_monitor.current_metrics.total_interrupts * 0.8f);
        if (mem_efficiency > 1.0f) mem_efficiency = 1.0f;
        efficiency += mem_efficiency * 20.0f; /* 20% weight */
    }

    /* Factor 4: CPU-specific optimizations active */
    if (perf_monitor.cpu_capabilities > 0) {
        efficiency += 20.0f; /* 20% weight for having optimizations */
    }

    return (efficiency > 100.0f) ? 100.0f : efficiency;
}

/**
 * Calculate composite performance index
 */
static uint16_t calculate_performance_index(void) {
    uint16_t index = 100; /* Baseline */

    /* Adjust based on ISR performance */
    if (perf_monitor.current_metrics.average_isr_time_us > 0) {
        if (perf_monitor.current_metrics.average_isr_time_us <= PERF_ISR_TARGET_TIME_US) {
            /* Better than target */
            float improvement = (PERF_ISR_TARGET_TIME_US - perf_monitor.current_metrics.average_isr_time_us) /
                               PERF_ISR_TARGET_TIME_US;
            index += (uint16_t)(improvement * 50); /* Up to 50 point bonus */
        } else {
            /* Worse than target */
            float penalty = (perf_monitor.current_metrics.average_isr_time_us - PERF_ISR_TARGET_TIME_US) /
                           PERF_ISR_TARGET_TIME_US;
            index -= (uint16_t)(penalty * 30); /* Up to 30 point penalty */
        }
    }

    /* Adjust based on optimization efficiency */
    index += (uint16_t)(perf_monitor.current_metrics.optimization_efficiency * 0.5f);

    /* Ensure reasonable bounds */
    if (index < 10) index = 10;
    if (index > 200) index = 200;

    return index;
}

/**
 * Analyze performance trends and detect issues
 */
static void analyze_performance_trends(void) {
    if (perf_monitor.history_count < 20) {
        return; /* Need more samples for trend analysis */
    }

    /* Analyze recent trend (last 20 samples) */
    float recent_avg = 0.0f;
    float earlier_avg = 0.0f;

    /* Calculate recent average (last 10 samples) */
    for (int i = 0; i < 10; i++) {
        uint16_t idx = (perf_monitor.history_index + PERF_MONITOR_HISTORY_SIZE - 1 - i) % 
                       PERF_MONITOR_HISTORY_SIZE;
        recent_avg += perf_monitor.history[idx].isr_execution_time_us;
    }
    recent_avg /= 10.0f;

    /* Calculate earlier average (samples 10-20 back) */
    for (int i = 10; i < 20; i++) {
        uint16_t idx = (perf_monitor.history_index + PERF_MONITOR_HISTORY_SIZE - 1 - i) % 
                       PERF_MONITOR_HISTORY_SIZE;
        earlier_avg += perf_monitor.history[idx].isr_execution_time_us;
    }
    earlier_avg /= 10.0f;

    /* Detect performance degradation */
    if (recent_avg > earlier_avg * 1.2f) {
        log_warning("Performance degradation detected: ISR time increased %.1f%% (%.1f -> %.1f µs)",
                   ((recent_avg - earlier_avg) / earlier_avg) * 100.0f,
                   earlier_avg, recent_avg);
        detect_performance_anomalies();
    }

    /* Detect performance improvement */
    if (recent_avg < earlier_avg * 0.8f) {
        log_info("Performance improvement detected: ISR time decreased %.1f%% (%.1f -> %.1f µs)",
                ((earlier_avg - recent_avg) / earlier_avg) * 100.0f,
                earlier_avg, recent_avg);
    }
}

/**
 * Detect performance anomalies and suggest fixes
 */
static void detect_performance_anomalies(void) {
    /* Check for excessive ISR execution times */
    if (perf_monitor.current_metrics.peak_isr_time_us > PERF_ISR_TARGET_TIME_US * 2) {
        log_error("Critical: Peak ISR execution time (%.1f µs) is %.1fx the target!",
                 perf_monitor.current_metrics.peak_isr_time_us,
                 perf_monitor.current_metrics.peak_isr_time_us / PERF_ISR_TARGET_TIME_US);
        log_error("Suggestion: Check for interrupt storms or disable interrupt coalescing");
    }

    /* Check for low batching efficiency */
    if (perf_monitor.current_metrics.total_interrupts > 100) {
        float batch_ratio = (float)perf_monitor.current_metrics.batched_interrupts / 
                           perf_monitor.current_metrics.total_interrupts;
        if (batch_ratio < 0.3f) {
            log_warning("Low interrupt batching efficiency (%.1f%%)",
                       batch_ratio * 100.0f);
            log_warning("Suggestion: Increase interrupt coalescing threshold");
        }
    }

    /* Check for memory optimization issues */
    if (perf_monitor.current_metrics.optimized_memory_ops == 0 && 
        perf_monitor.current_metrics.total_interrupts > 50) {
        log_warning("Memory operations not being optimized");
        log_warning("Suggestion: Check CPU capability detection and memory alignment");
    }
}

/**
 * Get current performance metrics
 */
performance_metrics_t* performance_monitor_get_metrics(void) {
    if (!performance_monitor_initialized) {
        return NULL;
    }
    
    update_performance_metrics();
    return &perf_monitor.current_metrics;
}

/**
 * Set baseline metrics for comparison
 */
void performance_monitor_set_baseline(void) {
    if (!performance_monitor_initialized) {
        return;
    }

    memcpy(&perf_monitor.baseline_metrics, &perf_monitor.current_metrics, 
           sizeof(performance_metrics_t));
    log_info("Performance baseline established");
}

/**
 * Get performance improvement over baseline
 */
float performance_monitor_get_improvement(void) {
    if (!performance_monitor_initialized || 
        perf_monitor.baseline_metrics.average_isr_time_us == 0) {
        return 0.0f;
    }

    float baseline_time = perf_monitor.baseline_metrics.average_isr_time_us;
    float current_time = perf_monitor.current_metrics.average_isr_time_us;

    if (current_time == 0) {
        return 0.0f;
    }

    return ((baseline_time - current_time) / baseline_time) * 100.0f;
}

/**
 * Display performance summary
 */
void performance_monitor_display_summary(void) {
    if (!performance_monitor_initialized) {
        printf("Performance monitoring not initialized\n");
        return;
    }

    update_performance_metrics();

    printf("\n=== PERFORMANCE MONITORING SUMMARY ===\n");
    printf("CPU: %s (%dMHz)\n", 
           (perf_monitor.current_cpu_type == 1) ? "80286" :
           (perf_monitor.current_cpu_type == 2) ? "80386" :
           (perf_monitor.current_cpu_type == 3) ? "80486" : "Unknown",
           perf_monitor.cpu_speed_mhz);
    printf("Monitoring Status: %s\n", 
           perf_monitor.monitoring_enabled ? "Active" : "Inactive");
    printf("Samples Collected: %d\n", perf_monitor.history_count);
    printf("\n--- ISR PERFORMANCE ---\n");
    printf("Target ISR Time: %d µs\n", PERF_ISR_TARGET_TIME_US);
    printf("Average ISR Time: %.1f µs\n", perf_monitor.current_metrics.average_isr_time_us);
    printf("Peak ISR Time: %.1f µs\n", perf_monitor.current_metrics.peak_isr_time_us);
    printf("Target Compliance: %s\n",
           (perf_monitor.current_metrics.average_isr_time_us <= PERF_ISR_TARGET_TIME_US) ?
           "✅ ACHIEVED" : "❌ EXCEEDED");

    printf("\n--- OPTIMIZATION METRICS ---\n");
    printf("Total Interrupts: %lu\n", perf_monitor.current_metrics.total_interrupts);
    printf("Batched Interrupts: %lu (%.1f%%)\n", 
           perf_monitor.current_metrics.batched_interrupts,
           perf_monitor.current_metrics.total_interrupts > 0 ?
           (float)perf_monitor.current_metrics.batched_interrupts / 
           perf_monitor.current_metrics.total_interrupts * 100.0f : 0.0f);
    printf("Optimized Memory Ops: %lu\n", perf_monitor.current_metrics.optimized_memory_ops);
    printf("LFSR Generations: %lu\n", perf_monitor.current_metrics.lfsr_generations);
    printf("Optimization Efficiency: %.1f%%\n", perf_monitor.current_metrics.optimization_efficiency);
    printf("Performance Index: %d/200\n", perf_monitor.current_metrics.performance_index);

    if (perf_monitor.baseline_metrics.average_isr_time_us > 0) {
        float improvement = performance_monitor_get_improvement();
        printf("\n--- IMPROVEMENT OVER BASELINE ---\n");
        printf("ISR Time Improvement: %.1f%%\n", improvement);
        printf("Performance Gain: %s\n",
               (improvement >= PERF_OPTIMIZATION_THRESHOLD) ?
               "🎯 SIGNIFICANT" :
               (improvement > 0) ? "📈 MODERATE" : "📉 DEGRADED");
    }

    printf("\n--- CPU OPTIMIZATION STATUS ---\n");
    printf("286+ Features: %s\n", (perf_monitor.cpu_capabilities & 0x01) ? "✅ Active" : "❌ Not Available");
    printf("386+ Features: %s\n", (perf_monitor.cpu_capabilities & 0x02) ? "✅ Active" : "❌ Not Available");
    printf("486+ Features: %s\n", (perf_monitor.cpu_capabilities & 0x04) ? "✅ Active" : "❌ Not Available");

    printf("========================================\n\n");

    /* Log to file as well */
    log_performance_summary();
}

/**
 * Log performance summary to log file
 */
static void log_performance_summary(void) {
    log_info("=== PERFORMANCE SUMMARY ===");
    log_info("Average ISR Time: %.1f µs (Target: %d µs)", 
             perf_monitor.current_metrics.average_isr_time_us, PERF_ISR_TARGET_TIME_US);
    log_info("Optimization Efficiency: %.1f%%", 
             perf_monitor.current_metrics.optimization_efficiency);
    log_info("Performance Index: %d/200", 
             perf_monitor.current_metrics.performance_index);
    log_info("Total Interrupts: %lu, Batched: %lu", 
             perf_monitor.current_metrics.total_interrupts,
             perf_monitor.current_metrics.batched_interrupts);
    
    if (perf_monitor.baseline_metrics.average_isr_time_us > 0) {
        log_info("Performance improvement: %.1f%%", performance_monitor_get_improvement());
    }
}

/**
 * Enable/disable performance monitoring
 */
void performance_monitor_enable(bool enable) {
    if (performance_monitor_initialized) {
        perf_monitor.monitoring_enabled = enable;
        log_info("Performance monitoring %s", enable ? "enabled" : "disabled");
    }
}

/**
 * Check if performance monitoring is active
 */
bool performance_monitor_is_active(void) {
    return performance_monitor_initialized && perf_monitor.monitoring_enabled;
}

/**
 * Reset performance monitoring statistics
 */
void performance_monitor_reset(void) {
    if (!performance_monitor_initialized) {
        return;
    }

    /* Clear history and metrics */
    memset(perf_monitor.history, 0, sizeof(perf_monitor.history));
    perf_monitor.history_index = 0;
    perf_monitor.history_count = 0;
    memset(&perf_monitor.current_metrics, 0, sizeof(performance_metrics_t));
    perf_monitor.current_metrics.performance_index = 100;

    log_info("Performance monitoring statistics reset");
}

/**
 * Get simple performance status for quick checks
 */
typedef enum {
    PERF_STATUS_OPTIMAL,        /* Performance is optimal */
    PERF_STATUS_GOOD,          /* Performance is good */
    PERF_STATUS_DEGRADED,      /* Performance is degraded */
    PERF_STATUS_CRITICAL       /* Performance is critical */
} performance_status_t;

performance_status_t performance_monitor_get_status(void) {
    if (!performance_monitor_initialized || perf_monitor.history_count < 5) {
        return PERF_STATUS_GOOD; /* Unknown, assume good */
    }

    float avg_time = perf_monitor.current_metrics.average_isr_time_us;
    
    if (avg_time <= PERF_ISR_TARGET_TIME_US * 0.8f) {
        return PERF_STATUS_OPTIMAL; /* 20% better than target */
    } else if (avg_time <= PERF_ISR_TARGET_TIME_US) {
        return PERF_STATUS_GOOD; /* At or better than target */
    } else if (avg_time <= PERF_ISR_TARGET_TIME_US * 1.5f) {
        return PERF_STATUS_DEGRADED; /* 50% worse than target */
    } else {
        return PERF_STATUS_CRITICAL; /* More than 50% worse */
    }
}

/**
 * Get performance status as a string
 */
const char* performance_monitor_get_status_string(void) {
    switch (performance_monitor_get_status()) {
        case PERF_STATUS_OPTIMAL:   return "OPTIMAL";
        case PERF_STATUS_GOOD:      return "GOOD";
        case PERF_STATUS_DEGRADED:  return "DEGRADED";
        case PERF_STATUS_CRITICAL:  return "CRITICAL";
        default:                    return "UNKNOWN";
    }
}