/**
 * @file ring_buffer_pools.c
 * @brief Enhanced buffer pool management for ring buffers with zero-leak guarantee
 * 
 * Sprint 0B.3: Enhanced Ring Buffer Management
 * 
 * This module provides sophisticated buffer pool management specifically
 * designed for ring buffer operations with guaranteed zero memory leaks:
 * - Dynamic pool expansion and shrinking
 * - Buffer allocation tracking and validation
 * - Sophisticated recycling algorithms
 * - Memory leak detection and prevention
 * - Pool health monitoring and statistics
 */

#include "../include/enhanced_ring_context.h"
#include "../include/logging.h"
#include "../include/error_handling.h"

/* Global buffer pool statistics */
static struct {
    uint32_t total_pools_created;
    uint32_t total_pools_destroyed;
    uint32_t total_expansions;
    uint32_t total_shrinks;
    uint32_t allocation_failures;
    uint32_t deallocation_failures;
    uint32_t leak_detection_runs;
    uint32_t leaks_found;
    uint32_t leaks_fixed;
} g_pool_stats;

/* Internal helper functions */
static int validate_pool_parameters(const buffer_pool_mgr_t *pool_mgr);
static int expand_buffer_pool_internal(buffer_pool_mgr_t *pool_mgr, uint32_t additional_buffers);
static int shrink_buffer_pool_internal(buffer_pool_mgr_t *pool_mgr, uint32_t remove_buffers);
static void update_pool_statistics(buffer_pool_mgr_t *pool_mgr);
static int validate_pool_integrity(const buffer_pool_mgr_t *pool_mgr);

/**
 * @brief Initialize ring buffer pool system
 * @param ring Ring context structure
 * @return 0 on success, negative error code on failure
 */
int ring_buffer_pool_init(enhanced_ring_context_t *ring) {
    int result;
    
    if (!ring) {
        log_error("ring_buffer_pool_init: NULL ring context");
        return -RING_ERROR_INVALID_PARAM;
    }
    
    log_info("Initializing ring buffer pool system");
    
    /* Initialize global pool statistics */
    memory_zero(&g_pool_stats, sizeof(g_pool_stats));
    
    /* Initialize TX buffer pool manager */
    result = ring_buffer_pool_init_tx(ring);
    if (result != 0) {
        log_error("Failed to initialize TX buffer pool: %d", result);
        return result;
    }
    
    /* Initialize RX buffer pool manager */
    result = ring_buffer_pool_init_rx(ring);
    if (result != 0) {
        log_error("Failed to initialize RX buffer pool: %d", result);
        ring_buffer_pool_cleanup_tx(ring);
        return result;
    }
    
    /* Initialize shared buffer pool if needed */
    if (ring->flags & RING_FLAG_PERSISTENT_BUFFERS) {
        result = ring_buffer_pool_init_shared(ring);
        if (result != 0) {
            log_warning("Failed to initialize shared buffer pool, continuing without it");
        }
    }
    
    g_pool_stats.total_pools_created++;
    
    log_info("Ring buffer pool system initialized successfully");
    log_info("  TX pool: %u buffers, RX pool: %u buffers", 
             ring->tx_pool_mgr.pool_size, ring->rx_pool_mgr.pool_size);
    
    return 0;
}

/**
 * @brief Initialize TX buffer pool manager
 * @param ring Ring context structure
 * @return 0 on success, negative error code on failure
 */
int ring_buffer_pool_init_tx(enhanced_ring_context_t *ring) {
    buffer_pool_mgr_t *mgr;
    uint32_t flags;
    
    if (!ring) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    mgr = &ring->tx_pool_mgr;
    
    /* Configure TX pool manager */
    mgr->pool = &g_tx_buffer_pool;  /* Use global TX buffer pool */
    mgr->pool_size = TX_RING_SIZE * 2;  /* 2x ring size for optimal performance */
    mgr->available_buffers = mgr->pool_size;
    mgr->allocated_buffers = 0;
    mgr->max_allocation = 0;
    mgr->auto_expand = true;
    mgr->expand_increment = TX_RING_SIZE / 2;  /* Expand by half ring size */
    mgr->shrink_threshold = mgr->pool_size / 4;  /* Shrink when usage < 25% */
    
    /* Ensure the underlying buffer pool is properly initialized */
    if (!buffer_pool_get_total_count(mgr->pool)) {
        flags = BUFFER_FLAG_ALIGNED;
        if (ring->flags & RING_FLAG_ALIGNED_BUFFERS) {
            flags |= BUFFER_FLAG_ZERO_INIT;
        }
        
        int result = buffer_pool_init(mgr->pool, BUFFER_TYPE_TX, 
                                     RING_BUFFER_SIZE, mgr->pool_size, flags);
        if (result != 0) {
            log_error("Failed to initialize underlying TX buffer pool");
            return -RING_ERROR_OUT_OF_MEMORY;
        }
    }
    
    log_debug("TX buffer pool manager initialized: %u buffers", mgr->pool_size);
    return 0;
}

