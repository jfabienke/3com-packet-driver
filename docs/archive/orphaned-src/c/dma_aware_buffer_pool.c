/**
 * @file dma_aware_buffer_pool.c
 * @brief DMA-aware buffer pool management with memory manager detection
 * 
 * Addresses GPT-5's critical feedback on UMB DMA safety by using our existing
 * memory manager detection to make intelligent buffer allocation decisions.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "dma_aware_buffer_pool.h"
#include "platform_probe.h"
#include "vds_manager.h"
#include "memory.h"
#include "logging.h"

/* Buffer pool types with DMA safety awareness */
typedef enum {
    POOL_DMA_SAFE = 0,      /* DMA-safe buffers (conventional memory) */
    POOL_COPY_ONLY = 1,     /* Copy-only buffers (UMB allowed) */
    POOL_METADATA = 2,      /* Metadata pools (UMB preferred) */
    POOL_TYPES = 3
} pool_category_t;

/* Enhanced buffer pool with DMA awareness */
struct dma_buffer_pool {
    /* Basic pool properties */
    void **free_list;
    uint8_t *buffer_memory;
    uint16_t buffer_size;
    uint16_t total_count;
    uint16_t free_count;
    
    /* DMA safety properties */
    bool dma_safe;              /* Buffers can be used for DMA */
    bool in_umb;                /* Pool allocated in UMB */
    uint32_t physical_base;     /* Physical address if known */
    
    /* Memory manager compatibility */
    bool vds_locked;            /* VDS lock acquired */
    uint32_t vds_handle;        /* VDS lock handle */
    
    /* Statistics */
    uint32_t allocations;
    uint32_t dma_allocations;   /* DMA-specific allocations */
    uint32_t copy_allocations;  /* Copy-specific allocations */
    uint32_t failures;
    uint32_t dma_failures;      /* DMA allocation failures */
};

/* Memory manager-aware configuration */
struct memory_manager_config {
    bool umb_safe_for_dma;      /* UMB can be used for DMA */
    bool requires_vds_lock;     /* DMA buffers need VDS locking */
    bool prefer_conventional;   /* Prefer conventional for DMA */
    bool umb_available;         /* UMB is available */
    const char *manager_name;   /* Memory manager name */
};

/* Global pools organized by category */
static struct dma_buffer_pool pools[POOL_TYPES][4];  /* 4 size categories each */
static struct memory_manager_config mem_config = {0};
static bool pools_initialized = false;

/* Buffer size categories */
#define SMALL_BUFFER    256
#define MEDIUM_BUFFER   512
#define LARGE_BUFFER    1536
#define JUMBO_BUFFER    2048

/**
 * Analyze memory manager environment for DMA safety
 */
static int analyze_memory_manager_environment(void)
{
    platform_probe_result_t platform = platform_detect();
    
    /* Clear configuration */
    memset(&mem_config, 0, sizeof(mem_config));
    
    if (platform.vds_available) {
        /* VDS available - safest option */
        mem_config.umb_safe_for_dma = false;    /* Use conventional + VDS */
        mem_config.requires_vds_lock = true;
        mem_config.prefer_conventional = true;
        mem_config.umb_available = true;        /* For copy-only buffers */
        mem_config.manager_name = "VDS-enabled";
        
        LOG_INFO("DMA strategy: VDS-based (conventional memory + VDS locking)");
        return 0;
    }
    
    /* No VDS - analyze specific memory managers */
    if (platform.emm386_detected) {
        /* EMM386 creates paged UMBs - never safe for DMA */
        mem_config.umb_safe_for_dma = false;
        mem_config.requires_vds_lock = false;
        mem_config.prefer_conventional = true;
        mem_config.umb_available = true;        /* For copies only */
        mem_config.manager_name = "EMM386";
        
        LOG_WARNING("DMA strategy: EMM386 detected - UMB unsafe for DMA");
        LOG_INFO("  DMA buffers: conventional memory only");
        LOG_INFO("  Copy buffers: UMB allowed");
        return 0;
    }
    
    if (platform.qemm_detected) {
        /* QEMM may create paged UMBs - be conservative */
        mem_config.umb_safe_for_dma = false;
        mem_config.requires_vds_lock = false;
        mem_config.prefer_conventional = true;
        mem_config.umb_available = true;
        mem_config.manager_name = "QEMM";
        
        LOG_WARNING("DMA strategy: QEMM detected - UMB unsafe for DMA (conservative)");
        return 0;
    }
    
    if (platform.windows_enhanced) {
        /* Windows Enhanced mode - definitely unsafe */
        mem_config.umb_safe_for_dma = false;
        mem_config.requires_vds_lock = false;
        mem_config.prefer_conventional = true;
        mem_config.umb_available = false;       /* Windows may control UMB */
        mem_config.manager_name = "Windows Enhanced";
        
        LOG_WARNING("DMA strategy: Windows Enhanced mode - UMB unsafe");
        return 0;
    }
    
    /* Check for HIMEM-only environment */
    bool xms_present = detect_xms_services();
    if (xms_present && !platform.vcpi_present && 
        !platform.emm386_detected && !platform.qemm_detected) {
        /* HIMEM-only: UMB may be safe if it's ROM shadowing */
        mem_config.umb_safe_for_dma = false;    /* Still conservative */
        mem_config.requires_vds_lock = false;
        mem_config.prefer_conventional = true;
        mem_config.umb_available = true;
        mem_config.manager_name = "HIMEM-only";
        
        LOG_INFO("DMA strategy: HIMEM-only detected - conservative UMB policy");
        return 0;
    }
    
    /* Pure DOS environment */
    mem_config.umb_safe_for_dma = false;        /* No UMB in pure DOS */
    mem_config.requires_vds_lock = false;
    mem_config.prefer_conventional = true;
    mem_config.umb_available = false;
    mem_config.manager_name = "Pure DOS";
    
    LOG_INFO("DMA strategy: Pure DOS - conventional memory only");
    return 0;
}

