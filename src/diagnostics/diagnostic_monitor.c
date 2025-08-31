/**
 * @file diagnostic_monitor.c
 * @brief Comprehensive diagnostic monitoring system - Agent 13 Week 1
 * 
 * 3Com Packet Driver - Diagnostics Agent
 * Implements comprehensive monitoring framework with microsecond precision timing
 */

#include "../../include/diagnostics.h"
#include "../../include/common.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../modules/common/module_bridge.h"
#include "../modules/ptask/ptask_internal.h"
#include "../modules/corkscrw/corkscrw_internal.h"
#include "../modules/boomtex/boomtex_internal.h"
#include "../../docs/agents/shared/timing-measurement.h"
#include "../../docs/agents/shared/error-codes.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Global diagnostic monitor state */
typedef struct diagnostic_monitor {
    bool initialized;
    bool monitoring_active;
    uint32_t monitor_interval_ms;
    uint32_t last_monitor_time;
    
    /* Performance monitoring */
    timing_stats_t cli_timing_stats;
    timing_stats_t isr_timing_stats;
    timing_stats_t api_timing_stats;
    timing_stats_t module_timing_stats;
    
    /* Hardware health monitoring */
    uint8_t nic_health_scores[MAX_NICS];
    uint32_t nic_error_counts[MAX_NICS];
    uint32_t nic_last_activity[MAX_NICS];
    
    /* Memory monitoring */
    uint32_t memory_usage_peak;
    uint32_t memory_usage_current;
    uint32_t memory_allocations;
    uint32_t memory_deallocations;
    uint32_t memory_leak_count;
    
    /* Network analysis */
    uint32_t packet_flow_active_count;
    uint32_t packet_inspection_count;
    uint32_t network_bottleneck_count;
    
    /* Alert system */
    uint32_t alert_thresholds[8];
    uint32_t alert_counts[8];
    bool alert_enabled[8];
    
} diagnostic_monitor_t;

static diagnostic_monitor_t g_diag_monitor = {0};

/* Performance monitoring with microsecond precision */
static int diag_monitor_init_performance_monitoring(void) {
    /* Initialize PIT for timing measurements */
    PIT_INIT();
    
    /* Reset timing statistics */
    memset(&g_diag_monitor.cli_timing_stats, 0, sizeof(timing_stats_t));
    memset(&g_diag_monitor.isr_timing_stats, 0, sizeof(timing_stats_t));
    memset(&g_diag_monitor.api_timing_stats, 0, sizeof(timing_stats_t));
    memset(&g_diag_monitor.module_timing_stats, 0, sizeof(timing_stats_t));
    
    return SUCCESS;
}

/* Hardware health monitoring initialization */
static int diag_monitor_init_hardware_monitoring(void) {
    int i;
    
    /* Initialize NIC health monitoring */
    for (i = 0; i < MAX_NICS; i++) {
        g_diag_monitor.nic_health_scores[i] = 100; /* Start with perfect health */
        g_diag_monitor.nic_error_counts[i] = 0;
        g_diag_monitor.nic_last_activity[i] = diag_get_timestamp();
    }
    
    return SUCCESS;
}

/* Memory monitoring initialization */
static int diag_monitor_init_memory_monitoring(void) {
    g_diag_monitor.memory_usage_peak = 0;
    g_diag_monitor.memory_usage_current = 0;
    g_diag_monitor.memory_allocations = 0;
    g_diag_monitor.memory_deallocations = 0;
    g_diag_monitor.memory_leak_count = 0;
    
    return SUCCESS;
}

/* Network analysis initialization */
static int diag_monitor_init_network_analysis(void) {
    g_diag_monitor.packet_flow_active_count = 0;
    g_diag_monitor.packet_inspection_count = 0;
    g_diag_monitor.network_bottleneck_count = 0;
    
    return SUCCESS;
}

