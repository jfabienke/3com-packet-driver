/**
 * @file error_handling.h
 * @brief Comprehensive error handling and recovery system
 *
 * Sprint 0B.2: Advanced Error Handling & Recovery
 * Implements sophisticated error classification, logging, and automatic recovery
 * mechanisms for 3COM packet driver resilience and fault tolerance.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _ERROR_HANDLING_H_
#define _ERROR_HANDLING_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "nic_defs.h"
#include "logging.h"
#include <stdint.h>
#include <stdbool.h>

/* Error severity levels matching Linux conventions */
#define ERROR_LEVEL_INFO        0   /* Informational message */
#define ERROR_LEVEL_WARNING     1   /* Warning condition */
#define ERROR_LEVEL_CRITICAL    2   /* Critical error condition */
#define ERROR_LEVEL_FATAL       3   /* Fatal error, system unstable */

/* Error type classifications for RX failures */
#define RX_ERROR_NONE           0x00
#define RX_ERROR_OVERRUN        0x01    /* FIFO overrun */
#define RX_ERROR_CRC            0x02    /* CRC checksum error */
#define RX_ERROR_FRAME          0x04    /* Framing error */
#define RX_ERROR_LENGTH         0x08    /* Invalid length */
#define RX_ERROR_ALIGNMENT      0x10    /* Alignment error */
#define RX_ERROR_COLLISION      0x20    /* Late collision */
#define RX_ERROR_TIMEOUT        0x40    /* Receive timeout */
#define RX_ERROR_DMA            0x80    /* DMA transfer error */

/* Error type classifications for TX failures */
#define TX_ERROR_NONE           0x00
#define TX_ERROR_COLLISION      0x01    /* Collision detected */
#define TX_ERROR_UNDERRUN       0x02    /* FIFO underrun */
#define TX_ERROR_TIMEOUT        0x04    /* Transmission timeout */
#define TX_ERROR_EXCESSIVE_COL  0x08    /* Excessive collisions */
#define TX_ERROR_CARRIER_LOST   0x10    /* Carrier lost */
#define TX_ERROR_HEARTBEAT      0x20    /* Heartbeat failure */
#define TX_ERROR_WINDOW         0x40    /* Out of window collision */
#define TX_ERROR_DMA            0x80    /* DMA transfer error */

/* Adapter failure types */
#define ADAPTER_FAILURE_NONE    0x00
#define ADAPTER_FAILURE_RESET   0x01    /* Reset required */
#define ADAPTER_FAILURE_HANG    0x02    /* Adapter hang detected */
#define ADAPTER_FAILURE_LINK    0x04    /* Link failure */
#define ADAPTER_FAILURE_MEMORY  0x08    /* Memory corruption */
#define ADAPTER_FAILURE_IRQ     0x10    /* IRQ problems */
#define ADAPTER_FAILURE_DMA     0x20    /* DMA subsystem failure */
#define ADAPTER_FAILURE_THERMAL 0x40    /* Thermal shutdown */
#define ADAPTER_FAILURE_POWER   0x80    /* Power supply issue */

/* Recovery strategy types */
#define RECOVERY_STRATEGY_NONE      0
#define RECOVERY_STRATEGY_SOFT      1   /* Soft reset and reconfigure */
#define RECOVERY_STRATEGY_HARD      2   /* Hard reset and reinitialize */
#define RECOVERY_STRATEGY_REINIT    3   /* Complete reinitialization */
#define RECOVERY_STRATEGY_DISABLE   4   /* Disable adapter */
#define RECOVERY_STRATEGY_FAILOVER  5   /* Failover to backup adapter */

/* Recovery result codes */
#define RECOVERY_SUCCESS            0   /* Recovery successful */
#define RECOVERY_PARTIAL            1   /* Partial recovery */
#define RECOVERY_FAILED            -1   /* Recovery failed */
#define RECOVERY_RETRY_NEEDED      -2   /* Retry recovery later */
#define RECOVERY_FATAL             -3   /* Fatal, cannot recover */