/**
 * Allocate DMA-safe buffer memory
 */
static void *allocate_dma_safe_memory(uint32_t size, bool *is_umb)
{
    void *memory = NULL;
    *is_umb = false;
    
    /* Always prefer conventional memory for DMA-safe pools */
    memory = malloc_conventional(size);
    if (memory) {
        LOG_DEBUG("Allocated %lu bytes in conventional memory (DMA-safe)", size);
        return memory;
    }
    
    /* If conventional memory exhausted, check policy */
    if (mem_config.umb_safe_for_dma && mem_config.umb_available) {
        /* Only if explicitly safe (very rare) */
        memory = alloc_umb_dos_api(size);
        if (memory) {
            *is_umb = true;
            LOG_WARNING("Allocated %lu bytes in UMB (DMA-safe by policy)", size);
            return memory;
        }
    }
    
    LOG_ERROR("Failed to allocate %lu bytes of DMA-safe memory", size);
    return NULL;
}

/**
 * Allocate copy-only buffer memory (UMB preferred)
 */
static void *allocate_copy_memory(uint32_t size, bool *is_umb)
{
    void *memory = NULL;
    *is_umb = false;
    
    /* Try UMB first for copy-only buffers */
    if (mem_config.umb_available) {
        memory = alloc_umb_dos_api(size);
        if (memory) {
            *is_umb = true;
            LOG_DEBUG("Allocated %lu bytes in UMB (copy-only)", size);
            return memory;
        }
    }
    
    /* Fall back to conventional memory */
    memory = malloc_conventional(size);
    if (memory) {
        LOG_DEBUG("Allocated %lu bytes in conventional memory (copy fallback)", size);
        return memory;
    }
    
    LOG_ERROR("Failed to allocate %lu bytes of copy memory", size);
    return NULL;
}

/**
 * Lock buffer for DMA if required
 */
static int lock_buffer_for_dma(struct dma_buffer_pool *pool)
{
    if (!mem_config.requires_vds_lock || pool->vds_locked) {
        return 0;  /* No locking required or already locked */
    }
    
    if (!vds_is_available()) {
        LOG_ERROR("VDS locking required but VDS not available");
        return -1;
    }
    
    /* Calculate buffer region size */
    uint32_t region_size = pool->total_count * pool->buffer_size;
    
    /* Lock via VDS */
    vds_lock_result_t lock_result = vds_lock_region(
        (uint32_t)pool->buffer_memory, region_size, VDS_LOCK_DMA_BUFFER);
    
    if (lock_result.success) {
        pool->vds_locked = true;
        pool->vds_handle = lock_result.handle;
        pool->physical_base = lock_result.physical_address;
        
        LOG_INFO("VDS locked %u bytes at 0x%08lX -> 0x%08lX", 
                region_size, (uint32_t)pool->buffer_memory, pool->physical_base);
        return 0;
    } else {
        LOG_ERROR("VDS lock failed for DMA buffer pool");
        return -1;
    }
}

