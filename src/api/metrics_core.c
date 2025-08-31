/**
 * @file metrics_core.c
 * @brief Core Metrics System Implementation
 * 
 * TSR-safe metrics collection with atomic counters and
 * deferred processing for expensive operations.
 */

#include "metrics_core.h"
#include "../loader/timer_services.h"
#include "../include/logging.h"
#include <string.h>
#include <dos.h>

/* Global metrics instance */
metrics_system_t g_metrics = {0};

/* BIOS tick counter (18.2Hz) */
#define BIOS_TICKS_PTR ((volatile uint32_t far *)0x0040006CL)

/**
 * @brief Generic interrupt disable/restore for non-Watcom compilers
 */
#ifndef __WATCOMC__
void irq_off_save(uint16_t *flags) {
    *flags = 0;
    _asm {
        pushf
        pop ax
        mov bx, flags
        mov [bx], ax
        cli
    }
}

void irq_restore(uint16_t flags) {
    _asm {
        mov ax, flags
        push ax
        popf
    }
}
#endif

/**
 * @brief Get high-resolution timestamp using PIT + BIOS tick
 * 
 * Combines 8253 PIT channel 0 with BIOS tick counter for ~1.193MHz resolution
 */
uint32_t metrics_time_1193khz(void)
{
    uint16_t flags;
    uint32_t ticks;
    uint16_t pit_count;
    uint8_t lo, hi;
    
    irq_off_save(&flags);
    
    /* Latch PIT channel 0 counter */
    outp(0x43, 0x00);  /* Counter latch command for channel 0 */
    
    /* Read BIOS tick counter (32-bit at 0040:006C) */
    ticks = *BIOS_TICKS_PTR;
    
    /* Read PIT counter (counts down from ~65536) */
    lo = inp(0x40);
    hi = inp(0x40);
    pit_count = ((uint16_t)hi << 8) | lo;
    
    irq_restore(flags);
    
    /* Combine: ticks * 65536 + (65535 - pit_count) */
    /* This gives ~1.193MHz resolution timestamp */
    return (ticks << 16) | (0xFFFF - pit_count);
}

/**
 * @brief Initialize metrics system
 */
int metrics_init(void)
{
    if (g_metrics.initialized) {
        return 0;
    }
    
    /* Clear all metrics */
    memset(&g_metrics, 0, sizeof(metrics_system_t));
    
    /* Initialize TX completion ring */
    g_metrics.tx_ring.head = 0;
    g_metrics.tx_ring.tail = 0;
    
    /* Set collection interval (1 second) */
    g_metrics.collection_interval = 1000;
    g_metrics.last_collection_time = get_millisecond_timestamp();
    
    g_metrics.initialized = 1;
    
    LOG_INFO("Metrics system initialized");
    return 0;
}

/**
 * @brief Cleanup metrics system
 */
void metrics_cleanup(void)
{
    if (!g_metrics.initialized) {
        return;
    }
    
    /* Process any remaining TX completions */
    metrics_process_tx_completions();
    
    /* Log final statistics */
    LOG_INFO("Metrics cleanup - Handles: %lu, Memory: %lu, IRQs: %lu",
             metrics_get_handle_count(),
             metrics_get_memory_usage(),
             metrics_get_interrupt_count());
    
    g_metrics.initialized = 0;
}

/**
 * @brief Handle opened (call from handle manager)
 */
void metrics_handle_opened(uint8_t module_id)
{
    uint16_t new_count;
    
    if (!g_metrics.initialized || module_id >= MAX_MODULES) {
        return;
    }
    
    /* Update global counters */
    isr_inc_u32(&g_metrics.handle_global.total_open_lo, 
                &g_metrics.handle_global.total_open_hi);
    
    /* Increment live count (atomic 16-bit) */
    _asm {
        inc word ptr [g_metrics.handle_global.live_count]
    }
    
    /* Update peak if necessary */
    new_count = g_metrics.handle_global.live_count;
    if (new_count > g_metrics.handle_global.peak_count) {
        g_metrics.handle_global.peak_count = new_count;
    }
    
    /* Update per-module counters */
    isr_inc_u32(&g_metrics.handle_modules[module_id].open_lo,
                &g_metrics.handle_modules[module_id].open_hi);
    
    _asm {
        mov si, module_id
        shl si, 1  ; multiply by sizeof(handle_module_counters_t) manually
        shl si, 1
        shl si, 1
        add si, offset g_metrics.handle_modules
        inc word ptr [si].handle_module_counters_t.live_count
    }
    
    new_count = g_metrics.handle_modules[module_id].live_count;
    if (new_count > g_metrics.handle_modules[module_id].peak_count) {
        g_metrics.handle_modules[module_id].peak_count = new_count;
    }
}