/* Error thresholds and limits */
#define MAX_ERROR_RATE_PERCENT      10  /* Maximum error rate before recovery */
#define MAX_CONSECUTIVE_ERRORS      5   /* Maximum consecutive errors */
#define MAX_RECOVERY_ATTEMPTS       3   /* Maximum recovery attempts */
#define ERROR_RATE_WINDOW_MS        5000 /* Error rate calculation window */
#define RECOVERY_TIMEOUT_MS         30000 /* Recovery operation timeout */
#define RECOVERY_RETRY_DELAY_MS     1000  /* Delay between recovery attempts */

/* Diagnostic ring buffer size */
#define ERROR_RING_BUFFER_SIZE      4096  /* 4KB ring buffer for error logs */
#define ERROR_LOG_ENTRY_SIZE        128   /* Maximum size per log entry */

/**
 * @brief Comprehensive error statistics structure
 * 
 * Tracks detailed error counters and recovery statistics for each adapter.
 * Based on proven Linux driver patterns for long-term stability.
 */
typedef struct {
    /* Basic error counters */
    uint32_t rx_errors;                 /* Total RX errors */
    uint32_t tx_errors;                 /* Total TX errors */
    uint32_t rx_overruns;               /* RX FIFO overruns */
    uint32_t rx_crc_errors;             /* RX CRC errors */
    uint32_t rx_frame_errors;           /* RX frame errors */
    uint32_t rx_length_errors;          /* RX length errors */
    uint32_t rx_alignment_errors;       /* RX alignment errors */
    uint32_t rx_collision_errors;       /* RX late collisions */
    uint32_t rx_timeout_errors;         /* RX timeout errors */
    uint32_t rx_dma_errors;             /* RX DMA errors */
    
    /* TX error breakdown */
    uint32_t tx_collisions;             /* TX collisions */
    uint32_t tx_underruns;              /* TX FIFO underruns */
    uint32_t tx_timeout_errors;         /* TX timeout errors */
    uint32_t tx_excessive_collisions;   /* TX excessive collisions */
    uint32_t tx_carrier_lost;           /* TX carrier lost */
    uint32_t tx_heartbeat_errors;       /* TX heartbeat failures */
    uint32_t tx_window_errors;          /* TX window errors */
    uint32_t tx_dma_errors;             /* TX DMA errors */
    
    /* Adapter-level statistics */
    uint32_t adapter_failures;          /* Total adapter failures */
    uint32_t adapter_resets;            /* Adapter resets performed */
    uint32_t adapter_hangs;             /* Adapter hang events */
    uint32_t link_failures;             /* Link failure events */
    uint32_t memory_errors;             /* Memory corruption events */
    uint32_t irq_errors;                /* IRQ-related errors */
    uint32_t dma_errors;                /* DMA subsystem errors */
    uint32_t thermal_events;            /* Thermal protection events */
    uint32_t power_events;              /* Power-related events */
    
    /* Recovery statistics */
    uint32_t recoveries_attempted;      /* Total recovery attempts */
    uint32_t recoveries_successful;     /* Successful recoveries */
    uint32_t recoveries_failed;         /* Failed recoveries */
    uint32_t soft_resets;               /* Soft resets performed */
    uint32_t hard_resets;               /* Hard resets performed */
    uint32_t reinitializations;         /* Complete reinitializations */
    uint32_t failovers;                 /* Failover events */
    uint32_t adapter_disabled_count;    /* Times adapter was disabled */
    
    /* Timing and rate tracking */
    uint32_t last_error_timestamp;      /* Last error time */
    uint32_t last_recovery_timestamp;   /* Last recovery time */
    uint32_t error_rate_window_start;   /* Error rate calculation window start */
    uint32_t errors_in_window;          /* Errors in current window */
    uint32_t consecutive_errors;        /* Current consecutive error count */
    uint32_t error_burst_count;         /* Error burst events */
    
    /* Performance impact tracking */
    uint32_t packets_dropped_due_errors; /* Packets dropped due to errors */
    uint32_t bandwidth_degradation_events; /* Bandwidth degradation events */
    uint32_t latency_spike_events;      /* Latency spike events */
    uint32_t throughput_loss_ms;        /* Total throughput loss time */
} error_stats_t;

/**
 * @brief NIC context structure with error handling state
 * 
 * Extended context structure that includes error statistics and recovery state
 * for comprehensive error management and automatic recovery.
 */
