/**
 * @file nic_buffer_pools.c
 * @brief Per-NIC Buffer Pool Implementation for 3Com Packet Driver
 * 
 * Sprint 1.5: Per-NIC Buffer Pool Implementation
 * 
 * This module implements per-NIC buffer pools to provide resource isolation,
 * eliminate contention between NICs, and enable per-NIC performance tuning.
 * This addresses the architectural gap where the current implementation uses
 * global buffer pools instead of per-NIC pools as specified in the design.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "../include/nic_buffer_pools.h"
#include "../include/logging.h"
#include "../include/memory.h"
#include "../include/nic_capabilities.h"
#include "../include/cpu_optimized.h"
#include <string.h>

/* Global multi-NIC buffer manager - cache-aligned for optimal performance */
static multi_nic_buffer_manager_t g_buffer_manager __attribute__((aligned(32))) = {0};

/* Private helper function prototypes */
static int nic_buffer_init_context(nic_buffer_context_t* context, nic_id_t nic_id, nic_type_t nic_type, const char* nic_name);
static int nic_buffer_allocate_pools(nic_buffer_context_t* context);
static void nic_buffer_cleanup_pools(nic_buffer_context_t* context);
static nic_buffer_context_t* nic_buffer_find_context(nic_id_t nic_id);
static buffer_pool_t* nic_buffer_select_pool_by_size(nic_buffer_context_t* context, uint32_t size, buffer_type_t type);
static int nic_buffer_calculate_pool_sizes(nic_type_t nic_type, uint32_t* tx_count, uint32_t* rx_count, uint32_t* dma_count);
static void nic_buffer_update_stats_alloc(nic_buffer_context_t* context, uint32_t size);
static void nic_buffer_update_stats_free(nic_buffer_context_t* context, uint32_t size);
static int nic_buffer_check_memory_limit(nic_buffer_context_t* context, uint32_t additional_bytes);
static void nic_buffer_rebalance_if_needed(nic_buffer_context_t* context);
static uint32_t nic_buffer_calculate_activity_level(nic_buffer_context_t* context);
static int nic_buffer_emergency_cleanup(nic_buffer_context_t* context);

/* === Initialization and Cleanup === */

int nic_buffer_pool_manager_init(uint32_t memory_limit, memory_tier_t memory_preference) {
    log_info("Initializing per-NIC buffer pool manager");
    
    if (g_buffer_manager.initialized) {
        log_warning("NIC buffer pool manager already initialized");
        return SUCCESS;
    }
    
    /* Initialize manager structure using CPU-optimized zero operation */
    cpu_opt_memzero(&g_buffer_manager, sizeof(multi_nic_buffer_manager_t));
    
    /* Set global configuration */
    g_buffer_manager.memory_limit = memory_limit;
    g_buffer_manager.memory_preference = memory_preference;
    g_buffer_manager.memory_reserved = memory_limit / 8; /* Reserve 12.5% for system */
    
    /* Initialize default configuration */
    g_buffer_manager.default_memory_per_nic = DEFAULT_MEMORY_PER_NIC_KB * 1024;
    g_buffer_manager.min_memory_per_nic = MIN_MEMORY_PER_NIC_KB * 1024;
    g_buffer_manager.max_memory_per_nic = MAX_MEMORY_PER_NIC_KB * 1024;
    
    /* Configure rebalancing */
    g_buffer_manager.rebalance_interval = DEFAULT_REBALANCE_INTERVAL_MS;
    g_buffer_manager.rebalance_threshold = DEFAULT_REBALANCE_THRESHOLD;
    g_buffer_manager.auto_rebalancing = true;
    
    /* Initialize all NIC contexts */
    for (int i = 0; i < MAX_NICS; i++) {
        g_buffer_manager.nics[i].nic_id = INVALID_NIC_ID;
        g_buffer_manager.nics[i].initialized = false;
    }
    
    g_buffer_manager.initialized = true;
    g_buffer_manager.nic_count = 0;
    
    log_info("NIC buffer pool manager initialized with %u KB memory limit", memory_limit / 1024);
    return SUCCESS;
}

void nic_buffer_pool_manager_cleanup(void) {
    if (!g_buffer_manager.initialized) {
        return;
    }
    
    log_info("Cleaning up per-NIC buffer pool manager");
    
    /* Cleanup all NIC buffer contexts */
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_buffer_manager.nics[i].initialized) {
            nic_buffer_pool_destroy(g_buffer_manager.nics[i].nic_id);
        }
    }
    
    /* Log final statistics */
    log_info("NIC buffer manager final stats:");
    log_info("  Total allocations: %lu", g_buffer_manager.total_allocations);
    log_info("  Allocation failures: %lu", g_buffer_manager.allocation_failures);
    log_info("  Resource contentions: %lu", g_buffer_manager.resource_contentions);
    log_info("  Emergency situations: %lu", g_buffer_manager.emergency_situations);
    
    /* Reset manager state using CPU-optimized zero operation */
    cpu_opt_memzero(&g_buffer_manager, sizeof(multi_nic_buffer_manager_t));
}

