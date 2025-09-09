/**
 * @file unified_statistics.c
 * @brief Unified Statistics API - Agent 12 Implementation
 *
 * 3Com Packet Driver - Unified Statistics Aggregation
 * Collects and aggregates statistics from all modules (PTASK/CORKSCRW/BOOMTEX)
 * providing comprehensive performance monitoring and reporting capabilities.
 * 
 * Features:
 * - Multi-module statistics aggregation
 * - Real-time performance monitoring
 * - Historical data tracking
 * - Configurable collection intervals
 * - Statistics export and reporting
 * - Performance trend analysis
 * 
 * Agent 12: Driver API
 * Week 1 Deliverable - Unified statistics system
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "unified_api.h"
#include "metrics_core.h"
#include "../include/logging.h"
#include "../include/stats.h"
#include "../../docs/agents/shared/error-codes.h"

/* Statistics Collection Constants */
#define STATISTICS_SIGNATURE       "STAT"
#define STATISTICS_VERSION         0x0100
#define MAX_HISTORY_SAMPLES        60      /* 60 samples for trend analysis */
#define DEFAULT_COLLECTION_INTERVAL 1000   /* 1 second in milliseconds */

/* Statistics Categories */
#define STAT_CATEGORY_GLOBAL       0x01
#define STAT_CATEGORY_MODULE       0x02
#define STAT_CATEGORY_HANDLE       0x04
#define STAT_CATEGORY_INTERFACE    0x08
#define STAT_CATEGORY_PERFORMANCE  0x10
#define STAT_CATEGORY_ERROR        0x20
#define STAT_CATEGORY_ALL          0xFF

/* Performance Counter Types */
typedef enum {
    PERF_COUNTER_PACKETS_RX = 0,
    PERF_COUNTER_PACKETS_TX,
    PERF_COUNTER_BYTES_RX,
    PERF_COUNTER_BYTES_TX,
    PERF_COUNTER_ERRORS,
    PERF_COUNTER_DROPS,
    PERF_COUNTER_API_CALLS,
    PERF_COUNTER_INTERRUPTS,
    PERF_COUNTER_COUNT
} perf_counter_type_t;

/* Historical Sample */
typedef struct {
    uint32_t timestamp;                 /* Sample timestamp */
    uint32_t counters[PERF_COUNTER_COUNT]; /* Performance counters */
} history_sample_t;

/* Module Statistics */
typedef struct {
    char module_name[12];               /* Module name */
    uint8_t module_id;                  /* Module identifier */
    bool active;                        /* Module is active */
    
    /* Packet Statistics */
    uint32_t packets_rx;                /* Received packets */
    uint32_t packets_tx;                /* Transmitted packets */
    uint32_t bytes_rx;                  /* Received bytes */
    uint32_t bytes_tx;                  /* Transmitted bytes */
    uint32_t packets_dropped;           /* Dropped packets */
    uint32_t errors;                    /* Error count */
    
    /* Performance Statistics */
    uint32_t avg_latency_us;            /* Average latency in microseconds */
    uint32_t max_latency_us;            /* Maximum latency in microseconds */
    uint32_t cpu_utilization;           /* CPU utilization percentage */
    uint32_t memory_usage;              /* Memory usage in bytes */
    
    /* Interface Statistics */
    uint32_t link_up_count;             /* Link up events */
    uint32_t link_down_count;           /* Link down events */
    uint32_t collision_count;           /* Collision count */
    uint32_t crc_errors;                /* CRC error count */
    
    /* Timing Statistics */
    uint32_t last_activity_time;        /* Last activity timestamp */
    uint32_t uptime;                    /* Module uptime */
    
} module_statistics_t;