/* Alert system initialization */
static int diag_monitor_init_alert_system(void) {
    int i;
    
    /* Set default alert thresholds */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_ERROR_RATE_HIGH] = 50;      /* 50 errors per 1000 packets */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_UTILIZATION_HIGH] = 90;     /* 90% utilization */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_MEMORY_LOW] = 85;           /* 85% memory usage */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_NIC_FAILURE] = 1;           /* Any failure */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_ROUTING_FAILURE] = 10;      /* 10 routing failures */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_API_ERROR] = 5;             /* 5 API errors */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_PERFORMANCE_DEGRADED] = 20; /* 20% performance drop */
    g_diag_monitor.alert_thresholds[ALERT_TYPE_BOTTLENECK_DETECTED] = 3;   /* 3 bottlenecks */
    
    /* Enable all alerts by default */
    for (i = 0; i < 8; i++) {
        g_diag_monitor.alert_enabled[i] = true;
        g_diag_monitor.alert_counts[i] = 0;
    }
    
    return SUCCESS;
}

/* Initialize diagnostic monitor */
int diag_monitor_init(void) {
    if (g_diag_monitor.initialized) {
        return SUCCESS;
    }
    
    /* Initialize all monitoring subsystems */
    int result = diag_monitor_init_performance_monitoring();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize performance monitoring: 0x%04X", result);
        return result;
    }
    
    result = diag_monitor_init_hardware_monitoring();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize hardware monitoring: 0x%04X", result);
        return result;
    }
    
    result = diag_monitor_init_memory_monitoring();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize memory monitoring: 0x%04X", result);
        return result;
    }
    
    result = diag_monitor_init_network_analysis();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize network analysis: 0x%04X", result);
        return result;
    }
    
    result = diag_monitor_init_alert_system();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize alert system: 0x%04X", result);
        return result;
    }
    
    g_diag_monitor.monitor_interval_ms = 1000; /* 1 second default */
    g_diag_monitor.last_monitor_time = diag_get_timestamp();
    g_diag_monitor.monitoring_active = true;
    g_diag_monitor.initialized = true;
    
    LOG_INFO("Diagnostic monitor initialized successfully");
    return SUCCESS;
}

/* Performance monitoring functions */
int diag_monitor_timing_cli_section(pit_timing_t *timing) {
    if (!g_diag_monitor.initialized || !timing) {
        return ERROR_INVALID_PARAM;
    }
    
    update_timing_stats(&g_diag_monitor.cli_timing_stats, timing);
    
    /* Check if CLI section exceeded timing constraint */
    if (!VALIDATE_CLI_TIMING(timing)) {
        diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, 
                           "CLI section exceeded 8us timing constraint");
        return ERROR_TIMEOUT;
    }
    
    return SUCCESS;
}

int diag_monitor_timing_isr_execution(pit_timing_t *timing) {
    if (!g_diag_monitor.initialized || !timing) {
        return ERROR_INVALID_PARAM;
    }
    
    update_timing_stats(&g_diag_monitor.isr_timing_stats, timing);
    
    /* Check if ISR exceeded timing constraint */
    if (!VALIDATE_ISR_TIMING(timing)) {
        diag_generate_alert(ALERT_TYPE_PERFORMANCE_DEGRADED, 
                           "ISR execution exceeded 60us timing constraint");
        return ERROR_TIMEOUT;
    }
    
    return SUCCESS;
}

int diag_monitor_timing_api_call(pit_timing_t *timing) {
    if (!g_diag_monitor.initialized || !timing) {
        return ERROR_INVALID_PARAM;
    }
    
    update_timing_stats(&g_diag_monitor.api_timing_stats, timing);
    return SUCCESS;
}

int diag_monitor_timing_module_call(pit_timing_t *timing) {
    if (!g_diag_monitor.initialized || !timing) {
        return ERROR_INVALID_PARAM;
    }
    
    update_timing_stats(&g_diag_monitor.module_timing_stats, timing);
    return SUCCESS;
}

