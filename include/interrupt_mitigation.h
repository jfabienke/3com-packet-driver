/**
 * @file interrupt_mitigation.h
 * @brief Interrupt Mitigation System for 3Com NICs
 * 
 * Sprint 1.3: Interrupt Mitigation Implementation
 * 
 * This module implements Becker's interrupt batching technique to reduce
 * CPU utilization by 15-25% under high load by processing multiple events
 * per interrupt instead of one event per interrupt.
 * 
 * Key Features:
 * - Configurable work limits per NIC type
 * - Interrupt statistics tracking  
 * - System responsiveness monitoring
 * - Batched event processing
 * - Performance measurement utilities
 * 
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _INTERRUPT_MITIGATION_H_
#define _INTERRUPT_MITIGATION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "hardware.h"

/* Work limits per NIC type based on hardware capabilities */
#define MAX_WORK_3C515   32   /* Bus mastering can handle more events */
#define MAX_WORK_3C509B  8    /* Programmed I/O needs more frequent yields */

/* System responsiveness thresholds */
#define MAX_INTERRUPT_TIME_MS    2    /* Maximum time to spend in interrupt handler */
#define CPU_YIELD_THRESHOLD      16   /* Yield CPU after this many events */
#define EMERGENCY_BREAK_COUNT    64   /* Emergency break to prevent system freeze */

/* Interrupt mitigation status flags */
#define IM_STATUS_ENABLED        BIT(0)   /* Interrupt mitigation enabled */
#define IM_STATUS_ACTIVE         BIT(1)   /* Currently processing batched interrupts */
#define IM_STATUS_OVERLOAD       BIT(2)   /* System overload detected */
#define IM_STATUS_EMERGENCY      BIT(3)   /* Emergency break activated */

/* Event types for statistics tracking */
typedef enum {
    EVENT_TYPE_RX_COMPLETE = 0,    /* Packet reception complete */
    EVENT_TYPE_TX_COMPLETE,        /* Packet transmission complete */
    EVENT_TYPE_RX_ERROR,           /* Reception error */
    EVENT_TYPE_TX_ERROR,           /* Transmission error */
    EVENT_TYPE_LINK_CHANGE,        /* Link status change */
    EVENT_TYPE_DMA_COMPLETE,       /* DMA operation complete (3C515 only) */
    EVENT_TYPE_COUNTER_OVERFLOW,   /* Statistics counter overflow */
    EVENT_TYPE_MAX                 /* Number of event types */
} interrupt_event_type_t;

/**
 * @brief Interrupt statistics structure for performance tracking
 */
typedef struct interrupt_stats {
    /* Overall interrupt statistics */
    uint32_t total_interrupts;          /* Total interrupt count */
    uint32_t events_processed;          /* Total events processed */
    uint32_t avg_events_per_interrupt;  /* Average events per interrupt */
    uint32_t max_events_per_interrupt;  /* Maximum events in single interrupt */
    
    /* Batching effectiveness */
    uint32_t work_limit_hits;           /* Times work limit was reached */
    uint32_t single_event_interrupts;  /* Interrupts with only one event */
    uint32_t batched_interrupts;        /* Interrupts with multiple events */
    
    /* System responsiveness */
    uint32_t cpu_yield_count;           /* Times CPU was yielded */
    uint32_t emergency_breaks;          /* Emergency break activations */
    uint32_t overload_events;           /* System overload events */
    
    /* Event type breakdown */
    uint32_t events_by_type[EVENT_TYPE_MAX];  /* Events per type */
    
    /* Performance metrics */
    uint32_t total_processing_time_us;  /* Total processing time in microseconds */
    uint32_t min_processing_time_us;    /* Minimum processing time */
    uint32_t max_processing_time_us;    /* Maximum processing time */
    
    /* Error tracking */
    uint32_t spurious_interrupts;      /* Spurious interrupt count */
    uint32_t processing_errors;        /* Event processing errors */
} interrupt_stats_t;

/**
 * @brief Interrupt mitigation context per NIC
 */
typedef struct interrupt_mitigation_context {
    /* Configuration */
    nic_type_t nic_type;                /* NIC type for work limit selection */
    uint8_t max_work_limit;             /* Maximum events per interrupt */
    uint8_t status_flags;               /* Status and control flags */
    
    /* Runtime state */
    uint8_t current_work_count;         /* Current work count in this interrupt */
    uint8_t consecutive_full_batches;   /* Consecutive full batch count */
    uint32_t last_interrupt_time;       /* Timestamp of last interrupt */
    uint32_t interrupt_start_time;      /* Start time of current interrupt */
    
    /* Statistics */
    interrupt_stats_t stats;            /* Performance statistics */
    
    /* NIC reference */
    struct nic_info *nic;               /* Pointer to associated NIC */
} interrupt_mitigation_context_t;

/* Function prototypes */

/**
 * @brief Initialize interrupt mitigation system
 * @param ctx Pointer to interrupt mitigation context
 * @param nic Pointer to NIC info structure
 * @return 0 on success, error code on failure
 */
