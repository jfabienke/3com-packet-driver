/**
 * @file ring_statistics.c
 * @brief Ring buffer statistics and monitoring implementation
 * 
 * Sprint 0B.3: Enhanced Ring Buffer Management
 * 
 * This module provides comprehensive statistics collection and monitoring
 * for enhanced ring buffer operations:
 * - Real-time performance metrics
 * - Memory usage tracking
 * - Leak detection statistics
 * - Performance analysis and reporting
 * - Health monitoring and alerts
 */

#include "../include/enhanced_ring_context.h"
#include "../include/logging.h"
#include "../include/error_handling.h"

/* Global statistics tracking */
static struct {
    uint32_t stats_updates;
    uint32_t reports_generated;
    uint32_t alerts_triggered;
    uint32_t health_checks;
    uint32_t performance_samples;
    bool monitoring_enabled;
    uint32_t last_update_time;
} g_stats_global;

/* Performance thresholds for alerts */
#define RING_FULL_THRESHOLD         90    /* Ring usage % that triggers alert */
#define RING_EMPTY_THRESHOLD        10    /* Ring usage % that triggers alert */
#define LEAK_DETECTION_THRESHOLD    5     /* Max leaks before alert */
#define ALLOCATION_FAILURE_THRESHOLD 10  /* Max failures before alert */

/* Internal helper functions */
static void update_performance_metrics(enhanced_ring_context_t *ring);
static void update_memory_metrics(enhanced_ring_context_t *ring);
static void update_error_metrics(enhanced_ring_context_t *ring);
static void check_performance_alerts(enhanced_ring_context_t *ring);
static uint32_t calculate_ring_usage_percent(uint16_t used, uint16_t total);
static void generate_performance_report(const enhanced_ring_context_t *ring);

/**
 * @brief Update ring statistics
 * @param ring Ring context structure
 */
void ring_stats_update(enhanced_ring_context_t *ring) {
    if (!ring || !(ring->flags & RING_FLAG_STATS_ENABLED)) {
        return;
    }
    
    /* Update various metric categories */
    update_performance_metrics(ring);
    update_memory_metrics(ring);
    update_error_metrics(ring);
    
    /* Check for performance alerts */
    check_performance_alerts(ring);
    
    /* Update global statistics */
    g_stats_global.stats_updates++;
    g_stats_global.last_update_time = 0; /* Basic timestamp implementation */
    ring->last_stats_update = g_stats_global.last_update_time;
    
    /* Log debug information periodically */
    if (g_stats_global.stats_updates % 1000 == 0) {
        log_debug("Ring statistics updated %u times", g_stats_global.stats_updates);
    }
}

/**
 * @brief Get ring statistics
 * @param ring Ring context structure
 * @return Pointer to statistics structure
 */
const ring_stats_t *get_ring_stats(const enhanced_ring_context_t *ring) {
    return ring ? &ring->stats : NULL;
}

/**
 * @brief Reset ring statistics
 * @param ring Ring context structure
 */
void reset_ring_stats(enhanced_ring_context_t *ring) {
    if (!ring) {
        return;
    }
    
    log_info("Resetting ring buffer statistics");
    
    /* Preserve some critical counters */
    uint32_t total_allocations = ring->stats.total_allocations;
    uint32_t total_deallocations = ring->stats.total_deallocations;
    uint32_t current_allocated = ring->stats.current_allocated_buffers;
    uint32_t max_allocated = ring->stats.max_allocated_buffers;
    uint32_t leaks_detected = ring->stats.buffer_leaks_detected;
    
    /* Reset statistics */
    ring_stats_init(&ring->stats);
    
    /* Restore critical counters */
    ring->stats.total_allocations = total_allocations;
    ring->stats.total_deallocations = total_deallocations;
    ring->stats.current_allocated_buffers = current_allocated;
    ring->stats.max_allocated_buffers = max_allocated;
    ring->stats.buffer_leaks_detected = leaks_detected;
    
    log_info("Ring statistics reset completed");
}