/* Hardware health monitoring functions */
int diag_monitor_nic_health_update(uint8_t nic_index, uint8_t health_score) {
    if (!g_diag_monitor.initialized || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    g_diag_monitor.nic_health_scores[nic_index] = health_score;
    g_diag_monitor.nic_last_activity[nic_index] = diag_get_timestamp();
    
    /* Check for NIC health alert threshold */
    if (health_score < 50) {
        char alert_msg[64];
        snprintf(alert_msg, sizeof(alert_msg), "NIC %d health degraded: %d%%", 
                 nic_index, health_score);
        diag_generate_alert(ALERT_TYPE_NIC_FAILURE, alert_msg);
    }
    
    return SUCCESS;
}

int diag_monitor_nic_error_count(uint8_t nic_index, uint32_t error_count) {
    if (!g_diag_monitor.initialized || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    g_diag_monitor.nic_error_counts[nic_index] = error_count;
    
    /* Check for high error rate alert */
    if (error_count > g_diag_monitor.alert_thresholds[ALERT_TYPE_ERROR_RATE_HIGH]) {
        char alert_msg[64];
        snprintf(alert_msg, sizeof(alert_msg), "NIC %d high error rate: %lu errors", 
                 nic_index, error_count);
        diag_generate_alert(ALERT_TYPE_ERROR_RATE_HIGH, alert_msg);
    }
    
    return SUCCESS;
}

/* Memory usage monitoring */
int diag_monitor_memory_allocation(uint32_t size, void *ptr) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_diag_monitor.memory_allocations++;
    g_diag_monitor.memory_usage_current += size;
    
    if (g_diag_monitor.memory_usage_current > g_diag_monitor.memory_usage_peak) {
        g_diag_monitor.memory_usage_peak = g_diag_monitor.memory_usage_current;
    }
    
    return SUCCESS;
}

int diag_monitor_memory_deallocation(uint32_t size, void *ptr) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_diag_monitor.memory_deallocations++;
    if (g_diag_monitor.memory_usage_current >= size) {
        g_diag_monitor.memory_usage_current -= size;
    } else {
        g_diag_monitor.memory_leak_count++;
        LOG_WARNING("Memory deallocation size mismatch - potential leak detected");
    }
    
    return SUCCESS;
}

int diag_monitor_check_memory_leaks(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    uint32_t allocation_diff = g_diag_monitor.memory_allocations - g_diag_monitor.memory_deallocations;
    
    if (allocation_diff > 10) { /* Threshold for leak detection */
        g_diag_monitor.memory_leak_count += allocation_diff - 10;
        diag_generate_alert(ALERT_TYPE_MEMORY_LOW, "Potential memory leak detected");
    }
    
    return SUCCESS;
}

/* Network analysis functions */
int diag_monitor_packet_flow_analysis(uint32_t active_flows) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_diag_monitor.packet_flow_active_count = active_flows;
    return SUCCESS;
}

int diag_monitor_packet_inspection(const void *packet_data, uint32_t packet_size) {
    if (!g_diag_monitor.initialized || !packet_data) {
        return ERROR_INVALID_PARAM;
    }
    
    g_diag_monitor.packet_inspection_count++;
    
    /* Basic packet validation */
    if (packet_size < 14) { /* Minimum Ethernet frame size */
        LOG_WARNING("Undersized packet detected: %lu bytes", packet_size);
        return ERROR_PACKET_INVALID;
    }
    
    if (packet_size > 1518) { /* Maximum Ethernet frame size */
        LOG_WARNING("Oversized packet detected: %lu bytes", packet_size);
        return ERROR_PACKET_TOO_LARGE;
    }
    
    return SUCCESS;
}