int interrupt_mitigation_init(interrupt_mitigation_context_t *ctx, struct nic_info *nic);

/**
 * @brief Cleanup interrupt mitigation resources
 * @param ctx Pointer to interrupt mitigation context
 */
void interrupt_mitigation_cleanup(interrupt_mitigation_context_t *ctx);

/**
 * @brief Process batched interrupts for 3C515 NIC
 * @param ctx Pointer to interrupt mitigation context
 * @return Number of events processed, or negative error code
 */
int process_batched_interrupts_3c515(interrupt_mitigation_context_t *ctx);

/**
 * @brief Process batched interrupts for 3C509B NIC
 * @param ctx Pointer to interrupt mitigation context
 * @return Number of events processed, or negative error code
 */
int process_batched_interrupts_3c509b(interrupt_mitigation_context_t *ctx);

/**
 * @brief Check if more work is available for processing
 * @param ctx Pointer to interrupt mitigation context
 * @return true if more work available, false otherwise
 */
bool more_work_available(interrupt_mitigation_context_t *ctx);

/**
 * @brief Process next available event
 * @param ctx Pointer to interrupt mitigation context
 * @param event_type Pointer to store the type of event processed
 * @return Number of events processed (1 on success, 0 if no work, negative on error)
 */
int process_next_event(interrupt_mitigation_context_t *ctx, interrupt_event_type_t *event_type);

/**
 * @brief Check if CPU should be yielded for system responsiveness
 * @param ctx Pointer to interrupt mitigation context
 * @return true if CPU should be yielded, false otherwise
 */
bool should_yield_cpu(interrupt_mitigation_context_t *ctx);

/**
 * @brief Update interrupt statistics
 * @param ctx Pointer to interrupt mitigation context
 * @param events_processed Number of events processed in this interrupt
 * @param processing_time_us Processing time in microseconds
 */
void update_interrupt_stats(interrupt_mitigation_context_t *ctx, 
                           int events_processed, 
                           uint32_t processing_time_us);

/**
 * @brief Get interrupt statistics
 * @param ctx Pointer to interrupt mitigation context
 * @param stats Pointer to buffer to receive statistics
 * @return 0 on success, error code on failure
 */
int get_interrupt_stats(interrupt_mitigation_context_t *ctx, interrupt_stats_t *stats);

/**
 * @brief Clear interrupt statistics
 * @param ctx Pointer to interrupt mitigation context
 */
void clear_interrupt_stats(interrupt_mitigation_context_t *ctx);

/**
 * @brief Check if interrupt mitigation is enabled
 * @param ctx Pointer to interrupt mitigation context
 * @return true if enabled, false otherwise
 */
bool is_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx);

/**
 * @brief Enable or disable interrupt mitigation
 * @param ctx Pointer to interrupt mitigation context
 * @param enable true to enable, false to disable
 * @return 0 on success, error code on failure
 */
int set_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx, bool enable);

/**
 * @brief Get current performance metrics
 * @param ctx Pointer to interrupt mitigation context
 * @param cpu_utilization Pointer to store CPU utilization percentage
 * @param avg_events_per_interrupt Pointer to store average events per interrupt
 * @param batching_efficiency Pointer to store batching efficiency percentage
 * @return 0 on success, error code on failure
 */
int get_performance_metrics(interrupt_mitigation_context_t *ctx,
                           float *cpu_utilization,
                           float *avg_events_per_interrupt,
                           float *batching_efficiency);

/**
 * @brief Set work limit for NIC type
 * @param ctx Pointer to interrupt mitigation context
 * @param work_limit New work limit (must be > 0 and <= EMERGENCY_BREAK_COUNT)
 * @return 0 on success, error code on failure
 */
int set_work_limit(interrupt_mitigation_context_t *ctx, uint8_t work_limit);

/**
 * @brief Get current work limit
 * @param ctx Pointer to interrupt mitigation context
 * @return Current work limit, or 0 on error
 */
uint8_t get_work_limit(interrupt_mitigation_context_t *ctx);

/* Utility macros for time measurement */
#define IM_START_TIMING(ctx) do { \
    (ctx)->interrupt_start_time = get_timestamp_us(); \
} while(0)

#define IM_END_TIMING(ctx, events) do { \
    uint32_t end_time = get_timestamp_us(); \
    uint32_t processing_time = end_time - (ctx)->interrupt_start_time; \
    update_interrupt_stats(ctx, events, processing_time); \
} while(0)

/* Debug macros for development */
#ifdef DEBUG_INTERRUPT_MITIGATION
#define IM_DEBUG(fmt, ...) LOG_DEBUG("[IM] " fmt, ##__VA_ARGS__)
#define IM_TRACE(fmt, ...) LOG_TRACE("[IM] " fmt, ##__VA_ARGS__)
#else
#define IM_DEBUG(fmt, ...)
#define IM_TRACE(fmt, ...)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _INTERRUPT_MITIGATION_H_ */