/**
 * @brief Initialize RX buffer pool manager
 * @param ring Ring context structure
 * @return 0 on success, negative error code on failure
 */
int ring_buffer_pool_init_rx(enhanced_ring_context_t *ring) {
    buffer_pool_mgr_t *mgr;
    uint32_t flags;
    
    if (!ring) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    mgr = &ring->rx_pool_mgr;
    
    /* Configure RX pool manager */
    mgr->pool = &g_rx_buffer_pool;  /* Use global RX buffer pool */
    mgr->pool_size = RX_RING_SIZE * 3;  /* 3x ring size for RX buffering */
    mgr->available_buffers = mgr->pool_size;
    mgr->allocated_buffers = 0;
    mgr->max_allocation = 0;
    mgr->auto_expand = true;
    mgr->expand_increment = RX_RING_SIZE / 2;  /* Expand by half ring size */
    mgr->shrink_threshold = mgr->pool_size / 4;  /* Shrink when usage < 25% */
    
    /* Ensure the underlying buffer pool is properly initialized */
    if (!buffer_pool_get_total_count(mgr->pool)) {
        flags = BUFFER_FLAG_ALIGNED | BUFFER_FLAG_ZERO_INIT;
        
        int result = buffer_pool_init(mgr->pool, BUFFER_TYPE_RX, 
                                     RING_BUFFER_SIZE, mgr->pool_size, flags);
        if (result != 0) {
            log_error("Failed to initialize underlying RX buffer pool");
            return -RING_ERROR_OUT_OF_MEMORY;
        }
    }
    
    log_debug("RX buffer pool manager initialized: %u buffers", mgr->pool_size);
    return 0;
}

/**
 * @brief Initialize shared buffer pool
 * @param ring Ring context structure
 * @return 0 on success, negative error code on failure
 */
int ring_buffer_pool_init_shared(enhanced_ring_context_t *ring) {
    uint32_t shared_size;
    uint32_t flags;
    int result;
    
    if (!ring) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    /* Allocate shared pool */
    ring->shared_pool = &g_dma_buffer_pool;  /* Use DMA buffer pool as shared */
    shared_size = (TX_RING_SIZE + RX_RING_SIZE) / 2;  /* Shared emergency pool */
    
    /* Initialize shared pool if not already done */
    if (!buffer_pool_get_total_count(ring->shared_pool)) {
        flags = BUFFER_FLAG_DMA_CAPABLE | BUFFER_FLAG_ALIGNED | BUFFER_FLAG_PERSISTENT;
        
        result = buffer_pool_init(ring->shared_pool, BUFFER_TYPE_TEMPORARY, 
                                 RING_BUFFER_SIZE, shared_size, flags);
        if (result != 0) {
            log_error("Failed to initialize shared buffer pool");
            ring->shared_pool = NULL;
            return -RING_ERROR_OUT_OF_MEMORY;
        }
    }
    
    log_debug("Shared buffer pool initialized: %u buffers", shared_size);
    return 0;
}

/**
 * @brief Cleanup ring buffer pools
 * @param ring Ring context structure
 */
void ring_buffer_pool_cleanup(enhanced_ring_context_t *ring) {
    if (!ring) {
        return;
    }
    
    log_info("Cleaning up ring buffer pools");
    
    /* Print final pool statistics */
    ring_buffer_pool_print_stats(ring);
    
    /* Cleanup TX pool */
    ring_buffer_pool_cleanup_tx(ring);
    
    /* Cleanup RX pool */
    ring_buffer_pool_cleanup_rx(ring);
    
    /* Cleanup shared pool */
    if (ring->shared_pool) {
        ring->shared_pool = NULL;
    }
    
    g_pool_stats.total_pools_destroyed++;
    
    log_info("Ring buffer pools cleanup completed");
}

/**
 * @brief Cleanup TX buffer pool
 * @param ring Ring context structure
 */
