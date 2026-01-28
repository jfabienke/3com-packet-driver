/**
 * @file irqmit_init.c
 * @brief Interrupt Mitigation Engine - Initialization functions (OVERLAY segment)
 *
 * This file contains initialization functions called once during startup:
 * - Interrupt mitigation initialization
 * - Configuration and threshold setup
 * - Cleanup functions
 * - Statistics management
 *
 * Runtime functions are in irqmit_rt.c (ROOT segment)
 *
 * Updated: 2026-01-28 09:59:33 CET
 */

#include <string.h>
#include <dos.h>
#include "irqmit.h"
#include "hardware.h"
#include "logging.h"
#include "common.h"

/* ============================================================================
 * External declarations for global state (defined in irqmit_rt.c)
 * ============================================================================ */

extern interrupt_mitigation_context_t g_mitigation_contexts[MAX_NICS];
extern bool g_mitigation_initialized;

/* ============================================================================
 * Global mitigation parameters (from runtime config AH=95h)
 * ============================================================================ */

extern uint8_t g_mitigation_batch;    /* Max packets per interrupt */
extern uint8_t g_mitigation_timeout;  /* Max ticks before forcing interrupt */

/* ============================================================================
 * Forward declarations for functions in irqmit_rt.c
 * ============================================================================ */

extern void interrupt_mitigation_apply_runtime(interrupt_mitigation_context_t *ctx);

/* ============================================================================
 * Per-NIC Initialization
 * ============================================================================ */

/**
 * Initialize interrupt mitigation for a NIC
 */
int interrupt_mitigation_init(interrupt_mitigation_context_t *ctx,
                              struct nic_info *nic) {
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

/* ============================================================================
 * Cleanup
 * ============================================================================ */

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

/* ============================================================================
 * Global Initialization
 * ============================================================================ */

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

/* ============================================================================
 * Configuration Application
 * ============================================================================ */

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

/* ============================================================================
 * Enable/Disable Control
 * ============================================================================ */

/**
 * Enable/disable interrupt mitigation
 */
int set_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx,
                                     bool enable) {
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

/* ============================================================================
 * Statistics Management
 * ============================================================================ */

/**
 * Get interrupt statistics
 */
int get_interrupt_stats(interrupt_mitigation_context_t *ctx,
                        interrupt_stats_t *stats) {
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

/* ============================================================================
 * Performance Metrics
 * ============================================================================ */

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

/* ============================================================================
 * Work Limit Configuration
 * ============================================================================ */

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