/**
 * Initialize a DMA-aware buffer pool
 */
static int init_dma_pool(struct dma_buffer_pool *pool, pool_category_t category,
                        uint16_t buffer_size, uint16_t count)
{
    bool is_umb = false;
    void *memory = NULL;
    
    /* Allocate based on category */
    switch (category) {
        case POOL_DMA_SAFE:
            memory = allocate_dma_safe_memory(
                count * (buffer_size + sizeof(void*)), &is_umb);
            pool->dma_safe = true;
            break;
            
        case POOL_COPY_ONLY:
        case POOL_METADATA:
            memory = allocate_copy_memory(
                count * (buffer_size + sizeof(void*)), &is_umb);
            pool->dma_safe = false;
            break;
            
        default:
            return -1;
    }
    
    if (!memory) {
        return -1;
    }
    
    /* Initialize pool structure */
    uint8_t *mem_ptr = (uint8_t *)memory;
    pool->free_list = (void **)mem_ptr;
    mem_ptr += count * sizeof(void*);
    pool->buffer_memory = mem_ptr;
    pool->buffer_size = buffer_size;
    pool->total_count = count;
    pool->free_count = count;
    pool->in_umb = is_umb;
    pool->vds_locked = false;
    pool->vds_handle = 0;
    pool->physical_base = 0;
    
    /* Initialize free list */
    for (uint16_t i = 0; i < count; i++) {
        pool->free_list[i] = pool->buffer_memory + (i * buffer_size);
    }
    
    /* Lock for DMA if required */
    if (pool->dma_safe) {
        if (lock_buffer_for_dma(pool) != 0) {
            free_memory(memory, is_umb);
            return -1;
        }
    }
    
    /* Clear statistics */
    pool->allocations = 0;
    pool->dma_allocations = 0;
    pool->copy_allocations = 0;
    pool->failures = 0;
    pool->dma_failures = 0;
    
    return 0;
}

/**
 * Initialize all DMA-aware buffer pools
 */
int dma_buffer_pools_init(void)
{
    if (pools_initialized) {
        return 0;
    }
    
    /* Analyze memory environment */
    if (analyze_memory_manager_environment() != 0) {
        return -1;
    }
    
    LOG_INFO("Initializing DMA-aware buffer pools:");
    LOG_INFO("  Memory Manager: %s", mem_config.manager_name);
    LOG_INFO("  UMB for DMA: %s", mem_config.umb_safe_for_dma ? "YES" : "NO");
    LOG_INFO("  VDS locking: %s", mem_config.requires_vds_lock ? "YES" : "NO");
    LOG_INFO("  UMB available: %s", mem_config.umb_available ? "YES" : "NO");
    
    /* Initialize DMA-safe pools (conventional memory) */
    if (init_dma_pool(&pools[POOL_DMA_SAFE][0], POOL_DMA_SAFE, SMALL_BUFFER, 16) != 0 ||
        init_dma_pool(&pools[POOL_DMA_SAFE][1], POOL_DMA_SAFE, MEDIUM_BUFFER, 12) != 0 ||
        init_dma_pool(&pools[POOL_DMA_SAFE][2], POOL_DMA_SAFE, LARGE_BUFFER, 8) != 0 ||
        init_dma_pool(&pools[POOL_DMA_SAFE][3], POOL_DMA_SAFE, JUMBO_BUFFER, 4) != 0) {
        LOG_ERROR("Failed to initialize DMA-safe buffer pools");
        return -1;
    }
    
    /* Initialize copy-only pools (UMB preferred) */
    if (init_dma_pool(&pools[POOL_COPY_ONLY][0], POOL_COPY_ONLY, SMALL_BUFFER, 32) != 0 ||
        init_dma_pool(&pools[POOL_COPY_ONLY][1], POOL_COPY_ONLY, MEDIUM_BUFFER, 16) != 0 ||
        init_dma_pool(&pools[POOL_COPY_ONLY][2], POOL_COPY_ONLY, LARGE_BUFFER, 8) != 0) {
        LOG_WARNING("Some copy-only pools failed to initialize (non-critical)");
        /* Continue - not critical for DMA operations */
    }
    
    /* Initialize metadata pools (UMB preferred, small) */
    if (init_dma_pool(&pools[POOL_METADATA][0], POOL_METADATA, 64, 64) != 0 ||
        init_dma_pool(&pools[POOL_METADATA][1], POOL_METADATA, 128, 32) != 0) {
        LOG_WARNING("Some metadata pools failed to initialize (non-critical)");
    }
    
    pools_initialized = true;
    
    LOG_INFO("DMA-aware buffer pools initialized successfully");
    print_memory_usage_summary();
    
    return 0;
}