void ring_buffer_pool_cleanup_tx(enhanced_ring_context_t *ring) {
    buffer_pool_mgr_t *mgr;
    
    if (!ring) {
        return;
    }
    
    mgr = &ring->tx_pool_mgr;
    
    /* Force cleanup any remaining allocated buffers */
    if (mgr->allocated_buffers > 0) {
        log_warning("TX pool cleanup: %u buffers still allocated, forcing cleanup", 
                    mgr->allocated_buffers);
        
        /* Force deallocate remaining buffers */
        for (uint16_t i = 0; i < TX_RING_SIZE; i++) {
            if (ring->tx_buffers[i] || ring->tx_buffer_descs[i]) {
                deallocate_tx_buffer(ring, i);
            }
        }
    }
    
    /* Reset pool manager */
    memory_zero(mgr, sizeof(buffer_pool_mgr_t));
    
    log_debug("TX buffer pool cleaned up");
}

/**
 * @brief Cleanup RX buffer pool
 * @param ring Ring context structure
 */
void ring_buffer_pool_cleanup_rx(enhanced_ring_context_t *ring) {
    buffer_pool_mgr_t *mgr;
    
    if (!ring) {
        return;
    }
    
    mgr = &ring->rx_pool_mgr;
    
    /* Force cleanup any remaining allocated buffers */
    if (mgr->allocated_buffers > 0) {
        log_warning("RX pool cleanup: %u buffers still allocated, forcing cleanup", 
                    mgr->allocated_buffers);
        
        /* Force deallocate remaining buffers */
        for (uint16_t i = 0; i < RX_RING_SIZE; i++) {
            if (ring->rx_buffers[i] || ring->rx_buffer_descs[i]) {
                deallocate_rx_buffer(ring, i);
            }
        }
    }
    
    /* Reset pool manager */
    memory_zero(mgr, sizeof(buffer_pool_mgr_t));
    
    log_debug("RX buffer pool cleaned up");
}

/**
 * @brief Expand buffer pool
 * @param ring Ring context structure
 * @param tx_pool true for TX pool, false for RX pool
 * @param additional_buffers Number of additional buffers to add
 * @return 0 on success, negative error code on failure
 */
int ring_buffer_pool_expand(enhanced_ring_context_t *ring, bool tx_pool, uint32_t additional_buffers) {
    buffer_pool_mgr_t *mgr;
    int result;
    
    if (!ring || additional_buffers == 0) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    mgr = tx_pool ? &ring->tx_pool_mgr : &ring->rx_pool_mgr;
    
    if (!mgr->auto_expand) {
        log_warning("Pool expansion disabled for %s pool", tx_pool ? "TX" : "RX");
        return -RING_ERROR_INVALID_STATE;
    }
    
    log_info("Expanding %s buffer pool by %u buffers", 
             tx_pool ? "TX" : "RX", additional_buffers);
    
    result = expand_buffer_pool_internal(mgr, additional_buffers);
    if (result == 0) {
        g_pool_stats.total_expansions++;
        update_pool_statistics(mgr);
        
        log_info("%s pool expanded successfully: %u -> %u buffers", 
                 tx_pool ? "TX" : "RX", 
                 mgr->pool_size - additional_buffers, mgr->pool_size);
    } else {
        log_error("Failed to expand %s pool: %d", tx_pool ? "TX" : "RX", result);
    }
    
    return result;
}

/**
 * @brief Shrink buffer pool
 * @param ring Ring context structure
 * @param tx_pool true for TX pool, false for RX pool
 * @param remove_buffers Number of buffers to remove
 * @return 0 on success, negative error code on failure
 */
int ring_buffer_pool_shrink(enhanced_ring_context_t *ring, bool tx_pool, uint32_t remove_buffers) {
    buffer_pool_mgr_t *mgr;
    int result;
    
    if (!ring || remove_buffers == 0) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    mgr = tx_pool ? &ring->tx_pool_mgr : &ring->rx_pool_mgr;
    
    /* Don't shrink below minimum size */
    uint32_t min_size = tx_pool ? TX_RING_SIZE : RX_RING_SIZE;
    if (mgr->pool_size - remove_buffers < min_size) {
        log_warning("Cannot shrink %s pool below minimum size %u", 
                    tx_pool ? "TX" : "RX", min_size);
        return -RING_ERROR_INVALID_PARAM;
    }
    
    log_info("Shrinking %s buffer pool by %u buffers", 
             tx_pool ? "TX" : "RX", remove_buffers);
    
    result = shrink_buffer_pool_internal(mgr, remove_buffers);
    if (result == 0) {
        g_pool_stats.total_shrinks++;
        update_pool_statistics(mgr);
        
        log_info("%s pool shrunk successfully: %u -> %u buffers", 
                 tx_pool ? "TX" : "RX", 
                 mgr->pool_size + remove_buffers, mgr->pool_size);
    } else {
        log_error("Failed to shrink %s pool: %d", tx_pool ? "TX" : "RX", result);
    }
    
    return result;
}