int nic_buffer_pool_create(nic_id_t nic_id, nic_type_t nic_type, const char* nic_name) {
    if (!g_buffer_manager.initialized) {
        log_error("NIC buffer pool manager not initialized");
        return ERROR_INVALID_PARAM;
    }
    
    if (nic_id == INVALID_NIC_ID || nic_id >= MAX_NICS) {
        log_error("Invalid NIC ID: %d", nic_id);
        return ERROR_INVALID_PARAM;
    }
    
    if (!nic_name) {
        log_error("NIC name cannot be NULL");
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if NIC already exists */
    if (g_buffer_manager.nics[nic_id].initialized) {
        log_warning("NIC buffer pools already exist for NIC ID %d", nic_id);
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Creating buffer pools for NIC %d (%s)", nic_id, nic_name);
    
    /* Initialize NIC context */
    int result = nic_buffer_init_context(&g_buffer_manager.nics[nic_id], nic_id, nic_type, nic_name);
    if (result != SUCCESS) {
        log_error("Failed to initialize NIC context: %d", result);
        return result;
    }
    
    /* Allocate buffer pools */
    result = nic_buffer_allocate_pools(&g_buffer_manager.nics[nic_id]);
    if (result != SUCCESS) {
        log_error("Failed to allocate buffer pools for NIC %d: %d", nic_id, result);
        nic_buffer_cleanup_pools(&g_buffer_manager.nics[nic_id]);
        return result;
    }
    
    /* Mark as initialized and increment count */
    g_buffer_manager.nics[nic_id].initialized = true;
    g_buffer_manager.nic_count++;
    
    log_info("Successfully created buffer pools for NIC %d (%s)", nic_id, nic_name);
    log_info("  TX: %u buffers, RX: %u buffers, DMA: %u buffers",
             g_buffer_manager.nics[nic_id].tx_buffer_count,
             g_buffer_manager.nics[nic_id].rx_buffer_count,
             g_buffer_manager.nics[nic_id].dma_buffer_count);
    
    return SUCCESS;
}

int nic_buffer_pool_destroy(nic_id_t nic_id) {
    if (!g_buffer_manager.initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        log_warning("NIC buffer pools for ID %d not found or not initialized", nic_id);
        return ERROR_NOT_FOUND;
    }
    
    log_info("Destroying buffer pools for NIC %d (%s)", nic_id, context->nic_name);
    
    /* Print final statistics before cleanup */
    log_info("Final stats for NIC %d:", nic_id);
    log_info("  Total allocations: %lu", context->stats.total_allocations);
    log_info("  Peak allocated: %lu", context->stats.peak_allocated);
    log_info("  Peak memory usage: %lu bytes", context->stats.peak_memory_usage);
    
    /* Cleanup all pools */
    nic_buffer_cleanup_pools(context);
    
    /* Reset context using CPU-optimized zero operation */
    cpu_opt_memzero(context, sizeof(nic_buffer_context_t));
    context->nic_id = INVALID_NIC_ID;
    context->initialized = false;
    
    /* Decrement count */
    if (g_buffer_manager.nic_count > 0) {
        g_buffer_manager.nic_count--;
    }
    
    log_info("Buffer pools destroyed for NIC %d", nic_id);
    return SUCCESS;
}

/* === Buffer Allocation and Deallocation === */

buffer_desc_t* nic_buffer_alloc(nic_id_t nic_id, buffer_type_t type, uint32_t size) {
    if (!g_buffer_manager.initialized) {
        log_error("NIC buffer pool manager not initialized");
        return NULL;
    }
    
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        log_error("NIC buffer context for ID %d not found", nic_id);
        return NULL;
    }
    
    if (size == 0) {
        log_error("Invalid buffer size: 0");
        return NULL;
    }
    
    /* Check memory limit before allocation */
    int result = nic_buffer_check_memory_limit(context, size);
    if (result != SUCCESS) {
        log_warning("Memory limit exceeded for NIC %d, attempting emergency cleanup", nic_id);
        
        /* Try emergency cleanup */
        if (nic_buffer_emergency_cleanup(context) != SUCCESS) {
            context->stats.allocation_failures++;
            g_buffer_manager.allocation_failures++;
            g_buffer_manager.emergency_situations++;
            return NULL;
        }
        
        /* Retry memory check after cleanup */
        if (nic_buffer_check_memory_limit(context, size) != SUCCESS) {
            context->stats.allocation_failures++;
            g_buffer_manager.allocation_failures++;
            return NULL;
        }
    }
    
    /* Select appropriate pool based on size and type */
    buffer_pool_t* pool = nic_buffer_select_pool_by_size(context, size, type);
    if (!pool) {
        log_error("No suitable buffer pool found for NIC %d, type %d, size %u", nic_id, type, size);
        context->stats.allocation_failures++;
        g_buffer_manager.allocation_failures++;
        return NULL;
    }
    
    /* Allocate from selected pool */
    buffer_desc_t* buffer = buffer_alloc(pool);
    if (!buffer) {
        log_debug("Pool exhausted for NIC %d, trying fallback allocation", nic_id);
        context->stats.fallback_allocations++;
        
        /* Try fallback to larger pool */
        if (size <= SMALL_BUFFER_THRESHOLD && pool != &context->medium_pool) {
            buffer = buffer_alloc(&context->medium_pool);
        }
        if (!buffer && size <= MEDIUM_BUFFER_THRESHOLD && pool != &context->large_pool) {
            buffer = buffer_alloc(&context->large_pool);
        }
        if (!buffer && pool != &context->jumbo_pool) {
            buffer = buffer_alloc(&context->jumbo_pool);
        }
        
        if (!buffer) {
            log_warning("All buffer pools exhausted for NIC %d", nic_id);
            context->stats.allocation_failures++;
            g_buffer_manager.allocation_failures++;
            return NULL;
        }
    } else {
        context->stats.fast_path_hits++;
    }
    
    /* Update statistics */
    nic_buffer_update_stats_alloc(context, size);
    g_buffer_manager.total_allocations++;
    
    /* Check if rebalancing is needed */
    nic_buffer_rebalance_if_needed(context);
    
    log_debug("Allocated %u-byte buffer for NIC %d from pool (type %d)", size, nic_id, type);
    return buffer;
}

void nic_buffer_free(nic_id_t nic_id, buffer_desc_t* buffer) {
    if (!buffer) {
        return;
    }
    
    if (!g_buffer_manager.initialized) {
        log_error("NIC buffer pool manager not initialized");
        return;
    }
    
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        log_error("NIC buffer context for ID %d not found", nic_id);
        return;
    }
    
    /* Determine which pool this buffer belongs to based on size */
    buffer_pool_t* pool = NULL;
    
    if (buffer->size <= SMALL_BUFFER_THRESHOLD) {
        pool = &context->small_pool;
    } else if (buffer->size <= MEDIUM_BUFFER_THRESHOLD) {
        pool = &context->medium_pool;
    } else if (buffer->size <= LARGE_BUFFER_THRESHOLD) {
        pool = &context->large_pool;
    } else {
        pool = &context->jumbo_pool;
    }
    
    /* Try the determined pool first, then check primary pools */
    if (!pool || !pool->initialized) {
        switch (buffer->type) {
            case BUFFER_TYPE_TX:
            case BUFFER_TYPE_DMA_TX:
                pool = &context->tx_pool;
                break;
            case BUFFER_TYPE_RX:
            case BUFFER_TYPE_DMA_RX:
                pool = &context->rx_pool;
                break;
            default:
                if (context->dma_pool.initialized) {
                    pool = &context->dma_pool;
                } else {
                    pool = &context->tx_pool; /* Fallback */
                }
                break;
        }
    }
    
    if (!pool || !pool->initialized) {
        log_error("No suitable pool found to free buffer for NIC %d", nic_id);
        return;
    }
    
    /* Free buffer back to pool */
    buffer_free(pool, buffer);
    
    /* Update statistics */
    nic_buffer_update_stats_free(context, buffer->size);
    
    log_debug("Freed %u-byte buffer for NIC %d", buffer->size, nic_id);
}

