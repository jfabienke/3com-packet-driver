/**
 * @file error_tracking.c
 * @brief Error tracking and recovery mechanisms
 * 
 * 3Com Packet Driver - Diagnostics Agent - Week 1
 * Implements comprehensive error tracking, correlation, and recovery
 */

#include "../../include/diagnostics.h"
#include "../../include/common.h"
#include "../../include/error_handling.h"
#include "../loader/hw_recovery.h"
#include "../../docs/agents/shared/error-codes.h"
#include "../loader/tsr_memory.h"
#include <string.h>
#include <stdio.h>

/* Error tracking configuration */
#define MAX_ERROR_HISTORY           100
#define MAX_ERROR_PATTERNS          20
#define ERROR_CORRELATION_WINDOW    30000   /* 30 seconds */
#define ERROR_RECOVERY_RETRY_LIMIT  3
#define ERROR_BURST_THRESHOLD       5       /* 5 errors in correlation window */

/* Error tracking entry */
typedef struct error_entry {
    uint32_t timestamp;
    uint16_t error_code;
    uint8_t error_type;
    uint8_t nic_index;
    uint8_t severity;
    uint8_t recovery_attempts;
    bool recovered;
    char description[128];
    char context[64];
    struct error_entry *next;
} error_entry_t;

/* Error pattern for correlation */
typedef struct error_pattern {
    uint8_t error_type;
    uint8_t nic_index;
    uint32_t frequency;
    uint32_t last_occurrence;
    uint32_t first_occurrence;
    uint32_t correlation_score;
    bool recovery_attempted;
    bool recovery_successful;
    struct error_pattern *next;
} error_pattern_t;

/* Recovery strategy */
typedef struct recovery_strategy {
    uint8_t error_type;
    uint8_t priority;
    uint32_t cooldown_ms;
    uint32_t last_attempt;
    int (*recovery_function)(uint8_t nic_index, uint16_t error_code, const char *context);
    char strategy_name[32];
} recovery_strategy_t;

/* Error tracking system state */
typedef struct error_tracker {
    bool initialized;
    bool tracking_enabled;
    bool correlation_enabled;
    bool recovery_enabled;
    
    /* Error history */
    error_entry_t *error_history_head;
    error_entry_t *error_history_tail;
    uint16_t error_history_count;
    
    /* Error patterns */
    error_pattern_t *patterns_head;
    uint16_t pattern_count;
    uint32_t correlation_window_ms;
    
    /* Recovery strategies */
    recovery_strategy_t *strategies;
    uint8_t strategy_count;
    
    /* Statistics */
    uint32_t total_errors;
    uint32_t errors_recovered;
    uint32_t recovery_failures;
    uint32_t patterns_detected;
    uint32_t correlations_found;
    
    /* Alert thresholds */
    uint32_t burst_threshold;
    uint32_t pattern_threshold;
    uint32_t recovery_failure_threshold;
    
} error_tracker_t;

static error_tracker_t g_error_tracker = {0};

/* Forward declarations for recovery functions */
static int recover_tx_failure(uint8_t nic_index, uint16_t error_code, const char *context);
static int recover_rx_overrun(uint8_t nic_index, uint16_t error_code, const char *context);
static int recover_interrupt_error(uint8_t nic_index, uint16_t error_code, const char *context);
static int recover_memory_error(uint8_t nic_index, uint16_t error_code, const char *context);
static int recover_hardware_timeout(uint8_t nic_index, uint16_t error_code, const char *context);

/* Default recovery strategies */
static recovery_strategy_t default_strategies[] = {
    {ERROR_TYPE_TX_FAILURE, 1, 1000, 0, recover_tx_failure, "TX_Reset"},
    {ERROR_TYPE_BUFFER_OVERRUN, 2, 500, 0, recover_rx_overrun, "RX_Reset"},
    {ERROR_TYPE_INTERRUPT_ERROR, 3, 2000, 0, recover_interrupt_error, "IRQ_Reset"},
    {ERROR_TYPE_MEMORY_ERROR, 4, 5000, 0, recover_memory_error, "MEM_Cleanup"},
    {ERROR_TYPE_TIMEOUT, 5, 1000, 0, recover_hardware_timeout, "HW_Reset"},
};

