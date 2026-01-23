/**
 * @file interrupt_mitigation.c
 * @brief Interrupt Mitigation Engine - Runtime windowed batching
 *
 * Implements Becker's interrupt batching technique without time math in ISR.
 * Uses counters and limits only for O(1) overhead in hot path.
 */

#include <string.h>
#include <dos.h>
#include "../include/irqmit.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/common.h"

/* Global mitigation parameters (from runtime config AH=95h) */
extern uint8_t g_mitigation_batch;    /* Max packets per interrupt */
extern uint8_t g_mitigation_timeout;  /* Max ticks before forcing interrupt */

/* Per-NIC mitigation contexts */
static interrupt_mitigation_context_t g_mitigation_contexts[MAX_NICS];
static bool g_mitigation_initialized = false;

/**
 * Initialize interrupt mitigation for a NIC
 */
int interrupt_mitigation_init(interrupt_mitigation_context_t *ctx, struct nic_info *nic) {
    if (!ctx || !nic) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(interrupt_mitigation_context_t));
    
    ctx->nic = nic;
    ctx->nic_type = nic->type;
    
    /* Set work limits based on NIC type */
    if (nic->type == NIC_TYPE_3C515_TX) {
        ctx->max_work_limit = MAX_WORK_3C515;
    } else if (nic->type == NIC_TYPE_3C509B) {
        ctx->max_work_limit = MAX_WORK_3C509B;
    } else {
        ctx->max_work_limit = 4;  /* Conservative default */
    }
    
    /* Apply runtime config override if set */
    if (g_mitigation_batch > 0 && g_mitigation_batch < ctx->max_work_limit) {
        ctx->max_work_limit = g_mitigation_batch;
    }
    
    /* Enable by default */
    ctx->status_flags = IM_STATUS_ENABLED;
    
    /* Initialize statistics */
    ctx->stats.min_processing_time_us = 0xFFFFFFFF;
    
    LOG_DEBUG("Interrupt mitigation initialized for NIC %d: limit=%u", 
              nic->index, ctx->max_work_limit);
    
    return 0;
}

/**
 * Cleanup interrupt mitigation
 */
void interrupt_mitigation_cleanup(interrupt_mitigation_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    ctx->status_flags = 0;
    ctx->nic = NULL;
}

/**
 * Check if more work is available (NIC-specific)
 */
bool more_work_available(interrupt_mitigation_context_t *ctx) {
    uint16_t status;
    
    if (!ctx || !ctx->nic) {
        return false;
    }
    
    /* Read interrupt status register */
    status = inw(ctx->nic->io_base + 0x0E);
    
    /* Check for any pending interrupt conditions */
    /* Bits: 0=IntReq, 1=AdapterFailure, 2=TxComplete, 3=TxAvailable,
     *       4=RxComplete, 5=RxEarly, 6=IntRequested, 7=UpdateStats */
    return (status & 0x00FF) != 0;
}

/**
 * Process next event for 3C515
 */
static int process_3c515_event(interrupt_mitigation_context_t *ctx, interrupt_event_type_t *event_type) {
    uint16_t status;
    uint16_t io_base = ctx->nic->io_base;
    
    /* Read and clear interrupt status */
    status = inw(io_base + 0x0E);
    
    /* Process highest priority event first */
    if (status & 0x0010) {  /* RX Complete */
        /* Process RX packet */
        *event_type = EVENT_TYPE_RX_COMPLETE;
        
        /* Clear interrupt bit */
        outw(io_base + 0x0E, 0x0010);
        
        /* Handle packet reception (delegate to packet_ops) */
        extern int handle_rx_complete(struct nic_info *nic);
        handle_rx_complete(ctx->nic);
        
        return 1;
    }
    
    if (status & 0x0004) {  /* TX Complete */
        *event_type = EVENT_TYPE_TX_COMPLETE;
        
        /* Clear interrupt bit */
        outw(io_base + 0x0E, 0x0004);
        
        /* Handle TX completion */
        extern int handle_tx_complete(struct nic_info *nic);
        handle_tx_complete(ctx->nic);
        
        return 1;
    }
    
    if (status & 0x0080) {  /* Update Stats */
        *event_type = EVENT_TYPE_COUNTER_OVERFLOW;
        
        /* Clear interrupt bit */
        outw(io_base + 0x0E, 0x0080);
        
        /* Update statistics counters */
        extern void update_nic_stats(struct nic_info *nic);
        update_nic_stats(ctx->nic);
        
        return 1;
    }
    
    /* No events processed */
    return 0;
}