/* Global Statistics */
typedef struct {
    /* Aggregate Packet Statistics */
    uint64_t total_packets_rx;          /* Total received packets (all modules) */
    uint64_t total_packets_tx;          /* Total transmitted packets (all modules) */
    uint64_t total_bytes_rx;            /* Total received bytes (all modules) */
    uint64_t total_bytes_tx;            /* Total transmitted bytes (all modules) */
    uint64_t total_errors;              /* Total error count (all modules) */
    uint64_t total_drops;               /* Total dropped packets (all modules) */
    
    /* API Statistics */
    uint32_t api_calls_total;           /* Total API calls */
    uint32_t api_calls_success;         /* Successful API calls */
    uint32_t api_calls_error;           /* Failed API calls */
    uint32_t api_avg_response_time;     /* Average API response time */
    uint32_t api_max_response_time;     /* Maximum API response time */
    
    /* Handle Management Statistics */
    uint16_t handles_active;            /* Currently active handles */
    uint16_t handles_peak;              /* Peak concurrent handles */
    uint32_t handles_allocated;         /* Total handles allocated */
    uint32_t handles_freed;             /* Total handles freed */
    
    /* System Statistics */
    uint32_t interrupts_total;          /* Total hardware interrupts */
    uint32_t context_switches;          /* Context switches between modules */
    uint32_t memory_allocated;          /* Total memory allocated */
    uint32_t memory_peak;               /* Peak memory usage */
    
} global_statistics_t;

/* Statistics Manager */
typedef struct {
    char signature[4];                  /* Statistics signature */
    uint16_t version;                   /* Statistics version */
    bool initialized;                   /* Initialization flag */
    uint32_t collection_interval;       /* Collection interval in ms */
    uint32_t last_collection_time;      /* Last collection timestamp */
    
    /* Statistics Data */
    global_statistics_t global;         /* Global statistics */
    module_statistics_t modules[UNIFIED_MODULE_COUNT]; /* Per-module statistics */
    
    /* Historical Data */
    uint8_t history_index;              /* Current history index */
    uint8_t history_count;              /* Number of history samples */
    history_sample_t history[MAX_HISTORY_SAMPLES]; /* Historical samples */
    
    /* Collection Control */
    uint8_t collection_mask;            /* Statistics collection mask */
    bool collection_enabled;            /* Collection enabled flag */
    bool trend_analysis_enabled;       /* Trend analysis enabled flag */
    
} statistics_manager_t;

/* Global Statistics Manager */
static statistics_manager_t g_stats_manager;

/* Forward Declarations */
static void collect_global_statistics(void);
static void collect_module_statistics(uint8_t module_id);
static void update_historical_data(void);
static void calculate_performance_trends(void);
static uint32_t calculate_rate_per_second(uint32_t current, uint32_t previous, uint32_t time_diff);

/**
 * @brief Initialize Unified Statistics System
 * @param collection_interval Collection interval in milliseconds (0 = default)
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_init(uint32_t collection_interval) {
    if (g_stats_manager.initialized) {
        return SUCCESS;
    }
    
    log_info("Initializing Unified Statistics System");
    
    /* Initialize statistics manager */
    memset(&g_stats_manager, 0, sizeof(statistics_manager_t));
    strncpy(g_stats_manager.signature, STATISTICS_SIGNATURE, 4);
    g_stats_manager.version = STATISTICS_VERSION;
    g_stats_manager.collection_interval = collection_interval ? collection_interval : DEFAULT_COLLECTION_INTERVAL;
    g_stats_manager.collection_mask = STAT_CATEGORY_ALL;
    g_stats_manager.collection_enabled = true;
    g_stats_manager.trend_analysis_enabled = true;
    
    /* Initialize module statistics */
    for (int i = 0; i < UNIFIED_MODULE_COUNT; i++) {
        module_statistics_t *module = &g_stats_manager.modules[i];
        module->module_id = i;
        module->active = false;
        
        switch (i) {
            case UNIFIED_MODULE_PTASK:
                strncpy(module->module_name, "PTASK", sizeof(module->module_name));
                break;
            case UNIFIED_MODULE_CORKSCRW:
                strncpy(module->module_name, "CORKSCRW", sizeof(module->module_name));
                break;
            case UNIFIED_MODULE_BOOMTEX:
                strncpy(module->module_name, "BOOMTEX", sizeof(module->module_name));
                break;
            default:
                snprintf(module->module_name, sizeof(module->module_name), "MODULE_%d", i);
                break;
        }
    }
    
    /* Initialize historical data */
    g_stats_manager.history_index = 0;
    g_stats_manager.history_count = 0;
    
    /* Initialize metrics core system */
    int metrics_result = metrics_init();
    if (metrics_result != 0) {
        log_error("Failed to initialize metrics core: %d", metrics_result);
        return ERROR_INITIALIZATION_FAILED;
    }
    
    g_stats_manager.last_collection_time = get_system_time();
    g_stats_manager.initialized = true;
    
    log_info("Unified Statistics System initialized (interval=%lu ms)", g_stats_manager.collection_interval);
    
    return SUCCESS;
}