/**
 * @brief Handle closed (call from handle manager)
 */
void metrics_handle_closed(uint8_t module_id)
{
    if (!g_metrics.initialized || module_id >= MAX_MODULES) {
        return;
    }
    
    /* Update global counters */
    isr_inc_u32(&g_metrics.handle_global.total_closed_lo,
                &g_metrics.handle_global.total_closed_hi);
    
    /* Decrement live count (atomic 16-bit) */
    _asm {
        dec word ptr [g_metrics.handle_global.live_count]
    }
    
    /* Update per-module counters */
    isr_inc_u32(&g_metrics.handle_modules[module_id].close_lo,
                &g_metrics.handle_modules[module_id].close_hi);
    
    _asm {
        mov si, module_id
        shl si, 1  ; multiply by sizeof(handle_module_counters_t)
        shl si, 1
        shl si, 1
        add si, offset g_metrics.handle_modules
        dec word ptr [si].handle_module_counters_t.live_count
    }
}

/**
 * @brief Memory allocated (call from memory manager)
 */
void metrics_memory_allocated(uint16_t size, uint8_t module_id)
{
    uint32_t current, peak;
    uint16_t flags;
    
    if (!g_metrics.initialized || module_id >= MAX_MODULES) {
        return;
    }
    
    /* Update global counters */
    isr_add_u32(&g_metrics.mem_global.cur_lo, &g_metrics.mem_global.cur_hi, size, 0);
    isr_inc_u32(&g_metrics.mem_global.total_allocs_lo, &g_metrics.mem_global.total_allocs_hi);
    
    /* Update peak (requires atomic read) */
    current = read_u32_atomic(&g_metrics.mem_global.cur_lo, &g_metrics.mem_global.cur_hi);
    peak = read_u32_atomic(&g_metrics.mem_global.peak_lo, &g_metrics.mem_global.peak_hi);
    
    if (current > peak) {
        irq_off_save(&flags);
        /* Re-read to avoid race condition */
        current = read_u32_atomic(&g_metrics.mem_global.cur_lo, &g_metrics.mem_global.cur_hi);
        g_metrics.mem_global.peak_lo = (uint16_t)(current & 0xFFFF);
        g_metrics.mem_global.peak_hi = (uint16_t)(current >> 16);
        irq_restore(flags);
    }
    
    /* Update per-module counters */
    isr_add_u32(&g_metrics.mem_modules[module_id].cur_lo,
                &g_metrics.mem_modules[module_id].cur_hi, size, 0);
    
    /* Update per-module peak */
    current = read_u32_atomic(&g_metrics.mem_modules[module_id].cur_lo,
                              &g_metrics.mem_modules[module_id].cur_hi);
    peak = read_u32_atomic(&g_metrics.mem_modules[module_id].peak_lo,
                           &g_metrics.mem_modules[module_id].peak_hi);
    
    if (current > peak) {
        irq_off_save(&flags);
        current = read_u32_atomic(&g_metrics.mem_modules[module_id].cur_lo,
                                  &g_metrics.mem_modules[module_id].cur_hi);
        g_metrics.mem_modules[module_id].peak_lo = (uint16_t)(current & 0xFFFF);
        g_metrics.mem_modules[module_id].peak_hi = (uint16_t)(current >> 16);
        irq_restore(flags);
    }
}

/**
 * @brief Memory freed (call from memory manager)
 */