/**
 * Process next event for 3C509B
 */
static int process_3c509b_event(interrupt_mitigation_context_t *ctx, interrupt_event_type_t *event_type) {
    uint16_t status;
    uint16_t io_base = ctx->nic->io_base;
    
    /* Read interrupt status */
    status = inw(io_base + 0x0E);
    
    /* Process RX first (higher priority) */
    if (status & 0x0010) {  /* RX Complete */
        *event_type = EVENT_TYPE_RX_COMPLETE;
        
        /* Clear interrupt bit */
        outw(io_base + 0x0E, 0x0010);
        
        /* Handle packet reception */
        extern int handle_rx_complete(struct nic_info *nic);
        handle_rx_complete(ctx->nic);
        
        return 1;
    }
    
    if (status & 0x0004) {  /* TX Complete */
        *event_type = EVENT_TYPE_TX_COMPLETE;
        
        /* Clear interrupt bit */
        outw(io_base + 0x0E, 0x0004);
        
        /* Handle TX completion */
        extern int handle_tx_complete(struct nic_info *nic);
        handle_tx_complete(ctx->nic);
        
        return 1;
    }
    
    return 0;
}

/**
 * Process next available event
 */
int process_next_event(interrupt_mitigation_context_t *ctx, interrupt_event_type_t *event_type) {
    if (!ctx || !ctx->nic || !event_type) {
        return -1;
    }
    
    /* Dispatch based on NIC type */
    if (ctx->nic_type == NIC_TYPE_3C515_TX) {
        return process_3c515_event(ctx, event_type);
    } else if (ctx->nic_type == NIC_TYPE_3C509B) {
        return process_3c509b_event(ctx, event_type);
    }
    
    return 0;
}

/**
 * Process batched interrupts for 3C515
 */
int process_batched_interrupts_3c515(interrupt_mitigation_context_t *ctx) {
    int events_processed = 0;
    interrupt_event_type_t event_type;
    int result;
    
    if (!ctx || !is_interrupt_mitigation_enabled(ctx)) {
        return -1;
    }
    
    ctx->status_flags |= IM_STATUS_ACTIVE;
    ctx->stats.total_interrupts++;
    
    /* Process up to max_work_limit events */
    while (events_processed < ctx->max_work_limit) {
        /* Check for more work */
        if (!more_work_available(ctx)) {
            break;
        }
        
        /* Process next event */
        result = process_next_event(ctx, &event_type);
        if (result <= 0) {
            break;
        }
        
        events_processed++;
        ctx->stats.events_processed++;
        ctx->stats.events_by_type[event_type]++;
        
        /* Check for emergency break */
        if (events_processed >= EMERGENCY_BREAK_COUNT) {
            ctx->stats.emergency_breaks++;
            ctx->status_flags |= IM_STATUS_EMERGENCY;
            LOG_WARNING("Emergency break at %d events", events_processed);
            break;
        }
    }
    
    /* Update statistics */
    if (events_processed > 0) {
        if (events_processed == 1) {
            ctx->stats.single_event_interrupts++;
        } else {
            ctx->stats.batched_interrupts++;
        }
        
        if (events_processed > ctx->stats.max_events_per_interrupt) {
            ctx->stats.max_events_per_interrupt = events_processed;
        }
        
        if (events_processed >= ctx->max_work_limit) {
            ctx->stats.work_limit_hits++;
            ctx->consecutive_full_batches++;
        } else {
            ctx->consecutive_full_batches = 0;
        }
    } else {
        ctx->stats.spurious_interrupts++;
    }
    
    ctx->status_flags &= ~(IM_STATUS_ACTIVE | IM_STATUS_EMERGENCY);
    
    return events_processed;
}

/**
 * Process batched interrupts for 3C509B
 */