/**
 * @brief Cleanup Unified Statistics System
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_cleanup(void) {
    if (!g_stats_manager.initialized) {
        return SUCCESS;
    }
    
    log_info("Cleaning up Unified Statistics System");
    
    /* Log final statistics summary */
    log_info("Final Statistics Summary:");
    log_info("  Total Packets RX: %lu", (uint32_t)g_stats_manager.global.total_packets_rx);
    log_info("  Total Packets TX: %lu", (uint32_t)g_stats_manager.global.total_packets_tx);
    log_info("  Total Bytes RX: %lu", (uint32_t)g_stats_manager.global.total_bytes_rx);
    log_info("  Total Bytes TX: %lu", (uint32_t)g_stats_manager.global.total_bytes_tx);
    log_info("  Total Errors: %lu", (uint32_t)g_stats_manager.global.total_errors);
    log_info("  Total API Calls: %lu", g_stats_manager.global.api_calls_total);
    log_info("  Peak Handles: %d", g_stats_manager.global.handles_peak);
    
    /* Cleanup metrics core system */
    metrics_cleanup();
    
    g_stats_manager.initialized = false;
    log_info("Unified Statistics System cleanup completed");
    
    return SUCCESS;
}

/**
 * @brief Collect current statistics from all modules
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_collect(void) {
    uint32_t current_time;
    
    if (!g_stats_manager.initialized || !g_stats_manager.collection_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    current_time = get_system_time();
    
    /* Check if collection interval has elapsed */
    if (current_time - g_stats_manager.last_collection_time < g_stats_manager.collection_interval) {
        return SUCCESS; /* Too soon to collect */
    }
    
    log_debug("Collecting unified statistics");
    
    /* Collect global statistics */
    if (g_stats_manager.collection_mask & STAT_CATEGORY_GLOBAL) {
        collect_global_statistics();
    }
    
    /* Collect per-module statistics */
    if (g_stats_manager.collection_mask & STAT_CATEGORY_MODULE) {
        for (int i = 0; i < UNIFIED_MODULE_COUNT; i++) {
            if (g_stats_manager.modules[i].active) {
                collect_module_statistics(i);
            }
        }
    }
    
    /* Update historical data */
    if (g_stats_manager.trend_analysis_enabled) {
        update_historical_data();
    }
    
    g_stats_manager.last_collection_time = current_time;
    
    /* Process TX completion metrics */
    metrics_process_tx_completions();
    
    return SUCCESS;
}