typedef struct {
    /* Basic NIC information */
    nic_info_t nic_info;               /* Standard NIC information */
    
    /* Error statistics */
    error_stats_t error_stats;         /* Comprehensive error statistics */
    
    /* Recovery state */
    uint8_t recovery_state;             /* Current recovery state */
    uint8_t recovery_attempts;          /* Current recovery attempt count */
    uint8_t recovery_strategy;          /* Current recovery strategy */
    uint32_t recovery_start_time;       /* Recovery operation start time */
    uint32_t next_recovery_time;        /* Next allowed recovery time */
    bool recovery_in_progress;          /* Recovery operation active */
    bool adapter_disabled;              /* Adapter disabled due to errors */
    
    /* Error rate tracking */
    uint32_t error_rate_percent;        /* Current error rate percentage */
    uint32_t peak_error_rate;           /* Peak error rate seen */
    uint32_t error_threshold_breaches;  /* Threshold breach count */
    
    /* Link state tracking */
    bool link_up;                       /* Current link state */
    uint32_t link_state_changes;        /* Link state change count */
    uint32_t link_down_duration;        /* Total link down time */
    
    /* Diagnostic state */
    bool diagnostic_mode;               /* Diagnostic mode active */
    uint32_t diagnostic_start_time;     /* Diagnostic mode start time */
    uint8_t last_error_type;            /* Last error type encountered */
    uint8_t last_failure_type;          /* Last adapter failure type */
} nic_context_t;

/**
 * @brief Error log entry structure for ring buffer
 */
typedef struct {
    uint32_t timestamp;                 /* Error timestamp */
    uint8_t severity;                   /* Error severity level */
    uint8_t error_type;                 /* Error type classification */
    uint8_t nic_id;                     /* NIC identifier */
    uint8_t recovery_action;            /* Recovery action taken */
    char message[ERROR_LOG_ENTRY_SIZE - 12]; /* Error message */
} error_log_entry_t;

/**
 * @brief Global error handling state
 */
typedef struct {
    /* Ring buffer for error logging */
    char *ring_buffer;                  /* Ring buffer memory */
    uint32_t ring_buffer_size;          /* Ring buffer size */
    uint32_t ring_write_pos;            /* Current write position */
    uint32_t ring_read_pos;             /* Current read position */
    uint32_t ring_entries;              /* Number of entries */
    bool ring_wrapped;                  /* Buffer has wrapped */
    
    /* Global error tracking */
    uint32_t total_errors;              /* Total system errors */
    uint32_t total_recoveries;          /* Total recovery attempts */
    uint32_t system_uptime_start;       /* System start timestamp */
    uint32_t last_global_error;         /* Last system-wide error */
    
    /* System health state */
    uint8_t system_health_level;        /* Overall system health (0-100) */
    bool emergency_mode;                /* Emergency mode active */
    bool logging_active;                /* Error logging active */
    
    /* Performance counters */
    uint32_t log_entries_written;       /* Log entries written */
    uint32_t log_entries_dropped;       /* Log entries dropped */
    uint32_t log_buffer_overruns;       /* Buffer overrun count */
} error_handling_state_t;

/* Global error handling state */
extern error_handling_state_t g_error_handling_state;

/* Core error handling functions */
int error_handling_init(void);
void error_handling_cleanup(void);
int error_handling_reset_stats(nic_context_t *ctx);

/* Error classification and handling */
int handle_rx_error(nic_context_t *ctx, uint32_t rx_status);
int handle_tx_error(nic_context_t *ctx, uint32_t tx_status);
int handle_adapter_error(nic_context_t *ctx, uint8_t failure_type);

/* Recovery functions */
int attempt_adapter_recovery(nic_context_t *ctx);
int perform_soft_reset(nic_context_t *ctx);
int perform_hard_reset(nic_context_t *ctx);
int perform_complete_reinit(nic_context_t *ctx);
int attempt_failover(nic_context_t *ctx);

/* Error rate and threshold management */
int update_error_rate(nic_context_t *ctx);
bool check_error_thresholds(nic_context_t *ctx);
int escalate_recovery_strategy(nic_context_t *ctx);