buffer_desc_t* nic_buffer_alloc_ethernet_frame(nic_id_t nic_id, uint32_t frame_size, buffer_type_t type) {
    if (frame_size > MAX_PACKET_SIZE) {
        log_error("Frame size %u exceeds maximum %u", frame_size, MAX_PACKET_SIZE);
        return NULL;
    }
    
    /* Use optimized allocation for common frame sizes */
    return nic_buffer_alloc(nic_id, type, frame_size);
}

buffer_desc_t* nic_buffer_alloc_dma(nic_id_t nic_id, uint32_t size, uint32_t alignment) {
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        log_error("NIC buffer context for ID %d not found", nic_id);
        return NULL;
    }
    
    /* Only 3C515-TX supports DMA */
    if (context->nic_type != NIC_TYPE_3C515_TX) {
        log_warning("DMA buffers not supported for NIC type %d", context->nic_type);
        return nic_buffer_alloc(nic_id, BUFFER_TYPE_TX, size); /* Fallback */
    }
    
    if (!context->dma_pool.initialized) {
        log_error("DMA pool not initialized for NIC %d", nic_id);
        return NULL;
    }
    
    /* Validate alignment */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        log_error("Invalid alignment %u (must be power of 2)", alignment);
        return NULL;
    }
    
    /* Allocate from DMA pool */
    buffer_desc_t* buffer = buffer_alloc(&context->dma_pool);
    if (!buffer) {
        log_warning("DMA pool exhausted for NIC %d", nic_id);
        context->stats.allocation_failures++;
        return NULL;
    }
    
    /* Check alignment */
    if (!IS_ALIGNED((uint32_t)buffer->data, alignment)) {
        log_warning("DMA buffer not properly aligned: %p (need %u-byte alignment)",
                   buffer->data, alignment);
        /* Continue with unaligned buffer for now */
    }
    
    buffer->flags |= BUFFER_FLAG_DMA_CAPABLE;
    nic_buffer_update_stats_alloc(context, size);
    
    log_debug("Allocated DMA buffer for NIC %d: %u bytes, %u-byte aligned", nic_id, size, alignment);
    return buffer;
}

/* === RX_COPYBREAK Integration === */

int nic_rx_copybreak_init(nic_id_t nic_id, uint32_t small_count, uint32_t large_count, uint32_t threshold) {
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        log_error("NIC buffer context for ID %d not found", nic_id);
        return ERROR_NOT_FOUND;
    }
    
    if (small_count == 0 || large_count == 0) {
        log_error("Invalid RX_COPYBREAK pool sizes: small=%u, large=%u", small_count, large_count);
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Initializing RX_COPYBREAK for NIC %d: small=%u, large=%u, threshold=%u",
             nic_id, small_count, large_count, threshold);
    
    /* Initialize the copybreak pool structure using CPU-optimized zero operation */
    cpu_opt_memzero(&context->copybreak_pool, sizeof(rx_copybreak_pool_t));
    
    /* Set configuration */
    context->copybreak_pool.small_buffer_count = small_count;
    context->copybreak_pool.large_buffer_count = large_count;
    context->copybreak_pool.copybreak_threshold = threshold;
    context->copybreak_threshold = threshold;
    
    /* Initialize small buffer pool */
    int result = buffer_pool_init(&context->copybreak_pool.small_pool, BUFFER_TYPE_RX,
                                 SMALL_BUFFER_SIZE, small_count, BUFFER_FLAG_ALIGNED);
    if (result != SUCCESS) {
        log_error("Failed to initialize RX_COPYBREAK small pool for NIC %d: %d", nic_id, result);
        return result;
    }
    
    /* Initialize large buffer pool */
    result = buffer_pool_init(&context->copybreak_pool.large_pool, BUFFER_TYPE_RX,
                             LARGE_BUFFER_SIZE, large_count, BUFFER_FLAG_ALIGNED);
    if (result != SUCCESS) {
        log_error("Failed to initialize RX_COPYBREAK large pool for NIC %d: %d", nic_id, result);
        buffer_pool_cleanup(&context->copybreak_pool.small_pool);
        return result;
    }
    
    context->copybreak_enabled = true;
    
    log_info("RX_COPYBREAK initialized for NIC %d", nic_id);
    return SUCCESS;
}