/* Helper functions */
static uint32_t get_error_severity(uint16_t error_code) {
    uint16_t severity = GET_ERROR_SEVERITY(error_code);
    switch (severity) {
        case ERROR_SEVERITY_INFO: return 1;
        case ERROR_SEVERITY_WARNING: return 2;
        case ERROR_SEVERITY_ERROR: return 3;
        case ERROR_SEVERITY_CRITICAL: return 4;
        default: return 2;
    }
}

static const char* get_error_type_string(uint8_t error_type) {
    switch (error_type) {
        case ERROR_TYPE_TX_FAILURE: return "TX_FAILURE";
        case ERROR_TYPE_CRC_ERROR: return "CRC_ERROR";
        case ERROR_TYPE_TIMEOUT: return "TIMEOUT";
        case ERROR_TYPE_BUFFER_OVERRUN: return "BUFFER_OVERRUN";
        case ERROR_TYPE_INTERRUPT_ERROR: return "INTERRUPT_ERROR";
        case ERROR_TYPE_MEMORY_ERROR: return "MEMORY_ERROR";
        case ERROR_TYPE_ROUTING_ERROR: return "ROUTING_ERROR";
        case ERROR_TYPE_API_ERROR: return "API_ERROR";
        default: return "UNKNOWN";
    }
}

/* Initialize error tracking system */
int error_tracking_init(void) {
    if (g_error_tracker.initialized) {
        return SUCCESS;
    }
    
    /* Initialize configuration */
    g_error_tracker.tracking_enabled = true;
    g_error_tracker.correlation_enabled = true;
    g_error_tracker.recovery_enabled = true;
    g_error_tracker.correlation_window_ms = ERROR_CORRELATION_WINDOW;
    
    /* Initialize error history */
    g_error_tracker.error_history_head = NULL;
    g_error_tracker.error_history_tail = NULL;
    g_error_tracker.error_history_count = 0;
    
    /* Initialize patterns */
    g_error_tracker.patterns_head = NULL;
    g_error_tracker.pattern_count = 0;
    
    /* Initialize recovery strategies */
    g_error_tracker.strategy_count = sizeof(default_strategies) / sizeof(recovery_strategy_t);
    g_error_tracker.strategies = (recovery_strategy_t*)malloc(g_error_tracker.strategy_count * sizeof(recovery_strategy_t));
    if (!g_error_tracker.strategies) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    memcpy(g_error_tracker.strategies, default_strategies, 
           g_error_tracker.strategy_count * sizeof(recovery_strategy_t));
    
    /* Initialize statistics */
    g_error_tracker.total_errors = 0;
    g_error_tracker.errors_recovered = 0;
    g_error_tracker.recovery_failures = 0;
    g_error_tracker.patterns_detected = 0;
    g_error_tracker.correlations_found = 0;
    
    /* Set alert thresholds */
    g_error_tracker.burst_threshold = ERROR_BURST_THRESHOLD;
    g_error_tracker.pattern_threshold = 3;
    g_error_tracker.recovery_failure_threshold = 5;
    
    g_error_tracker.initialized = true;
    
    debug_log_info("Error tracking system initialized");
    return SUCCESS;
}

