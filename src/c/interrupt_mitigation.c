/**
 * @file interrupt_mitigation.c
 * @brief Interrupt Mitigation System Implementation
 * 
 * Sprint 1.3: Interrupt Mitigation Implementation
 * 
 * This module implements Becker's interrupt batching technique to reduce
 * CPU utilization by 15-25% under high load by processing multiple events
 * per interrupt instead of one event per interrupt.
 * 
 * Implementation follows the principle:
 * - Traditional: Interrupt → Process 1 event → Return (32 interrupts = 32 overhead cycles)
 * - Batched: Interrupt → Process up to 32 events → Return (1 interrupt = 1 overhead cycle)
 * 
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/interrupt_mitigation.h"
#include "../include/hardware.h"
#include "../include/3c515.h"
#include "../include/3c509b.h"
#include "../include/logging.h"
#include "../include/timestamp.h"
#include "../include/cpu_optimized.h"
#include <string.h>

/* Internal helper functions */
static int validate_context(interrupt_mitigation_context_t *ctx);
static uint8_t get_max_work_for_nic_type(nic_type_t nic_type);
static uint32_t get_timestamp_us(void);
static bool check_emergency_conditions(interrupt_mitigation_context_t *ctx);
static void record_event_type(interrupt_mitigation_context_t *ctx, interrupt_event_type_t event_type);

/**
 * @brief Initialize interrupt mitigation system
 */