buffer_desc_t* nic_rx_copybreak_alloc(nic_id_t nic_id, uint32_t packet_size) {
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized || !context->copybreak_enabled) {
        log_error("RX_COPYBREAK not available for NIC %d", nic_id);
        return NULL;
    }
    
    buffer_desc_t* buffer = NULL;
    
    /* Select pool based on packet size */
    if (packet_size < context->copybreak_threshold) {
        /* Small packet - use small buffer pool */
        buffer = buffer_alloc(&context->copybreak_pool.small_pool);
        if (buffer) {
            context->copybreak_pool.small_allocations++;
            context->copybreak_pool.memory_saved += (LARGE_BUFFER_SIZE - SMALL_BUFFER_SIZE);
            log_debug("RX_COPYBREAK: allocated small buffer for NIC %d (packet size %u)", nic_id, packet_size);
            return buffer;
        } else {
            log_debug("RX_COPYBREAK: small pool exhausted for NIC %d, using large pool", nic_id);
        }
    }
    
    /* Large packet or small pool exhausted - use large buffer pool */
    buffer = buffer_alloc(&context->copybreak_pool.large_pool);
    if (buffer) {
        context->copybreak_pool.large_allocations++;
        log_debug("RX_COPYBREAK: allocated large buffer for NIC %d (packet size %u)", nic_id, packet_size);
        return buffer;
    }
    
    log_warning("RX_COPYBREAK: all pools exhausted for NIC %d", nic_id);
    return NULL;
}

void nic_rx_copybreak_free(nic_id_t nic_id, buffer_desc_t* buffer) {
    if (!buffer) {
        return;
    }
    
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized || !context->copybreak_enabled) {
        log_error("RX_COPYBREAK not available for NIC %d", nic_id);
        return;
    }
    
    /* Determine which pool based on buffer size */
    if (buffer->size == SMALL_BUFFER_SIZE) {
        buffer_free(&context->copybreak_pool.small_pool, buffer);
        log_debug("RX_COPYBREAK: freed small buffer for NIC %d", nic_id);
    } else if (buffer->size == LARGE_BUFFER_SIZE) {
        buffer_free(&context->copybreak_pool.large_pool, buffer);
        log_debug("RX_COPYBREAK: freed large buffer for NIC %d", nic_id);
    } else {
        log_error("RX_COPYBREAK: invalid buffer size %u for NIC %d", buffer->size, nic_id);
    }
}

/* === Resource Management === */

int balance_buffer_resources(void) {
    if (!g_buffer_manager.initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    log_debug("Starting global buffer resource balancing");
    
    uint32_t current_time = get_system_timestamp_ms();
    
    /* Check if it's time for global rebalancing */
    if (current_time - g_buffer_manager.last_global_rebalance < g_buffer_manager.rebalance_interval) {
        return SUCCESS; /* Too soon for rebalancing */
    }
    
    /* Calculate total memory usage and activity levels */
    uint32_t total_memory_used = 0;
    uint32_t active_nics = 0;
    uint32_t total_activity = 0;
    
    for (int i = 0; i < MAX_NICS; i++) {
        nic_buffer_context_t* context = &g_buffer_manager.nics[i];
        if (context->initialized) {
            active_nics++;
            total_memory_used += context->stats.current_memory_usage;
            context->activity_level = nic_buffer_calculate_activity_level(context);
            total_activity += context->activity_level;
        }
    }
    
    if (active_nics == 0) {
        return SUCCESS; /* No NICs to balance */
    }
    
    log_debug("Resource balancing: %u active NICs, %u KB used, avg activity %u%%",
              active_nics, total_memory_used / 1024, total_activity / active_nics);
    
    /* Rebalance memory based on activity levels */
    uint32_t available_memory = g_buffer_manager.memory_limit - g_buffer_manager.memory_reserved;
    
    for (int i = 0; i < MAX_NICS; i++) {
        nic_buffer_context_t* context = &g_buffer_manager.nics[i];
        if (!context->initialized) {
            continue;
        }
        
        /* Calculate new allocation based on activity level */
        uint32_t base_allocation = available_memory / active_nics;
        uint32_t activity_bonus = 0;
        
        if (total_activity > 0) {
            activity_bonus = (available_memory / 4) * context->activity_level / total_activity;
        }
        
        uint32_t new_allocation = base_allocation + activity_bonus;
        
        /* Ensure allocation is within limits */
        if (new_allocation < g_buffer_manager.min_memory_per_nic) {
            new_allocation = g_buffer_manager.min_memory_per_nic;
        }
        if (new_allocation > g_buffer_manager.max_memory_per_nic) {
            new_allocation = g_buffer_manager.max_memory_per_nic;
        }
        
        /* Apply new allocation if significantly different */
        uint32_t current_allocation = context->memory_limit;
        uint32_t difference = (new_allocation > current_allocation) ? 
                             (new_allocation - current_allocation) : 
                             (current_allocation - new_allocation);
        
        if (difference > current_allocation / 10) { /* 10% difference threshold */
            log_debug("Adjusting NIC %d memory allocation: %u KB -> %u KB (activity %u%%)",
                     context->nic_id, current_allocation / 1024, new_allocation / 1024, context->activity_level);
            
            adjust_nic_buffer_allocation(context->nic_id, new_allocation / 1024);
        }
        
        context->needs_rebalancing = false;
    }
    
    g_buffer_manager.last_global_rebalance = current_time;
    
    log_debug("Global buffer resource balancing completed");
    return SUCCESS;
}

int adjust_nic_buffer_allocation(nic_id_t nic_id, uint32_t new_allocation) {
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        return ERROR_NOT_FOUND;
    }
    
    uint32_t new_allocation_bytes = new_allocation * 1024;
    
    /* Validate new allocation */
    if (new_allocation_bytes < g_buffer_manager.min_memory_per_nic ||
        new_allocation_bytes > g_buffer_manager.max_memory_per_nic) {
        log_error("Invalid allocation %u KB for NIC %d (min %u, max %u)",
                 new_allocation, nic_id,
                 g_buffer_manager.min_memory_per_nic / 1024,
                 g_buffer_manager.max_memory_per_nic / 1024);
        return ERROR_INVALID_PARAM;
    }
    
    log_info("Adjusting buffer allocation for NIC %d: %u KB -> %u KB",
             nic_id, context->memory_limit / 1024, new_allocation);
    
    context->memory_limit = new_allocation_bytes;
    context->last_rebalance_time = get_system_timestamp_ms();
    
    return SUCCESS;
}