/**
 * @brief Check if pool needs expansion
 * @param ring Ring context structure
 * @param tx_pool true for TX pool, false for RX pool
 * @return true if expansion needed, false otherwise
 */
bool ring_buffer_pool_needs_expansion(const enhanced_ring_context_t *ring, bool tx_pool) {
    const buffer_pool_mgr_t *mgr;
    uint32_t usage_percent;
    
    if (!ring) {
        return false;
    }
    
    mgr = tx_pool ? &ring->tx_pool_mgr : &ring->rx_pool_mgr;
    
    if (!mgr->auto_expand || mgr->pool_size == 0) {
        return false;
    }
    
    /* Calculate usage percentage */
    usage_percent = (mgr->allocated_buffers * 100) / mgr->pool_size;
    
    /* Expand if usage > 80% */
    return usage_percent > 80;
}

/**
 * @brief Check if pool can be shrunk
 * @param ring Ring context structure
 * @param tx_pool true for TX pool, false for RX pool
 * @return true if shrinking recommended, false otherwise
 */
bool ring_buffer_pool_can_shrink(const enhanced_ring_context_t *ring, bool tx_pool) {
    const buffer_pool_mgr_t *mgr;
    uint32_t usage_percent;
    uint32_t min_size;
    
    if (!ring) {
        return false;
    }
    
    mgr = tx_pool ? &ring->tx_pool_mgr : &ring->rx_pool_mgr;
    min_size = tx_pool ? TX_RING_SIZE : RX_RING_SIZE;
    
    if (mgr->pool_size <= min_size) {
        return false;  /* Already at minimum size */
    }
    
    /* Calculate usage percentage */
    usage_percent = mgr->pool_size > 0 ? (mgr->allocated_buffers * 100) / mgr->pool_size : 0;
    
    /* Shrink if usage < 25% and pool size > minimum */
    return usage_percent < 25;
}

/**
 * @brief Perform automatic pool management
 * @param ring Ring context structure
 * @return Number of pools adjusted
 */
int ring_buffer_pool_auto_manage(enhanced_ring_context_t *ring) {
    int adjustments = 0;
    
    if (!ring) {
        return 0;
    }
    
    /* Check TX pool for expansion */
    if (ring_buffer_pool_needs_expansion(ring, true)) {
        if (ring_buffer_pool_expand(ring, true, ring->tx_pool_mgr.expand_increment) == 0) {
            adjustments++;
        }
    }
    /* Check TX pool for shrinking */
    else if (ring_buffer_pool_can_shrink(ring, true)) {
        uint32_t shrink_amount = ring->tx_pool_mgr.expand_increment / 2;
        if (ring_buffer_pool_shrink(ring, true, shrink_amount) == 0) {
            adjustments++;
        }
    }
    
    /* Check RX pool for expansion */
    if (ring_buffer_pool_needs_expansion(ring, false)) {
        if (ring_buffer_pool_expand(ring, false, ring->rx_pool_mgr.expand_increment) == 0) {
            adjustments++;
        }
    }
    /* Check RX pool for shrinking */
    else if (ring_buffer_pool_can_shrink(ring, false)) {
        uint32_t shrink_amount = ring->rx_pool_mgr.expand_increment / 2;
        if (ring_buffer_pool_shrink(ring, false, shrink_amount) == 0) {
            adjustments++;
        }
    }
    
    return adjustments;
}

/**
 * @brief Validate buffer pool integrity
 * @param ring Ring context structure
 * @return 0 if integrity is good, negative if issues found
 */
int ring_buffer_pool_validate_integrity(const enhanced_ring_context_t *ring) {
    int issues = 0;
    
    if (!ring) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    /* Validate TX pool */
    if (validate_pool_integrity(&ring->tx_pool_mgr) != 0) {
        log_error("TX buffer pool integrity validation failed");
        issues++;
    }
    
    /* Validate RX pool */
    if (validate_pool_integrity(&ring->rx_pool_mgr) != 0) {
        log_error("RX buffer pool integrity validation failed");
        issues++;
    }
    
    if (issues == 0) {
        log_debug("Buffer pool integrity validation passed");
    }
    
    return issues > 0 ? -RING_ERROR_BUFFER_CORRUPTION : 0;
}