/**
 * @brief Enable ring statistics monitoring
 * @param ring Ring context structure
 * @param enable true to enable, false to disable
 */
void ring_stats_enable_monitoring(enhanced_ring_context_t *ring, bool enable) {
    if (!ring) {
        return;
    }
    
    if (enable) {
        ring->flags |= RING_FLAG_STATS_ENABLED;
        g_stats_global.monitoring_enabled = true;
        log_info("Ring statistics monitoring enabled");
    } else {
        ring->flags &= ~RING_FLAG_STATS_ENABLED;
        g_stats_global.monitoring_enabled = false;
        log_info("Ring statistics monitoring disabled");
    }
}

/**
 * @brief Get ring health status
 * @param ring Ring context structure
 * @return Health status (0 = healthy, negative = issues)
 */
int ring_get_health_status(const enhanced_ring_context_t *ring) {
    int health_score = 0;
    uint32_t tx_usage, rx_usage;
    
    if (!ring) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    g_stats_global.health_checks++;
    
    /* Check ring usage levels */
    tx_usage = calculate_ring_usage_percent(ring->cur_tx - ring->dirty_tx, TX_RING_SIZE);
    rx_usage = calculate_ring_usage_percent(ring->cur_rx - ring->dirty_rx, RX_RING_SIZE);
    
    /* Deduct points for high ring usage */
    if (tx_usage > RING_FULL_THRESHOLD) {
        health_score -= 20;
        log_warning("TX ring usage high: %u%%", tx_usage);
    }
    
    if (rx_usage > RING_FULL_THRESHOLD) {
        health_score -= 20;
        log_warning("RX ring usage high: %u%%", rx_usage);
    }
    
    /* Deduct points for memory leaks */
    if (ring->stats.buffer_leaks_detected > LEAK_DETECTION_THRESHOLD) {
        health_score -= 30;
        log_warning("Memory leaks detected: %u", ring->stats.buffer_leaks_detected);
    }
    
    /* Deduct points for allocation failures */
    if (ring->stats.allocation_failures > ALLOCATION_FAILURE_THRESHOLD) {
        health_score -= 25;
        log_warning("High allocation failures: %u", ring->stats.allocation_failures);
    }
    
    /* Deduct points for errors */
    if (ring->stats.tx_errors > 0 || ring->stats.rx_errors > 0) {
        health_score -= 15;
        log_debug("Transmission errors detected: TX=%u, RX=%u", 
                  ring->stats.tx_errors, ring->stats.rx_errors);
    }
    
    /* Check for buffer pool exhaustion */
    if (ring->stats.buffer_pool_exhausted > 0) {
        health_score -= 25;
        log_warning("Buffer pool exhaustion events: %u", ring->stats.buffer_pool_exhausted);
    }
    
    return health_score;
}

/**
 * @brief Generate comprehensive ring statistics report
 * @param ring Ring context structure
 */