int process_batched_interrupts_3c509b(interrupt_mitigation_context_t *ctx) {
    int events_processed = 0;
    interrupt_event_type_t event_type;
    int result;
    
    if (!ctx || !is_interrupt_mitigation_enabled(ctx)) {
        return -1;
    }
    
    ctx->status_flags |= IM_STATUS_ACTIVE;
    ctx->stats.total_interrupts++;
    
    /* Process up to max_work_limit events (lower for PIO) */
    while (events_processed < ctx->max_work_limit) {
        /* Check for more work */
        if (!more_work_available(ctx)) {
            break;
        }
        
        /* Process next event */
        result = process_next_event(ctx, &event_type);
        if (result <= 0) {
            break;
        }
        
        events_processed++;
        ctx->stats.events_processed++;
        ctx->stats.events_by_type[event_type]++;
        
        /* PIO needs more frequent yields */
        if (events_processed >= CPU_YIELD_THRESHOLD / 2) {
            ctx->stats.cpu_yield_count++;
            break;
        }
    }
    
    /* Update statistics */
    if (events_processed > 0) {
        if (events_processed == 1) {
            ctx->stats.single_event_interrupts++;
        } else {
            ctx->stats.batched_interrupts++;
        }
        
        if (events_processed > ctx->stats.max_events_per_interrupt) {
            ctx->stats.max_events_per_interrupt = events_processed;
        }
    } else {
        ctx->stats.spurious_interrupts++;
    }
    
    ctx->status_flags &= ~IM_STATUS_ACTIVE;
    
    return events_processed;
}

/**
 * Check if CPU should be yielded
 */
bool should_yield_cpu(interrupt_mitigation_context_t *ctx) {
    if (!ctx) {
        return false;
    }
    
    /* Yield if we've hit work limit multiple times */
    if (ctx->consecutive_full_batches >= 3) {
        return true;
    }
    
    /* Yield if emergency flag is set */
    if (ctx->status_flags & IM_STATUS_EMERGENCY) {
        return true;
    }
    
    /* Yield for PIO NICs more frequently */
    if (ctx->nic_type == NIC_TYPE_3C509B && 
        ctx->current_work_count >= CPU_YIELD_THRESHOLD / 2) {
        return true;
    }
    
    return false;
}

/**
 * Update interrupt statistics
 */
void update_interrupt_stats(interrupt_mitigation_context_t *ctx, 
                           int events_processed, 
                           uint32_t processing_time_us) {
    if (!ctx) {
        return;
    }
    
    /* Update timing stats */
    ctx->stats.total_processing_time_us += processing_time_us;
    
    if (processing_time_us < ctx->stats.min_processing_time_us) {
        ctx->stats.min_processing_time_us = processing_time_us;
    }
    
    if (processing_time_us > ctx->stats.max_processing_time_us) {
        ctx->stats.max_processing_time_us = processing_time_us;
    }
    
    /* Calculate average events per interrupt */
    if (ctx->stats.total_interrupts > 0) {
        ctx->stats.avg_events_per_interrupt = 
            ctx->stats.events_processed / ctx->stats.total_interrupts;
    }
}

/**
 * Get interrupt statistics
 */
int get_interrupt_stats(interrupt_mitigation_context_t *ctx, interrupt_stats_t *stats) {
    if (!ctx || !stats) {
        return -1;
    }
    
    memcpy(stats, &ctx->stats, sizeof(interrupt_stats_t));
    return 0;
}

/**
 * Clear interrupt statistics
 */
void clear_interrupt_stats(interrupt_mitigation_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    memset(&ctx->stats, 0, sizeof(interrupt_stats_t));
    ctx->stats.min_processing_time_us = 0xFFFFFFFF;
}

/**
 * Check if mitigation is enabled
 */
bool is_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx) {
    return ctx && (ctx->status_flags & IM_STATUS_ENABLED);
}

/**
 * Enable/disable interrupt mitigation
 */
int set_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx, bool enable) {
    if (!ctx) {
        return -1;
    }
    
    if (enable) {
        ctx->status_flags |= IM_STATUS_ENABLED;
        LOG_INFO("Interrupt mitigation enabled for NIC %d", ctx->nic->index);
    } else {
        ctx->status_flags &= ~IM_STATUS_ENABLED;
        LOG_INFO("Interrupt mitigation disabled for NIC %d", ctx->nic->index);
    }
    
    return 0;
}

/**
 * Get performance metrics
 */