/**
 * @brief Get unified statistics
 * @param stats Statistics structure to fill
 * @param category Statistics category mask
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_get(unified_statistics_t *stats, uint8_t category) {
    if (!g_stats_manager.initialized || !stats) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Collect current statistics first */
    unified_statistics_collect();
    
    /* Clear output structure */
    memset(stats, 0, sizeof(unified_statistics_t));
    
    /* Fill global statistics */
    if (category & STAT_CATEGORY_GLOBAL) {
        stats->total_packets_in = (uint32_t)g_stats_manager.global.total_packets_rx;
        stats->total_packets_out = (uint32_t)g_stats_manager.global.total_packets_tx;
        stats->total_bytes_in = (uint32_t)g_stats_manager.global.total_bytes_rx;
        stats->total_bytes_out = (uint32_t)g_stats_manager.global.total_bytes_tx;
        stats->total_errors = (uint32_t)g_stats_manager.global.total_errors;
        stats->total_drops = (uint32_t)g_stats_manager.global.total_drops;
        
        stats->api_call_count = g_stats_manager.global.api_calls_total;
        stats->api_total_time = g_stats_manager.global.api_avg_response_time;
        stats->api_max_time = g_stats_manager.global.api_max_response_time;
        stats->api_min_time = 0; /* Min time tracking not implemented yet */
        
        stats->active_handles = g_stats_manager.global.handles_active;
        stats->peak_handles = g_stats_manager.global.handles_peak;
        stats->handle_allocations = g_stats_manager.global.handles_allocated;
        stats->handle_deallocations = g_stats_manager.global.handles_freed;
    }
    
    /* Fill per-module statistics */
    if (category & STAT_CATEGORY_MODULE) {
        for (int i = 0; i < UNIFIED_MODULE_COUNT && i < UNIFIED_MODULE_COUNT; i++) {
            if (g_stats_manager.modules[i].active) {
                stats->module_packets_in[i] = g_stats_manager.modules[i].packets_rx;
                stats->module_packets_out[i] = g_stats_manager.modules[i].packets_tx;
                stats->module_errors[i] = g_stats_manager.modules[i].errors;
            }
        }
    }
    
    /* Fill performance statistics */
    if (category & STAT_CATEGORY_PERFORMANCE) {
        stats->interrupt_count = g_stats_manager.global.interrupts_total;
        stats->context_switches = g_stats_manager.global.context_switches;
        stats->memory_allocated = (uint16_t)(g_stats_manager.global.memory_allocated / 16); /* Paragraphs */
        stats->memory_peak = (uint16_t)(g_stats_manager.global.memory_peak / 16); /* Paragraphs */
    }
    
    return SUCCESS;
}

/**
 * @brief Update statistics for a module
 * @param module_id Module identifier
 * @param packets_rx Received packets delta
 * @param packets_tx Transmitted packets delta  
 * @param bytes_rx Received bytes delta
 * @param bytes_tx Transmitted bytes delta
 * @param errors Error count delta
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_update_module(uint8_t module_id, uint32_t packets_rx, uint32_t packets_tx,
                                    uint32_t bytes_rx, uint32_t bytes_tx, uint32_t errors) {
    
    if (!g_stats_manager.initialized || module_id >= UNIFIED_MODULE_COUNT) {
        return ERROR_INVALID_PARAM;
    }
    
    module_statistics_t *module = &g_stats_manager.modules[module_id];
    
    /* Update module statistics */
    module->packets_rx += packets_rx;
    module->packets_tx += packets_tx;
    module->bytes_rx += bytes_rx;
    module->bytes_tx += bytes_tx;
    module->errors += errors;
    module->last_activity_time = get_system_time();
    module->active = true;
    
    /* Update global aggregates */
    g_stats_manager.global.total_packets_rx += packets_rx;
    g_stats_manager.global.total_packets_tx += packets_tx;
    g_stats_manager.global.total_bytes_rx += bytes_rx;
    g_stats_manager.global.total_bytes_tx += bytes_tx;
    g_stats_manager.global.total_errors += errors;
    
    return SUCCESS;
}

/**
 * @brief Update API call statistics
 * @param success API call was successful
 * @param response_time API response time in microseconds
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_update_api(bool success, uint32_t response_time) {
    if (!g_stats_manager.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_stats_manager.global.api_calls_total++;
    
    if (success) {
        g_stats_manager.global.api_calls_success++;
    } else {
        g_stats_manager.global.api_calls_error++;
    }
    
    /* Update response time statistics */
    if (response_time > g_stats_manager.global.api_max_response_time) {
        g_stats_manager.global.api_max_response_time = response_time;
    }
    
    /* Calculate running average */
    if (g_stats_manager.global.api_calls_total > 0) {
        g_stats_manager.global.api_avg_response_time = 
            ((g_stats_manager.global.api_avg_response_time * (g_stats_manager.global.api_calls_total - 1)) + 
             response_time) / g_stats_manager.global.api_calls_total;
    }
    
    return SUCCESS;
}