void ring_generate_stats_report(const enhanced_ring_context_t *ring) {
    if (!ring) {
        return;
    }
    
    log_info("=== ENHANCED RING BUFFER STATISTICS REPORT ===");
    
    /* Ring configuration */
    log_info("Ring Configuration:");
    log_info("  TX ring size: %u descriptors", TX_RING_SIZE);
    log_info("  RX ring size: %u descriptors", RX_RING_SIZE);
    log_info("  Buffer size: %u bytes", RING_BUFFER_SIZE);
    log_info("  Ring state: %d", ring->state);
    log_info("  Flags: 0x%08x", ring->flags);
    
    /* Current ring status */
    uint32_t tx_usage = calculate_ring_usage_percent(ring->cur_tx - ring->dirty_tx, TX_RING_SIZE);
    uint32_t rx_usage = calculate_ring_usage_percent(ring->cur_rx - ring->dirty_rx, RX_RING_SIZE);
    
    log_info("Current Ring Status:");
    log_info("  TX: cur=%u, dirty=%u, usage=%u%%", ring->cur_tx, ring->dirty_tx, tx_usage);
    log_info("  RX: cur=%u, dirty=%u, usage=%u%%", ring->cur_rx, ring->dirty_rx, rx_usage);
    log_info("  TX free slots: %u", get_tx_free_slots(ring));
    log_info("  RX filled slots: %u", get_rx_filled_slots(ring));
    
    /* Traffic statistics */
    log_info("Traffic Statistics:");
    log_info("  TX packets: %u (%u bytes)", ring->stats.tx_packets, ring->stats.tx_bytes);
    log_info("  RX packets: %u (%u bytes)", ring->stats.rx_packets, ring->stats.rx_bytes);
    log_info("  TX errors: %u", ring->stats.tx_errors);
    log_info("  RX errors: %u", ring->stats.rx_errors);
    
    /* Buffer management statistics */
    log_info("Buffer Management:");
    log_info("  Total allocations: %u", ring->stats.total_allocations);
    log_info("  Total deallocations: %u", ring->stats.total_deallocations);
    log_info("  Current allocated: %u", ring->stats.current_allocated_buffers);
    log_info("  Maximum allocated: %u", ring->stats.max_allocated_buffers);
    log_info("  Allocation failures: %u", ring->stats.allocation_failures);
    log_info("  Deallocation failures: %u", ring->stats.deallocation_failures);
    log_info("  Buffers recycled: %u", ring->stats.buffer_recycled);
    
    /* Memory leak detection */
    log_info("Memory Leak Detection:");
    log_info("  Leaks detected: %u", ring->stats.buffer_leaks_detected);
    log_info("  Leaked buffers: %u", ring->stats.leaked_buffers);
    
    if (ring->stats.buffer_leaks_detected == 0 && ring->stats.current_allocated_buffers == 0) {
        log_info("  ✓ ZERO MEMORY LEAKS - Perfect buffer management");
    } else {
        log_info("  ✗ MEMORY ISSUES - %u leaks, %u buffers not freed", 
                 ring->stats.buffer_leaks_detected, ring->stats.current_allocated_buffers);
    }
    
    /* Performance metrics */
    log_info("Performance Metrics:");
    log_info("  Ring full events: %u", ring->stats.ring_full_events);
    log_info("  Ring empty events: %u", ring->stats.ring_empty_events);
    log_info("  DMA stall events: %u", ring->stats.dma_stall_events);
    log_info("  Refill failures: %u", ring->stats.refill_failures);
    log_info("  Pool exhausted events: %u", ring->stats.buffer_pool_exhausted);
    log_info("  Peak TX usage: %u descriptors", ring->stats.peak_tx_usage);
    log_info("  Peak RX usage: %u descriptors", ring->stats.peak_rx_usage);
    
    /* Buffer pool status */
    log_info("Buffer Pool Status:");
    log_info("  TX pool: %u/%u allocated", ring->tx_pool_mgr.allocated_buffers, ring->tx_pool_mgr.pool_size);
    log_info("  RX pool: %u/%u allocated", ring->rx_pool_mgr.allocated_buffers, ring->rx_pool_mgr.pool_size);
    
    /* Health assessment */
    int health = ring_get_health_status(ring);
    log_info("Health Assessment:");
    if (health >= 0) {
        log_info("  ✓ HEALTHY - Ring buffer system operating normally");
    } else {
        log_info("  ✗ ISSUES DETECTED - Health score: %d", health);
    }
    
    g_stats_global.reports_generated++;
    log_info("=== END STATISTICS REPORT ===");
}

/**
 * @brief Record TX packet transmission
 * @param ring Ring context structure
 * @param bytes Number of bytes transmitted
 */
void ring_stats_record_tx_packet(enhanced_ring_context_t *ring, uint32_t bytes) {
    if (!ring || !(ring->flags & RING_FLAG_STATS_ENABLED)) {
        return;
    }
    
    ring->stats.tx_packets++;
    ring->stats.tx_bytes += bytes;
    
    /* Update peak usage */
    uint16_t tx_usage = ring->cur_tx - ring->dirty_tx;
    if (tx_usage > ring->stats.peak_tx_usage) {
        ring->stats.peak_tx_usage = tx_usage;
    }
}

