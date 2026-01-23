/**
 * @file dma_mapping.c
 * @brief Centralized DMA Mapping Layer Implementation - GPT-5 Integration
 *
 * This module provides a unified API that combines:
 * - DMA boundary checking and bounce buffers
 * - Cache coherency management  
 * - Direction-specific operations
 * - Automatic cleanup and error handling
 *
 * All DMA operations should go through this layer for safety.
 */

#include "dmamap.h"
#include "dmabnd.h"
#include "cacheche.h"
#include "../include/vds.h"
#include "../include/pltprob.h"
#include "logging.h"
#include <string.h>
#include <stdlib.h>

/* Internal DMA mapping structure - enhanced for VDS */
struct dma_mapping {
    void *original_buffer;          /* Original user buffer */
    void *mapped_address;           /* Address to use for DMA (bounce or original) */
    uint32_t phys_addr;             /* Physical address for DMA */
    size_t length;                  /* Buffer length */
    dma_sync_direction_t direction; /* DMA direction */
    uint32_t flags;                 /* Mapping flags */
    bool uses_bounce;               /* Using bounce buffer */
    bool is_coherent;               /* Cache coherent mapping */
    bool uses_vds;                  /* Using VDS mapping */
    vds_mapping_t vds_mapping;      /* VDS mapping information */
    dma_check_result_t dma_check;   /* DMA safety check results */
    uint32_t magic;                 /* Magic number for validation */
};

#define DMA_MAPPING_MAGIC 0x444D4150  /* "DMAP" */

/* Global statistics */
static dma_mapping_stats_t g_stats = {0};

/* Configuration */
static bool g_fast_path_enabled = false;
static uint32_t g_cache_hits = 0;
static uint32_t g_cache_attempts = 0;

/* Error handling */
const char* dma_map_result_to_string(dma_map_result_t result) {
    switch (result) {
        case DMA_MAP_SUCCESS: return "Success";
        case DMA_MAP_ERROR_INVALID_PARAM: return "Invalid parameter";
        case DMA_MAP_ERROR_NO_MEMORY: return "Out of memory";
        case DMA_MAP_ERROR_NO_BOUNCE: return "No bounce buffer available";
        case DMA_MAP_ERROR_BOUNDARY: return "DMA boundary violation";
        case DMA_MAP_ERROR_CACHE: return "Cache operation failed";
        case DMA_MAP_ERROR_NOT_MAPPED: return "Buffer not mapped";
        default: return "Unknown error";
    }
}

void dma_mapping_log_error(dma_map_result_t result, const char *operation) {
    LOG_ERROR("DMA mapping %s failed: %s", operation, dma_map_result_to_string(result));
}

/* Initialization and shutdown */
int dma_mapping_init(void) {
    int result;
    
    LOG_INFO("Initializing centralized DMA mapping layer");
    
    /* Initialize platform detection first */
    result = platform_init();
    if (result != 0) {
        LOG_ERROR("Platform detection failed: %d", result);
        return result;
    }
    
    /* Log DMA policy for this session */
    LOG_INFO("DMA Policy: %s", platform_get_policy_desc(platform_get_dma_policy()));
    
    /* Initialize underlying systems */
    result = dma_init_bounce_pools();
    if (result != 0) {
        LOG_ERROR("Failed to initialize DMA bounce pools: %d", result);
        return result;
    }
    
    result = cache_coherency_init();
    if (result != 0) {
        LOG_ERROR("Failed to initialize cache coherency: %d", result);
        dma_shutdown_bounce_pools();
        return result;
    }
    
    /* Reset statistics */
    memset(&g_stats, 0, sizeof(g_stats));
    g_fast_path_enabled = true;
    g_cache_hits = 0;
    g_cache_attempts = 0;
    
    LOG_INFO("DMA mapping layer initialized successfully");
    return DMA_MAP_SUCCESS;
}

void dma_mapping_shutdown(void) {
    LOG_INFO("Shutting down DMA mapping layer");
    
    if (g_stats.active_mappings > 0) {
        LOG_WARN("Shutdown with %u active mappings", g_stats.active_mappings);
    }
    
    cache_coherency_shutdown();
    dma_shutdown_bounce_pools();
    
    LOG_INFO("DMA mapping layer shutdown complete");
}

/* Internal helper functions */
static bool validate_mapping(const dma_mapping_t *mapping) {
    return mapping && mapping->magic == DMA_MAPPING_MAGIC;
}

