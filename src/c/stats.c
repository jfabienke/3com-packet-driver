/**
 * @file stats.c
 * @brief Statistics gathering and reporting
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "dos_io.h"
#include <string.h>
#include <dos.h>
#include "stats.h"
#include "logging.h"
#include "hardware.h"
#include "common.h"

/* Global statistics */
static driver_stats_t global_stats = {0};
static nic_stats_t nic_stats[MAX_NICS] = {0};
static int stats_initialized = 0;
static uint32_t stats_start_time = 0;

/* Production hardware statistics state */
static struct {
    /* Real-time hardware register tracking */
    uint32_t register_read_count;
    uint32_t register_read_errors;
    uint32_t register_corruption_events;
    
    /* Performance metrics collection */
    uint32_t metrics_collection_count;
    uint32_t metrics_collection_errors;
    uint32_t last_metrics_time;
    
    /* Error pattern analysis */
    uint32_t error_patterns[MAX_NICS][16];  /* Error pattern tracking per NIC */
    uint32_t error_burst_events;
    uint32_t error_trend_changes;
    
    /* Memory usage tracking */
    uint32_t peak_memory_usage;
    uint32_t current_memory_usage;
    uint32_t memory_leak_events;
    
    /* Network health monitoring */
    uint32_t health_checks_performed;
    uint32_t health_warnings_issued;
    uint32_t health_critical_events;
    
    /* Predictive analysis */
    uint32_t prediction_calculations;
    uint32_t prediction_accuracy;
    uint32_t early_warnings_issued;
} g_production_stats = {0};

/**
 * @brief Get system timestamp
 * @return Current timestamp (implementation-dependent)
 */
uint32_t stats_get_timestamp(void) {
    /* Use central timestamp function for consistency */
    return get_system_timestamp_ms();
}

/**
 * @brief Initialize statistics subsystem
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int stats_subsystem_init(const config_t *config) {
    if (!config) {
        log_error("stats_subsystem_init: NULL config parameter");
        return STATS_ERR_INVALID_PARAM;
    }
    
    log_info("Initializing statistics subsystem with production features");
    
    /* Clear all statistics */
    memset(&global_stats, 0, sizeof(global_stats));
    memset(nic_stats, 0, sizeof(nic_stats));
    memset(&g_production_stats, 0, sizeof(g_production_stats));
    
    /* Record start time */
    stats_start_time = stats_get_timestamp();
    global_stats.start_time = stats_start_time;
    g_production_stats.last_metrics_time = stats_start_time;
    
    stats_initialized = 1;
    
    log_info("Statistics subsystem initialized with production monitoring");
    return 0;
}

/**
 * @brief Increment transmit packet counter
 */
void stats_increment_tx_packets(void) {
    if (stats_initialized) {
        global_stats.tx_packets++;
    }
}

/**
 * @brief Add to transmit byte counter
 * @param bytes Number of bytes to add
 */
void stats_add_tx_bytes(uint32_t bytes) {
    if (stats_initialized) {
        global_stats.tx_bytes += bytes;
    }
}

/**
 * @brief Increment transmit error counter
 */
void stats_increment_tx_errors(void) {
    if (stats_initialized) {
        global_stats.tx_errors++;
    }
}

/**
 * @brief Increment receive packet counter
 */
void stats_increment_rx_packets(void) {
    if (stats_initialized) {
        global_stats.rx_packets++;
    }
}

/**
 * @brief Add to receive byte counter
 * @param bytes Number of bytes to add
 */
void stats_add_rx_bytes(uint32_t bytes) {
    if (stats_initialized) {
        global_stats.rx_bytes += bytes;
    }
}

/**
 * @brief Increment receive error counter
 */
void stats_increment_rx_errors(void) {
    if (stats_initialized) {
        global_stats.rx_errors++;
    }
}

/**
 * @brief Increment dropped packet counter
 */