int interrupt_mitigation_init(interrupt_mitigation_context_t *ctx, struct nic_info *nic)
{
    if (!ctx || !nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Clear the context structure using CPU-optimized zero operation */
    cpu_opt_memzero(ctx, sizeof(interrupt_mitigation_context_t));
    
    /* Initialize basic configuration */
    ctx->nic_type = nic->type;
    ctx->nic = nic;
    ctx->max_work_limit = get_max_work_for_nic_type(nic->type);
    ctx->status_flags = IM_STATUS_ENABLED;
    
    /* Initialize timing */
    ctx->last_interrupt_time = get_timestamp_us();
    ctx->interrupt_start_time = 0;
    
    /* Initialize statistics */
    clear_interrupt_stats(ctx);
    
    IM_DEBUG("Interrupt mitigation initialized for %s (max_work=%d)",
             nic->type == NIC_TYPE_3C515_TX ? "3C515" : "3C509B",
             ctx->max_work_limit);
    
    return SUCCESS;
}

/**
 * @brief Cleanup interrupt mitigation resources
 */
void interrupt_mitigation_cleanup(interrupt_mitigation_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    
    /* Disable interrupt mitigation */
    ctx->status_flags &= ~IM_STATUS_ENABLED;
    
    /* Clear NIC reference */
    ctx->nic = NULL;
    
    IM_DEBUG("Interrupt mitigation cleanup completed");
}

/**
 * @brief Process batched interrupts for 3C515 NIC
 */
int process_batched_interrupts_3c515(interrupt_mitigation_context_t *ctx)
{
    int total_work = 0;
    int work_done = 0;
    interrupt_event_type_t event_type;
    
    if (validate_context(ctx) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    if (ctx->nic->type != NIC_TYPE_3C515_TX) {
        return ERROR_INVALID_OPERATION;
    }
    
    /* Mark as active */
    ctx->status_flags |= IM_STATUS_ACTIVE;
    ctx->current_work_count = 0;
    
    IM_START_TIMING(ctx);
    
    IM_TRACE("Starting batched interrupt processing for 3C515");
    
    /* Process events up to work limit - CPU-optimized loop structure */
    uint8_t work_limit = ctx->max_work_limit;
    while (ctx->current_work_count < work_limit) {
        /* Prefetch next iteration data for better cache performance */
        if (ctx->current_work_count + 1 < work_limit) {
            cpu_opt_prefetch(&ctx->stats);
        }
        /* Check for more work */
        if (!more_work_available(ctx)) {
            IM_TRACE("No more work available, stopping batch");
            break;
        }
        
        /* Emergency conditions check */
        if (check_emergency_conditions(ctx)) {
            IM_DEBUG("Emergency conditions detected, breaking batch");
            ctx->stats.emergency_breaks++;
            break;
        }
        
        /* Process next event */
        work_done = process_next_event(ctx, &event_type);
        if (work_done <= 0) {
            if (work_done < 0) {
                ctx->stats.processing_errors++;
                IM_DEBUG("Event processing error: %d", work_done);
            }
            break;
        }
        
        total_work += work_done;
        ctx->current_work_count += work_done;
        record_event_type(ctx, event_type);
        
        /* System responsiveness check */
        if (should_yield_cpu(ctx)) {
            IM_TRACE("CPU yield requested, stopping batch");
            ctx->stats.cpu_yield_count++;
            break;
        }
    }
    
    /* Check if we hit the work limit */
    if (ctx->current_work_count >= ctx->max_work_limit) {
        ctx->stats.work_limit_hits++;
        ctx->consecutive_full_batches++;
        IM_TRACE("Work limit reached (%d events)", ctx->max_work_limit);
    } else {
        ctx->consecutive_full_batches = 0;
    }
    
    /* Update statistics */
    if (total_work == 1) {
        ctx->stats.single_event_interrupts++;
    } else if (total_work > 1) {
        ctx->stats.batched_interrupts++;
    }
    
    /* Clear active flag */
    ctx->status_flags &= ~IM_STATUS_ACTIVE;
    
    IM_END_TIMING(ctx, total_work);
    
    IM_TRACE("Completed batch processing: %d events", total_work);
    
    return total_work;
}

/**
 * @brief Process batched interrupts for 3C509B NIC
 */
int process_batched_interrupts_3c509b(interrupt_mitigation_context_t *ctx)
{
    int total_work = 0;
    int work_done = 0;
    interrupt_event_type_t event_type;
    
    if (validate_context(ctx) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    if (ctx->nic->type != NIC_TYPE_3C509B) {
        return ERROR_INVALID_OPERATION;
    }
    
    /* Mark as active */
    ctx->status_flags |= IM_STATUS_ACTIVE;
    ctx->current_work_count = 0;
    
    IM_START_TIMING(ctx);
    
    IM_TRACE("Starting batched interrupt processing for 3C509B");
    
    /* Process events up to work limit (smaller for PIO-based 3C509B) - CPU-optimized loop */
    uint8_t work_limit = ctx->max_work_limit;
    while (ctx->current_work_count < work_limit) {
        /* Prefetch for PIO operations on older CPUs */
        if (ctx->current_work_count + 1 < work_limit) {
            cpu_opt_prefetch(&ctx->stats);
        }
        /* Check for more work */
        if (!more_work_available(ctx)) {
            IM_TRACE("No more work available, stopping batch");
            break;
        }
        
        /* Emergency conditions check */
        if (check_emergency_conditions(ctx)) {
            IM_DEBUG("Emergency conditions detected, breaking batch");
            ctx->stats.emergency_breaks++;
            break;
        }
        
        /* Process next event */
        work_done = process_next_event(ctx, &event_type);
        if (work_done <= 0) {
            if (work_done < 0) {
                ctx->stats.processing_errors++;
                IM_DEBUG("Event processing error: %d", work_done);
            }
            break;
        }
        
        total_work += work_done;
        ctx->current_work_count += work_done;
        record_event_type(ctx, event_type);
        
        /* System responsiveness check (more frequent for PIO) */
        if (should_yield_cpu(ctx)) {
            IM_TRACE("CPU yield requested, stopping batch");
            ctx->stats.cpu_yield_count++;
            break;
        }
    }
    
    /* Check if we hit the work limit */
    if (ctx->current_work_count >= ctx->max_work_limit) {
        ctx->stats.work_limit_hits++;
        ctx->consecutive_full_batches++;
        IM_TRACE("Work limit reached (%d events)", ctx->max_work_limit);
    } else {
        ctx->consecutive_full_batches = 0;
    }
    
    /* Update statistics */
    if (total_work == 1) {
        ctx->stats.single_event_interrupts++;
    } else if (total_work > 1) {
        ctx->stats.batched_interrupts++;
    }
    
    /* Clear active flag */
    ctx->status_flags &= ~IM_STATUS_ACTIVE;
    
    IM_END_TIMING(ctx, total_work);
    
    IM_TRACE("Completed batch processing: %d events", total_work);
    
    return total_work;
}

/**
 * @brief Check if more work is available for processing
 */
bool more_work_available(interrupt_mitigation_context_t *ctx)
{
    if (!ctx || !ctx->nic) {
        return false;
    }
    
    /* Use NIC-specific check_interrupt function to see if more work is available */
    if (ctx->nic->type == NIC_TYPE_3C515_TX) {
        /* Call 3C515-specific function */
        extern int _3c515_check_interrupt(struct nic_info *nic);
        return _3c515_check_interrupt(ctx->nic) > 0;
    } else if (ctx->nic->type == NIC_TYPE_3C509B) {
        /* Call 3C509B-specific function */
        extern int _3c509b_check_interrupt_batched(struct nic_info *nic);
        return _3c509b_check_interrupt_batched(ctx->nic) > 0;
    }
    
    /* Fallback: assume no more work if unsupported NIC type */
    return false;
}

/**
 * @brief Process next available event
 */
int process_next_event(interrupt_mitigation_context_t *ctx, interrupt_event_type_t *event_type)
{
    if (!ctx || !ctx->nic || !event_type) {
        return ERROR_INVALID_PARAM;
    }
    
    *event_type = EVENT_TYPE_RX_COMPLETE; /* Default event type */
    
    /* Call NIC-specific single event processing function */
    if (ctx->nic->type == NIC_TYPE_3C515_TX) {
        /* Call 3C515-specific single event processor */
        extern int _3c515_process_single_event(struct nic_info *nic, interrupt_event_type_t *event_type);
        return _3c515_process_single_event(ctx->nic, event_type);
    } else if (ctx->nic->type == NIC_TYPE_3C509B) {
        /* Call 3C509B-specific single event processor */
        extern int _3c509b_process_single_event(struct nic_info *nic, interrupt_event_type_t *event_type);
        return _3c509b_process_single_event(ctx->nic, event_type);
    }
    
    return 0; /* No event processed - unsupported NIC type */
}

/**
 * @brief Check if CPU should be yielded for system responsiveness
 */
bool should_yield_cpu(interrupt_mitigation_context_t *ctx)
{
    if (!ctx) {
        return true;
    }
    
    /* Yield if we've processed too many events consecutively */
    if (ctx->current_work_count >= CPU_YIELD_THRESHOLD) {
        return true;
    }
    
    /* Yield if we've been in the interrupt handler too long - CPU-optimized timing check */
    uint32_t current_time = get_timestamp_us();
    uint32_t elapsed_time = current_time - ctx->interrupt_start_time;
    uint32_t max_time_us = MAX_INTERRUPT_TIME_MS * 1000;
    
    /* Use CPU-optimized comparison for better branch prediction */
    if (elapsed_time > max_time_us) {
        return true;
    }
    
    /* Yield if we have too many consecutive full batches (system overload) */
    if (ctx->consecutive_full_batches > 3) {
        ctx->status_flags |= IM_STATUS_OVERLOAD;
        return true;
    }
    
    return false;
}

/**
 * @brief Update interrupt statistics
 */
void update_interrupt_stats(interrupt_mitigation_context_t *ctx, 
                           int events_processed, 
                           uint32_t processing_time_us)
{
    if (!ctx || events_processed < 0) {
        return;
    }
    
    interrupt_stats_t *stats = &ctx->stats;
    
    /* Update basic counters using CPU-optimized atomic operations where beneficial */
    const cpu_opt_context_t* cpu_ctx = cpu_opt_get_context();
    if (cpu_ctx && cpu_ctx->cpu_type >= CPU_TYPE_80486) {
        /* Use atomic operations for thread safety on capable CPUs */
        cpu_opt_atomic_inc(&stats->total_interrupts);
        stats->events_processed += events_processed; /* Non-atomic for now */
    } else {
        /* Standard operations for older CPUs */
        stats->total_interrupts++;
        stats->events_processed += events_processed;
    }
    
    /* Update max events per interrupt */
    if (events_processed > stats->max_events_per_interrupt) {
        stats->max_events_per_interrupt = events_processed;
    }
    
    /* Update average events per interrupt */
    if (stats->total_interrupts > 0) {
        stats->avg_events_per_interrupt = stats->events_processed / stats->total_interrupts;
    }
    
    /* Update timing statistics */
    stats->total_processing_time_us += processing_time_us;
    
    if (stats->min_processing_time_us == 0 || processing_time_us < stats->min_processing_time_us) {
        stats->min_processing_time_us = processing_time_us;
    }
    
    if (processing_time_us > stats->max_processing_time_us) {
        stats->max_processing_time_us = processing_time_us;
    }
    
    ctx->last_interrupt_time = get_timestamp_us();
}

/**
 * @brief Get interrupt statistics
 */
int get_interrupt_stats(interrupt_mitigation_context_t *ctx, interrupt_stats_t *stats)
{
    if (!ctx || !stats) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Copy statistics structure using CPU-optimized copy operation */
    cpu_opt_memcpy(stats, &ctx->stats, sizeof(interrupt_stats_t), CPU_OPT_FLAG_CACHE_ALIGN);
    
    return SUCCESS;
}

/**
 * @brief Clear interrupt statistics
 */
void clear_interrupt_stats(interrupt_mitigation_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    
    cpu_opt_memzero(&ctx->stats, sizeof(interrupt_stats_t));
    ctx->consecutive_full_batches = 0;
    ctx->status_flags &= ~(IM_STATUS_OVERLOAD | IM_STATUS_EMERGENCY);
}

/**
 * @brief Check if interrupt mitigation is enabled
 */
bool is_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx)
{
    if (!ctx) {
        return false;
    }
    
    return (ctx->status_flags & IM_STATUS_ENABLED) != 0;
}

/**
 * @brief Enable or disable interrupt mitigation
 */
int set_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx, bool enable)
{
    if (!ctx) {
        return ERROR_INVALID_PARAM;
    }
    
    if (enable) {
        ctx->status_flags |= IM_STATUS_ENABLED;
        IM_DEBUG("Interrupt mitigation enabled");
    } else {
        ctx->status_flags &= ~IM_STATUS_ENABLED;
        IM_DEBUG("Interrupt mitigation disabled");
    }
    
    return SUCCESS;
}

/**
 * @brief Get current performance metrics
 */
int get_performance_metrics(interrupt_mitigation_context_t *ctx,
                           float *cpu_utilization,
                           float *avg_events_per_interrupt,
                           float *batching_efficiency)
{
    if (!ctx || !cpu_utilization || !avg_events_per_interrupt || !batching_efficiency) {
        return ERROR_INVALID_PARAM;
    }
    
    interrupt_stats_t *stats = &ctx->stats;
    
    /* Calculate average events per interrupt */
    if (stats->total_interrupts > 0) {
        *avg_events_per_interrupt = (float)stats->events_processed / stats->total_interrupts;
    } else {
        *avg_events_per_interrupt = 0.0f;
    }
    
    /* Calculate batching efficiency (percentage of interrupts that processed multiple events) */
    if (stats->total_interrupts > 0) {
        *batching_efficiency = (float)stats->batched_interrupts * 100.0f / stats->total_interrupts;
    } else {
        *batching_efficiency = 0.0f;
    }
    
    /* Calculate approximate CPU utilization based on interrupt processing time */
    /* This is a rough estimate - actual implementation would need system timing info */
    if (stats->total_interrupts > 0) {
        uint32_t avg_processing_time = stats->total_processing_time_us / stats->total_interrupts;
        *cpu_utilization = (float)avg_processing_time / 10000.0f; /* Rough estimate */
        if (*cpu_utilization > 100.0f) *cpu_utilization = 100.0f;
    } else {
        *cpu_utilization = 0.0f;
    }
    
    return SUCCESS;
}

/**
 * @brief Set work limit for NIC type
 */
int set_work_limit(interrupt_mitigation_context_t *ctx, uint8_t work_limit)
{
    if (!ctx) {
        return ERROR_INVALID_PARAM;
    }
    
    if (work_limit == 0 || work_limit > EMERGENCY_BREAK_COUNT) {
        return ERROR_INVALID_PARAM;
    }
    
    ctx->max_work_limit = work_limit;
    
    IM_DEBUG("Work limit set to %d", work_limit);
    
    return SUCCESS;
}

/**
 * @brief Get current work limit
 */
uint8_t get_work_limit(interrupt_mitigation_context_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    
    return ctx->max_work_limit;
}

/* Internal helper functions */

/**
 * @brief Validate interrupt mitigation context
 */
static int validate_context(interrupt_mitigation_context_t *ctx)
{
    if (!ctx) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!ctx->nic) {
        return ERROR_INVALID_STATE;
    }
    
    if (!(ctx->status_flags & IM_STATUS_ENABLED)) {
        return ERROR_DISABLED;
    }
    
    return SUCCESS;
}