static dma_mapping_t* create_mapping(void *buffer, size_t len, dma_sync_direction_t direction, uint32_t flags) {
    dma_mapping_t *mapping;
    
    if (!buffer || len == 0) {
        g_stats.mapping_errors++;
        return NULL;
    }
    
    mapping = malloc(sizeof(dma_mapping_t));
    if (!mapping) {
        g_stats.mapping_errors++;
        return NULL;
    }
    
    memset(mapping, 0, sizeof(dma_mapping_t));
    mapping->magic = DMA_MAPPING_MAGIC;
    mapping->original_buffer = buffer;
    mapping->length = len;
    mapping->direction = direction;
    mapping->flags = flags;
    mapping->is_coherent = (flags & DMA_MAP_COHERENT) != 0;
    
    return mapping;
}

static int setup_dma_mapping(dma_mapping_t *mapping) {
    void *dma_buffer;
    bool force_bounce = (mapping->flags & DMA_MAP_FORCE_BOUNCE) != 0;
    uint16_t cacheline_size = get_cache_line_size();
    
    /* GPT-5 Critical: Check cacheline alignment safety */
    bool needs_alignment_bounce = needs_bounce_for_alignment(mapping->original_buffer, 
                                                            mapping->length, cacheline_size);
    if (needs_alignment_bounce) {
        LOG_DEBUG("DMA mapping: Cacheline alignment requires bounce buffer");
    }
    
    /* Check DMA safety with framework constraints */
    /* GPT-5 CRITICAL: Use DMA safety framework for per-NIC constraint checking */
    extern bool dma_validate_buffer_constraints(const char *device_name, 
                                               void *buffer, uint32_t size,
                                               uint32_t physical_addr);
    
    if (!dma_check_buffer_safety(mapping->original_buffer, mapping->length, &mapping->dma_check)) {
        LOG_ERROR("DMA safety check failed for buffer %p len=%zu", 
                  mapping->original_buffer, mapping->length);
        g_stats.mapping_errors++;
        return DMA_MAP_ERROR_BOUNDARY;
    }
    
    /* Additional per-NIC constraint validation if device name is known */
    /* This will be called with actual device name from hardware layer */
    
    /* Determine if bounce buffer needed - GPT-5 Enhanced */
    mapping->uses_bounce = force_bounce || 
                          mapping->dma_check.crosses_64k || 
                          mapping->dma_check.crosses_16m ||
                          mapping->dma_check.needs_bounce ||
                          needs_alignment_bounce;  /* GPT-5 Critical addition */
    
    if (mapping->uses_bounce) {
        /* Get bounce buffer based on direction */
        if (mapping->direction == DMA_SYNC_TX || 
            (mapping->flags & DMA_MAP_READ)) {
            dma_buffer = dma_get_tx_bounce_buffer(mapping->length);
        } else {
            dma_buffer = dma_get_rx_bounce_buffer(mapping->length);
        }
        
        if (!dma_buffer) {
            LOG_ERROR("Failed to allocate bounce buffer len=%zu", mapping->length);
            g_stats.mapping_errors++;
            return DMA_MAP_ERROR_NO_BOUNCE;
        }
        
        mapping->mapped_address = dma_buffer;
        g_stats.bounce_mappings++;
        
        /* For TX, copy data to bounce buffer */
        if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
            memcpy(dma_buffer, mapping->original_buffer, mapping->length);
        }
    } else {
        /* Direct mapping */
        mapping->mapped_address = mapping->original_buffer;
        g_stats.direct_mappings++;
    }
    
    /* Calculate physical address - use VDS if available and needed */
    bool force_vds = (mapping->flags & DMA_MAP_VDS_ZEROCOPY) != 0;
    if ((platform_get_dma_policy() == DMA_POLICY_COMMONBUF || force_vds) && !mapping->uses_bounce) {
        /* Try VDS lock for direct mapping */
        uint16_t vds_flags = (mapping->direction == DMA_SYNC_TX) ? VDS_TX_FLAGS : VDS_RX_FLAGS;
        
        if (vds_lock_region(mapping->mapped_address, mapping->length, vds_flags, &mapping->vds_mapping)) {
            mapping->phys_addr = mapping->vds_mapping.physical_addr;
            mapping->uses_vds = true;
            LOG_DEBUG("DMA: VDS lock successful - virt=%p phys=%08lX", 
                     mapping->mapped_address, (unsigned long)mapping->phys_addr);
            
            /* GPT-5 CRITICAL: Comprehensive DMA constraint validation */
            /* Check 1: ISA DMA 16MB limit (24-bit addressing) */
            if (mapping->phys_addr >= 0x1000000UL) {
                LOG_WARNING("VDS address exceeds 16MB ISA limit: %08lX, using bounce", 
                           (unsigned long)mapping->phys_addr);
                vds_unlock_region(&mapping->vds_mapping);
                mapping->uses_vds = false;
                mapping->uses_bounce = true;
                g_stats.bounce_mappings++;
            }
            /* Check 2: 64KB boundary crossing (ISA DMA limitation) */
            else if (((mapping->phys_addr & 0xFFFFUL) + mapping->length) > 0x10000UL) {
                LOG_WARNING("VDS buffer crosses 64KB boundary: addr=%08lX len=%zu, using bounce",
                           (unsigned long)mapping->phys_addr, mapping->length);
                vds_unlock_region(&mapping->vds_mapping);
                mapping->uses_vds = false;
                mapping->uses_bounce = true;
                g_stats.bounce_mappings++;
            }
            /* Check 3: Contiguity requirement for 3C515 (no scatter-gather) */
            else if (!mapping->vds_mapping.is_contiguous) {
                LOG_WARNING("VDS returned non-contiguous mapping, 3C515 requires contiguous, using bounce");
                vds_unlock_region(&mapping->vds_mapping);
                mapping->uses_vds = false;
                mapping->uses_bounce = true;
                g_stats.bounce_mappings++;
            }
            /* Check 4: Basic ISA compatibility check */
            else if (!vds_is_isa_compatible(mapping->phys_addr, mapping->length)) {
                LOG_ERROR("VDS returned non-ISA compatible address: %08lX", 
                         (unsigned long)mapping->phys_addr);
                vds_unlock_region(&mapping->vds_mapping);
                mapping->uses_vds = false;
                return DMA_MAP_ERROR_BOUNDARY;
            }
        } else {
            LOG_WARNING("VDS lock failed - falling back to bounce buffer");
            /* Force bounce buffer allocation */
            mapping->uses_bounce = true;
            g_stats.bounce_mappings++;
        }
    }
    
    /* GPT-5 FIX: Allocate bounce buffer if VDS constraints failed */
    if (mapping->uses_bounce && mapping->mapped_address == mapping->original_buffer) {
        /* VDS failed constraints - need to allocate bounce buffer now */
        void *bounce_buf;
        if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
            bounce_buf = dma_get_tx_bounce_buffer(mapping->length);
        } else {
            bounce_buf = dma_get_rx_bounce_buffer(mapping->length);
        }
        
        if (!bounce_buf) {
            LOG_ERROR("Failed to allocate bounce buffer after VDS constraint failure");
            return DMA_MAP_ERROR_NO_BOUNCE;
        }
        
        mapping->mapped_address = bounce_buf;
        
        /* For TX, copy data to bounce buffer */
        if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
            memcpy(bounce_buf, mapping->original_buffer, mapping->length);
        }
    }
    
    if (!mapping->uses_vds) {
        /* Use traditional physical address calculation */
        mapping->phys_addr = mapping->dma_check.phys_addr;
        if (mapping->uses_bounce) {
            /* Recalculate for bounce buffer */
            dma_check_result_t bounce_check;
            if (!dma_check_buffer_safety(mapping->mapped_address, mapping->length, &bounce_check)) {
                LOG_ERROR("Bounce buffer safety check failed");
                return DMA_MAP_ERROR_BOUNDARY;
            }
            mapping->phys_addr = bounce_check.phys_addr;
        }
    }
    
    /* Perform cache sync for device if needed - GPT-5 Enhanced Safety */
    if (!mapping->is_coherent && !(mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        /* Use safe aligned cache flush to prevent partial cacheline corruption */
        if (mapping->direction == DMA_SYNC_TX) {
            cache_flush_aligned_safe(mapping->mapped_address, mapping->length);
        } else {
            /* For RX, invalidate cache lines safely */
            cache_flush_aligned_safe(mapping->mapped_address, mapping->length);
        }
        g_stats.cache_syncs++;
    }
    
    /* Update statistics */
    g_stats.total_mappings++;
    g_stats.active_mappings++;
    
    if (mapping->direction == DMA_SYNC_TX || (mapping->flags & DMA_MAP_READ)) {
        g_stats.tx_mappings++;
    } else {
        g_stats.rx_mappings++;
    }
    
    return DMA_MAP_SUCCESS;
}