void metrics_memory_freed(uint16_t size, uint8_t module_id)
{
    if (!g_metrics.initialized || module_id >= MAX_MODULES) {
        return;
    }
    
    /* Update global counters - subtract using two's complement */
    isr_add_u32(&g_metrics.mem_global.cur_lo, &g_metrics.mem_global.cur_hi,
                (uint16_t)(-size), (uint16_t)~0);
    isr_inc_u32(&g_metrics.mem_global.total_frees_lo, &g_metrics.mem_global.total_frees_hi);
    
    /* Update per-module counters */
    isr_add_u32(&g_metrics.mem_modules[module_id].cur_lo,
                &g_metrics.mem_modules[module_id].cur_hi,
                (uint16_t)(-size), (uint16_t)~0);
}

/**
 * @brief TX started (foreground - record timestamp)
 */
void metrics_tx_start(tx_desc_metrics_t *desc, uint8_t module_id)
{
    if (!desc || !g_metrics.initialized) {
        return;
    }
    
    desc->submit_time_1193k = metrics_time_1193khz();
    desc->module_id = module_id;
    desc->flags = 0;
}

/**
 * @brief TX completed (ISR - enqueue for deferred processing)
 */
void metrics_isr_tx_complete(tx_desc_metrics_t *desc)
{
    uint16_t next;
    
    if (!desc || !g_metrics.initialized) {
        return;
    }
    
    /* Try to enqueue to completion ring (never block ISR) */
    next = (g_metrics.tx_ring.head + 1) & (TX_COMPLETE_RING_SIZE - 1);
    
    if (next != g_metrics.tx_ring.tail) {
        g_metrics.tx_ring.tx_ring[g_metrics.tx_ring.head] = desc;
        g_metrics.tx_ring.head = next;
    }
    /* If ring is full, drop the metric - never block ISR */
}

/**
 * @brief Process TX completions (foreground - call periodically)
 */
int metrics_process_tx_completions(void)
{
    int processed = 0;
    uint32_t now, dt;
    uint8_t module_id;
    uint32_t ewma, min_lat, max_lat;
    uint16_t flags;
    
    if (!g_metrics.initialized) {
        return 0;
    }
    
    now = metrics_time_1193khz();
    
    /* Process up to 8 completions per call to avoid hogging CPU */
    while (g_metrics.tx_ring.tail != g_metrics.tx_ring.head && processed < 8) {
        tx_desc_metrics_t *desc = (tx_desc_metrics_t *)g_metrics.tx_ring.tx_ring[g_metrics.tx_ring.tail];
        
        g_metrics.tx_ring.tail = (g_metrics.tx_ring.tail + 1) & (TX_COMPLETE_RING_SIZE - 1);
        
        if (!desc) {
            processed++;
            continue;
        }
        
        module_id = desc->module_id;
        if (module_id >= MAX_MODULES) {
            processed++;
            continue;
        }
        
        /* Calculate latency in PIT ticks (~1.193MHz) */
        dt = now - desc->submit_time_1193k;
        
        /* Convert to microseconds: dt * 1000000 / 1193182 ≈ dt * 838 / 1000 */
        uint32_t latency_us = (dt * 838UL) / 1000UL;
        
        /* Update module latency statistics */
        module_perf_stats_t *stats = &g_metrics.perf_modules[module_id];
        
        /* Read current EWMA (Q16.16 fixed point) */
        ewma = read_u32_atomic(&stats->tx_lat_ewma_lo, &stats->tx_lat_ewma_hi);
        
        /* EWMA update: ewma = ewma + (latency - ewma)/16 */
        /* For simplicity, treat as integer microseconds */
        if (ewma == 0) {
            ewma = latency_us << 16;  /* Initialize EWMA */
        } else {
            uint32_t ewma_int = ewma >> 16;
            int32_t diff = (int32_t)latency_us - (int32_t)ewma_int;
            ewma += (diff << 12);  /* alpha = 1/16, so diff/16 = diff >> 4, << 16 = << 12 */
        }
        
        /* Update min/max latencies */
        min_lat = read_u32_atomic(&stats->tx_lat_min_lo, &stats->tx_lat_min_hi);
        max_lat = read_u32_atomic(&stats->tx_lat_max_lo, &stats->tx_lat_max_hi);
        
        if (min_lat == 0 || latency_us < min_lat) {
            min_lat = latency_us;
        }
        if (latency_us > max_lat) {
            max_lat = latency_us;
        }
        
        /* Atomic update of statistics */
        irq_off_save(&flags);
        stats->tx_lat_ewma_lo = (uint16_t)(ewma & 0xFFFF);
        stats->tx_lat_ewma_hi = (uint16_t)(ewma >> 16);
        stats->tx_lat_min_lo = (uint16_t)(min_lat & 0xFFFF);
        stats->tx_lat_min_hi = (uint16_t)(min_lat >> 16);
        stats->tx_lat_max_lo = (uint16_t)(max_lat & 0xFFFF);
        stats->tx_lat_max_hi = (uint16_t)(max_lat >> 16);
        irq_restore(flags);
        
        processed++;
    }
    
    return processed;
}

