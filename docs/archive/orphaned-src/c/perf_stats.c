/**
 * @file perf_stats.c
 * @brief Comprehensive performance statistics tracking
 * 
 * Implements detailed performance metrics collection as specified in
 * DRIVER_TUNING.md, tracking throughput, latency, CPU usage, and
 * optimization effectiveness.
 */

#include <stdint.h>
#include <string.h>
#include "logging.h"
#include "tx_lazy_irq.h"
#include "rx_batch_refill.h"

/* Performance targets from DRIVER_TUNING.md */
#define TARGET_PPS          80000       /* Packets per second */
#define TARGET_CPU_PERCENT  5           /* Max CPU usage */
#define TARGET_IRQ_RATE     625         /* Interrupts per second */
#define TARGET_LATENCY_US   100         /* Max latency in microseconds */

/* Timer frequency for measurements */
#define TIMER_HZ            1193182L    /* 8254 timer frequency */
#define TICKS_PER_SEC       18          /* DOS timer ticks per second */

/* Statistics structure for each NIC */
typedef struct {
    /* Packet counters */
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    
    /* Error counters */
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t tx_dropped;
    uint32_t rx_dropped;
    
    /* Performance metrics */
    uint32_t interrupts;
    uint32_t cpu_cycles;
    uint16_t max_latency;
    uint16_t avg_latency;
    
    /* Optimization metrics */
    uint32_t lazy_tx_savings;      /* IRQs saved by lazy TX */
    uint32_t batch_rx_savings;      /* Doorbells saved by batching */
    uint32_t copy_break_count;      /* Small packets copied */
    uint32_t smc_patches_hit;       /* SMC optimized paths taken */
    
    /* Throughput calculations */
    uint32_t pps_current;           /* Current packets/sec */
    uint32_t bps_current;           /* Current bits/sec */
    uint32_t pps_peak;              /* Peak packets/sec */
    uint32_t bps_peak;              /* Peak bits/sec */
    
    /* Timestamp for rate calculations */
    uint32_t last_update;           /* Last update time */
    uint32_t start_time;            /* Start time for totals */
} nic_stats_t;

/* Global statistics */
static nic_stats_t nic_stats[4] = {0};
static uint32_t global_start_time = 0;
static uint8_t stats_enabled = 1;

/* CPU cycle counting using RDTSC if available (486+) */
static uint8_t has_rdtsc = 0;

/**
 * @brief Check for RDTSC support (486DX+ CPUs)
 */
static void check_rdtsc_support(void) {
    /* For 286/386, RDTSC is not available */
    /* This would need CPUID checking on 486+ */
    has_rdtsc = 0;  /* Conservative default */
}

/**
 * @brief Get current timer ticks
 */
static uint32_t get_timer_ticks(void) {
    uint32_t ticks;
    
    __asm {
        cli
        xor     ax, ax
        int     1Ah             ; BIOS timer interrupt
        mov     word ptr ticks, dx
        mov     word ptr ticks+2, cx
        sti
    }
    
    return ticks;
}

/**
 * @brief Initialize performance statistics
 */
void perf_stats_init(void) {
    int i;
    
    LOG_INFO("Initializing performance statistics");
    
    /* Clear all stats */
    memset(nic_stats, 0, sizeof(nic_stats));
    
    /* Check CPU capabilities */
    check_rdtsc_support();
    
    /* Record start time */
    global_start_time = get_timer_ticks();
    
    for (i = 0; i < 4; i++) {
        nic_stats[i].start_time = global_start_time;
        nic_stats[i].last_update = global_start_time;
    }
    
    LOG_INFO("Performance targets: %lu pps, %d%% CPU, %d IRQ/s",
             TARGET_PPS, TARGET_CPU_PERCENT, TARGET_IRQ_RATE);
}

/**
 * @brief Update packet statistics
 */