/**
 * @brief Record RX packet reception
 * @param ring Ring context structure
 * @param bytes Number of bytes received
 */
void ring_stats_record_rx_packet(enhanced_ring_context_t *ring, uint32_t bytes) {
    if (!ring || !(ring->flags & RING_FLAG_STATS_ENABLED)) {
        return;
    }
    
    ring->stats.rx_packets++;
    ring->stats.rx_bytes += bytes;
    
    /* Update peak usage */
    uint16_t rx_usage = ring->cur_rx - ring->dirty_rx;
    if (rx_usage > ring->stats.peak_rx_usage) {
        ring->stats.peak_rx_usage = rx_usage;
    }
}

/**
 * @brief Record transmission error
 * @param ring Ring context structure
 * @param error_type Type of error
 */
void ring_stats_record_tx_error(enhanced_ring_context_t *ring, uint32_t error_type) {
    if (!ring || !(ring->flags & RING_FLAG_STATS_ENABLED)) {
        return;
    }
    
    ring->stats.tx_errors++;
    
    log_debug("TX error recorded: type=0x%08x, total=%u", error_type, ring->stats.tx_errors);
}

/**
 * @brief Record reception error
 * @param ring Ring context structure
 * @param error_type Type of error
 */
void ring_stats_record_rx_error(enhanced_ring_context_t *ring, uint32_t error_type) {
    if (!ring || !(ring->flags & RING_FLAG_STATS_ENABLED)) {
        return;
    }
    
    ring->stats.rx_errors++;
    
    log_debug("RX error recorded: type=0x%08x, total=%u", error_type, ring->stats.rx_errors);
}

/**
 * @brief Record buffer allocation
 * @param ring Ring context structure
 * @param success true if allocation succeeded, false if failed
 */
void ring_stats_record_allocation(enhanced_ring_context_t *ring, bool success) {
    if (!ring || !(ring->flags & RING_FLAG_STATS_ENABLED)) {
        return;
    }
    
    if (success) {
        ring->stats.total_allocations++;
        ring->stats.current_allocated_buffers++;
        
        if (ring->stats.current_allocated_buffers > ring->stats.max_allocated_buffers) {
            ring->stats.max_allocated_buffers = ring->stats.current_allocated_buffers;
        }
    } else {
        ring->stats.allocation_failures++;
    }
}

/**
 * @brief Record buffer deallocation
 * @param ring Ring context structure
 * @param success true if deallocation succeeded, false if failed
 */
void ring_stats_record_deallocation(enhanced_ring_context_t *ring, bool success) {
    if (!ring || !(ring->flags & RING_FLAG_STATS_ENABLED)) {
        return;
    }
    
    if (success) {
        ring->stats.total_deallocations++;
        if (ring->stats.current_allocated_buffers > 0) {
            ring->stats.current_allocated_buffers--;
        }
    } else {
        ring->stats.deallocation_failures++;
    }
}

/* Internal helper function implementations */

static void update_performance_metrics(enhanced_ring_context_t *ring) {
    uint16_t tx_used, rx_used;
    uint32_t tx_usage, rx_usage;
    
    /* Calculate current ring usage */
    tx_used = ring->cur_tx - ring->dirty_tx;
    rx_used = ring->cur_rx - ring->dirty_rx;
    
    tx_usage = calculate_ring_usage_percent(tx_used, TX_RING_SIZE);
    rx_usage = calculate_ring_usage_percent(rx_used, RX_RING_SIZE);
    
    /* Check for ring full/empty conditions */
    if (tx_usage >= RING_FULL_THRESHOLD) {
        ring->stats.ring_full_events++;
    }
    
    if (rx_usage <= RING_EMPTY_THRESHOLD) {
        ring->stats.ring_empty_events++;
    }
    
    /* Update peak usage */
    if (tx_used > ring->stats.peak_tx_usage) {
        ring->stats.peak_tx_usage = tx_used;
    }
    
    if (rx_used > ring->stats.peak_rx_usage) {
        ring->stats.peak_rx_usage = rx_used;
    }
    
    g_stats_global.performance_samples++;
}

