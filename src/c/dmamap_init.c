/**
 * @file dmamap_init.c
 * @brief DMA Mapping Layer - Initialization Functions (OVERLAY Segment)
 *
 * Created: 2026-01-28 08:00:00 UTC
 *
 * This file contains DMA subsystem initialization, configuration, and
 * cleanup functions. These functions are only called during driver
 * startup/shutdown and can be placed in an overlay segment to save
 * memory during normal operation.
 *
 * Functions included:
 * - dma_mapping_init / dma_mapping_shutdown
 * - Coherent memory allocation (dma_alloc / dma_free)
 * - Batch/scatter-gather operations
 * - Statistics and debugging functions
 * - Configuration functions
 *
 * Split from dmamap.c for memory segmentation optimization.
 * Runtime TX/RX functions are in dmamap_rt.c (ROOT segment).
 */

#include "dmamap.h"
#include "dmabnd.h"
#include "cacheche.h"
#include "vds.h"
#include "vds_mapping.h"
#include "pltprob.h"
#include "diag.h"      /* For LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR macros */
#include "portabl.h"   /* C89 compatibility: bool, uint32_t, etc. */
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

/*==============================================================================
 * Global state definitions (extern'd by dmamap_rt.c)
 *==============================================================================*/

/* Global statistics */
dma_mapping_stats_t g_stats = {0};

/* Configuration */
bool g_fast_path_enabled = false;
uint32_t g_cache_hits = 0;
uint32_t g_cache_attempts = 0;

/*==============================================================================
 * Coherent memory tracking structure
 *==============================================================================*/

typedef struct coherent_allocation {
    void *virtual_addr;
    uint32_t physical_addr;
    size_t size;
    size_t alignment;
    struct coherent_allocation *next;
} coherent_allocation_t;

static coherent_allocation_t *g_coherent_allocations = NULL;
static uint32_t g_coherent_allocation_count = 0;

/*==============================================================================
 * Error handling utilities
 *==============================================================================*/

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

/*==============================================================================
 * Initialization and shutdown
 *==============================================================================*/

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
        LOG_WARNING("Shutdown with %u active mappings", g_stats.active_mappings);
    }

    cache_coherency_shutdown();
    dma_shutdown_bounce_pools();

    LOG_INFO("DMA mapping layer shutdown complete");
}

/*==============================================================================
 * Coherent Memory Allocation APIs (GPT-5 Enhancement)
 *==============================================================================*/

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
    void *virtual_addr;
    uint32_t physical_addr;
    coherent_allocation_t *allocation;
    size_t total_size;
    void *raw_ptr;
    uint32_t raw_addr;
    uint32_t aligned_addr;
    dma_check_result_t check_result;

    /* Initialize */
    virtual_addr = NULL;
    physical_addr = 0;
    allocation = NULL;

    if (size == 0 || !phys_addr) {
        LOG_ERROR("DMA alloc: Invalid parameters");
        return NULL;
    }

    /* Validate alignment (must be power of 2) */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        LOG_ERROR("DMA alloc: Invalid alignment %lu", (unsigned long)alignment);
        return NULL;
    }

    /* Minimum alignment for DMA operations */
    if (alignment < 4) {
        alignment = 4;
    }

    LOG_DEBUG("DMA alloc: size=%lu alignment=%lu", (unsigned long)size, (unsigned long)alignment);

    /* Allocate extra space for alignment and tracking */
    total_size = size + alignment + sizeof(void*);
    raw_ptr = malloc(total_size);
    if (!raw_ptr) {
        LOG_ERROR("DMA alloc: Failed to allocate %lu bytes", (unsigned long)total_size);
        return NULL;
    }

    /* GPT-5 A+: Zero the allocated memory like typical allocators */
    memset(raw_ptr, 0, total_size);

    /* Align the pointer - use uint32_t for DOS 16-bit compatibility */
    raw_addr = (uint32_t)(unsigned long)raw_ptr;
    aligned_addr = (raw_addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    virtual_addr = (void*)(unsigned long)aligned_addr;

    /* Store the original pointer before the aligned memory */
    *((void**)(unsigned long)(aligned_addr - sizeof(void*))) = raw_ptr;

    /* Verify the allocation is DMA-safe */
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

    LOG_INFO("DMA alloc: %lu bytes at virt=%p phys=0x%08lX align=%lu (CACHEABLE - requires sync)",
             (unsigned long)size, virtual_addr, (unsigned long)physical_addr, (unsigned long)alignment);

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
    void *original_ptr;
    size_t freed_size;

    if (!addr) {
        return;
    }

    LOG_DEBUG("DMA free: addr=%p size=%lu", addr, (unsigned long)size);

    /* Find the allocation in our tracking list */
    for (current = &g_coherent_allocations; *current; current = &(*current)->next) {
        if ((*current)->virtual_addr == addr) {
            /* Validate size if provided */
            if (size > 0 && (*current)->size != size) {
                LOG_WARNING("DMA coherent free: Size mismatch - expected %lu, got %lu",
                           (unsigned long)(*current)->size, (unsigned long)size);
            }

            to_free = *current;
            freed_size = to_free->size;
            *current = to_free->next;

            /* Retrieve the original malloc pointer */
            original_ptr = *((void**)((uint32_t)(unsigned long)addr - sizeof(void*)));

            LOG_DEBUG("DMA coherent free: freeing original ptr=%p", original_ptr);

            free(original_ptr);
            free(to_free);
            g_coherent_allocation_count--;

            LOG_INFO("DMA coherent free: Released %lu bytes", (unsigned long)freed_size);
            return;
        }
    }

    LOG_ERROR("DMA coherent free: Address %p not found in coherent allocations", addr);
}

/*==============================================================================
 * Batch operations for scatter-gather
 *==============================================================================*/

/* External declarations for runtime functions */
extern void dma_unmap_buffer(dma_mapping_t *mapping);

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
    if (!batch || !mapping || batch->count >= batch->capacity) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }

    batch->mappings[batch->count] = mapping;
    batch->count++;
    batch->total_length += dma_mapping_get_length(mapping);

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

/*==============================================================================
 * Statistics and debugging
 *==============================================================================*/

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

/*==============================================================================
 * Configuration functions
 *==============================================================================*/

void dma_mapping_enable_fast_path(int enable) {
    g_fast_path_enabled = enable ? true : false;
    LOG_INFO("DMA mapping fast path %s", enable ? "enabled" : "disabled");
}

/*==============================================================================
 * Testing and coherency validation
 *==============================================================================*/

int dma_mapping_test_coherency(void *buffer, size_t len) {
    /* Simple coherency test - write pattern, sync, verify */
    uint8_t *test_buf;
    uint8_t pattern;
    size_t i;

    if (!buffer || len == 0) {
        return DMA_MAP_ERROR_INVALID_PARAM;
    }

    test_buf = (uint8_t*)buffer;
    pattern = 0xAA;

    /* Write test pattern */
    for (i = 0; i < len; i++) {
        test_buf[i] = pattern;
    }

    /* Sync for device (flush cache so device sees the writes) */
    cache_sync_for_device(buffer, len);

    /* Sync for CPU (invalidate cache to see device writes) */
    cache_sync_for_cpu(buffer, len);

    /* Verify pattern */
    for (i = 0; i < len; i++) {
        if (test_buf[i] != pattern) {
            return DMA_MAP_ERROR_CACHE;
        }
    }

    return DMA_MAP_SUCCESS;
}

/* External declaration for dma_mapping_get_length from runtime file */
extern size_t dma_mapping_get_length(const dma_mapping_t *mapping);