/**
 * @brief Get current handle count
 */
uint32_t metrics_get_handle_count(void)
{
    if (!g_metrics.initialized) {
        return 0;
    }
    
    return (uint32_t)g_metrics.handle_global.live_count;
}

/**
 * @brief Get current memory usage
 */
uint32_t metrics_get_memory_usage(void)
{
    if (!g_metrics.initialized) {
        return 0;
    }
    
    return read_u32_atomic(&g_metrics.mem_global.cur_lo, &g_metrics.mem_global.cur_hi);
}

/**
 * @brief Get interrupt count
 */
uint32_t metrics_get_interrupt_count(void)
{
    if (!g_metrics.initialized) {
        return 0;
    }
    
    return read_u32_atomic(&g_metrics.irq_global.irq_lo, &g_metrics.irq_global.irq_hi);
}

/**
 * @brief Get per-module handle count
 */
uint32_t metrics_get_module_handles(uint8_t module_id)
{
    if (!g_metrics.initialized || module_id >= MAX_MODULES) {
        return 0;
    }
    
    return (uint32_t)g_metrics.handle_modules[module_id].live_count;
}

/**
 * @brief Get module performance statistics
 */
void metrics_get_module_perf(uint8_t module_id, uint32_t *rx_packets,
                           uint32_t *tx_packets, uint32_t *errors,
                           uint32_t *avg_latency_us, uint32_t *min_latency_us,
                           uint32_t *max_latency_us)
{
    module_perf_stats_t *stats;
    uint32_t ewma;
    
    if (!g_metrics.initialized || module_id >= MAX_MODULES) {
        if (rx_packets) *rx_packets = 0;
        if (tx_packets) *tx_packets = 0;
        if (errors) *errors = 0;
        if (avg_latency_us) *avg_latency_us = 0;
        if (min_latency_us) *min_latency_us = 0;
        if (max_latency_us) *max_latency_us = 0;
        return;
    }
    
    stats = &g_metrics.perf_modules[module_id];
    
    if (rx_packets) {
        *rx_packets = read_u32_atomic(&stats->rx_ok_lo, &stats->rx_ok_hi);
    }
    if (tx_packets) {
        *tx_packets = read_u32_atomic(&stats->tx_ok_lo, &stats->tx_ok_hi);
    }
    if (errors) {
        *errors = read_u32_atomic(&stats->err_lo, &stats->err_hi);
    }
    
    if (avg_latency_us) {
        ewma = read_u32_atomic(&stats->tx_lat_ewma_lo, &stats->tx_lat_ewma_hi);
        *avg_latency_us = ewma >> 16;  /* Convert Q16.16 to integer */
    }
    if (min_latency_us) {
        *min_latency_us = read_u32_atomic(&stats->tx_lat_min_lo, &stats->tx_lat_min_hi);
    }
    if (max_latency_us) {
        *max_latency_us = read_u32_atomic(&stats->tx_lat_max_lo, &stats->tx_lat_max_hi);
    }
}