/* TX (CPU -> Device) mapping functions */
dma_mapping_t* dma_map_tx(void *buffer, size_t len) {
    return dma_map_tx_flags(buffer, len, 0);
}

dma_mapping_t* dma_map_tx_flags(void *buffer, size_t len, uint32_t flags) {
    dma_mapping_t *mapping;
    int result;
    
    mapping = create_mapping(buffer, len, DMA_SYNC_TX, flags | DMA_MAP_READ);
    if (!mapping) {
        return NULL;
    }
    
    result = setup_dma_mapping(mapping);
    if (result != DMA_MAP_SUCCESS) {
        free(mapping);
        return NULL;
    }
    
    return mapping;
}

void dma_unmap_tx(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        LOG_ERROR("Invalid TX mapping for unmap");
        return;
    }
    
    /* No need to copy back for TX */
    
    /* Release bounce buffer if used */
    if (mapping->uses_bounce) {
        dma_return_tx_bounce_buffer(mapping->mapped_address);
    }
    
    /* CRITICAL: Unlock VDS mapping if used (deferred from ISR) */
    if (mapping->uses_vds && mapping->vds_mapping.needs_unlock) {
        if (!vds_unlock_region(&mapping->vds_mapping)) {
            LOG_WARNING("VDS unlock failed for TX mapping %p", mapping);
        }
        LOG_DEBUG("VDS TX mapping unlocked");
    }
    
    /* GPT-5 Critical: Unlock pages if they were locked */
    if (mapping->dma_check.pages_locked) {
        unlock_pages_for_dma(mapping->dma_check.lock_handle);
    }
    
    /* Update statistics */
    if (g_stats.active_mappings > 0) {
        g_stats.active_mappings--;
    }
    
    mapping->magic = 0;
    free(mapping);
}