/* Diagnostic and logging functions */
void log_error(uint8_t severity, nic_context_t *ctx, uint8_t error_type, const char *format, ...);
int write_error_to_ring_buffer(uint8_t severity, uint8_t nic_id, uint8_t error_type, 
                               uint8_t recovery_action, const char *message);
int read_error_log_entries(error_log_entry_t *entries, int max_entries);

/* Recovery validation and monitoring */
int validate_recovery_success(nic_context_t *ctx);
int monitor_post_recovery_health(nic_context_t *ctx);
int schedule_recovery_retry(nic_context_t *ctx, uint32_t delay_ms);

/* Statistics and reporting */
void print_error_statistics(nic_context_t *ctx);
void print_global_error_summary(void);
int get_system_health_status(void);
int export_error_statistics(nic_context_t *ctx, char *buffer, size_t buffer_size);

/* Error threshold configuration */
int configure_error_thresholds(nic_context_t *ctx, uint32_t max_error_rate, 
                              uint32_t max_consecutive, uint32_t recovery_timeout);

/* Hardware-specific error handling */
int handle_3c509b_error(nic_context_t *ctx, uint16_t status_reg);
int handle_3c515_error(nic_context_t *ctx, uint16_t status_reg);

/* Recovery strategy selection */
int select_recovery_strategy(nic_context_t *ctx, uint8_t error_severity);
int determine_recovery_timeout(nic_context_t *ctx, uint8_t strategy);

/* Advanced Error Recovery System */
int advanced_recovery_init(void);
void advanced_recovery_cleanup(void);
int enhanced_adapter_recovery(nic_context_t *ctx, uint8_t error_type);

/* Protected Hardware Operations with Timeout */
int protected_hardware_operation(nic_context_t *ctx, uint16_t port, uint8_t operation,
                                uint16_t data, uint16_t timeout_ms);
int protected_wait_ready(nic_context_t *ctx, uint16_t status_port, uint8_t ready_mask, 
                        uint16_t timeout_ms);
int protected_dma_operation(nic_context_t *ctx, uint16_t dma_port, uint8_t completion_mask,
                           uint16_t timeout_ms);

/* Recovery Method Implementations */
int perform_retry_recovery(nic_context_t *ctx);
int perform_protected_soft_reset(nic_context_t *ctx);
int perform_protected_hard_reset(nic_context_t *ctx);
int perform_driver_restart(nic_context_t *ctx);
int perform_adapter_disable(nic_context_t *ctx);
int perform_system_failover(nic_context_t *ctx);

/* Recovery Statistics and Reporting */
void print_recovery_statistics(void);

/* Utility functions */
const char* error_severity_to_string(uint8_t severity);
const char* error_type_to_string(uint8_t error_type);
const char* recovery_strategy_to_string(uint8_t strategy);
const char* adapter_failure_to_string(uint8_t failure_type);

/* Convenience macros for error logging */
#define LOG_ERROR_INFO(ctx, type, fmt, ...) \
    log_error(ERROR_LEVEL_INFO, ctx, type, fmt, ##__VA_ARGS__)

#define LOG_ERROR_WARNING(ctx, type, fmt, ...) \
    log_error(ERROR_LEVEL_WARNING, ctx, type, fmt, ##__VA_ARGS__)

#define LOG_ERROR_CRITICAL(ctx, type, fmt, ...) \
    log_error(ERROR_LEVEL_CRITICAL, ctx, type, fmt, ##__VA_ARGS__)

#define LOG_ERROR_FATAL(ctx, type, fmt, ...) \
    log_error(ERROR_LEVEL_FATAL, ctx, type, fmt, ##__VA_ARGS__)

/* Error checking macros */
#define CHECK_ERROR_THRESHOLD(ctx) \
    (check_error_thresholds(ctx) && attempt_adapter_recovery(ctx) >= 0)

#define UPDATE_ERROR_STATS(ctx, stat_field) \
    do { (ctx)->error_stats.stat_field++; update_error_rate(ctx); } while(0)

#define RECOVERY_NEEDED(ctx) \
    ((ctx)->error_stats.consecutive_errors >= MAX_CONSECUTIVE_ERRORS || \
     (ctx)->error_rate_percent >= MAX_ERROR_RATE_PERCENT)

#ifdef __cplusplus
}
#endif

#endif /* _ERROR_HANDLING_H_ */