/* Bottleneck detection */
int diag_monitor_detect_bottlenecks(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    bool bottleneck_detected = false;
    
    /* Check CLI timing bottleneck */
    if (g_diag_monitor.cli_timing_stats.count > 0 &&
        AVERAGE_TIMING_US(&g_diag_monitor.cli_timing_stats) > 6) {
        LOG_WARNING("CLI timing bottleneck detected: avg %luus", 
                   AVERAGE_TIMING_US(&g_diag_monitor.cli_timing_stats));
        bottleneck_detected = true;
    }
    
    /* Check ISR timing bottleneck */
    if (g_diag_monitor.isr_timing_stats.count > 0 &&
        AVERAGE_TIMING_US(&g_diag_monitor.isr_timing_stats) > 50) {
        LOG_WARNING("ISR timing bottleneck detected: avg %luus", 
                   AVERAGE_TIMING_US(&g_diag_monitor.isr_timing_stats));
        bottleneck_detected = true;
    }
    
    /* Check memory pressure bottleneck */
    if (g_diag_monitor.memory_usage_current > (640 * 1024 * 85 / 100)) { /* 85% of 640KB */
        LOG_WARNING("Memory pressure bottleneck detected: %lu bytes used", 
                   g_diag_monitor.memory_usage_current);
        bottleneck_detected = true;
    }
    
    if (bottleneck_detected) {
        g_diag_monitor.network_bottleneck_count++;
        diag_generate_alert(ALERT_TYPE_BOTTLENECK_DETECTED, "Performance bottleneck detected");
    }
    
    return bottleneck_detected ? 1 : 0;
}

/* Monitoring dashboard functions */
int diag_monitor_print_performance_dashboard(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== DIAGNOSTIC PERFORMANCE DASHBOARD ===\n");
    
    /* CLI Timing Statistics */
    printf("CLI Timing Stats:\n");
    printf("  Count: %lu, Min: %luus, Max: %luus, Avg: %luus, Overflows: %lu\n",
           g_diag_monitor.cli_timing_stats.count,
           g_diag_monitor.cli_timing_stats.min_us,
           g_diag_monitor.cli_timing_stats.max_us,
           AVERAGE_TIMING_US(&g_diag_monitor.cli_timing_stats),
           g_diag_monitor.cli_timing_stats.overflow_count);
    
    /* ISR Timing Statistics */
    printf("ISR Timing Stats:\n");
    printf("  Count: %lu, Min: %luus, Max: %luus, Avg: %luus, Overflows: %lu\n",
           g_diag_monitor.isr_timing_stats.count,
           g_diag_monitor.isr_timing_stats.min_us,
           g_diag_monitor.isr_timing_stats.max_us,
           AVERAGE_TIMING_US(&g_diag_monitor.isr_timing_stats),
           g_diag_monitor.isr_timing_stats.overflow_count);
    
    return SUCCESS;
}

int diag_monitor_print_hardware_dashboard(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== HARDWARE HEALTH DASHBOARD ===\n");
    
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_diag_monitor.nic_health_scores[i] < 100 || g_diag_monitor.nic_error_counts[i] > 0) {
            printf("NIC %d: Health=%d%%, Errors=%lu, Last Activity=%lu\n",
                   i, g_diag_monitor.nic_health_scores[i],
                   g_diag_monitor.nic_error_counts[i],
                   g_diag_monitor.nic_last_activity[i]);
        }
    }
    
    return SUCCESS;
}

int diag_monitor_print_memory_dashboard(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== MEMORY USAGE DASHBOARD ===\n");
    printf("Current Usage: %lu bytes\n", g_diag_monitor.memory_usage_current);
    printf("Peak Usage: %lu bytes\n", g_diag_monitor.memory_usage_peak);
    printf("Allocations: %lu\n", g_diag_monitor.memory_allocations);
    printf("Deallocations: %lu\n", g_diag_monitor.memory_deallocations);
    printf("Potential Leaks: %lu\n", g_diag_monitor.memory_leak_count);
    
    return SUCCESS;
}

int diag_monitor_print_network_dashboard(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== NETWORK ANALYSIS DASHBOARD ===\n");
    printf("Active Flows: %lu\n", g_diag_monitor.packet_flow_active_count);
    printf("Packets Inspected: %lu\n", g_diag_monitor.packet_inspection_count);
    printf("Bottlenecks Detected: %lu\n", g_diag_monitor.network_bottleneck_count);
    
    return SUCCESS;
}