/**
 * Allocate DMA-safe buffer
 */
void *alloc_dma_buffer(uint16_t size)
{
    if (!pools_initialized) {
        if (dma_buffer_pools_init() != 0) {
            return NULL;
        }
    }
    
    /* Select appropriate DMA-safe pool */
    int pool_idx = -1;
    if (size <= SMALL_BUFFER) pool_idx = 0;
    else if (size <= MEDIUM_BUFFER) pool_idx = 1;
    else if (size <= LARGE_BUFFER) pool_idx = 2;
    else if (size <= JUMBO_BUFFER) pool_idx = 3;
    else {
        LOG_ERROR("Requested DMA buffer size %u exceeds maximum %u", 
                 size, JUMBO_BUFFER);
        return NULL;
    }
    
    struct dma_buffer_pool *pool = &pools[POOL_DMA_SAFE][pool_idx];
    
    if (pool->free_count == 0) {
        pool->dma_failures++;
        pool->failures++;
        LOG_DEBUG("DMA buffer pool exhausted (size %u)", pool->buffer_size);
        return NULL;
    }
    
    /* Allocate buffer */
    pool->free_count--;
    void *buffer = pool->free_list[pool->free_count];
    
    pool->allocations++;
    pool->dma_allocations++;
    
    LOG_DEBUG("Allocated DMA buffer: size %u, pool %u, remaining %u",
             pool->buffer_size, pool_idx, pool->free_count);
    
    return buffer;
}

/**
 * Allocate copy-only buffer (may be in UMB)
 */
void *alloc_copy_buffer(uint16_t size)
{
    if (!pools_initialized) {
        if (dma_buffer_pools_init() != 0) {
            return NULL;
        }
    }
    
    /* Select appropriate copy-only pool */
    int pool_idx = -1;
    if (size <= SMALL_BUFFER) pool_idx = 0;
    else if (size <= MEDIUM_BUFFER) pool_idx = 1;
    else if (size <= LARGE_BUFFER) pool_idx = 2;
    else {
        LOG_DEBUG("Copy buffer size %u exceeds pool maximum, using malloc", size);
        return malloc_conventional(size);  /* Fallback for large copies */
    }
    
    struct dma_buffer_pool *pool = &pools[POOL_COPY_ONLY][pool_idx];
    
    if (pool->free_count == 0) {
        pool->failures++;
        LOG_DEBUG("Copy buffer pool exhausted, falling back to malloc");
        return malloc_conventional(size);
    }
    
    /* Allocate buffer */
    pool->free_count--;
    void *buffer = pool->free_list[pool->free_count];
    
    pool->allocations++;
    pool->copy_allocations++;
    
    return buffer;
}

/**
 * Free buffer back to appropriate pool
 */
void free_dma_aware_buffer(void *buffer)
{
    if (!buffer || !pools_initialized) {
        return;
    }
    
    /* Find which pool this buffer belongs to */
    for (int category = 0; category < POOL_TYPES; category++) {
        for (int size = 0; size < 4; size++) {
            struct dma_buffer_pool *pool = &pools[category][size];
            
            if (pool->buffer_memory == NULL) continue;
            
            uint8_t *pool_start = pool->buffer_memory;
            uint8_t *pool_end = pool_start + (pool->total_count * pool->buffer_size);
            
            if ((uint8_t *)buffer >= pool_start && (uint8_t *)buffer < pool_end) {
                /* Verify alignment */
                uint32_t offset = (uint8_t *)buffer - pool_start;
                if (offset % pool->buffer_size != 0) {
                    LOG_ERROR("Buffer %p not aligned in pool", buffer);
                    return;
                }
                
                /* Check for double-free */
                if (pool->free_count >= pool->total_count) {
                    LOG_ERROR("Double-free detected for buffer %p", buffer);
                    return;
                }
                
                /* Return to pool */
                pool->free_list[pool->free_count] = buffer;
                pool->free_count++;
                
                LOG_DEBUG("Freed buffer to pool: category %d, size %u",
                         category, pool->buffer_size);
                return;
            }
        }
    }
    
    /* Not from our pools - assume malloc'd */
    free_conventional(buffer);
}

/**
 * Get physical address for DMA buffer (if VDS locked)
 */