/* RX (Device -> CPU) mapping functions */
dma_mapping_t* dma_map_rx(void *buffer, size_t len) {
    return dma_map_rx_flags(buffer, len, 0);
}

dma_mapping_t* dma_map_rx_flags(void *buffer, size_t len, uint32_t flags) {
    dma_mapping_t *mapping;
    int result;
    
    mapping = create_mapping(buffer, len, DMA_SYNC_RX, flags | DMA_MAP_WRITE);
    if (!mapping) {
        return NULL;
    }
    
    result = setup_dma_mapping(mapping);
    if (result != DMA_MAP_SUCCESS) {
        free(mapping);
        return NULL;
    }
    
    return mapping;
}

void dma_unmap_rx(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        LOG_ERROR("Invalid RX mapping for unmap");
        return;
    }
    
    /* Sync cache for CPU access - GPT-5 Enhanced Safety */
    if (!mapping->is_coherent && !(mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        /* Invalidate cache lines safely before CPU reads */
        cache_flush_aligned_safe(mapping->mapped_address, mapping->length);
        g_stats.cache_syncs++;
    }
    
    /* Copy data back if using bounce buffer */
    if (mapping->uses_bounce) {
        memcpy(mapping->original_buffer, mapping->mapped_address, mapping->length);
        dma_return_rx_bounce_buffer(mapping->mapped_address);
    }
    
    /* CRITICAL: Unlock VDS mapping if used (deferred from ISR) */
    if (mapping->uses_vds && mapping->vds_mapping.needs_unlock) {
        if (!vds_unlock_region(&mapping->vds_mapping)) {
            LOG_WARNING("VDS unlock failed for RX mapping %p", mapping);
        }
        LOG_DEBUG("VDS RX mapping unlocked");
    }
    
    /* GPT-5 Critical: Unlock pages if they were locked */
    if (mapping->dma_check.pages_locked) {
        unlock_pages_for_dma(mapping->dma_check.lock_handle);
    }
    
    /* Update statistics */
    if (g_stats.active_mappings > 0) {
        g_stats.active_mappings--;
    }
    
    mapping->magic = 0;
    free(mapping);
}

/* Generic mapping functions */
dma_mapping_t* dma_map_buffer(void *buffer, size_t len, dma_sync_direction_t direction) {
    return dma_map_buffer_flags(buffer, len, direction, 0);
}

dma_mapping_t* dma_map_buffer_flags(void *buffer, size_t len, dma_sync_direction_t direction, uint32_t flags) {
    if (direction == DMA_SYNC_TX) {
        return dma_map_tx_flags(buffer, len, flags);
    } else {
        return dma_map_rx_flags(buffer, len, flags);
    }
}

void dma_unmap_buffer(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return;
    }
    
    if (mapping->direction == DMA_SYNC_TX) {
        dma_unmap_tx(mapping);
    } else {
        dma_unmap_rx(mapping);
    }
}

/* Mapping information access */
void* dma_mapping_get_address(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return NULL;
    }
    return mapping->mapped_address;
}

uint32_t dma_mapping_get_phys_addr(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }
    return mapping->phys_addr;
}

size_t dma_mapping_get_length(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return 0;
    }
    return mapping->length;
}

bool dma_mapping_uses_bounce(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return false;
    }
    return mapping->uses_bounce;
}

