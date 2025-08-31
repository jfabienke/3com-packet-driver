/**
 * @file stats.c
 * @brief Statistics gathering and reporting
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "../include/stats.h"
#include "../include/logging.h"
#include "../include/hardware.h"
#include "../include/common.h"

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
    if (!stats_initialized) {
        return STATS_ERR_NOT_INITIALIZED;
    }
    
    log_info("Resetting all statistics");
    
    /* Clear global statistics but preserve start time */
    uint32_t start_time = global_stats.start_time;
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

/**
 * @brief Read hardware registers for real-time statistics
 * @param nic_id NIC identifier
 * @param reg_stats Pointer to store register statistics
 * @return 0 on success, negative on error
 */
static int stats_read_hardware_registers(int nic_id, hardware_register_stats_t *reg_stats) {
    nic_info_t *nic;
    uint32_t registers[32];  /* Space for 32 register readings */
    int reg_count = 0;
    
    if (!reg_stats || nic_id < 0 || nic_id >= MAX_NICS) {
        return STATS_ERR_INVALID_PARAM;
    }
    
    nic = hardware_get_nic(nic_id);
    if (!nic || !(nic->status & NIC_STATUS_PRESENT)) {
        return STATS_ERR_INVALID_NIC;
    }
    
    g_production_stats.register_read_count++;
    
    memset(reg_stats, 0, sizeof(hardware_register_stats_t));
    
    /* Read NIC-specific registers based on type */
    switch (nic->type) {
        case NIC_TYPE_3C509B:
            /* Read 3C509B specific registers */
            if (nic->ops && nic->io_base) {
                /* Status register */
                registers[reg_count++] = inw(nic->io_base + 0x0E);  /* Status */
                registers[reg_count++] = inw(nic->io_base + 0x0C);  /* Command */
                registers[reg_count++] = inw(nic->io_base + 0x08);  /* TX Free */
                registers[reg_count++] = inw(nic->io_base + 0x04);  /* RX Status */
                
                /* Window 1 registers for statistics */
                outw(nic->io_base + 0x0C, 0x0800 | 1);  /* Select Window 1 */
                registers[reg_count++] = inw(nic->io_base + 0x0A);  /* TX Bytes OK */
                registers[reg_count++] = inw(nic->io_base + 0x0B);  /* RX Bytes OK */
                
                /* Window 6 for statistics */
                outw(nic->io_base + 0x0C, 0x0800 | 6);  /* Select Window 6 */
                registers[reg_count++] = inb(nic->io_base + 0x00);  /* Carrier Lost */
                registers[reg_count++] = inb(nic->io_base + 0x01);  /* SQE Errors */
                registers[reg_count++] = inb(nic->io_base + 0x02);  /* Multiple Collisions */
                registers[reg_count++] = inb(nic->io_base + 0x03);  /* Single Collisions */
                registers[reg_count++] = inb(nic->io_base + 0x04);  /* Late Collisions */
                registers[reg_count++] = inb(nic->io_base + 0x05);  /* RX Overruns */
                registers[reg_count++] = inb(nic->io_base + 0x06);  /* Frames Xmitted OK */
                registers[reg_count++] = inb(nic->io_base + 0x07);  /* Frames Rcvd OK */
                registers[reg_count++] = inb(nic->io_base + 0x08);  /* Frames Deferred */
                
                /* Restore to Window 1 */
                outw(nic->io_base + 0x0C, 0x0800 | 1);
            }
            break;
            
        case NIC_TYPE_3C515_TX:
            /* Read 3C515-TX specific registers */
            if (nic->ops && nic->io_base) {
                registers[reg_count++] = inw(nic->io_base + 0x0E);  /* Status */
                registers[reg_count++] = inl(nic->io_base + 0x24);  /* Down List Ptr */
                registers[reg_count++] = inl(nic->io_base + 0x38);  /* Up List Ptr */
                registers[reg_count++] = inw(nic->io_base + 0x20);  /* DMA Control */
            }
            break;
            
        default:
            log_warning("Unknown NIC type for register reading: %d", nic->type);
            g_production_stats.register_read_errors++;
            return STATS_ERR_INVALID_NIC;
    }
    
    /* Validate register data */
    if (!stats_validate_register_data(registers, reg_count)) {
        g_production_stats.register_corruption_events++;
        g_production_stats.register_read_errors++;
        log_warning("Register corruption detected on NIC %d", nic_id);
        return STATS_ERR_INVALID_PARAM;
    }
    
    /* Fill in register statistics structure */
    reg_stats->nic_id = nic_id;
    reg_stats->timestamp = stats_get_timestamp();
    reg_stats->register_count = reg_count;
    
    /* Copy up to max registers */
    int copy_count = (reg_count < 32) ? reg_count : 32;\n    for (int i = 0; i < copy_count; i++) {\n        reg_stats->register_values[i] = registers[i];\n    }\n    \n    /* Calculate basic health metrics from registers */\n    reg_stats->tx_active = (registers[0] & 0x1000) != 0;  /* TX in progress */\n    reg_stats->rx_active = (registers[0] & 0x2000) != 0;  /* RX in progress */\n    reg_stats->error_flags = registers[0] & 0x00FF;       /* Error bits */\n    \n    if (reg_count >= 5) {\n        reg_stats->tx_bytes_ok = registers[4];\n        reg_stats->rx_bytes_ok = registers[5];\n    }\n    \n    log_trace(\"Read %d hardware registers from NIC %d\", reg_count, nic_id);\n    return STATS_SUCCESS;\n}\n\n/**\n * @brief Collect real-time performance metrics\n * @param nic_id NIC identifier\n * @param metrics Pointer to store performance metrics\n * @return 0 on success, negative on error\n */\nstatic int stats_collect_realtime_metrics(int nic_id, realtime_performance_metrics_t *metrics) {\n    nic_info_t *nic;\n    uint32_t current_time;\n    hardware_register_stats_t reg_stats;\n    int result;\n    \n    if (!metrics || nic_id < 0 || nic_id >= MAX_NICS) {\n        return STATS_ERR_INVALID_PARAM;\n    }\n    \n    nic = hardware_get_nic(nic_id);\n    if (!nic) {\n        return STATS_ERR_INVALID_NIC;\n    }\n    \n    current_time = stats_get_timestamp();\n    g_production_stats.metrics_collection_count++;\n    \n    memset(metrics, 0, sizeof(realtime_performance_metrics_t));\n    \n    /* Read hardware registers first */\n    result = stats_read_hardware_registers(nic_id, &reg_stats);\n    if (result != STATS_SUCCESS) {\n        g_production_stats.metrics_collection_errors++;\n        log_warning(\"Failed to read hardware registers for metrics collection\");\n        return result;\n    }\n    \n    /* Fill basic information */\n    metrics->nic_id = nic_id;\n    metrics->timestamp = current_time;\n    metrics->collection_interval = current_time - g_production_stats.last_metrics_time;\n    \n    /* Calculate throughput metrics */\n    if (nic->tx_packets > 0) {\n        uint32_t uptime = current_time - stats_start_time;\n        if (uptime > 0) {\n            metrics->tx_packets_per_sec = (nic->tx_packets * 1000) / uptime;\n            metrics->rx_packets_per_sec = (nic->rx_packets * 1000) / uptime;\n            metrics->tx_bytes_per_sec = (nic->tx_bytes * 1000) / uptime;\n            metrics->rx_bytes_per_sec = (nic->rx_bytes * 1000) / uptime;\n        }\n    }\n    \n    /* Calculate error rates */\n    if (nic->tx_packets > 0) {\n        metrics->tx_error_rate = (nic->tx_errors * 10000) / nic->tx_packets;  /* Per 10k */\n    }\n    if (nic->rx_packets > 0) {\n        metrics->rx_error_rate = (nic->rx_errors * 10000) / nic->rx_packets;\n    }\n    \n    /* Network utilization estimate */\n    uint32_t total_bits = (nic->tx_bytes + nic->rx_bytes) * 8;\n    uint32_t uptime_sec = (current_time - stats_start_time) / 1000;\n    if (uptime_sec > 0) {\n        uint32_t max_bits = (nic->speed * 1000000UL) * uptime_sec;\n        if (max_bits > 0) {\n            metrics->network_utilization = (total_bits * 100) / max_bits;\n        }\n    }\n    \n    /* Link quality assessment */\n    metrics->link_quality = 100;  /* Start with perfect */\n    if (!nic->link_up) {\n        metrics->link_quality = 0;\n    } else {\n        /* Deduct for errors */\n        if (metrics->tx_error_rate > 100) metrics->link_quality -= 20;\n        if (metrics->rx_error_rate > 100) metrics->link_quality -= 20;\n        if (nic->interrupts > 1000) metrics->link_quality -= 10;  /* High interrupt load */\n    }\n    \n    /* Memory usage from hardware registers */\n    if (reg_stats.register_count >= 3) {\n        metrics->memory_usage = reg_stats.register_values[2];  /* TX Free register */\n    }\n    \n    /* Temperature estimation (simplified) */\n    metrics->temperature_estimate = 25 + (nic->error_count / 100);  /* Rough estimate */\n    \n    /* Power consumption estimate */\n    metrics->power_consumption = 500;  /* Base 500mW */\n    if (nic->link_up) metrics->power_consumption += 200;\n    if (nic->status & NIC_STATUS_100MBPS) metrics->power_consumption += 300;\n    \n    g_production_stats.last_metrics_time = current_time;\n    \n    log_debug(\"Collected real-time metrics for NIC %d: util=%d%%, quality=%d%%\",\n              nic_id, metrics->network_utilization, metrics->link_quality);\n    \n    return STATS_SUCCESS;\n}\n\n/**\n * @brief Analyze error patterns for predictive maintenance\n * @param nic_id NIC identifier\n * @param analysis Pointer to store error analysis\n * @return 0 on success, negative on error\n */\nstatic int stats_analyze_error_patterns(int nic_id, error_pattern_analysis_t *analysis) {\n    nic_info_t *nic;\n    uint32_t *patterns;\n    uint32_t current_time;\n    \n    if (!analysis || nic_id < 0 || nic_id >= MAX_NICS) {\n        return STATS_ERR_INVALID_PARAM;\n    }\n    \n    nic = hardware_get_nic(nic_id);\n    if (!nic) {\n        return STATS_ERR_INVALID_NIC;\n    }\n    \n    current_time = stats_get_timestamp();\n    patterns = g_production_stats.error_patterns[nic_id];\n    \n    memset(analysis, 0, sizeof(error_pattern_analysis_t));\n    \n    analysis->nic_id = nic_id;\n    analysis->timestamp = current_time;\n    \n    /* Analyze error trends */\n    analysis->total_errors = nic->tx_errors + nic->rx_errors + nic->error_count;\n    \n    /* Check for error bursts */\n    analysis->error_burst_detected = stats_detect_error_burst(nic_id);\n    if (analysis->error_burst_detected) {\n        g_production_stats.error_burst_events++;\n        log_warning(\"Error burst detected on NIC %d\", nic_id);\n    }\n    \n    /* Calculate error trend */\n    analysis->error_trend = stats_calculate_trend(patterns, 16);\n    \n    /* Failure probability assessment */\n    analysis->failure_probability = 0;\n    if (analysis->total_errors > 1000) analysis->failure_probability += 20;\n    if (analysis->error_burst_detected) analysis->failure_probability += 30;\n    if (!nic->link_up) analysis->failure_probability += 40;\n    if (nic->error_count > 50) analysis->failure_probability += 25;\n    \n    /* Cap at 100% */\n    if (analysis->failure_probability > 100) {\n        analysis->failure_probability = 100;\n    }\n    \n    /* Time to next failure estimate (hours) */\n    if (analysis->failure_probability > 80) {\n        analysis->time_to_failure_hours = 1;\n    } else if (analysis->failure_probability > 50) {\n        analysis->time_to_failure_hours = 24;\n    } else if (analysis->failure_probability > 20) {\n        analysis->time_to_failure_hours = 168;  /* 1 week */\n    } else {\n        analysis->time_to_failure_hours = 8760;  /* 1 year */\n    }\n    \n    /* Recommended actions */\n    if (analysis->failure_probability > 75) {\n        snprintf(analysis->recommended_action, sizeof(analysis->recommended_action),\n                \"URGENT: Replace NIC %d immediately\", nic_id);\n    } else if (analysis->failure_probability > 50) {\n        snprintf(analysis->recommended_action, sizeof(analysis->recommended_action),\n                \"Schedule NIC %d replacement within 24 hours\", nic_id);\n    } else if (analysis->failure_probability > 25) {\n        snprintf(analysis->recommended_action, sizeof(analysis->recommended_action),\n                \"Monitor NIC %d closely, schedule maintenance\", nic_id);\n    } else {\n        snprintf(analysis->recommended_action, sizeof(analysis->recommended_action),\n                \"NIC %d operating normally\", nic_id);\n    }\n    \n    log_debug(\"Error analysis for NIC %d: failure_prob=%d%%, ttf=%d hours\",\n              nic_id, analysis->failure_probability, analysis->time_to_failure_hours);\n    \n    return STATS_SUCCESS;\n}\n\n/**\n * @brief Track memory usage statistics\n * @param mem_stats Pointer to store memory statistics\n * @return 0 on success, negative on error\n */\nstatic int stats_track_memory_usage(memory_usage_stats_t *mem_stats) {\n    uint32_t current_usage;\n    \n    if (!mem_stats) {\n        return STATS_ERR_INVALID_PARAM;\n    }\n    \n    /* Get current memory usage from global stats */\n    current_usage = global_stats.memory_allocated;\n    g_production_stats.current_memory_usage = current_usage;\n    \n    /* Update peak if necessary */\n    if (current_usage > g_production_stats.peak_memory_usage) {\n        g_production_stats.peak_memory_usage = current_usage;\n    }\n    \n    memset(mem_stats, 0, sizeof(memory_usage_stats_t));\n    \n    mem_stats->timestamp = stats_get_timestamp();\n    mem_stats->current_usage = current_usage;\n    mem_stats->peak_usage = g_production_stats.peak_memory_usage;\n    mem_stats->leak_events = g_production_stats.memory_leak_events;\n    \n    /* Calculate fragmentation estimate */\n    mem_stats->fragmentation_percent = 5;  /* Simplified estimate */\n    \n    /* Memory efficiency */\n    if (g_production_stats.peak_memory_usage > 0) {\n        mem_stats->efficiency_percent = \n            (current_usage * 100) / g_production_stats.peak_memory_usage;\n    } else {\n        mem_stats->efficiency_percent = 100;\n    }\n    \n    /* Available memory estimate */\n    mem_stats->available_memory = 65536 - current_usage;  /* 64KB system assumption */\n    \n    /* Memory health score */\n    mem_stats->health_score = 100;\n    if (mem_stats->fragmentation_percent > 20) mem_stats->health_score -= 20;\n    if (mem_stats->efficiency_percent < 50) mem_stats->health_score -= 30;\n    if (mem_stats->leak_events > 0) mem_stats->health_score -= 25;\n    \n    /* Memory pressure level */\n    if (current_usage > 50000) {\n        mem_stats->pressure_level = 3;  /* Critical */\n    } else if (current_usage > 30000) {\n        mem_stats->pressure_level = 2;  /* High */\n    } else if (current_usage > 15000) {\n        mem_stats->pressure_level = 1;  /* Medium */\n    } else {\n        mem_stats->pressure_level = 0;  /* Low */\n    }\n    \n    log_trace(\"Memory usage: %lu bytes (peak: %lu), efficiency: %d%%\",\n              current_usage, g_production_stats.peak_memory_usage, \n              mem_stats->efficiency_percent);\n    \n    return STATS_SUCCESS;\n}\n\n/**\n * @brief Monitor overall network health\n * @param health Pointer to store health statistics\n * @return 0 on success, negative on error\n */\nstatic int stats_monitor_network_health(network_health_stats_t *health) {\n    int total_nics, active_nics, healthy_nics;\n    uint32_t total_errors, total_packets;\n    \n    if (!health) {\n        return STATS_ERR_INVALID_PARAM;\n    }\n    \n    g_production_stats.health_checks_performed++;\n    \n    memset(health, 0, sizeof(network_health_stats_t));\n    \n    health->timestamp = stats_get_timestamp();\n    \n    /* Count NICs and assess health */\n    total_nics = hardware_get_nic_count();\n    active_nics = 0;\n    healthy_nics = 0;\n    total_errors = 0;\n    total_packets = 0;\n    \n    for (int i = 0; i < total_nics && i < MAX_NICS; i++) {\n        nic_info_t *nic = hardware_get_nic(i);\n        if (nic && (nic->status & NIC_STATUS_PRESENT)) {\n            if (nic->status & NIC_STATUS_ACTIVE) {\n                active_nics++;\n                \n                /* Consider healthy if error rate < 1% */\n                uint32_t nic_errors = nic->tx_errors + nic->rx_errors;\n                uint32_t nic_packets = nic->tx_packets + nic->rx_packets;\n                \n                if (nic_packets > 0) {\n                    uint32_t error_rate = (nic_errors * 100) / nic_packets;\n                    if (error_rate < 1 && nic->link_up) {\n                        healthy_nics++;\n                    }\n                }\n                \n                total_errors += nic_errors;\n                total_packets += nic_packets;\n            }\n        }\n    }\n    \n    health->total_nics = total_nics;\n    health->active_nics = active_nics;\n    health->healthy_nics = healthy_nics;\n    \n    /* Overall health score */\n    health->overall_health_score = 0;\n    if (total_nics > 0) {\n        health->overall_health_score = (healthy_nics * 100) / total_nics;\n    }\n    \n    /* Network availability */\n    health->network_availability = 0;\n    if (total_nics > 0) {\n        health->network_availability = (active_nics * 100) / total_nics;\n    }\n    \n    /* Total error rate */\n    if (total_packets > 0) {\n        health->total_error_rate = (total_errors * 10000) / total_packets;  /* Per 10k */\n    }\n    \n    /* Issue warnings and alerts */\n    if (health->overall_health_score < 50) {\n        g_production_stats.health_critical_events++;\n        log_error(\"CRITICAL: Network health below 50%% (score: %d%%)\",\n                  health->overall_health_score);\n        health->alert_level = 3;\n    } else if (health->overall_health_score < 75) {\n        g_production_stats.health_warnings_issued++;\n        log_warning(\"WARNING: Network health degraded (score: %d%%)\",\n                   health->overall_health_score);\n        health->alert_level = 2;\n    } else if (health->overall_health_score < 90) {\n        log_info(\"NOTICE: Network health fair (score: %d%%)\",\n                health->overall_health_score);\n        health->alert_level = 1;\n    } else {\n        health->alert_level = 0;\n    }\n    \n    /* Recovery recommendations */\n    if (active_nics == 0) {\n        snprintf(health->recommendation, sizeof(health->recommendation),\n                \"CRITICAL: No active NICs - check hardware and restart driver\");\n    } else if (healthy_nics < active_nics / 2) {\n        snprintf(health->recommendation, sizeof(health->recommendation),\n                \"Replace failing NICs and check network infrastructure\");\n    } else if (health->total_error_rate > 100) {\n        snprintf(health->recommendation, sizeof(health->recommendation),\n                \"High error rate detected - check cables and network equipment\");\n    } else {\n        snprintf(health->recommendation, sizeof(health->recommendation),\n                \"Network operating normally\");\n    }\n    \n    log_debug(\"Network health: %d%% (%d/%d NICs healthy, %d active)\",\n              health->overall_health_score, healthy_nics, total_nics, active_nics);\n    \n    return STATS_SUCCESS;\n}\n\n/**\n * @brief Predict potential failures using collected data\n * @param nic_id NIC identifier\n * @param prediction Pointer to store prediction results\n * @return 0 on success, negative on error\n */\nstatic int stats_predict_failures(int nic_id, failure_prediction_t *prediction) {\n    nic_info_t *nic;\n    realtime_performance_metrics_t metrics;\n    error_pattern_analysis_t error_analysis;\n    int result;\n    \n    if (!prediction || nic_id < 0 || nic_id >= MAX_NICS) {\n        return STATS_ERR_INVALID_PARAM;\n    }\n    \n    nic = hardware_get_nic(nic_id);\n    if (!nic) {\n        return STATS_ERR_INVALID_NIC;\n    }\n    \n    g_production_stats.prediction_calculations++;\n    \n    /* Collect current metrics and error patterns */\n    result = stats_collect_realtime_metrics(nic_id, &metrics);\n    if (result != STATS_SUCCESS) {\n        return result;\n    }\n    \n    result = stats_analyze_error_patterns(nic_id, &error_analysis);\n    if (result != STATS_SUCCESS) {\n        return result;\n    }\n    \n    memset(prediction, 0, sizeof(failure_prediction_t));\n    \n    prediction->nic_id = nic_id;\n    prediction->timestamp = stats_get_timestamp();\n    \n    /* Combine various factors for failure prediction */\n    prediction->failure_probability = error_analysis.failure_probability;\n    \n    /* Adjust based on performance metrics */\n    if (metrics.link_quality < 50) {\n        prediction->failure_probability += 20;\n    }\n    if (metrics.tx_error_rate > 1000) {  /* >10% error rate */\n        prediction->failure_probability += 25;\n    }\n    if (metrics.temperature_estimate > 70) {\n        prediction->failure_probability += 15;\n    }\n    \n    /* Hardware age factor (simplified) */\n    uint32_t uptime_hours = (stats_get_timestamp() - stats_start_time) / 3600000;\n    if (uptime_hours > 8760) {  /* More than 1 year */\n        prediction->failure_probability += 10;\n    }\n    \n    /* Cap at 100% */\n    if (prediction->failure_probability > 100) {\n        prediction->failure_probability = 100;\n    }\n    \n    /* Confidence level based on data quality */\n    prediction->confidence_level = 70;  /* Base confidence */\n    if (nic->tx_packets > 1000) prediction->confidence_level += 10;\n    if (g_production_stats.register_read_count > 100) prediction->confidence_level += 10;\n    if (g_production_stats.metrics_collection_count > 50) prediction->confidence_level += 10;\n    \n    /* Time to failure estimate */\n    prediction->time_to_failure_hours = error_analysis.time_to_failure_hours;\n    \n    /* Prediction accuracy tracking */\n    if (prediction->failure_probability > 80) {\n        /* High probability prediction - track for accuracy */\n        g_production_stats.early_warnings_issued++;\n    }\n    \n    /* Recommended actions */\n    if (prediction->failure_probability > 90) {\n        snprintf(prediction->recommended_action, sizeof(prediction->recommended_action),\n                \"IMMEDIATE: Replace NIC %d - failure imminent\", nic_id);\n    } else if (prediction->failure_probability > 70) {\n        snprintf(prediction->recommended_action, sizeof(prediction->recommended_action),\n                \"URGENT: Schedule NIC %d replacement within 24 hours\", nic_id);\n    } else if (prediction->failure_probability > 40) {\n        snprintf(prediction->recommended_action, sizeof(prediction->recommended_action),\n                \"PLANNED: Schedule NIC %d maintenance within 1 week\", nic_id);\n    } else {\n        snprintf(prediction->recommended_action, sizeof(prediction->recommended_action),\n                \"NORMAL: NIC %d operating within parameters\", nic_id);\n    }\n    \n    log_info(\"Failure prediction for NIC %d: %d%% probability (confidence: %d%%)\",\n             nic_id, prediction->failure_probability, prediction->confidence_level);\n    \n    return STATS_SUCCESS;\n}\n\n/**\n * @brief Update error pattern tracking\n * @param nic_id NIC identifier\n * @param error_type Type of error\n */\nstatic void stats_update_error_patterns(int nic_id, int error_type) {\n    if (nic_id < 0 || nic_id >= MAX_NICS || error_type < 0 || error_type >= 16) {\n        return;\n    }\n    \n    g_production_stats.error_patterns[nic_id][error_type]++;\n    \n    /* Check for trend changes */\n    static uint32_t last_check[MAX_NICS] = {0};\n    uint32_t current_time = stats_get_timestamp();\n    \n    if (current_time - last_check[nic_id] > 60000) {  /* Check every minute */\n        uint32_t trend = stats_calculate_trend(g_production_stats.error_patterns[nic_id], 16);\n        static uint32_t last_trend[MAX_NICS] = {0};\n        \n        if (trend != last_trend[nic_id]) {\n            g_production_stats.error_trend_changes++;\n            last_trend[nic_id] = trend;\n        }\n        \n        last_check[nic_id] = current_time;\n    }\n}\n\n/**\n * @brief Detect error burst conditions\n * @param nic_id NIC identifier\n * @return true if error burst detected\n */\nstatic bool stats_detect_error_burst(int nic_id) {\n    nic_info_t *nic;\n    static uint32_t last_error_count[MAX_NICS] = {0};\n    static uint32_t last_check_time[MAX_NICS] = {0};\n    uint32_t current_time, current_errors;\n    \n    if (nic_id < 0 || nic_id >= MAX_NICS) {\n        return false;\n    }\n    \n    nic = hardware_get_nic(nic_id);\n    if (!nic) {\n        return false;\n    }\n    \n    current_time = stats_get_timestamp();\n    current_errors = nic->tx_errors + nic->rx_errors + nic->error_count;\n    \n    /* Check if we've seen a significant increase in errors in short time */\n    if (current_time - last_check_time[nic_id] > 5000) {  /* 5 second window */\n        uint32_t error_increase = current_errors - last_error_count[nic_id];\n        \n        /* Burst if more than 10 errors in 5 seconds */\n        if (error_increase > 10) {\n            last_error_count[nic_id] = current_errors;\n            last_check_time[nic_id] = current_time;\n            return true;\n        }\n        \n        last_error_count[nic_id] = current_errors;\n        last_check_time[nic_id] = current_time;\n    }\n    \n    return false;\n}\n\n/**\n * @brief Validate register data for corruption\n * @param registers Array of register values\n * @param count Number of registers\n * @return true if data appears valid\n */\nstatic bool stats_validate_register_data(uint32_t *registers, int count) {\n    if (!registers || count <= 0) {\n        return false;\n    }\n    \n    /* Basic validation - check for all 0xFF or 0x00 (likely hardware failure) */\n    bool all_zeros = true;\n    bool all_ones = true;\n    \n    for (int i = 0; i < count; i++) {\n        if (registers[i] != 0) all_zeros = false;\n        if (registers[i] != 0xFFFFFFFF) all_ones = false;\n    }\n    \n    /* If all registers are the same suspicious value, likely corruption */\n    if (all_zeros || all_ones) {\n        return false;\n    }\n    \n    return true;\n}\n\n/**\n * @brief Log performance anomaly\n * @param nic_id NIC identifier\n * @param description Anomaly description\n */\nstatic void stats_log_performance_anomaly(int nic_id, const char *description) {\n    log_warning(\"Performance anomaly on NIC %d: %s\", nic_id, description);\n    \n    /* Could be extended to maintain an anomaly log */\n}\n\n/**\n * @brief Calculate trend from series of values\n * @param values Array of values\n * @param count Number of values\n * @return Trend indicator\n */\nstatic uint32_t stats_calculate_trend(uint32_t *values, int count) {\n    if (!values || count < 2) {\n        return 0;\n    }\n    \n    /* Simple trend calculation - compare recent vs older values */\n    uint32_t recent_sum = 0;\n    uint32_t older_sum = 0;\n    int half = count / 2;\n    \n    for (int i = 0; i < half; i++) {\n        older_sum += values[i];\n    }\n    \n    for (int i = half; i < count; i++) {\n        recent_sum += values[i];\n    }\n    \n    /* Return trend as percentage change */\n    if (older_sum > 0) {\n        return (recent_sum * 100) / older_sum;\n    }\n    \n    return 100;  /* No change */\n}\n\n/**\n * @brief Get comprehensive production statistics\n * @param stats Pointer to store production statistics\n * @return 0 on success, negative on error\n */\nint stats_get_production_stats(production_stats_summary_t *stats) {\n    if (!stats) {\n        return STATS_ERR_INVALID_PARAM;\n    }\n    \n    if (!stats_initialized) {\n        return STATS_ERR_NOT_INITIALIZED;\n    }\n    \n    memset(stats, 0, sizeof(production_stats_summary_t));\n    \n    stats->timestamp = stats_get_timestamp();\n    \n    /* Copy production statistics */\n    stats->register_reads = g_production_stats.register_read_count;\n    stats->register_errors = g_production_stats.register_read_errors;\n    stats->metrics_collections = g_production_stats.metrics_collection_count;\n    stats->health_checks = g_production_stats.health_checks_performed;\n    stats->predictions_made = g_production_stats.prediction_calculations;\n    stats->early_warnings = g_production_stats.early_warnings_issued;\n    \n    stats->error_bursts = g_production_stats.error_burst_events;\n    stats->corruption_events = g_production_stats.register_corruption_events;\n    stats->memory_leaks = g_production_stats.memory_leak_events;\n    \n    stats->peak_memory = g_production_stats.peak_memory_usage;\n    stats->current_memory = g_production_stats.current_memory_usage;\n    \n    return STATS_SUCCESS;\n}\n\n/**\n * @brief Enhanced NIC statistics update with error pattern tracking\n * @param nic_id NIC identifier\n * @param stat_type Type of statistic\n * @param value Value to add\n * @return 0 on success, negative on error\n */\nint stats_update_nic_enhanced(int nic_id, int stat_type, uint32_t value) {\n    int result = stats_update_nic(nic_id, stat_type, value);\n    \n    /* Update error pattern tracking */\n    if (stat_type == STAT_TYPE_TX_ERRORS || stat_type == STAT_TYPE_RX_ERRORS) {\n        stats_update_error_patterns(nic_id, stat_type);\n    }\n    \n    return result;\n}\n\n/**\n * @brief Print comprehensive production statistics\n */\nvoid stats_print_production_summary(void) {\n    production_stats_summary_t stats;\n    \n    if (stats_get_production_stats(&stats) != STATS_SUCCESS) {\n        log_error(\"Failed to get production statistics\");\n        return;\n    }\n    \n    log_info(\"=== Production Statistics Summary ===\");\n    log_info(\"Register Operations: %lu reads, %lu errors\", \n             stats.register_reads, stats.register_errors);\n    log_info(\"Metrics Collections: %lu performed, %lu health checks\",\n             stats.metrics_collections, stats.health_checks);\n    log_info(\"Predictions: %lu made, %lu early warnings issued\",\n             stats.predictions_made, stats.early_warnings);\n    log_info(\"Error Events: %lu bursts, %lu corruption events\",\n             stats.error_bursts, stats.corruption_events);\n    log_info(\"Memory: %lu current, %lu peak, %lu leaks\",\n             stats.current_memory, stats.peak_memory, stats.memory_leaks);\n    log_info(\"======================================\");\n}\n\n/**\n * @brief Force health monitoring check on all NICs\n * @return 0 on success, negative on error\n */\nint stats_force_health_check(void) {\n    network_health_stats_t health;\n    int result;\n    \n    result = stats_monitor_network_health(&health);\n    if (result == STATS_SUCCESS) {\n        log_info(\"Forced health check: %d%% health score, %d/%d NICs healthy\",\n                 health.overall_health_score, health.healthy_nics, health.total_nics);\n    }\n    \n    return result;\n}