uint32_t get_buffer_physical_address(void *buffer)
{
    if (!buffer || !pools_initialized) {
        return 0;
    }
    
    /* Find DMA-safe pool containing this buffer */
    for (int size = 0; size < 4; size++) {
        struct dma_buffer_pool *pool = &pools[POOL_DMA_SAFE][size];
        
        if (!pool->vds_locked || pool->buffer_memory == NULL) continue;
        
        uint8_t *pool_start = pool->buffer_memory;
        uint8_t *pool_end = pool_start + (pool->total_count * pool->buffer_size);
        
        if ((uint8_t *)buffer >= pool_start && (uint8_t *)buffer < pool_end) {
            /* Calculate physical offset */
            uint32_t offset = (uint8_t *)buffer - pool_start;
            return pool->physical_base + offset;
        }
    }
    
    /* Not VDS-locked or not found - return linear address */
    return (uint32_t)buffer;
}

/**
 * Print memory usage summary
 */
void print_memory_usage_summary(void)
{
    uint32_t dma_total = 0, copy_total = 0, meta_total = 0;
    uint32_t dma_conv = 0, copy_umb = 0;
    
    LOG_INFO("=== DMA-Aware Buffer Pool Summary ===");
    
    /* Calculate totals */
    for (int size = 0; size < 4; size++) {
        struct dma_buffer_pool *dma = &pools[POOL_DMA_SAFE][size];
        struct dma_buffer_pool *copy = &pools[POOL_COPY_ONLY][size];
        struct dma_buffer_pool *meta = &pools[POOL_METADATA][size];
        
        if (dma->buffer_memory) {
            uint32_t pool_size = dma->total_count * dma->buffer_size;
            dma_total += pool_size;
            if (!dma->in_umb) dma_conv += pool_size;
        }
        
        if (copy->buffer_memory) {
            uint32_t pool_size = copy->total_count * copy->buffer_size;
            copy_total += pool_size;
            if (copy->in_umb) copy_umb += pool_size;
        }
        
        if (meta->buffer_memory) {
            uint32_t pool_size = meta->total_count * meta->buffer_size;
            meta_total += pool_size;
        }
    }
    
    LOG_INFO("DMA-safe pools: %lu bytes (conventional: %lu)", dma_total, dma_conv);
    LOG_INFO("Copy-only pools: %lu bytes (UMB: %lu)", copy_total, copy_umb);
    LOG_INFO("Metadata pools: %lu bytes", meta_total);
    LOG_INFO("Total allocated: %lu bytes", dma_total + copy_total + meta_total);
    LOG_INFO("Conventional preserved: %lu bytes via UMB usage", copy_umb);
    
    if (mem_config.requires_vds_lock) {
        LOG_INFO("VDS locking: ACTIVE for DMA buffers");
    }
}

/**
 * Get buffer pool statistics
 */
void get_dma_buffer_stats(struct dma_buffer_stats *stats)
{
    if (!stats || !pools_initialized) {
        return;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    /* Aggregate statistics */
    for (int category = 0; category < POOL_TYPES; category++) {
        for (int size = 0; size < 4; size++) {
            struct dma_buffer_pool *pool = &pools[category][size];
            
            if (pool->buffer_memory == NULL) continue;
            
            stats->total_allocations += pool->allocations;
            stats->total_failures += pool->failures;
            
            if (category == POOL_DMA_SAFE) {
                stats->dma_allocations += pool->dma_allocations;
                stats->dma_failures += pool->dma_failures;
                stats->dma_buffers_free += pool->free_count;
                stats->dma_buffers_total += pool->total_count;
            } else {
                stats->copy_allocations += pool->copy_allocations;
                stats->copy_buffers_free += pool->free_count;
                stats->copy_buffers_total += pool->total_count;
            }
        }
    }
    
    /* Calculate utilization */
    if (stats->dma_buffers_total > 0) {
        stats->dma_utilization = 
            ((stats->dma_buffers_total - stats->dma_buffers_free) * 100) / 
            stats->dma_buffers_total;
    }
    
    if (stats->copy_buffers_total > 0) {
        stats->copy_utilization = 
            ((stats->copy_buffers_total - stats->copy_buffers_free) * 100) / 
            stats->copy_buffers_total;
    }
    
    /* Memory manager info */
    strcpy(stats->memory_manager, mem_config.manager_name);
    stats->vds_available = mem_config.requires_vds_lock;
    stats->umb_in_use = mem_config.umb_available;
}