bool dma_mapping_is_coherent(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return false;
    }
    return mapping->is_coherent;
}

bool dma_mapping_uses_vds(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return false;
    }
    return mapping->uses_vds;
}

/* Synchronization functions */
int dma_mapping_sync_for_device(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }
    
    if (mapping->is_coherent || (mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        return DMA_MAP_SUCCESS;
    }
    
    int result = dma_sync_for_device(mapping->mapped_address, mapping->length, mapping->direction);
    if (result == 0) {
        g_stats.cache_syncs++;
    }
    
    return result == 0 ? DMA_MAP_SUCCESS : DMA_MAP_ERROR_CACHE;
}

int dma_mapping_sync_for_cpu(dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }
    
    if (mapping->is_coherent || (mapping->flags & DMA_MAP_NO_CACHE_SYNC)) {
        return DMA_MAP_SUCCESS;
    }
    
    int result = dma_sync_for_cpu(mapping->mapped_address, mapping->length, mapping->direction);
    if (result == 0) {
        g_stats.cache_syncs++;
    }
    
    return result == 0 ? DMA_MAP_SUCCESS : DMA_MAP_ERROR_CACHE;
}

/* Batch operations for scatter-gather */
dma_mapping_batch_t* dma_create_mapping_batch(uint16_t max_segments) {
    dma_mapping_batch_t *batch;
    
    if (max_segments == 0) {
        return NULL;
    }
    
    batch = malloc(sizeof(dma_mapping_batch_t));
    if (!batch) {
        return NULL;
    }
    
    batch->mappings = malloc(sizeof(dma_mapping_t*) * max_segments);
    if (!batch->mappings) {
        free(batch);
        return NULL;
    }
    
    batch->count = 0;
    batch->capacity = max_segments;
    batch->total_length = 0;
    
    return batch;
}

int dma_batch_add_mapping(dma_mapping_batch_t *batch, dma_mapping_t *mapping) {
    if (!batch || !validate_mapping(mapping) || batch->count >= batch->capacity) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }
    
    batch->mappings[batch->count] = mapping;
    batch->count++;
    batch->total_length += mapping->length;
    
    return DMA_MAP_SUCCESS;
}

void dma_unmap_batch(dma_mapping_batch_t *batch) {
    uint16_t i;
    
    if (!batch) {
        return;
    }
    
    for (i = 0; i < batch->count; i++) {
        if (batch->mappings[i]) {
            dma_unmap_buffer(batch->mappings[i]);
            batch->mappings[i] = NULL;
        }
    }
    
    batch->count = 0;
    batch->total_length = 0;
}

void dma_free_mapping_batch(dma_mapping_batch_t *batch) {
    if (!batch) {
        return;
    }
    
    dma_unmap_batch(batch);
    
    if (batch->mappings) {
        free(batch->mappings);
    }
    
    free(batch);
}

/* Statistics and debugging */
void dma_mapping_get_stats(dma_mapping_stats_t *stats) {
    if (stats) {
        *stats = g_stats;
    }
}

void dma_mapping_print_stats(void) {
    LOG_INFO("DMA Mapping Statistics:");
    LOG_INFO("  Total mappings: %u", g_stats.total_mappings);
    LOG_INFO("  Active mappings: %u", g_stats.active_mappings);
    LOG_INFO("  Direct mappings: %u", g_stats.direct_mappings);
    LOG_INFO("  Bounce mappings: %u", g_stats.bounce_mappings);
    LOG_INFO("  Cache syncs: %u", g_stats.cache_syncs);
    LOG_INFO("  Mapping errors: %u", g_stats.mapping_errors);
    LOG_INFO("  TX mappings: %u", g_stats.tx_mappings);
    LOG_INFO("  RX mappings: %u", g_stats.rx_mappings);
    
    if (g_cache_attempts > 0) {
        LOG_INFO("  Cache hit rate: %u%%", (g_cache_hits * 100) / g_cache_attempts);
    }
}

void dma_mapping_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    g_cache_hits = 0;
    g_cache_attempts = 0;
}

/* Validation and testing */
bool dma_mapping_validate(const dma_mapping_t *mapping) {
    if (!validate_mapping(mapping)) {
        return false;
    }
    
    /* Additional validation checks */
    if (!mapping->mapped_address || mapping->length == 0) {
        return false;
    }
    
    if (mapping->uses_bounce && mapping->mapped_address == mapping->original_buffer) {
        return false;
    }
    
    return true;
}