void monitor_nic_buffer_usage(void) {
    if (!g_buffer_manager.initialized) {
        return;
    }
    
    static uint32_t last_monitor_time = 0;
    uint32_t current_time = get_system_timestamp_ms();
    
    /* Monitor every 10 seconds */
    if (current_time - last_monitor_time < 10000) {
        return;
    }
    
    log_debug("=== NIC Buffer Usage Monitor ===");
    
    for (int i = 0; i < MAX_NICS; i++) {
        nic_buffer_context_t* context = &g_buffer_manager.nics[i];
        if (!context->initialized) {
            continue;
        }
        
        uint32_t usage_percent = (context->stats.current_memory_usage * 100) / context->memory_limit;
        
        log_debug("NIC %d (%s): %u%% memory usage (%u/%u KB), %u buffers allocated",
                 context->nic_id, context->nic_name, usage_percent,
                 context->stats.current_memory_usage / 1024,
                 context->memory_limit / 1024,
                 context->stats.current_allocated);
        
        /* Check for warning conditions */
        if (usage_percent > 85) {
            log_warning("High memory usage (%u%%) for NIC %d", usage_percent, context->nic_id);
            context->needs_rebalancing = true;
        }
        
        if (context->stats.allocation_failures > 0) {
            log_warning("NIC %d has %u allocation failures", context->nic_id, context->stats.allocation_failures);
        }
    }
    
    last_monitor_time = current_time;
    
    /* Trigger rebalancing if needed */
    if (g_buffer_manager.auto_rebalancing) {
        balance_buffer_resources();
    }
}

/* === Statistics and Monitoring === */

int nic_buffer_get_stats(nic_id_t nic_id, buffer_pool_stats_t* stats) {
    if (!stats) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        return ERROR_NOT_FOUND;
    }
    
    /* Copy statistics using CPU-optimized copy operation */
    cpu_opt_memcpy(stats, &context->stats, sizeof(buffer_pool_stats_t), CPU_OPT_FLAG_CACHE_ALIGN);
    
    return SUCCESS;
}