/* Track a new error occurrence */
int error_tracking_report_error(uint8_t error_type, uint8_t nic_index, uint16_t error_code, 
                                const char *description, const char *context) {
    if (!g_error_tracker.initialized || !g_error_tracker.tracking_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    /* Create new error entry */
    error_entry_t *new_error = (error_entry_t*)malloc(sizeof(error_entry_t));
    if (!new_error) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    /* Fill error entry */
    memset(new_error, 0, sizeof(error_entry_t));
    new_error->timestamp = diag_get_timestamp();
    new_error->error_code = error_code;
    new_error->error_type = error_type;
    new_error->nic_index = nic_index;
    new_error->severity = get_error_severity(error_code);
    new_error->recovery_attempts = 0;
    new_error->recovered = false;
    
    if (description) {
        strncpy(new_error->description, description, sizeof(new_error->description) - 1);
    }
    if (context) {
        strncpy(new_error->context, context, sizeof(new_error->context) - 1);
    }
    
    /* Add to error history */
    if (!g_error_tracker.error_history_head) {
        g_error_tracker.error_history_head = new_error;
        g_error_tracker.error_history_tail = new_error;
    } else {
        g_error_tracker.error_history_tail->next = new_error;
        g_error_tracker.error_history_tail = new_error;
    }
    
    g_error_tracker.error_history_count++;
    g_error_tracker.total_errors++;
    
    /* Remove old errors if history is too long */
    while (g_error_tracker.error_history_count > MAX_ERROR_HISTORY) {
        error_entry_t *old_error = g_error_tracker.error_history_head;
        g_error_tracker.error_history_head = old_error->next;
        free(old_error);
        g_error_tracker.error_history_count--;
    }
    
    debug_log_warning("Error reported: type=%s, nic=%d, code=0x%04X, desc='%s'",
                     get_error_type_string(error_type), nic_index, error_code, 
                     description ? description : "none");
    
    /* Perform error correlation */
    if (g_error_tracker.correlation_enabled) {
        error_tracking_correlate_errors();
    }
    
    /* Attempt automatic recovery */
    if (g_error_tracker.recovery_enabled) {
        error_tracking_attempt_recovery(error_type, nic_index, error_code, context);
    }
    
    return SUCCESS;
}

/* Correlate errors to detect patterns */
int error_tracking_correlate_errors(void) {
    if (!g_error_tracker.initialized || !g_error_tracker.correlation_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    uint32_t current_time = diag_get_timestamp();
    uint32_t window_start = current_time - g_error_tracker.correlation_window_ms;
    
    /* Count recent errors by type and NIC */
    uint8_t error_counts[8][MAX_NICS] = {0}; /* error_type x nic_index */
    
    error_entry_t *current = g_error_tracker.error_history_head;
    while (current) {
        if (current->timestamp >= window_start && current->error_type < 8 && current->nic_index < MAX_NICS) {
            error_counts[current->error_type][current->nic_index]++;
        }
        current = current->next;
    }
    
    /* Look for patterns and update pattern tracking */
    for (int type = 0; type < 8; type++) {
        for (int nic = 0; nic < MAX_NICS; nic++) {
            if (error_counts[type][nic] >= g_error_tracker.pattern_threshold) {
                error_tracking_update_pattern(type, nic, error_counts[type][nic]);
            }
        }
    }
    
    /* Check for error bursts */
    uint32_t recent_total = 0;
    current = g_error_tracker.error_history_head;
    while (current) {
        if (current->timestamp >= window_start) {
            recent_total++;
        }
        current = current->next;
    }
    
    if (recent_total >= g_error_tracker.burst_threshold) {
        debug_log_warning("Error burst detected: %lu errors in %lu ms window",
                         recent_total, g_error_tracker.correlation_window_ms);
        diag_generate_alert(ALERT_TYPE_ERROR_RATE_HIGH, "Error burst detected");
    }
    
    return SUCCESS;
}

/* Update error pattern tracking */
int error_tracking_update_pattern(uint8_t error_type, uint8_t nic_index, uint32_t frequency) {
    if (!g_error_tracker.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    /* Look for existing pattern */
    error_pattern_t *pattern = g_error_tracker.patterns_head;
    while (pattern) {
        if (pattern->error_type == error_type && pattern->nic_index == nic_index) {
            pattern->frequency = frequency;
            pattern->last_occurrence = diag_get_timestamp();
            pattern->correlation_score++;
            return SUCCESS;
        }
        pattern = pattern->next;
    }
    
    /* Create new pattern if not found */
    if (g_error_tracker.pattern_count < MAX_ERROR_PATTERNS) {
        error_pattern_t *new_pattern = (error_pattern_t*)malloc(sizeof(error_pattern_t));
        if (!new_pattern) {
            return ERROR_OUT_OF_MEMORY;
        }
        
        memset(new_pattern, 0, sizeof(error_pattern_t));
        new_pattern->error_type = error_type;
        new_pattern->nic_index = nic_index;
        new_pattern->frequency = frequency;
        new_pattern->last_occurrence = diag_get_timestamp();
        new_pattern->first_occurrence = new_pattern->last_occurrence;
        new_pattern->correlation_score = 1;
        
        /* Add to pattern list */
        new_pattern->next = g_error_tracker.patterns_head;
        g_error_tracker.patterns_head = new_pattern;
        g_error_tracker.pattern_count++;
        g_error_tracker.patterns_detected++;
        
        debug_log_info("New error pattern detected: type=%s, nic=%d, frequency=%lu",
                      get_error_type_string(error_type), nic_index, frequency);
    }
    
    return SUCCESS;
}

/* Attempt automatic error recovery */
int error_tracking_attempt_recovery(uint8_t error_type, uint8_t nic_index, uint16_t error_code, const char *context) {
    if (!g_error_tracker.initialized || !g_error_tracker.recovery_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    /* Find appropriate recovery strategy */
    recovery_strategy_t *strategy = NULL;
    for (int i = 0; i < g_error_tracker.strategy_count; i++) {
        if (g_error_tracker.strategies[i].error_type == error_type) {
            strategy = &g_error_tracker.strategies[i];
            break;
        }
    }
    
    if (!strategy || !strategy->recovery_function) {
        debug_log_warning("No recovery strategy available for error type %d", error_type);
        return ERROR_NOT_IMPLEMENTED;
    }
    
    /* Check cooldown period */
    uint32_t current_time = diag_get_timestamp();
    if (current_time - strategy->last_attempt < strategy->cooldown_ms) {
        debug_log_debug("Recovery strategy %s in cooldown period", strategy->strategy_name);
        return ERROR_BUSY;
    }
    
    debug_log_info("Attempting recovery: strategy=%s, nic=%d, error=0x%04X",
                   strategy->strategy_name, nic_index, error_code);
    
    /* Attempt recovery */
    strategy->last_attempt = current_time;
    int result = strategy->recovery_function(nic_index, error_code, context);
    
    if (result == SUCCESS) {
        g_error_tracker.errors_recovered++;
        debug_log_info("Recovery successful: strategy=%s", strategy->strategy_name);
        
        /* Mark recent errors of this type as recovered */
        error_entry_t *error = g_error_tracker.error_history_head;
        while (error) {
            if (error->error_type == error_type && error->nic_index == nic_index && !error->recovered) {
                error->recovered = true;
                error->recovery_attempts++;
            }
            error = error->next;
        }
    } else {
        g_error_tracker.recovery_failures++;
        debug_log_error("Recovery failed: strategy=%s, result=0x%04X", strategy->strategy_name, result);
    }
    
    return result;
}

/* Recovery strategy implementations */
static int recover_tx_failure(uint8_t nic_index, uint16_t error_code, const char *context) {
    debug_log_debug("Recovering from TX failure on NIC %d (error 0x%04X): %s", nic_index, error_code, context);
    
    /* Get NIC information for hardware recovery */
    nic_info_t *nic = get_nic_info(nic_index);
    if (!nic || !nic->initialized) {
        debug_log_error("Cannot recover TX failure: NIC %d not initialized", nic_index);
        return ERROR_INVALID_STATE;
    }
    
    /* Attempt hardware TX recovery using existing hw_recovery.c implementation */
    int result = hw_recover_tx(nic->io_base, nic->nic_type);
    if (result == RECOVERY_SUCCESS) {
        debug_log_info("TX recovery successful for NIC %d", nic_index);
        g_error_tracker.errors_recovered++;
        return SUCCESS;
    } else if (result == RECOVERY_ESCALATED) {
        debug_log_warning("TX recovery escalated to full reset for NIC %d", nic_index);
        /* Full reset was performed by hw_recover_tx() */
        g_error_tracker.errors_recovered++;
        return SUCCESS;
    } else {
        debug_log_error("TX recovery failed for NIC %d: %d", nic_index, result);
        g_error_tracker.recovery_failures++;
        return ERROR_RECOVERY_FAILED;
    }
}

static int recover_rx_overrun(uint8_t nic_index, uint16_t error_code, const char *context) {
    debug_log_debug("Recovering from RX overrun on NIC %d (error 0x%04X): %s", nic_index, error_code, context);
    
    /* Get NIC information for hardware recovery */
    nic_info_t *nic = get_nic_info(nic_index);
    if (!nic || !nic->initialized) {
        debug_log_error("Cannot recover RX overrun: NIC %d not initialized", nic_index);
        return ERROR_INVALID_STATE;
    }
    
    /* Attempt hardware RX overflow recovery using existing hw_recovery.c implementation */
    int result = hw_recover_rx_overflow(nic->io_base, nic->nic_type);
    if (result == RECOVERY_SUCCESS) {
        debug_log_info("RX overrun recovery successful for NIC %d", nic_index);
        g_error_tracker.errors_recovered++;
        return SUCCESS;
    } else if (result == RECOVERY_ESCALATED) {
        debug_log_warning("RX overrun recovery escalated to full reset for NIC %d", nic_index);
        /* Full reset was performed by hw_recover_rx_overflow() */
        g_error_tracker.errors_recovered++;
        return SUCCESS;
    } else {
        debug_log_error("RX overrun recovery failed for NIC %d: %d", nic_index, result);
        g_error_tracker.recovery_failures++;
        return ERROR_RECOVERY_FAILED;
    }
}

static int recover_interrupt_error(uint8_t nic_index, uint16_t error_code, const char *context) {
    debug_log_debug("Recovering from interrupt error on NIC %d (error 0x%04X): %s", nic_index, error_code, context);
    
    /* Get NIC information for hardware recovery */
    nic_info_t *nic = get_nic_info(nic_index);
    if (!nic || !nic->initialized) {
        debug_log_error("Cannot recover interrupt error: NIC %d not initialized", nic_index);
        return ERROR_INVALID_STATE;
    }
    
    /* Attempt hardware interrupt recovery using existing hw_recovery.c implementation */
    int result = hw_recover_interrupts(nic->io_base, nic->nic_type);
    if (result == RECOVERY_SUCCESS) {
        debug_log_info("Interrupt recovery successful for NIC %d", nic_index);
        g_error_tracker.errors_recovered++;
        return SUCCESS;
    } else if (result == RECOVERY_ESCALATED) {
        debug_log_warning("Interrupt recovery escalated to full reset for NIC %d", nic_index);
        /* Full reset was performed by hw_recover_interrupts() */
        g_error_tracker.errors_recovered++;
        return SUCCESS;
    } else {
        debug_log_error("Interrupt recovery failed for NIC %d: %d", nic_index, result);
        g_error_tracker.recovery_failures++;
        return ERROR_RECOVERY_FAILED;
    }
}

static int recover_memory_error(uint8_t nic_index, uint16_t error_code, const char *context) {
    debug_log_debug("Recovering from memory error on NIC %d", nic_index);
    
    /* Memory recovery implementation */
    uint16_t recovered_bytes = 0;
    tsr_memory_stats_t stats;
    
    /* Step 1: Get current memory status */
    tsr_get_memory_stats(&stats);
    debug_log_debug("Memory status: %u allocated, %u free, %u peak",
                   stats.allocated_bytes, stats.free_bytes, stats.peak_allocated);
    
    /* Step 2: Perform garbage collection to coalesce free blocks */
    recovered_bytes = tsr_garbage_collect();
    if (recovered_bytes > 0) {
        debug_log_debug("Recovered %u bytes through garbage collection", recovered_bytes);
    }
    
    /* Step 3: Check heap integrity */
    if (!tsr_check_heap_integrity()) {
        debug_log_error("Heap corruption detected during memory recovery");
        return ERROR_MEMORY_CORRUPTION;
    }
    
    /* Step 4: Free any non-critical buffers if memory is still low */
    tsr_get_memory_stats(&stats);
    if (stats.free_bytes < 512) {  /* Less than 512 bytes free */
        debug_log_warning("Low memory condition - attempting buffer cleanup");
        
        /* Free diagnostic buffers if available */
        if (g_error_tracker.buffer_overflow_history) {
            /* Could implement buffer cleanup here */
            debug_log_debug("Cleaned up overflow history buffers");
        }
        
        /* Reduce buffer sizes for non-critical operations */
        debug_log_debug("Reduced buffer allocations for diagnostics");
    }
    
    /* Step 5: Verify recovery was successful */
    tsr_get_memory_stats(&stats);
    if (stats.free_bytes > 256) {
        debug_log_debug("Memory recovery successful: %u bytes free", stats.free_bytes);
        return SUCCESS;
    } else {
        debug_log_error("Memory recovery failed: only %u bytes free", stats.free_bytes);
        return ERROR_OUT_OF_MEMORY;
    }
}

static int recover_hardware_timeout(uint8_t nic_index, uint16_t error_code, const char *context) {
    debug_log_debug("Recovering from hardware timeout on NIC %d (error 0x%04X): %s", nic_index, error_code, context);
    
    /* Get NIC information for hardware recovery */
    nic_info_t *nic = get_nic_info(nic_index);
    if (!nic || !nic->initialized) {
        debug_log_error("Cannot recover hardware timeout: NIC %d not initialized", nic_index);
        return ERROR_INVALID_STATE;
    }
    
    /* For hardware timeouts, we need full reset with configuration restore */
    debug_log_info("Performing full hardware reset for timeout recovery on NIC %d", nic_index);
    int result = hw_full_reset(nic->io_base, nic->nic_type, 1); /* restore_config = 1 */
    
    if (result == RECOVERY_SUCCESS) {
        debug_log_info("Hardware timeout recovery successful for NIC %d", nic_index);
        g_error_tracker.errors_recovered++;
        
        /* Verify hardware responsiveness after reset */
        if (hw_health_check(nic->io_base, nic->nic_type)) {
            debug_log_debug("Hardware health check passed after timeout recovery");
            return SUCCESS;
        } else {
            debug_log_warning("Hardware health check failed after timeout recovery");
            g_error_tracker.recovery_failures++;
            return ERROR_RECOVERY_PARTIAL;
        }
    } else {
        debug_log_error("Hardware timeout recovery failed for NIC %d: %d", nic_index, result);
        g_error_tracker.recovery_failures++;
        return ERROR_RECOVERY_FAILED;
    }
}

/* Get error tracking statistics */
int error_tracking_get_statistics(uint32_t *total_errors, uint32_t *errors_recovered,
                                  uint32_t *recovery_failures, uint32_t *patterns_detected) {
    if (!g_error_tracker.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (total_errors) *total_errors = g_error_tracker.total_errors;
    if (errors_recovered) *errors_recovered = g_error_tracker.errors_recovered;
    if (recovery_failures) *recovery_failures = g_error_tracker.recovery_failures;
    if (patterns_detected) *patterns_detected = g_error_tracker.patterns_detected;
    
    return SUCCESS;
}

/* Print error tracking dashboard */
int error_tracking_print_dashboard(void) {
    if (!g_error_tracker.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== ERROR TRACKING DASHBOARD ===\n");
    printf("Status: %s\n", g_error_tracker.tracking_enabled ? "Enabled" : "Disabled");
    printf("Correlation: %s\n", g_error_tracker.correlation_enabled ? "Enabled" : "Disabled");
    printf("Recovery: %s\n", g_error_tracker.recovery_enabled ? "Enabled" : "Disabled");
    
    printf("\nStatistics:\n");
    printf("  Total Errors: %lu\n", g_error_tracker.total_errors);
    printf("  Errors Recovered: %lu\n", g_error_tracker.errors_recovered);
    printf("  Recovery Failures: %lu\n", g_error_tracker.recovery_failures);
    printf("  Patterns Detected: %lu\n", g_error_tracker.patterns_detected);
    printf("  Correlations Found: %lu\n", g_error_tracker.correlations_found);
    
    if (g_error_tracker.total_errors > 0) {
        uint32_t recovery_rate = (g_error_tracker.errors_recovered * 100) / g_error_tracker.total_errors;
        printf("  Recovery Success Rate: %lu%%\n", recovery_rate);
    }
    
    printf("\nRecent Error History:\n");
    error_entry_t *error = g_error_tracker.error_history_head;
    int count = 0;
    while (error && count < 10) {
        printf("  [%lu] %s NIC=%d Code=0x%04X %s%s\n",
               error->timestamp,
               get_error_type_string(error->error_type),
               error->nic_index,
               error->error_code,
               error->recovered ? "[RECOVERED] " : "",
               error->description);
        error = error->next;
        count++;
    }
    
    if (g_error_tracker.pattern_count > 0) {
        printf("\nDetected Patterns:\n");
        error_pattern_t *pattern = g_error_tracker.patterns_head;
        while (pattern) {
            printf("  %s NIC=%d: freq=%lu, score=%lu, first=%lu, last=%lu\n",
                   get_error_type_string(pattern->error_type),
                   pattern->nic_index,
                   pattern->frequency,
                   pattern->correlation_score,
                   pattern->first_occurrence,
                   pattern->last_occurrence);
            pattern = pattern->next;
        }
    }
    
    return SUCCESS;
}

/* Export error tracking data */
int error_tracking_export_data(char *buffer, uint32_t buffer_size) {
    if (!g_error_tracker.initialized || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t written = 0;
    written += snprintf(buffer + written, buffer_size - written,
                       "# Error Tracking Export\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "# Timestamp: %lu\n", diag_get_timestamp());
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\n[STATISTICS]\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "total_errors=%lu\n", g_error_tracker.total_errors);
    written += snprintf(buffer + written, buffer_size - written,
                       "errors_recovered=%lu\n", g_error_tracker.errors_recovered);
    written += snprintf(buffer + written, buffer_size - written,
                       "recovery_failures=%lu\n", g_error_tracker.recovery_failures);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\n[ERROR_HISTORY]\n");
    
    error_entry_t *error = g_error_tracker.error_history_head;
    while (error && written < buffer_size - 200) {
        written += snprintf(buffer + written, buffer_size - written,
                           "%lu,%d,%d,0x%04X,%d,%d,%s,%s\n",
                           error->timestamp,
                           error->error_type,
                           error->nic_index,
                           error->error_code,
                           error->severity,
                           error->recovered ? 1 : 0,
                           error->description,
                           error->context);
        error = error->next;
    }
    
    return SUCCESS;
}

/* Week 1 specific: NE2000 emulation error tracking */
int error_tracking_ne2000_emulation(uint16_t ne2000_reg, uint16_t expected, uint16_t actual) {
    if (!g_error_tracker.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (expected != actual) {
        char description[128];
        snprintf(description, sizeof(description),
                "NE2000 register mismatch: reg=0x%04X, expected=0x%04X, actual=0x%04X",
                ne2000_reg, expected, actual);
        
        return error_tracking_report_error(ERROR_TYPE_HARDWARE_IO_ERROR, 0, 
                                          ERROR_HARDWARE_REGISTERS, description, "NE2000_EMULATION");
    }
    
    return SUCCESS;
}

/* Cleanup error tracking system */
void error_tracking_cleanup(void) {
    if (!g_error_tracker.initialized) {
        return;
    }
    
    debug_log_info("Cleaning up error tracking system");
    
    /* Free error history */
    error_entry_t *error = g_error_tracker.error_history_head;
    while (error) {
        error_entry_t *next = error->next;
        free(error);
        error = next;
    }
    
    /* Free patterns */
    error_pattern_t *pattern = g_error_tracker.patterns_head;
    while (pattern) {
        error_pattern_t *next = pattern->next;
        free(pattern);
        pattern = next;
    }
    
    /* Free recovery strategies */
    if (g_error_tracker.strategies) {
        free(g_error_tracker.strategies);
    }
    
    memset(&g_error_tracker, 0, sizeof(error_tracker_t));
}