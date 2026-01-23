/**
 * @file performance_enabler.h
 * @brief Performance enabler system declarations
 *
 * 3Com Packet Driver - Performance Enabler System
 *
 * This header defines the interface for the performance enabler system
 * that guides users to achieve 15-35% system-wide performance improvements
 * by safely enabling write-back caching.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef PERFORMANCE_ENABLER_H
#define PERFORMANCE_ENABLER_H

#include <stdint.h>
#include <stdbool.h>
#include "cachecoh.h"

/* Performance validation status */
typedef enum {
    VALIDATION_STATUS_SUCCESS,      /* Optimization successful */
    VALIDATION_STATUS_PARTIAL,      /* Partial success */
    VALIDATION_STATUS_PENDING,      /* Changes pending (reboot needed) */
    VALIDATION_STATUS_ERROR         /* Validation error */
} validation_status_t;

/* Performance metrics for before/after comparison */
typedef struct {
    uint32_t file_io_benchmark;     /* Milliseconds for 1MB file operation */
    uint32_t memory_bandwidth;      /* KB/s memory copy performance */
    uint32_t network_throughput;    /* Packets/second sustainable rate */
    cache_mode_t cache_mode;        /* Current cache configuration */
    uint32_t timestamp;             /* When metrics were collected */
} performance_metrics_t;

/* Performance validation result */
typedef struct {
    validation_status_t validation_status;
    float overall_improvement;          /* Overall improvement percentage */
    float file_io_improvement;          /* File I/O improvement percentage */
    float memory_improvement;           /* Memory improvement percentage */
    float network_improvement;          /* Network improvement percentage */
    char status_message[128];           /* Human-readable status */
    uint32_t validation_timestamp;      /* When validation was performed */
} performance_validation_result_t;

/* Performance enabler configuration */
typedef struct {
    performance_opportunity_t opportunity;              /* Detected opportunity level */
    cache_recommendation_t recommendation;              /* Recommended action */
    cache_mode_t current_cache_mode;                   /* Current cache configuration */
    bool cache_management_available;                   /* DMA cache management available */
    bool write_back_safe;                              /* Write-back is safe with our management */
    float expected_system_improvement;                 /* Expected system-wide improvement % */
    float expected_file_improvement;                   /* Expected file I/O improvement % */
    float expected_compile_improvement;                /* Expected compilation improvement % */
    float expected_graphics_improvement;               /* Expected graphics improvement % */
    bool user_guidance_offered;                        /* Whether user was offered guidance */
    bool user_accepted_guidance;                       /* Whether user accepted guidance */
} performance_enabler_config_t;

/* Performance improvement statistics */
typedef struct {
    uint32_t systems_analyzed;                         /* Total systems analyzed */
    uint32_t write_through_systems_found;              /* Systems with write-through cache */
    uint32_t users_who_enabled_write_back;             /* Users who enabled write-back */
    float average_performance_improvement;             /* Average improvement percentage */
    uint32_t total_performance_years_saved;            /* Cumulative time saved (hours) */
    char most_improved_application[64];                /* Application with biggest gain */
} performance_improvement_statistics_t;

/* Function declarations */

/* Initialization and configuration */
bool initialize_performance_enabler(const coherency_analysis_t *analysis);
performance_enabler_config_t get_performance_enabler_config(void);
bool is_performance_enabler_initialized(void);

/* Core analysis functions */
performance_opportunity_t analyze_performance_opportunity(const coherency_analysis_t *coherency);
cache_recommendation_t generate_cache_recommendation(const coherency_analysis_t *coherency, 
                                                    performance_opportunity_t opportunity);

/* User interaction functions */
void display_performance_opportunity_analysis(void);
void display_optimization_success_message(void);
void display_bios_troubleshooting_guide(void);

/* Performance measurement and validation */
performance_metrics_t measure_system_performance(void);
performance_validation_result_t validate_write_back_enablement_success(
    const performance_metrics_t *before,
    const performance_metrics_t *after);
void display_performance_validation_results(const performance_validation_result_t *result);

/* Community contribution functions */
void contribute_performance_improvement_case(const performance_metrics_t *before,
                                           const performance_metrics_t *after);
performance_improvement_statistics_t get_community_improvement_statistics(void);

/* Utility functions */
float calculate_performance_improvement_percentage(const performance_metrics_t *before,
                                                  const performance_metrics_t *after);
float estimate_annual_time_savings(float improvement_percentage);
const char* get_performance_opportunity_description(performance_opportunity_t opportunity);
const char* get_cache_recommendation_description(cache_recommendation_t recommendation);

/* Configuration and settings */
bool should_offer_performance_guidance(const coherency_analysis_t *analysis);
bool is_dedicated_networking_system(void);
bool user_previously_declined_guidance(void);
void mark_user_guidance_offered(bool accepted);

/* Performance benchmarking helpers */
uint32_t benchmark_file_io_performance(void);
uint32_t benchmark_memory_bandwidth(void);
uint32_t benchmark_network_throughput(void);

/* Constants for performance thresholds */
#define MIN_SIGNIFICANT_IMPROVEMENT     10.0f   /* Minimum improvement % to report */
#define EXPECTED_FILE_IO_IMPROVEMENT    30.0f   /* Expected file I/O improvement % */
#define EXPECTED_MEMORY_IMPROVEMENT     25.0f   /* Expected memory improvement % */
#define EXPECTED_COMPILE_IMPROVEMENT    35.0f   /* Expected compilation improvement % */
#define EXPECTED_GRAPHICS_IMPROVEMENT   20.0f   /* Expected graphics improvement % */

#define PERFORMANCE_MEASUREMENT_TIMEOUT 5000    /* Timeout for performance tests (ms) */
#define PERFORMANCE_VALIDATION_DELAY    2000    /* Delay before validation (ms) */

/* Performance improvement ranges */
#define IMPROVEMENT_RANGE_LOW           15.0f   /* Low end of improvement range */
#define IMPROVEMENT_RANGE_HIGH          35.0f   /* High end of improvement range */
#define IMPROVEMENT_RANGE_AVERAGE       25.0f   /* Average expected improvement */

/* Error codes */
#define PERF_ENABLER_SUCCESS            0
#define PERF_ENABLER_ERROR_INVALID      -1
#define PERF_ENABLER_ERROR_NOT_INIT     -2
#define PERF_ENABLER_ERROR_NO_OPPORTUNITY -3
#define PERF_ENABLER_ERROR_MEASUREMENT  -4

/* Macros for performance analysis */
#define HAS_MAJOR_PERFORMANCE_OPPORTUNITY(config) \
    ((config)->opportunity == PERFORMANCE_OPPORTUNITY_ENABLE_WB)

#define IS_ALREADY_OPTIMIZED(config) \
    ((config)->opportunity == PERFORMANCE_OPPORTUNITY_OPTIMIZED || \
     (config)->opportunity == PERFORMANCE_OPPORTUNITY_OPTIMAL)

#define SHOULD_ENCOURAGE_WRITE_BACK(analysis) \
    (((analysis)->coherency != COHERENCY_PROBLEM || \
      (analysis)->selected_tier <= CACHE_TIER_3_SOFTWARE) && \
     detect_cache_mode() != CACHE_WRITE_BACK)

#define PERFORMANCE_IMPROVEMENT_SIGNIFICANT(before, after) \
    (calculate_performance_improvement_percentage((before), (after)) >= MIN_SIGNIFICANT_IMPROVEMENT)

#endif /* PERFORMANCE_ENABLER_H */