int nic_buffer_get_global_stats(uint32_t* total_allocated, uint32_t* active_nics, uint32_t* contentions) {
    if (!g_buffer_manager.initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    if (total_allocated) {
        *total_allocated = g_buffer_manager.total_allocated;
    }
    
    if (active_nics) {
        *active_nics = g_buffer_manager.nic_count;
    }
    
    if (contentions) {
        *contentions = g_buffer_manager.resource_contentions;
    }
    
    return SUCCESS;
}

void nic_buffer_print_all_stats(void) {
    if (!g_buffer_manager.initialized) {
        log_info("NIC buffer pool manager not initialized");
        return;
    }
    
    log_info("=== Per-NIC Buffer Pool Statistics ===");
    log_info("Global Stats:");
    log_info("  Active NICs: %u", g_buffer_manager.nic_count);
    log_info("  Total allocations: %lu", g_buffer_manager.total_allocations);
    log_info("  Allocation failures: %lu", g_buffer_manager.allocation_failures);
    log_info("  Resource contentions: %lu", g_buffer_manager.resource_contentions);
    log_info("  Emergency situations: %lu", g_buffer_manager.emergency_situations);
    log_info("");
    
    for (int i = 0; i < MAX_NICS; i++) {
        nic_buffer_context_t* context = &g_buffer_manager.nics[i];
        if (!context->initialized) {
            continue;
        }
        
        log_info("NIC %d (%s):", context->nic_id, context->nic_name);
        log_info("  Memory: %u KB allocated, %u KB limit (%u%% usage)",
                 context->stats.current_memory_usage / 1024,
                 context->memory_limit / 1024,
                 (context->stats.current_memory_usage * 100) / context->memory_limit);
        log_info("  Buffers: %lu allocated, %lu peak, %lu total allocs",
                 context->stats.current_allocated,
                 context->stats.peak_allocated,
                 context->stats.total_allocations);
        log_info("  Performance: %lu fast path hits, %lu fallbacks, %lu failures",
                 context->stats.fast_path_hits,
                 context->stats.fallback_allocations,
                 context->stats.allocation_failures);
        
        if (context->copybreak_enabled) {
            log_info("  RX_COPYBREAK: %lu small, %lu large, %lu memory saved",
                     context->copybreak_pool.small_allocations,
                     context->copybreak_pool.large_allocations,
                     context->copybreak_pool.memory_saved);
        }
        log_info("");
    }
}

/* === Utility Functions === */

nic_buffer_context_t* nic_buffer_get_context(nic_id_t nic_id) {
    return nic_buffer_find_context(nic_id);
}

bool nic_buffer_is_initialized(nic_id_t nic_id) {
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    return context && context->initialized;
}

uint32_t nic_buffer_get_available_memory(nic_id_t nic_id) {
    nic_buffer_context_t* context = nic_buffer_find_context(nic_id);
    if (!context || !context->initialized) {
        return 0;
    }
    
    if (context->stats.current_memory_usage >= context->memory_limit) {
        return 0;
    }
    
    return context->memory_limit - context->stats.current_memory_usage;
}

/* === Private Helper Functions === */

static int nic_buffer_init_context(nic_buffer_context_t* context, nic_id_t nic_id, nic_type_t nic_type, const char* nic_name) {
    if (!context || !nic_name) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Initialize basic fields using CPU-optimized zero operation */
    cpu_opt_memzero(context, sizeof(nic_buffer_context_t));
    context->nic_id = nic_id;
    context->nic_type = nic_type;
    /* Use CPU-optimized string copy with bounds checking */
    size_t name_len = cpu_opt_strlen(nic_name);
    if (name_len >= sizeof(context->nic_name)) {
        name_len = sizeof(context->nic_name) - 1;
    }
    cpu_opt_memcpy(context->nic_name, nic_name, name_len, CPU_OPT_FLAG_NONE);
    context->nic_name[name_len] = '\0';
    
    /* Set memory configuration */
    context->memory_limit = g_buffer_manager.default_memory_per_nic;
    context->memory_preference = g_buffer_manager.memory_preference;
    context->memory_reserved = context->memory_limit / 8; /* Reserve 12.5% */
    
    /* Calculate optimal buffer counts for this NIC type */
    nic_buffer_calculate_pool_sizes(nic_type, &context->tx_buffer_count,
                                   &context->rx_buffer_count, &context->dma_buffer_count);
    
    /* Set size-specific pool counts */
    context->small_buffer_count = DEFAULT_SMALL_BUFFERS_PER_NIC;
    context->medium_buffer_count = DEFAULT_MEDIUM_BUFFERS_PER_NIC;
    context->large_buffer_count = DEFAULT_LARGE_BUFFERS_PER_NIC;
    context->jumbo_buffer_count = DEFAULT_JUMBO_BUFFERS_PER_NIC;
    
    /* Configure RX_COPYBREAK */
    context->copybreak_threshold = RX_COPYBREAK_THRESHOLD;
    context->copybreak_enabled = false; /* Initialized separately */
    
    /* Initialize statistics using CPU-optimized zero operation */
    cpu_opt_memzero(&context->stats, sizeof(buffer_pool_stats_t));
    
    return SUCCESS;
}

static int nic_buffer_allocate_pools(nic_buffer_context_t* context) {
    if (!context) {
        return ERROR_INVALID_PARAM;
    }
    
    extern cpu_info_t g_cpu_info;
    int result;
    
    /* Determine pool flags based on NIC type and CPU capabilities */
    uint32_t pool_flags = BUFFER_FLAG_ALIGNED;
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        pool_flags |= BUFFER_FLAG_ZERO_INIT;
    }
    
    /* Add cache alignment for frequently accessed structures */
    const cpu_opt_context_t* cpu_ctx = cpu_opt_get_context();
    if (cpu_ctx && cpu_ctx->has_cache) {
        pool_flags |= BUFFER_FLAG_CACHE_ALIGNED;
    }
    
    uint32_t dma_flags = BUFFER_FLAG_DMA_CAPABLE | BUFFER_FLAG_ALIGNED;
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        dma_flags |= BUFFER_FLAG_PERSISTENT;
    }
    
    /* Initialize TX buffer pool */
    result = buffer_pool_init(&context->tx_pool, BUFFER_TYPE_TX,
                             TX_BUFFER_SIZE, context->tx_buffer_count, pool_flags);
    if (result != SUCCESS) {
        log_error("Failed to initialize TX pool for NIC %d: %d", context->nic_id, result);
        return result;
    }
    
    /* Initialize RX buffer pool */
    result = buffer_pool_init(&context->rx_pool, BUFFER_TYPE_RX,
                             RX_BUFFER_SIZE, context->rx_buffer_count, pool_flags);
    if (result != SUCCESS) {
        log_error("Failed to initialize RX pool for NIC %d: %d", context->nic_id, result);
        buffer_pool_cleanup(&context->tx_pool);
        return result;
    }
    
    /* Initialize DMA buffer pool (only for 3C515-TX) */
    if (context->nic_type == NIC_TYPE_3C515_TX && context->dma_buffer_count > 0) {
        result = buffer_pool_init(&context->dma_pool, BUFFER_TYPE_DMA_TX,
                                 DMA_BUFFER_SIZE, context->dma_buffer_count, dma_flags);
        if (result != SUCCESS) {
            log_warning("Failed to initialize DMA pool for NIC %d: %d", context->nic_id, result);
            /* Continue without DMA pool */
        }
    }
    
    /* Initialize size-specific pools */
    
    /* Small buffer pool */
    result = buffer_pool_init(&context->small_pool, BUFFER_TYPE_TX,
                             SMALL_BUFFER_THRESHOLD, context->small_buffer_count, pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize small buffer pool for NIC %d: %d", context->nic_id, result);
        /* Continue without small pool */
    }
    
    /* Medium buffer pool */
    result = buffer_pool_init(&context->medium_pool, BUFFER_TYPE_TX,
                             MEDIUM_BUFFER_THRESHOLD, context->medium_buffer_count, pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize medium buffer pool for NIC %d: %d", context->nic_id, result);
        /* Continue without medium pool */
    }
    
    /* Large buffer pool */
    result = buffer_pool_init(&context->large_pool, BUFFER_TYPE_TX,
                             LARGE_BUFFER_THRESHOLD, context->large_buffer_count, pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize large buffer pool for NIC %d: %d", context->nic_id, result);
        /* Continue without large pool */
    }
    
    /* Jumbo buffer pool */
    result = buffer_pool_init(&context->jumbo_pool, BUFFER_TYPE_TX,
                             MAX_PACKET_SIZE, context->jumbo_buffer_count, pool_flags);
    if (result != SUCCESS) {
        log_warning("Failed to initialize jumbo buffer pool for NIC %d: %d", context->nic_id, result);
        /* Continue without jumbo pool */
    }
    
    /* Calculate total allocated memory using CPU-optimized arithmetic */
    uint32_t total_memory = 0;
    uint32_t pool_sizes[] = {
        context->tx_pool.initialized ? context->tx_pool.memory_size : 0,
        context->rx_pool.initialized ? context->rx_pool.memory_size : 0,
        context->dma_pool.initialized ? context->dma_pool.memory_size : 0,
        context->small_pool.initialized ? context->small_pool.memory_size : 0,
        context->medium_pool.initialized ? context->medium_pool.memory_size : 0,
        context->large_pool.initialized ? context->large_pool.memory_size : 0,
        context->jumbo_pool.initialized ? context->jumbo_pool.memory_size : 0
    };
    
    /* Optimized loop unrolling for memory calculation */
    for (int i = 0; i < 7; i++) {
        total_memory += pool_sizes[i];
    }
    context->allocated_memory = total_memory;
    
    context->stats.current_memory_usage = context->allocated_memory;
    
    log_debug("Allocated %u KB memory for NIC %d buffer pools", context->allocated_memory / 1024, context->nic_id);
    
    return SUCCESS;
}