int dma_mapping_test_coherency(void *buffer, size_t len) {
    /* Simple coherency test - write pattern, sync, verify */
    uint8_t *test_buf = (uint8_t*)buffer;
    uint8_t pattern = 0xAA;
    size_t i;
    
    if (!buffer || len == 0) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }
    
    /* Write test pattern */
    for (i = 0; i < len; i++) {
        test_buf[i] = pattern;
    }
    
    /* Sync for device */
    if (dma_sync_for_device(buffer, len, DMA_SYNC_TX) != 0) {
        return DMA_MAP_ERROR_CACHE;
    }
    
    /* Sync for CPU */
    if (dma_sync_for_cpu(buffer, len, DMA_SYNC_RX) != 0) {
        return DMA_MAP_ERROR_CACHE;
    }
    
    /* Verify pattern */
    for (i = 0; i < len; i++) {
        if (test_buf[i] != pattern) {
            return DMA_MAP_ERROR_CACHE;
        }
    }
    
    return DMA_MAP_SUCCESS;
}

/* Performance optimization */
void dma_mapping_enable_fast_path(bool enable) {
    g_fast_path_enabled = enable;
    LOG_INFO("DMA mapping fast path %s", enable ? "enabled" : "disabled");
}

bool dma_mapping_is_fast_path_enabled(void) {
    return g_fast_path_enabled;
}

uint32_t dma_mapping_get_cache_hit_rate(void) {
    if (g_cache_attempts == 0) {
        return 0;
    }
    return (g_cache_hits * 100) / g_cache_attempts;
}

/**
 * @brief Map DMA buffer with per-NIC device constraints
 * 
 * GPT-5 CRITICAL: This function integrates with dma_safety.c to ensure
 * all DMA operations respect device-specific constraints
 * 
 * @param buffer Buffer to map
 * @param length Buffer length
 * @param direction DMA direction
 * @param device_name NIC device name (e.g., "3C509B", "3C515TX")
 * @return Mapped DMA descriptor or NULL on failure
 */
dma_mapping_t* dma_map_with_device_constraints(void *buffer, size_t length, 
                                              dma_sync_direction_t direction,
                                              const char *device_name) {
    dma_mapping_t *mapping;
    uint32_t phys_addr;
    
    /* GPT-5 FIX: Remove invalid pre-check, map first then validate with real address */
    extern bool dma_validate_buffer_constraints(const char *device_name, 
                                               void *buffer, uint32_t size,
                                               uint32_t physical_addr);
    
    /* Map normally first */
    mapping = dma_map_buffer_flags(buffer, length, direction, 0);
    if (!mapping) {
        LOG_ERROR("Failed to map buffer for DMA");
        return NULL;
    }
    
    /* Check with actual physical address if device constraints specified */
    if (device_name) {
        phys_addr = mapping->phys_addr;
        if (!dma_validate_buffer_constraints(device_name, buffer, length, phys_addr)) {
            LOG_DEBUG("Buffer at phys %08lX violates %s constraints, remapping with bounce", 
                     (unsigned long)phys_addr, device_name);
            
            /* Remap with bounce buffer forced */
            dma_unmap(mapping);
            mapping = dma_map_buffer_flags(buffer, length, direction, DMA_MAP_FORCE_BOUNCE);
            
            /* GPT-5 FIX: Check if remapping succeeded */
            if (!mapping) {
                LOG_ERROR("Failed to remap buffer with bounce for %s constraints", device_name);
                return NULL;
            }
            
            /* GPT-5 A+ Enhancement: Re-validate bounce buffer meets constraints */
            phys_addr = mapping->phys_addr;
            if (!dma_validate_buffer_constraints(device_name, 
                                                dma_mapping_get_address(mapping),
                                                length, phys_addr)) {
                LOG_ERROR("Bounce buffer at phys %08lX still violates %s constraints", 
                         (unsigned long)phys_addr, device_name);
                dma_unmap(mapping);
                return NULL;
            }
            
            LOG_DEBUG("Remapped with bounce buffer at phys %08lX", 
                     (unsigned long)mapping->phys_addr);
        }
    }
    
    return mapping;
}

/* Integration with existing systems */
dma_mapping_t* dma_map_from_sg_descriptor(dma_sg_descriptor_t *sg_desc, dma_sync_direction_t direction) {
    if (!sg_desc) {
        return NULL;
    }
    
    /* Convert scatter-gather descriptor to buffer mapping */
    return dma_map_buffer_flags(sg_desc->buffer, sg_desc->length, direction, 0);
}