/**
 * @brief Get performance trends
 * @param trends Trends structure to fill
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_get_trends(void *trends) {
    /* Basic trend analysis using historical samples */
    if (!g_stats_manager.initialized || g_stats_manager.history_count < 2) {
        return ERROR_INSUFFICIENT_DATA;
    }
    
    /* Simple trend calculation using first and last samples */
    uint8_t first_idx = (g_stats_manager.history_index + MAX_HISTORY_SAMPLES - g_stats_manager.history_count) % MAX_HISTORY_SAMPLES;
    uint8_t last_idx = (g_stats_manager.history_index + MAX_HISTORY_SAMPLES - 1) % MAX_HISTORY_SAMPLES;
    
    history_sample_t *first = &g_stats_manager.history[first_idx];
    history_sample_t *last = &g_stats_manager.history[last_idx];
    
    uint32_t time_diff = last->timestamp - first->timestamp;
    if (time_diff == 0) {
        return ERROR_INVALID_DATA;
    }
    
    /* Calculate packet rate trends (packets per second) */
    typedef struct {
        int32_t packet_trend;     /* Change in packets/sec */
        int32_t error_trend;      /* Change in error rate */
    } simple_trends_t;
    
    simple_trends_t *simple_trends = (simple_trends_t *)trends;
    
    /* RX packet trend */
    uint32_t rx_first = first->counters[PERF_COUNTER_PACKETS_RX];
    uint32_t rx_last = last->counters[PERF_COUNTER_PACKETS_RX];
    simple_trends->packet_trend = (int32_t)((rx_last - rx_first) * 1000 / time_diff);
    
    /* Error trend */
    uint32_t err_first = first->counters[PERF_COUNTER_ERRORS];
    uint32_t err_last = last->counters[PERF_COUNTER_ERRORS];
    simple_trends->error_trend = (int32_t)((err_last - err_first) * 1000 / time_diff);
    
    return SUCCESS;
}

/**
 * @brief Reset statistics counters
 * @param category Statistics category to reset
 * @return SUCCESS on success, error code on failure
 */