int get_performance_metrics(interrupt_mitigation_context_t *ctx,
                           float *cpu_utilization,
                           float *avg_events_per_interrupt,
                           float *batching_efficiency) {
    if (!ctx) {
        return -1;
    }
    
    if (cpu_utilization) {
        /* Estimate based on processing time */
        if (ctx->stats.total_interrupts > 0) {
            *cpu_utilization = (float)ctx->stats.total_processing_time_us / 
                             (float)(ctx->stats.total_interrupts * 1000);
        } else {
            *cpu_utilization = 0.0f;
        }
    }
    
    if (avg_events_per_interrupt) {
        if (ctx->stats.total_interrupts > 0) {
            *avg_events_per_interrupt = (float)ctx->stats.events_processed / 
                                       (float)ctx->stats.total_interrupts;
        } else {
            *avg_events_per_interrupt = 0.0f;
        }
    }
    
    if (batching_efficiency) {
        if (ctx->stats.total_interrupts > 0) {
            *batching_efficiency = (float)ctx->stats.batched_interrupts * 100.0f / 
                                 (float)ctx->stats.total_interrupts;
        } else {
            *batching_efficiency = 0.0f;
        }
    }
    
    return 0;
}

/**
 * Set work limit
 */
int set_work_limit(interrupt_mitigation_context_t *ctx, uint8_t work_limit) {
    if (!ctx || work_limit == 0 || work_limit > EMERGENCY_BREAK_COUNT) {
        return -1;
    }
    
    ctx->max_work_limit = work_limit;
    LOG_DEBUG("Work limit set to %u for NIC %d", work_limit, ctx->nic->index);
    
    return 0;
}

/**
 * Get work limit
 */
uint8_t get_work_limit(interrupt_mitigation_context_t *ctx) {
    return ctx ? ctx->max_work_limit : 0;
}

/**
 * Global initialization for all NICs
 */
int interrupt_mitigation_global_init(void) {
    int i;
    nic_info_t *nic;
    
    if (g_mitigation_initialized) {
        return 0;
    }
    
    memset(g_mitigation_contexts, 0, sizeof(g_mitigation_contexts));
    
    /* Initialize mitigation for each detected NIC */
    for (i = 0; i < MAX_NICS; i++) {
        nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_PRESENT)) {
            interrupt_mitigation_init(&g_mitigation_contexts[i], nic);
        }
    }
    
    g_mitigation_initialized = true;
    LOG_INFO("Interrupt mitigation system initialized");
    
    return 0;
}

/**
 * Get mitigation context for NIC
 */
interrupt_mitigation_context_t* get_mitigation_context(int nic_index) {
    if (nic_index < 0 || nic_index >= MAX_NICS) {
        return NULL;
    }
    
    return &g_mitigation_contexts[nic_index];
}

/**
 * Apply runtime configuration to all NICs
 * Called from AH=95h to update all contexts immediately
 */
void interrupt_mitigation_apply_all(void) {
    int i;
    
    for (i = 0; i < MAX_NICS; i++) {
        if (g_mitigation_contexts[i].nic && 
            (g_mitigation_contexts[i].nic->status & NIC_STATUS_PRESENT)) {
            interrupt_mitigation_apply_runtime(&g_mitigation_contexts[i]);
        }
    }
}

/**
 * Apply runtime configuration immediately
 * Updates mitigation parameters without time math in ISR
 */
void interrupt_mitigation_apply_runtime(interrupt_mitigation_context_t *ctx) {
    uint8_t hardware_limit;
    
    if (!ctx || !ctx->nic) {
        return;
    }
    
    /* Preserve hardware-based per-NIC work limits */
    if (ctx->nic->type == NIC_TYPE_3C515_TX) {
        hardware_limit = MAX_WORK_3C515;
    } else if (ctx->nic->type == NIC_TYPE_3C509B) {
        hardware_limit = MAX_WORK_3C509B;
    } else {
        hardware_limit = 4;
    }
    
    /* Apply runtime config but respect hardware limits */
    if (g_mitigation_batch > 0 && g_mitigation_batch <= hardware_limit) {
        ctx->max_work_limit = g_mitigation_batch;
    } else {
        ctx->max_work_limit = hardware_limit;
    }
    
    /* If batch target is 1, effectively disable mitigation */
    if (ctx->max_work_limit <= 1) {
        ctx->max_work_limit = 1;
        ctx->status_flags &= ~IM_STATUS_ENABLED;
    } else {
        ctx->status_flags |= IM_STATUS_ENABLED;
    }
    
    /* Reset only the batching state; preserve global stats */
    ctx->current_work_count = 0;
    ctx->consecutive_full_batches = 0;
    
    LOG_DEBUG("Runtime mitigation config applied: limit=%u for NIC %d",
              ctx->max_work_limit, ctx->nic->index);
}