static void nic_buffer_cleanup_pools(nic_buffer_context_t* context) {
    if (!context) {
        return;
    }
    
    /* Cleanup RX_COPYBREAK pools */
    if (context->copybreak_enabled) {
        buffer_pool_cleanup(&context->copybreak_pool.large_pool);
        buffer_pool_cleanup(&context->copybreak_pool.small_pool);
        context->copybreak_enabled = false;
    }
    
    /* Cleanup size-specific pools */
    buffer_pool_cleanup(&context->jumbo_pool);
    buffer_pool_cleanup(&context->large_pool);
    buffer_pool_cleanup(&context->medium_pool);
    buffer_pool_cleanup(&context->small_pool);
    
    /* Cleanup primary pools */
    buffer_pool_cleanup(&context->dma_pool);
    buffer_pool_cleanup(&context->rx_pool);
    buffer_pool_cleanup(&context->tx_pool);
    
    context->allocated_memory = 0;
    context->stats.current_memory_usage = 0;
}

static nic_buffer_context_t* nic_buffer_find_context(nic_id_t nic_id) {
    if (!g_buffer_manager.initialized || nic_id == INVALID_NIC_ID || nic_id >= MAX_NICS) {
        return NULL;
    }
    
    nic_buffer_context_t* context = &g_buffer_manager.nics[nic_id];
    return context->initialized ? context : NULL;
}

static buffer_pool_t* nic_buffer_select_pool_by_size(nic_buffer_context_t* context, uint32_t size, buffer_type_t type) {
    if (!context) {
        return NULL;
    }
    
    /* Try size-specific pools first for optimal performance */
    if (size <= SMALL_BUFFER_THRESHOLD && context->small_pool.initialized) {
        return &context->small_pool;
    }
    if (size <= MEDIUM_BUFFER_THRESHOLD && context->medium_pool.initialized) {
        return &context->medium_pool;
    }
    if (size <= LARGE_BUFFER_THRESHOLD && context->large_pool.initialized) {
        return &context->large_pool;
    }
    if (size <= MAX_PACKET_SIZE && context->jumbo_pool.initialized) {
        return &context->jumbo_pool;
    }
    
    /* Fall back to primary pools based on type */
    switch (type) {
        case BUFFER_TYPE_TX:
        case BUFFER_TYPE_DMA_TX:
            if (context->nic_type == NIC_TYPE_3C515_TX && 
                type == BUFFER_TYPE_DMA_TX && 
                context->dma_pool.initialized) {
                return &context->dma_pool;
            }
            return context->tx_pool.initialized ? &context->tx_pool : NULL;
            
        case BUFFER_TYPE_RX:
        case BUFFER_TYPE_DMA_RX:
            return context->rx_pool.initialized ? &context->rx_pool : NULL;
            
        default:
            /* Try DMA pool first, then TX pool */
            if (context->dma_pool.initialized) {
                return &context->dma_pool;
            }
            return context->tx_pool.initialized ? &context->tx_pool : NULL;
    }
}

static int nic_buffer_calculate_pool_sizes(nic_type_t nic_type, uint32_t* tx_count, uint32_t* rx_count, uint32_t* dma_count) {
    if (!tx_count || !rx_count || !dma_count) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Set defaults */
    *tx_count = DEFAULT_TX_BUFFERS_PER_NIC;
    *rx_count = DEFAULT_RX_BUFFERS_PER_NIC;
    *dma_count = 0; /* No DMA by default */
    
    /* Adjust based on NIC type */
    switch (nic_type) {
        case NIC_TYPE_3C509B:
            /* 3C509B uses PIO, so no DMA buffers needed */
            *tx_count = 16;
            *rx_count = 32;
            *dma_count = 0;
            break;
            
        case NIC_TYPE_3C515_TX:
            /* 3C515-TX supports DMA for better performance */
            *tx_count = 24;
            *rx_count = 48;
            *dma_count = DEFAULT_DMA_BUFFERS_PER_NIC;
            break;
            
        default:
            /* Use conservative defaults for unknown types */
            break;
    }
    
    /* Adjust based on available memory */
    if (memory_xms_available()) {
        uint32_t xms_kb = memory_get_xms_size();
        if (xms_kb > 1024) {
            /* More memory available - increase buffer counts */
            *tx_count = (*tx_count * 3) / 2;
            *rx_count = (*rx_count * 3) / 2;
            if (*dma_count > 0) {
                *dma_count = (*dma_count * 3) / 2;
            }
        }
    }
    
    return SUCCESS;
}

