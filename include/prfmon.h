/**
 * @file performance_monitor.h
 * @brief Phase 3 Performance Monitoring System Header
 *
 * 3Com Packet Driver - Performance Monitoring and Analysis
 *
 * This header defines the interface for the comprehensive performance
 * monitoring system that tracks ISR execution times, optimization
 * effectiveness, and provides real-time performance analysis.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* Performance monitoring constants */
#define PERF_ISR_TARGET_TIME_US         100     /* Target ISR execution time */
#define PERF_MONITOR_MAX_SAMPLES        1000    /* Maximum history samples */
#define PERF_OPTIMIZATION_THRESHOLD     10      /* 10% improvement threshold */

/* Performance metrics structure */
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

/* Performance status enumeration */
typedef enum {
    PERF_STATUS_OPTIMAL,        /* Performance is optimal */
    PERF_STATUS_GOOD,          /* Performance is good */
    PERF_STATUS_DEGRADED,      /* Performance is degraded */
    PERF_STATUS_CRITICAL       /* Performance is critical */
} performance_status_t;

/* Function prototypes */

/**
 * Initialize the performance monitoring system
 * @return true if successful, false otherwise
 */
bool performance_monitor_init(void);

/**
 * Record a performance sample
 * @param interrupt_type Type of interrupt processed
 * @param batch_size Number of interrupts in this batch
 */
void performance_monitor_record_sample(uint8_t interrupt_type, uint8_t batch_size);

/**
 * Get current performance metrics
 * @return Pointer to current performance metrics structure
 */
performance_metrics_t* performance_monitor_get_metrics(void);

/**
 * Set baseline metrics for comparison
 */
void performance_monitor_set_baseline(void);

/**
 * Get performance improvement over baseline
 * @return Improvement percentage (positive = better, negative = worse)
 */
float performance_monitor_get_improvement(void);

/**
 * Display comprehensive performance summary
 */
void performance_monitor_display_summary(void);

/**
 * Enable or disable performance monitoring
 * @param enable true to enable, false to disable
 */
void performance_monitor_enable(bool enable);

/**
 * Check if performance monitoring is active
 * @return true if active, false otherwise
 */
bool performance_monitor_is_active(void);

/**
 * Reset performance monitoring statistics
 */
void performance_monitor_reset(void);

/**
 * Get simple performance status
 * @return Performance status enumeration
 */
performance_status_t performance_monitor_get_status(void);

/**
 * Get performance status as a string
 * @return Status string
 */
const char* performance_monitor_get_status_string(void);

/* Assembly interface functions (called from assembly code) */

/**
 * Get current tick count for timing
 * @return Current system tick count
 */
uint32_t get_current_tick_count(void);

/* Critical errors counter (referenced by assembly) */
extern uint32_t critical_errors;

#endif /* PERFORMANCE_MONITOR_H */