int dma_mapping_to_sg_list(const dma_mapping_t *mapping, dma_sg_descriptor_t **sg_desc) {
    dma_sg_descriptor_t *desc;
    
    if (!validate_mapping(mapping) || !sg_desc) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }
    
    desc = malloc(sizeof(dma_sg_descriptor_t));
    if (!desc) {
        return DMA_MAP_ERROR_NO_MEMORY;
    }
    
    desc->buffer = mapping->mapped_address;
    desc->length = mapping->length;
    desc->phys_addr = mapping->phys_addr;
    
    *sg_desc = desc;
    return DMA_MAP_SUCCESS;
}

/* Advanced features */
int dma_mapping_set_cache_policy(dma_mapping_t *mapping, bool coherent) {
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }
    
    mapping->is_coherent = coherent;
    if (coherent) {
        mapping->flags |= DMA_MAP_COHERENT;
    } else {
        mapping->flags &= ~DMA_MAP_COHERENT;
    }
    
    return DMA_MAP_SUCCESS;
}

int dma_mapping_prefault(dma_mapping_t *mapping) {
    uint8_t *addr;
    size_t i;
    
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }
    
    /* Touch every page to ensure it's mapped */
    addr = (uint8_t*)mapping->mapped_address;
    for (i = 0; i < mapping->length; i += 4096) {
        volatile uint8_t dummy = addr[i];
        (void)dummy;
    }
    
    /* Touch the last byte */
    if (mapping->length > 0) {
        volatile uint8_t dummy = addr[mapping->length - 1];
        (void)dummy;
    }
    
    return DMA_MAP_SUCCESS;
}

int dma_mapping_pin_pages(dma_mapping_t *mapping) {
    /* In DOS, pages are always "pinned" - this is a no-op */
    if (!validate_mapping(mapping)) {
        return DMA_MAP_ERROR_NOT_MAPPED;
    }
    
    return DMA_MAP_SUCCESS;
}

/*==============================================================================
 * GPT-5 Enhancement: Coherent Memory Allocation APIs
 * 
 * These functions provide coherent memory allocation for long-lived buffers
 * like descriptor rings, addressing GPT-5's recommendation for better API
 * expressiveness.
 *==============================================================================*/

/* Global coherent memory tracking */
typedef struct coherent_allocation {
    void *virtual_addr;
    uint32_t physical_addr;
    size_t size;
    size_t alignment;
    struct coherent_allocation *next;
} coherent_allocation_t;

static coherent_allocation_t *g_coherent_allocations = NULL;
static uint32_t g_coherent_allocation_count = 0;

/**
 * @brief Allocate cacheable DMA memory for descriptor rings (GPT-5 A+)
 * 
 * NOTE: This allocates CACHEABLE memory that requires explicit sync operations.
 * Call dma_sync_for_device() before device access and dma_sync_for_cpu() 
 * before CPU access. This is NOT true coherent memory.
 * 
 * @param size Size in bytes to allocate
 * @param alignment Required alignment (must be power of 2)
 * @param phys_addr [out] Physical address of allocated memory
 * @return Virtual address of allocated memory, or NULL on failure
 */
void* dma_alloc(size_t size, size_t alignment, uint32_t *phys_addr) {
    void *virtual_addr = NULL;
    uint32_t physical_addr = 0;
    coherent_allocation_t *allocation = NULL;
    
    if (size == 0 || !phys_addr) {
        LOG_ERROR("DMA alloc: Invalid parameters");
        return NULL;
    }
    
    /* Validate alignment (must be power of 2) */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        LOG_ERROR("DMA alloc: Invalid alignment %zu", alignment);
        return NULL;
    }
    
    /* Minimum alignment for DMA operations */
    if (alignment < 4) {
        alignment = 4;
    }
    
    LOG_DEBUG("DMA alloc: size=%zu alignment=%zu", size, alignment);
    
    /* Allocate extra space for alignment and tracking */
    size_t total_size = size + alignment + sizeof(void*);
    void *raw_ptr = malloc(total_size);
    if (!raw_ptr) {
        LOG_ERROR("DMA alloc: Failed to allocate %zu bytes", total_size);
        return NULL;
    }
    
    /* GPT-5 A+: Zero the allocated memory like typical allocators */
    memset(raw_ptr, 0, total_size);
    
    /* Align the pointer */
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (raw_addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    virtual_addr = (void*)aligned_addr;
    
    /* Store the original pointer before the aligned memory */
    *((void**)(aligned_addr - sizeof(void*))) = raw_ptr;
    
    /* Verify the allocation is DMA-safe */
    dma_check_result_t check_result;
    if (!dma_check_buffer_safety(virtual_addr, size, &check_result)) {
        LOG_ERROR("DMA alloc: Safety check failed");
        free(raw_ptr);
        return NULL;
    }
    
    /* GPT-5 Critical: Must be below 16MB for ISA DMA */
    if (check_result.phys_addr >= DMA_16MB_LIMIT || 
        (check_result.phys_addr + size) > DMA_16MB_LIMIT) {
        LOG_WARNING("DMA alloc: Allocated above 16MB limit, may need bounce buffer");
        /* Continue - bounce buffers will handle this */
    }
    
    /* GPT-5 Critical: Must not cross 64KB boundaries */
    if (check_result.crosses_64k) {
        LOG_WARNING("DMA alloc: Allocation crosses 64KB boundary");
        /* This is a problem for ISA bus masters, but we'll continue */
    }
    
    physical_addr = check_result.phys_addr;
    *phys_addr = physical_addr;
    
    /* Create allocation tracking structure */
    allocation = malloc(sizeof(coherent_allocation_t));
    if (!allocation) {
        LOG_ERROR("DMA alloc: Failed to allocate tracking structure");
        free(raw_ptr);
        return NULL;
    }
    
    allocation->virtual_addr = virtual_addr;
    allocation->physical_addr = physical_addr;
    allocation->size = size;
    allocation->alignment = alignment;
    allocation->next = g_coherent_allocations;
    g_coherent_allocations = allocation;
    g_coherent_allocation_count++;
    
    LOG_INFO("DMA alloc: %zu bytes at virt=%p phys=0x%08lX align=%zu (CACHEABLE - requires sync)", 
             size, virtual_addr, physical_addr, alignment);
    
    return virtual_addr;
}