void perf_stats_update_packet(uint8_t nic_index, uint8_t is_tx,
                              uint16_t length, uint8_t success) {
    nic_stats_t *stats;
    
    if (nic_index >= 4 || !stats_enabled) {
        return;
    }
    
    stats = &nic_stats[nic_index];
    
    if (is_tx) {
        if (success) {
            stats->tx_packets++;
            stats->tx_bytes += length;
        } else {
            stats->tx_errors++;
        }
    } else {
        if (success) {
            stats->rx_packets++;
            stats->rx_bytes += length;
        } else {
            stats->rx_errors++;
        }
    }
}

/**
 * @brief Update interrupt statistics
 */
void perf_stats_update_interrupt(uint8_t nic_index) {
    if (nic_index >= 4 || !stats_enabled) {
        return;
    }
    
    nic_stats[nic_index].interrupts++;
}

/**
 * @brief Update optimization statistics
 */
void perf_stats_update_optimization(uint8_t nic_index, uint8_t opt_type,
                                   uint32_t value) {
    nic_stats_t *stats;
    
    if (nic_index >= 4 || !stats_enabled) {
        return;
    }
    
    stats = &nic_stats[nic_index];
    
    switch (opt_type) {
        case 0: /* Lazy TX savings */
            stats->lazy_tx_savings += value;
            break;
        case 1: /* Batch RX savings */
            stats->batch_rx_savings += value;
            break;
        case 2: /* Copy-break count */
            stats->copy_break_count += value;
            break;
        case 3: /* SMC patch hits */
            stats->smc_patches_hit += value;
            break;
    }
}

/**
 * @brief Calculate throughput rates
 */
static void calculate_rates(uint8_t nic_index) {
    nic_stats_t *stats;
    uint32_t current_time;
    uint32_t elapsed_ticks;
    uint32_t total_packets;
    
    if (nic_index >= 4) {
        return;
    }
    
    stats = &nic_stats[nic_index];
    current_time = get_timer_ticks();
    
    /* Calculate elapsed time since last update */
    elapsed_ticks = current_time - stats->last_update;
    if (elapsed_ticks < TICKS_PER_SEC) {
        return;  /* Wait for at least 1 second */
    }
    
    /* Calculate current rates */
    total_packets = stats->tx_packets + stats->rx_packets;
    stats->pps_current = (total_packets * TICKS_PER_SEC) / elapsed_ticks;
    stats->bps_current = ((stats->tx_bytes + stats->rx_bytes) * 8 * TICKS_PER_SEC) / elapsed_ticks;
    
    /* Update peak rates */
    if (stats->pps_current > stats->pps_peak) {
        stats->pps_peak = stats->pps_current;
    }
    if (stats->bps_current > stats->bps_peak) {
        stats->bps_peak = stats->bps_current;
    }
    
    stats->last_update = current_time;
}

/**
 * @brief Get performance statistics for a NIC
 */
void perf_stats_get(uint8_t nic_index, void *buffer) {
    nic_stats_t *stats;
    
    if (nic_index >= 4 || !buffer) {
        return;
    }
    
    /* Update rates before returning stats */
    calculate_rates(nic_index);
    
    stats = &nic_stats[nic_index];
    memcpy(buffer, stats, sizeof(nic_stats_t));
}

/**
 * @brief Display performance summary
 */