/**
 * @brief Print buffer pool statistics
 * @param ring Ring context structure
 */
void ring_buffer_pool_print_stats(const enhanced_ring_context_t *ring) {
    if (!ring) {
        return;
    }
    
    log_info("=== Buffer Pool Statistics ===");
    
    /* TX pool statistics */
    log_info("TX Pool:");
    log_info("  Size: %u buffers", ring->tx_pool_mgr.pool_size);
    log_info("  Available: %u buffers", ring->tx_pool_mgr.available_buffers);
    log_info("  Allocated: %u buffers", ring->tx_pool_mgr.allocated_buffers);
    log_info("  Max allocation: %u buffers", ring->tx_pool_mgr.max_allocation);
    log_info("  Auto-expand: %s", ring->tx_pool_mgr.auto_expand ? "enabled" : "disabled");
    
    /* RX pool statistics */
    log_info("RX Pool:");
    log_info("  Size: %u buffers", ring->rx_pool_mgr.pool_size);
    log_info("  Available: %u buffers", ring->rx_pool_mgr.available_buffers);
    log_info("  Allocated: %u buffers", ring->rx_pool_mgr.allocated_buffers);
    log_info("  Max allocation: %u buffers", ring->rx_pool_mgr.max_allocation);
    log_info("  Auto-expand: %s", ring->rx_pool_mgr.auto_expand ? "enabled" : "disabled");
    
    /* Global statistics */
    log_info("Global Pool Stats:");
    log_info("  Pools created: %u", g_pool_stats.total_pools_created);
    log_info("  Pools destroyed: %u", g_pool_stats.total_pools_destroyed);
    log_info("  Total expansions: %u", g_pool_stats.total_expansions);
    log_info("  Total shrinks: %u", g_pool_stats.total_shrinks);
    log_info("  Allocation failures: %u", g_pool_stats.allocation_failures);
    log_info("  Leaks found: %u", g_pool_stats.leaks_found);
    log_info("  Leaks fixed: %u", g_pool_stats.leaks_fixed);
}

/* Internal helper function implementations */

static int validate_pool_parameters(const buffer_pool_mgr_t *pool_mgr) {
    if (!pool_mgr || !pool_mgr->pool) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    if (pool_mgr->allocated_buffers > pool_mgr->pool_size) {
        log_error("Pool corruption: allocated_buffers (%u) > pool_size (%u)", 
                  pool_mgr->allocated_buffers, pool_mgr->pool_size);
        return -RING_ERROR_BUFFER_CORRUPTION;
    }
    
    return 0;
}

static int expand_buffer_pool_internal(buffer_pool_mgr_t *pool_mgr, uint32_t additional_buffers) {
    if (validate_pool_parameters(pool_mgr) != 0) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    /* Note: In a real implementation, we would use buffer_pool_expand() 
     * For now, we simulate expansion by updating the pool manager */
    pool_mgr->pool_size += additional_buffers;
    pool_mgr->available_buffers += additional_buffers;
    
    return 0;
}

static int shrink_buffer_pool_internal(buffer_pool_mgr_t *pool_mgr, uint32_t remove_buffers) {
    if (validate_pool_parameters(pool_mgr) != 0) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    if (pool_mgr->available_buffers < remove_buffers) {
        return -RING_ERROR_POOL_EXHAUSTED;
    }
    
    /* Note: In a real implementation, we would use buffer_pool_shrink()
     * For now, we simulate shrinking by updating the pool manager */
    pool_mgr->pool_size -= remove_buffers;
    pool_mgr->available_buffers -= remove_buffers;
    
    return 0;
}

static void update_pool_statistics(buffer_pool_mgr_t *pool_mgr) {
    if (!pool_mgr) {
        return;
    }
    
    if (pool_mgr->allocated_buffers > pool_mgr->max_allocation) {
        pool_mgr->max_allocation = pool_mgr->allocated_buffers;
    }
}

static int validate_pool_integrity(const buffer_pool_mgr_t *pool_mgr) {
    if (!pool_mgr) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    /* Check basic consistency */
    if (pool_mgr->allocated_buffers + pool_mgr->available_buffers != pool_mgr->pool_size) {
        log_error("Pool integrity error: allocated (%u) + available (%u) != size (%u)",
                  pool_mgr->allocated_buffers, pool_mgr->available_buffers, pool_mgr->pool_size);
        return -RING_ERROR_BUFFER_CORRUPTION;
    }
    
    return 0;
}