void stats_increment_dropped_packets(void) {
    if (stats_initialized) {
        global_stats.dropped_packets++;
    }
}

/**
 * @brief Update NIC-specific statistics
 * @param nic_id NIC identifier
 * @param stat_type Type of statistic to update
 * @param value Value to add
 * @return 0 on success, negative on error
 */
int stats_update_nic(int nic_id, int stat_type, uint32_t value) {
    if (!stats_initialized) {
        return STATS_ERR_NOT_INITIALIZED;
    }
    
    if (nic_id < 0 || nic_id >= MAX_NICS) {
        return STATS_ERR_INVALID_NIC;
    }
    
    /* NIC-specific statistics updates - per-interface counters */
    switch (stat_type) {
        case STAT_TYPE_TX_PACKETS:
            nic_stats[nic_id].tx_packets += value;
            break;
        case STAT_TYPE_TX_BYTES:
            nic_stats[nic_id].tx_bytes += value;
            break;
        case STAT_TYPE_TX_ERRORS:
            nic_stats[nic_id].tx_errors += value;
            break;
        case STAT_TYPE_RX_PACKETS:
            nic_stats[nic_id].rx_packets += value;
            break;
        case STAT_TYPE_RX_BYTES:
            nic_stats[nic_id].rx_bytes += value;
            break;
        case STAT_TYPE_RX_ERRORS:
            nic_stats[nic_id].rx_errors += value;
            break;
        case STAT_TYPE_COLLISIONS:
            nic_stats[nic_id].collisions += value;
            break;
        case STAT_TYPE_CRC_ERRORS:
            nic_stats[nic_id].crc_errors += value;
            break;
        default:
            log_warning("Unknown statistic type: %d", stat_type);
            return STATS_ERR_INVALID_TYPE;
    }
    
    /* Update last activity time */
    nic_stats[nic_id].last_activity = stats_get_timestamp();
    
    return 0;
}

/**
 * @brief Get global driver statistics
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int stats_get_global(driver_stats_t *stats) {
    if (!stats) {
        return STATS_ERR_INVALID_PARAM;
    }
    
    if (!stats_initialized) {
        return STATS_ERR_NOT_INITIALIZED;
    }
    
    /* Update uptime */
    global_stats.uptime = stats_get_timestamp() - stats_start_time;
    
    *stats = global_stats;
    return 0;
}

/**
 * @brief Get NIC-specific statistics
 * @param nic_id NIC identifier
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int stats_get_nic(int nic_id, nic_stats_t *stats) {
    if (!stats) {
        return STATS_ERR_INVALID_PARAM;
    }
    
    if (!stats_initialized) {
        return STATS_ERR_NOT_INITIALIZED;
    }
    
    if (nic_id < 0 || nic_id >= MAX_NICS) {
        return STATS_ERR_INVALID_NIC;
    }
    
    *stats = nic_stats[nic_id];
    return 0;
}

/**
 * @brief Reset all statistics
 * @return 0 on success, negative on error
 */
int stats_reset_all(void) {
    uint32_t start_time;  /* C89: declarations at block start */

    if (!stats_initialized) {
        return STATS_ERR_NOT_INITIALIZED;
    }

    log_info("Resetting all statistics");

    /* Clear global statistics but preserve start time */
    start_time = global_stats.start_time;
    memset(&global_stats, 0, sizeof(global_stats));
    global_stats.start_time = start_time;
    
    /* Clear NIC statistics */
    memset(nic_stats, 0, sizeof(nic_stats));
    
    return 0;
}

/**
 * @brief Reset NIC-specific statistics
 * @param nic_id NIC identifier
 * @return 0 on success, negative on error
 */
int stats_reset_nic(int nic_id) {
    if (!stats_initialized) {
        return STATS_ERR_NOT_INITIALIZED;
    }
    
    if (nic_id < 0 || nic_id >= MAX_NICS) {
        return STATS_ERR_INVALID_NIC;
    }
    
    log_debug("Resetting statistics for NIC %d", nic_id);
    memset(&nic_stats[nic_id], 0, sizeof(nic_stats[nic_id]));
    
    return 0;
}