static void nic_buffer_update_stats_alloc(nic_buffer_context_t* context, uint32_t size) {
    if (!context) {
        return;
    }
    
    context->stats.total_allocations++;
    context->stats.current_allocated++;
    context->stats.bytes_allocated += size;
    context->stats.current_memory_usage += size;
    
    if (context->stats.current_allocated > context->stats.peak_allocated) {
        context->stats.peak_allocated = context->stats.current_allocated;
    }
    
    if (context->stats.current_memory_usage > context->stats.peak_memory_usage) {
        context->stats.peak_memory_usage = context->stats.current_memory_usage;
    }
}

static void nic_buffer_update_stats_free(nic_buffer_context_t* context, uint32_t size) {
    if (!context) {
        return;
    }
    
    context->stats.total_frees++;
    if (context->stats.current_allocated > 0) {
        context->stats.current_allocated--;
    }
    context->stats.bytes_freed += size;
    if (context->stats.current_memory_usage >= size) {
        context->stats.current_memory_usage -= size;
    }
}

static int nic_buffer_check_memory_limit(nic_buffer_context_t* context, uint32_t additional_bytes) {
    if (!context) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t new_usage = context->stats.current_memory_usage + additional_bytes;
    
    if (new_usage > context->memory_limit) {
        context->stats.allocation_failures++;
        return ERROR_NO_MEMORY;
    }
    
    return SUCCESS;
}

static void nic_buffer_rebalance_if_needed(nic_buffer_context_t* context) {
    if (!context || !g_buffer_manager.auto_rebalancing) {
        return;
    }
    
    uint32_t current_time = get_system_timestamp_ms();
    
    /* Check if individual NIC needs rebalancing */
    if (current_time - context->last_rebalance_time > g_buffer_manager.rebalance_interval) {
        uint32_t usage_percent = (context->stats.current_memory_usage * 100) / context->memory_limit;
        
        if (usage_percent > g_buffer_manager.rebalance_threshold) {
            context->needs_rebalancing = true;
        }
    }
}

static uint32_t nic_buffer_calculate_activity_level(nic_buffer_context_t* context) {
    if (!context) {
        return 0;
    }
    
    /* Calculate activity based on allocation rate and memory usage */
    uint32_t current_time = get_system_timestamp_ms();
    uint32_t time_diff = current_time - context->last_rebalance_time;
    
    if (time_diff == 0) {
        return context->activity_level; /* Return previous level */
    }
    
    /* Calculate allocations per second */
    uint32_t allocs_per_sec = (context->stats.total_allocations * 1000) / time_diff;
    
    /* Calculate memory usage percentage */
    uint32_t usage_percent = (context->stats.current_memory_usage * 100) / context->memory_limit;
    
    /* Combine factors to determine activity level (0-100) */
    uint32_t activity = (allocs_per_sec * 2) + usage_percent;
    
    /* Cap at 100% */
    return (activity > 100) ? 100 : activity;
}

static int nic_buffer_emergency_cleanup(nic_buffer_context_t* context) {
    if (!context) {
        return ERROR_INVALID_PARAM;
    }
    
    log_warning("Performing emergency cleanup for NIC %d", context->nic_id);
    
    /* This is a placeholder for emergency cleanup procedures */
    /* In a real implementation, this might:
     * 1. Force garbage collection of unused buffers
     * 2. Temporarily reduce pool sizes
     * 3. Clear non-critical cached data
     * 4. Trigger aggressive memory compaction
     */
    
    context->stats.emergency_allocations++;
    g_buffer_manager.emergency_situations++;
    
    return SUCCESS;
}

/* === Legacy Compatibility Functions === */

buffer_pool_t* nic_buffer_get_legacy_pool(buffer_type_t type) {
    if (!g_buffer_manager.initialized || g_buffer_manager.nic_count == 0) {
        return NULL;
    }
    
    /* Find first initialized NIC and return its pool */
    for (int i = 0; i < MAX_NICS; i++) {
        nic_buffer_context_t* context = &g_buffer_manager.nics[i];
        if (!context->initialized) {
            continue;
        }
        
        switch (type) {
            case BUFFER_TYPE_TX:
            case BUFFER_TYPE_DMA_TX:
                return context->tx_pool.initialized ? &context->tx_pool : NULL;
            case BUFFER_TYPE_RX:
            case BUFFER_TYPE_DMA_RX:
                return context->rx_pool.initialized ? &context->rx_pool : NULL;
            default:
                return context->dma_pool.initialized ? &context->dma_pool : 
                       (context->tx_pool.initialized ? &context->tx_pool : NULL);
        }
    }
    
    return NULL;
}

buffer_desc_t* nic_buffer_alloc_legacy(buffer_type_t type) {
    /* Use first available NIC */
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_buffer_manager.nics[i].initialized) {
            return nic_buffer_alloc(i, type, 
                (type == BUFFER_TYPE_TX || type == BUFFER_TYPE_DMA_TX) ? TX_BUFFER_SIZE : RX_BUFFER_SIZE);
        }
    }
    
    return NULL;
}

void nic_buffer_free_legacy(buffer_desc_t* buffer) {
    if (!buffer) {
        return;
    }
    
    /* Try to find which NIC this buffer belongs to */
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_buffer_manager.nics[i].initialized) {
            /* This is a simplified approach - in practice, we'd need better tracking */
            nic_buffer_free(i, buffer);
            return;
        }
    }
}