/**
 * @brief Get maximum work limit for NIC type
 */
static uint8_t get_max_work_for_nic_type(nic_type_t nic_type)
{
    switch (nic_type) {
        case NIC_TYPE_3C515_TX:
            return MAX_WORK_3C515;
        case NIC_TYPE_3C509B:
            return MAX_WORK_3C509B;
        default:
            return MAX_WORK_3C509B; /* Conservative default */
    }
}

/**
 * @brief Get current timestamp in microseconds using CPU-optimized timer
 */
static uint32_t get_timestamp_us(void)
{
    /* Use CPU-optimized high-precision timer if available */
    const cpu_opt_context_t* cpu_ctx = cpu_opt_get_context();
    if (cpu_ctx && cpu_ctx->cpu_type >= CPU_TYPE_80486) {
        /* Use high-precision timer for 486+ CPUs */
        return (uint32_t)(cpu_opt_read_timer() / 1000);
    } else {
        /* Fallback for older CPUs */
        static uint32_t fake_time = 0;
        return ++fake_time * 1000; /* Increment by 1ms each call */
    }
}

/**
 * @brief Check for emergency conditions that require immediate break
 */
static bool check_emergency_conditions(interrupt_mitigation_context_t *ctx)
{
    if (!ctx) {
        return true;
    }
    
    /* Emergency break if we've processed too many events */
    if (ctx->current_work_count >= EMERGENCY_BREAK_COUNT) {
        ctx->status_flags |= IM_STATUS_EMERGENCY;
        return true;
    }
    
    /* Emergency break if system is in overload state for too long */
    if ((ctx->status_flags & IM_STATUS_OVERLOAD) && ctx->consecutive_full_batches > 5) {
        ctx->status_flags |= IM_STATUS_EMERGENCY;
        return true;
    }
    
    return false;
}

/**
 * @brief Record event type for statistics
 */
static void record_event_type(interrupt_mitigation_context_t *ctx, interrupt_event_type_t event_type)
{
    if (!ctx || event_type >= EVENT_TYPE_MAX) {
        return;
    }
    
    ctx->stats.events_by_type[event_type]++;
}