/**
 * @brief Print global statistics
 */
void stats_print_global(void) {
    driver_stats_t stats;
    uint32_t uptime_seconds;
    
    if (stats_get_global(&stats) < 0) {
        log_error("Failed to get global statistics");
        return;
    }
    
    uptime_seconds = stats.uptime / 18; /* Convert ticks to seconds (approx) */
    
    log_info("=== Global Driver Statistics ===");
    log_info("Uptime: %lu seconds", uptime_seconds);
    log_info("TX: %lu packets, %lu bytes, %lu errors",
             stats.tx_packets, stats.tx_bytes, stats.tx_errors);
    log_info("RX: %lu packets, %lu bytes, %lu errors",
             stats.rx_packets, stats.rx_bytes, stats.rx_errors);
    log_info("Dropped: %lu packets", stats.dropped_packets);
    log_info("Interrupts: %lu", stats.interrupts_handled);
    log_info("Memory allocated: %lu bytes", stats.memory_allocated);
}

/**
 * @brief Print NIC-specific statistics
 * @param nic_id NIC identifier
 */
void stats_print_nic(int nic_id) {
    nic_stats_t stats;
    
    if (stats_get_nic(nic_id, &stats) < 0) {
        log_error("Failed to get statistics for NIC %d", nic_id);
        return;
    }
    
    log_info("=== NIC %d Statistics ===", nic_id);
    log_info("TX: %lu packets, %lu bytes, %lu errors",
             stats.tx_packets, stats.tx_bytes, stats.tx_errors);
    log_info("RX: %lu packets, %lu bytes, %lu errors",
             stats.rx_packets, stats.rx_bytes, stats.rx_errors);
    log_info("Collisions: %lu, CRC errors: %lu",
             stats.collisions, stats.crc_errors);
    log_info("Frame errors: %lu, Overruns: %lu",
             stats.frame_errors, stats.overrun_errors);
    log_info("Last activity: %lu", stats.last_activity);
}

/**
 * @brief Print all statistics
 */
void stats_print_all(void) {
    int i, num_nics;
    
    if (!stats_initialized) {
        log_error("Statistics not initialized");
        return;
    }
    
    /* Print global statistics */
    stats_print_global();
    
    /* Print NIC statistics */
    num_nics = hardware_get_nic_count();
    for (i = 0; i < num_nics && i < MAX_NICS; i++) {
        stats_print_nic(i);
    }
}

/**
 * @brief Check if statistics are initialized
 * @return 1 if initialized, 0 otherwise
 */
int stats_is_initialized(void) {
    return stats_initialized;
}

/**
 * @brief Increment interrupt counter
 */
void stats_increment_interrupts(void) {
    if (stats_initialized) {
        global_stats.interrupts_handled++;
    }
}

/**
 * @brief Update memory allocation statistics
 * @param bytes Number of bytes allocated (positive) or freed (negative)
 */
void stats_update_memory(int32_t bytes) {
    if (stats_initialized) {
        if (bytes > 0) {
            global_stats.memory_allocated += bytes;
        } else if (bytes < 0 && global_stats.memory_allocated >= (uint32_t)(-bytes)) {
            global_stats.memory_allocated -= (uint32_t)(-bytes);
        }
    }
}

/**
 * @brief Cleanup statistics subsystem
 * @return 0 on success
 */
int stats_cleanup(void) {
    if (!stats_initialized) {
        return 0;
    }
    
    /* Statistics cleanup - reset all counters and free resources */
    log_info("Cleaning up statistics subsystem");
    
    /* Print final statistics */
    stats_print_all();
    
    stats_initialized = 0;
    
    log_info("Statistics cleanup completed");
    return 0;
}