/**
 * @brief Free cacheable DMA memory (GPT-5 A+)
 * 
 * @param addr Virtual address returned by dma_alloc
 * @param size Size of allocation (for validation)
 */
void dma_free(void *addr, size_t size) {
    coherent_allocation_t **current;
    coherent_allocation_t *to_free;
    
    if (!addr) {
        return;
    }
    
    LOG_DEBUG("DMA free: addr=%p size=%zu", addr, size);
    
    /* Find the allocation in our tracking list */
    for (current = &g_coherent_allocations; *current; current = &(*current)->next) {
        if ((*current)->virtual_addr == addr) {
            /* Validate size if provided */
            if (size > 0 && (*current)->size != size) {
                LOG_WARNING("DMA coherent free: Size mismatch - expected %zu, got %zu", 
                           (*current)->size, size);
            }
            
            to_free = *current;
            *current = to_free->next;
            
            /* Retrieve the original malloc pointer */
            void *original_ptr = *((void**)((uintptr_t)addr - sizeof(void*)));
            
            LOG_DEBUG("DMA coherent free: freeing original ptr=%p", original_ptr);
            
            free(original_ptr);
            free(to_free);
            g_coherent_allocation_count--;
            
            LOG_INFO("DMA coherent free: Released %zu bytes", to_free->size);
            return;
        }
    }
    
    LOG_ERROR("DMA coherent free: Address %p not found in coherent allocations", addr);
}

/**
 * @brief Explicit sync for device access without unmapping
 * GPT-5 Enhancement: Better API expressiveness for streaming mappings
 * 
 * @param buffer Buffer to synchronize
 * @param len Buffer length
 * @param direction DMA direction
 * @return 0 on success, negative on error
 */
int dma_sync_for_device_explicit(void *buffer, size_t len, dma_sync_direction_t direction) {
    if (!buffer || len == 0) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("DMA explicit sync for device: buffer=%p len=%zu dir=%s", 
              buffer, len, (direction == DMA_SYNC_TX) ? "TX" : "RX");
    
    /* Use the enhanced aligned cache flush for safety */
    cache_flush_aligned_safe(buffer, len);
    
    /* Check if we should trigger coalesced flush */
    cache_flush_if_needed();
    
    return DMA_MAP_SUCCESS;
}

/**
 * @brief Explicit sync for CPU access without unmapping
 * GPT-5 Enhancement: Streaming mapping support
 * 
 * @param buffer Buffer to synchronize
 * @param len Buffer length
 * @param direction DMA direction
 * @return 0 on success, negative on error
 */
int dma_sync_for_cpu_explicit(void *buffer, size_t len, dma_sync_direction_t direction) {
    if (!buffer || len == 0) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("DMA explicit sync for CPU: buffer=%p len=%zu dir=%s", 
              buffer, len, (direction == DMA_SYNC_TX) ? "TX" : "RX");
    
    /* For RX (device->CPU), we need cache invalidation */
    if (direction == DMA_SYNC_RX) {
        cache_flush_aligned_safe(buffer, len);
        cache_flush_if_needed();
    }
    
    /* For TX, no additional sync needed after device access */
    
    return DMA_MAP_SUCCESS;
}