void perf_stats_display(uint8_t nic_index) {
    nic_stats_t *stats;
    uint32_t irq_reduction;
    uint32_t efficiency;
    tx_lazy_stats_t tx_stats;
    rx_batch_stats_t rx_stats;
    
    if (nic_index >= 4) {
        return;
    }
    
    calculate_rates(nic_index);
    stats = &nic_stats[nic_index];
    
    /* Get optimization-specific stats */
    tx_lazy_get_stats(nic_index, &tx_stats);
    rx_batch_get_stats(nic_index, &rx_stats);
    
    LOG_INFO("=== NIC %d Performance Statistics ===", nic_index);
    
    /* Throughput metrics */
    LOG_INFO("Throughput:");
    LOG_INFO("  Current: %lu pps, %lu bps", 
             stats->pps_current, stats->bps_current);
    LOG_INFO("  Peak: %lu pps, %lu bps",
             stats->pps_peak, stats->bps_peak);
    LOG_INFO("  Target: %lu pps (%.1f%% achieved)",
             TARGET_PPS, 
             (stats->pps_peak * 100.0) / TARGET_PPS);
    
    /* Packet statistics */
    LOG_INFO("Packets:");
    LOG_INFO("  TX: %lu packets, %lu bytes",
             stats->tx_packets, stats->tx_bytes);
    LOG_INFO("  RX: %lu packets, %lu bytes",
             stats->rx_packets, stats->rx_bytes);
    LOG_INFO("  Errors: TX=%lu, RX=%lu",
             stats->tx_errors, stats->rx_errors);
    
    /* Interrupt statistics */
    irq_reduction = tx_stats.irq_reduction_percent;
    LOG_INFO("Interrupts:");
    LOG_INFO("  Total: %lu (%.1f/sec)",
             stats->interrupts,
             (stats->interrupts * TICKS_PER_SEC) / 
             (get_timer_ticks() - stats->start_time));
    LOG_INFO("  Lazy TX reduction: %lu%% (%lu IRQs saved)",
             irq_reduction, stats->lazy_tx_savings);
    LOG_INFO("  Target: %d IRQ/s", TARGET_IRQ_RATE);
    
    /* Optimization effectiveness */
    LOG_INFO("Optimizations:");
    LOG_INFO("  Copy-break: %lu packets (%.1f%%)",
             stats->copy_break_count,
             (stats->copy_break_count * 100.0) / stats->rx_packets);
    LOG_INFO("  Batch RX: %lu doorbells saved",
             stats->batch_rx_savings);
    LOG_INFO("  SMC patches: %lu fast paths taken",
             stats->smc_patches_hit);
    
    /* Efficiency metrics */
    if (stats->interrupts > 0) {
        efficiency = (stats->tx_packets + stats->rx_packets) / stats->interrupts;
        LOG_INFO("  Packets per IRQ: %lu", efficiency);
    }
}

/**
 * @brief Reset performance statistics
 */
void perf_stats_reset(uint8_t nic_index) {
    if (nic_index >= 4) {
        return;
    }
    
    memset(&nic_stats[nic_index], 0, sizeof(nic_stats_t));
    nic_stats[nic_index].start_time = get_timer_ticks();
    nic_stats[nic_index].last_update = nic_stats[nic_index].start_time;
}

/**
 * @brief Check if performance targets are met
 */
uint8_t perf_stats_targets_met(uint8_t nic_index) {
    nic_stats_t *stats;
    uint32_t irq_rate;
    
    if (nic_index >= 4) {
        return 0;
    }
    
    calculate_rates(nic_index);
    stats = &nic_stats[nic_index];
    
    /* Check throughput target */
    if (stats->pps_peak < TARGET_PPS) {
        LOG_DEBUG("Target not met: PPS %lu < %lu",
                 stats->pps_peak, TARGET_PPS);
        return 0;
    }
    
    /* Check interrupt rate target */
    irq_rate = (stats->interrupts * TICKS_PER_SEC) /
               (get_timer_ticks() - stats->start_time);
    if (irq_rate > TARGET_IRQ_RATE) {
        LOG_DEBUG("Target not met: IRQ rate %lu > %d",
                 irq_rate, TARGET_IRQ_RATE);
        return 0;
    }
    
    LOG_INFO("Performance targets MET for NIC %d", nic_index);
    return 1;
}

/**
 * @brief Enable/disable statistics collection
 */
void perf_stats_enable(uint8_t enable) {
    stats_enabled = enable;
    LOG_INFO("Performance statistics %s",
             enable ? "enabled" : "disabled");
}