int unified_statistics_reset(uint8_t category) {
    if (!g_stats_manager.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    log_info("Resetting statistics (category=0x%02X)", category);
    
    /* Reset global statistics */
    if (category & STAT_CATEGORY_GLOBAL) {
        memset(&g_stats_manager.global, 0, sizeof(global_statistics_t));
    }
    
    /* Reset module statistics */
    if (category & STAT_CATEGORY_MODULE) {
        for (int i = 0; i < UNIFIED_MODULE_COUNT; i++) {
            module_statistics_t *module = &g_stats_manager.modules[i];
            /* Preserve module identification and active status */
            uint8_t module_id = module->module_id;
            char module_name[12];
            bool active = module->active;
            strncpy(module_name, module->module_name, sizeof(module_name));
            
            memset(module, 0, sizeof(module_statistics_t));
            
            /* Restore preserved fields */
            module->module_id = module_id;
            strncpy(module->module_name, module_name, sizeof(module->module_name));
            module->active = active;
        }
    }
    
    /* Reset historical data */
    if (category & STAT_CATEGORY_PERFORMANCE) {
        memset(g_stats_manager.history, 0, sizeof(g_stats_manager.history));
        g_stats_manager.history_index = 0;
        g_stats_manager.history_count = 0;
    }
    
    return SUCCESS;
}

/* Internal Helper Functions */

static void collect_global_statistics(void) {
    /* Update handle statistics */
    /* Get current handle count from metrics core */
    g_stats_manager.global.handles_active = (uint16_t)metrics_get_handle_count();
    if (g_stats_manager.global.handles_active > g_stats_manager.global.handles_peak) {
        g_stats_manager.global.handles_peak = g_stats_manager.global.handles_active;
    }
    
    /* Update memory statistics */
    /* Get current memory usage from metrics core */
    uint32_t mem_used = metrics_get_memory_usage();
    g_stats_manager.global.memory_allocated = mem_used;
    if (mem_used > g_stats_manager.global.memory_peak) {
        g_stats_manager.global.memory_peak = mem_used;
    }
    
    /* Update interrupt statistics */
    /* Get interrupt count from metrics core */
    g_stats_manager.global.interrupts_total = metrics_get_interrupt_count();
}

static void collect_module_statistics(uint8_t module_id) {
    if (module_id >= UNIFIED_MODULE_COUNT) {
        return;
    }
    
    module_statistics_t *module = &g_stats_manager.modules[module_id];
    
    /* Update uptime */
    module->uptime = get_system_time(); /* Simplified uptime calculation */
    
    /* Collect additional module-specific statistics from metrics core */
    uint32_t rx_packets, tx_packets, errors, avg_lat, min_lat, max_lat;
    
    metrics_get_module_perf(module_id, &rx_packets, &tx_packets, &errors,
                           &avg_lat, &min_lat, &max_lat);
    
    /* Update module statistics */
    module->packets_rx = rx_packets;
    module->packets_tx = tx_packets;
    module->errors = errors;
    module->avg_latency_us = avg_lat;
    module->max_latency_us = max_lat;
    
    /* Update per-module memory usage */
    uint32_t module_handles = metrics_get_module_handles(module_id);
    /* Store handle count in cpu_utilization field for now */
    module->cpu_utilization = module_handles;
}

static void update_historical_data(void) {
    history_sample_t *sample = &g_stats_manager.history[g_stats_manager.history_index];
    
    /* Record current timestamp */
    sample->timestamp = get_system_time();
    
    /* Record performance counters */
    sample->counters[PERF_COUNTER_PACKETS_RX] = (uint32_t)g_stats_manager.global.total_packets_rx;
    sample->counters[PERF_COUNTER_PACKETS_TX] = (uint32_t)g_stats_manager.global.total_packets_tx;
    sample->counters[PERF_COUNTER_BYTES_RX] = (uint32_t)g_stats_manager.global.total_bytes_rx;
    sample->counters[PERF_COUNTER_BYTES_TX] = (uint32_t)g_stats_manager.global.total_bytes_tx;
    sample->counters[PERF_COUNTER_ERRORS] = (uint32_t)g_stats_manager.global.total_errors;
    sample->counters[PERF_COUNTER_DROPS] = (uint32_t)g_stats_manager.global.total_drops;
    sample->counters[PERF_COUNTER_API_CALLS] = g_stats_manager.global.api_calls_total;
    sample->counters[PERF_COUNTER_INTERRUPTS] = g_stats_manager.global.interrupts_total;
    
    /* Advance history index */
    g_stats_manager.history_index = (g_stats_manager.history_index + 1) % MAX_HISTORY_SAMPLES;
    
    if (g_stats_manager.history_count < MAX_HISTORY_SAMPLES) {
        g_stats_manager.history_count++;
    }
}

static void calculate_performance_trends(void) {
    /* Calculate performance trends from historical data */
    if (g_stats_manager.history_count < 2) {
        return;
    }
    
    /* Use simple linear regression approach for trends */
    uint32_t sum_t = 0, sum_v = 0, sum_tv = 0, sum_t2 = 0;
    uint32_t n = g_stats_manager.history_count;
    
    /* Calculate sums for linear regression */
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (g_stats_manager.history_index + MAX_HISTORY_SAMPLES - n + i) % MAX_HISTORY_SAMPLES;
        history_sample_t *sample = &g_stats_manager.history[idx];
        
        uint32_t t = i;  /* Simplified time index */
        uint32_t v = sample->counters[PERF_COUNTER_PACKETS_RX];
        
        sum_t += t;
        sum_v += v;
        sum_tv += t * v;
        sum_t2 += t * t;
    }
    
    /* Calculate trend slope (simplified) */
    if (n * sum_t2 > sum_t * sum_t) {
        /* slope = (n*sum_tv - sum_t*sum_v) / (n*sum_t2 - sum_t*sum_t) */
        /* Simplified calculation to avoid overflow */
        uint32_t numerator = (n * sum_tv > sum_t * sum_v) ? (n * sum_tv - sum_t * sum_v) : 0;
        uint32_t denominator = n * sum_t2 - sum_t * sum_t;
        /* Store trend info in global stats for later retrieval */
        g_stats_manager.global.context_switches = numerator / denominator;
    }
}

static uint32_t calculate_rate_per_second(uint32_t current, uint32_t previous, uint32_t time_diff) {
    if (time_diff == 0) {
        return 0;
    }
    
    if (current >= previous) {
        return ((current - previous) * 1000) / time_diff; /* Convert ms to seconds */
    }
    
    return 0; /* Handle counter wraparound case */
}

/* External system time function */
extern uint32_t get_system_time(void);