static void update_memory_metrics(enhanced_ring_context_t *ring) {
    /* Check buffer pool status */
    if (ring->tx_pool_mgr.available_buffers == 0) {
        ring->stats.buffer_pool_exhausted++;
    }
    
    if (ring->rx_pool_mgr.available_buffers == 0) {
        ring->stats.buffer_pool_exhausted++;
    }
    
    /* Update pool manager statistics */
    if (ring->tx_pool_mgr.allocated_buffers > ring->tx_pool_mgr.max_allocation) {
        ring->tx_pool_mgr.max_allocation = ring->tx_pool_mgr.allocated_buffers;
    }
    
    if (ring->rx_pool_mgr.allocated_buffers > ring->rx_pool_mgr.max_allocation) {
        ring->rx_pool_mgr.max_allocation = ring->rx_pool_mgr.allocated_buffers;
    }
}

static void update_error_metrics(enhanced_ring_context_t *ring) {
    /* Perform leak detection check */
    if (ring->flags & RING_FLAG_LEAK_DETECTION) {
        int leaks = ring_leak_detection_check(ring);
        if (leaks > 0) {
            log_warning("Memory leaks detected during statistics update: %d", leaks);
        }
    }
}

static void check_performance_alerts(enhanced_ring_context_t *ring) {
    uint32_t tx_usage, rx_usage;
    bool alert_triggered = false;
    
    /* Calculate current usage */
    tx_usage = calculate_ring_usage_percent(ring->cur_tx - ring->dirty_tx, TX_RING_SIZE);
    rx_usage = calculate_ring_usage_percent(ring->cur_rx - ring->dirty_rx, RX_RING_SIZE);
    
    /* Check for high ring usage */
    if (tx_usage > RING_FULL_THRESHOLD) {
        log_warning("ALERT: TX ring usage critically high: %u%%", tx_usage);
        alert_triggered = true;
    }
    
    if (rx_usage > RING_FULL_THRESHOLD) {
        log_warning("ALERT: RX ring usage critically high: %u%%", rx_usage);
        alert_triggered = true;
    }
    
    /* Check for memory leaks */
    if (ring->stats.buffer_leaks_detected > LEAK_DETECTION_THRESHOLD) {
        log_warning("ALERT: Memory leaks detected: %u", ring->stats.buffer_leaks_detected);
        alert_triggered = true;
    }
    
    /* Check for allocation failures */
    if (ring->stats.allocation_failures > ALLOCATION_FAILURE_THRESHOLD) {
        log_warning("ALERT: High allocation failures: %u", ring->stats.allocation_failures);
        alert_triggered = true;
    }
    
    if (alert_triggered) {
        g_stats_global.alerts_triggered++;
    }
}

static uint32_t calculate_ring_usage_percent(uint16_t used, uint16_t total) {
    if (total == 0) {
        return 0;
    }
    
    return (used * 100) / total;
}

/**
 * @brief Get global statistics information
 * @return Pointer to global statistics
 */
const void *ring_get_global_stats(void) {
    return &g_stats_global;
}

/**
 * @brief Initialize statistics system
 * @return 0 on success, negative on error
 */
int ring_stats_system_init(void) {
    memory_zero(&g_stats_global, sizeof(g_stats_global));
    g_stats_global.monitoring_enabled = true;
    
    log_info("Ring statistics system initialized");
    return 0;
}

/**
 * @brief Cleanup statistics system
 */
void ring_stats_system_cleanup(void) {
    log_info("Ring statistics system final report:");
    log_info("  Total updates: %u", g_stats_global.stats_updates);
    log_info("  Reports generated: %u", g_stats_global.reports_generated);
    log_info("  Alerts triggered: %u", g_stats_global.alerts_triggered);
    log_info("  Health checks: %u", g_stats_global.health_checks);
    log_info("  Performance samples: %u", g_stats_global.performance_samples);
    
    memory_zero(&g_stats_global, sizeof(g_stats_global));
    log_info("Ring statistics system cleanup completed");
}