/* Generate comprehensive monitoring report */
int diag_monitor_generate_report(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n========================================\n");
    printf("   COMPREHENSIVE DIAGNOSTIC REPORT\n");
    printf("   Agent 13 - Week 1 Implementation\n");
    printf("========================================\n");
    
    diag_monitor_print_performance_dashboard();
    diag_monitor_print_hardware_dashboard();
    diag_monitor_print_memory_dashboard();
    diag_monitor_print_network_dashboard();
    
    printf("\n=== ALERT SUMMARY ===\n");
    for (int i = 0; i < 8; i++) {
        if (g_diag_monitor.alert_counts[i] > 0) {
            printf("Alert Type %d: %lu occurrences (threshold: %lu)\n",
                   i, g_diag_monitor.alert_counts[i], g_diag_monitor.alert_thresholds[i]);
        }
    }
    
    printf("\n========================================\n");
    
    return SUCCESS;
}

/* Module integration validation */
int diag_monitor_validate_module_integration(void) {
    if (!g_diag_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    LOG_INFO("Validating module integration for PTASK/CORKSCRW/BOOMTEX...");
    
    /* Check if modules are responding to diagnostic queries */
    bool ptask_responding = false;
    bool corkscrw_responding = false;
    bool boomtex_responding = false;
    
    /* Test PTASK module health (3C509B) */
    module_init_context_t *ptask_context = module_get_context_from_detection(MODULE_ID_PTASK, NIC_TYPE_3C509B);
    if (ptask_context && ptask_context->detected_io_base != 0) {
        /* Check if PTASK module bridge is active */
        if (hw_health_check(ptask_context->detected_io_base, NIC_TYPE_3C509B)) {
            ptask_responding = true;
            debug_log_debug("PTASK module health check passed");
        } else {
            debug_log_warning("PTASK module health check failed");
        }
    }
    
    /* Test CORKSCRW module health (3C515) */
    module_init_context_t *corkscrw_context = module_get_context_from_detection(MODULE_ID_CORKSCRW, NIC_TYPE_3C515_TX);
    if (corkscrw_context && corkscrw_context->detected_io_base != 0) {
        /* Check if CORKSCRW module bridge is active */
        if (hw_health_check(corkscrw_context->detected_io_base, NIC_TYPE_3C515_TX)) {
            corkscrw_responding = true;
            debug_log_debug("CORKSCRW module health check passed");
        } else {
            debug_log_warning("CORKSCRW module health check failed");
        }
    }
    
    /* Test BOOMTEX module health (any PCI NIC) */
    module_init_context_t *boomtex_context = module_get_context_from_detection(MODULE_ID_BOOMTEX, NIC_TYPE_3C905B);
    if (boomtex_context && boomtex_context->detected_io_base != 0) {
        /* Check if BOOMTEX module bridge is active */
        if (hw_health_check(boomtex_context->detected_io_base, NIC_TYPE_3C905B)) {
            boomtex_responding = true;
            debug_log_debug("BOOMTEX module health check passed");
        } else {
            debug_log_warning("BOOMTEX module health check failed");
        }
    }
    
    if (!ptask_responding) {
        LOG_ERROR("PTASK module not responding to diagnostic queries");
        return ERROR_MODULE_INIT_FAILED;
    }
    
    if (!corkscrw_responding) {
        LOG_ERROR("CORKSCRW module not responding to diagnostic queries");
        return ERROR_MODULE_INIT_FAILED;
    }
    
    if (!boomtex_responding) {
        LOG_ERROR("BOOMTEX module not responding to diagnostic queries");
        return ERROR_MODULE_INIT_FAILED;
    }
    
    LOG_INFO("All modules responding to diagnostic integration checks");
    return SUCCESS;
}

/* Hardware monitoring function - NE2000 emulation removed */
/* Focus on actual 3Com hardware monitoring only */

/* Cleanup diagnostic monitor */
void diag_monitor_cleanup(void) {
    if (!g_diag_monitor.initialized) {
        return;
    }
    
    g_diag_monitor.monitoring_active = false;
    g_diag_monitor.initialized = false;
    
    LOG_INFO("Diagnostic monitor cleaned up successfully");
}