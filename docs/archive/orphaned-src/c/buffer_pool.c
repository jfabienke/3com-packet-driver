/**
 * @file buffer_pool.c
 * @brief Buffer pool management for copy-break optimization
 * 
 * Manages pre-allocated buffer pools in UMB and conventional memory
 * for efficient small packet handling.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "buffer_pool.h"
#include "memory.h"
#include "logging.h"

/* Buffer pool configuration */
#define SMALL_BUFFER_SIZE       256     /* Size of each small buffer */
#define SMALL_BUFFER_COUNT      32      /* Number of small buffers */
#define MEDIUM_BUFFER_SIZE      512     /* Size of each medium buffer */
#define MEDIUM_BUFFER_COUNT     16      /* Number of medium buffers */
#define LARGE_BUFFER_SIZE       1536    /* Size of each large buffer (MTU) */
#define LARGE_BUFFER_COUNT      8       /* Number of large buffers */

/* Buffer types */
typedef enum {
    BUFFER_SMALL = 0,
    BUFFER_MEDIUM = 1,
    BUFFER_LARGE = 2,
    BUFFER_TYPES = 3
} buffer_type_t;

/* Buffer pool structure */
struct buffer_pool {
    void **free_list;           /* Array of free buffer pointers */
    uint8_t *buffer_memory;     /* Actual buffer memory block */
    uint16_t buffer_size;       /* Size of each buffer */
    uint16_t total_count;       /* Total buffers in pool */
    uint16_t free_count;        /* Currently free buffers */
    uint16_t high_watermark;    /* High watermark for monitoring */
    uint16_t low_watermark;     /* Low watermark for refill */
    
    /* Statistics */
    uint32_t allocations;       /* Total allocations */
    uint32_t frees;             /* Total frees */
    uint32_t failures;          /* Allocation failures */
    uint32_t peak_usage;        /* Peak simultaneous usage */
};

/* Global buffer pools */
static struct buffer_pool pools[BUFFER_TYPES];

/* Memory segments for buffer storage */
static void *umb_memory = NULL;         /* UMB memory for buffers */
static void *conventional_memory = NULL; /* Conventional memory fallback */
static uint16_t umb_size = 0;           /* Size of UMB allocation */
static uint16_t conventional_size = 0;   /* Size of conventional allocation */

/* Pool initialization status */
static bool pools_initialized = false;

/**
 * Calculate memory requirements for all pools
 */
static uint32_t calculate_memory_requirements(void)
{
    uint32_t total = 0;
    
    /* Small buffers */
    total += SMALL_BUFFER_COUNT * SMALL_BUFFER_SIZE;
    total += SMALL_BUFFER_COUNT * sizeof(void*);  /* Free list */
    
    /* Medium buffers */
    total += MEDIUM_BUFFER_COUNT * MEDIUM_BUFFER_SIZE;
    total += MEDIUM_BUFFER_COUNT * sizeof(void*);
    
    /* Large buffers */
    total += LARGE_BUFFER_COUNT * LARGE_BUFFER_SIZE;
    total += LARGE_BUFFER_COUNT * sizeof(void*);
    
    /* Alignment padding */
    total += 256;  /* Conservative padding for alignment */
    
    return total;
}

/**
 * Initialize a single buffer pool
 */
static int init_pool(struct buffer_pool *pool, void *memory, 
                    uint16_t buffer_size, uint16_t count)
{
    uint8_t *mem_ptr = (uint8_t *)memory;
    
    /* Initialize pool structure */
    pool->buffer_size = buffer_size;
    pool->total_count = count;
    pool->free_count = count;
    pool->high_watermark = count * 9 / 10;  /* 90% */
    pool->low_watermark = count / 4;        /* 25% */
    
    /* Allocate free list */
    pool->free_list = (void **)mem_ptr;
    mem_ptr += count * sizeof(void*);
    
    /* Allocate buffer memory */
    pool->buffer_memory = mem_ptr;
    
    /* Initialize free list */
    for (uint16_t i = 0; i < count; i++) {
        pool->free_list[i] = pool->buffer_memory + (i * buffer_size);
    }
    
    /* Clear statistics */
    pool->allocations = 0;
    pool->frees = 0;
    pool->failures = 0;
    pool->peak_usage = 0;
    
    return 0;
}

/**
 * Try to allocate memory in UMB first, then conventional
 */
static void *allocate_buffer_memory(uint32_t size)
{
    void *memory = NULL;
    
    /* Try UMB first */
    memory = xms_alloc_umb(size);
    if (memory) {
        umb_memory = memory;
        umb_size = size;
        LOG_INFO("Allocated %lu bytes in UMB for buffer pools", size);
        return memory;
    }
    
    /* Fall back to conventional memory */
    memory = malloc_conventional(size);
    if (memory) {
        conventional_memory = memory;
        conventional_size = size;
        LOG_INFO("Allocated %lu bytes in conventional memory for buffer pools", size);
        return memory;
    }
    
    LOG_ERROR("Failed to allocate %lu bytes for buffer pools", size);
    return NULL;
}

/**
 * Initialize all buffer pools
 */
int buffer_pool_init(void)
{
    if (pools_initialized) {
        return 0;  /* Already initialized */
    }
    
    /* Calculate total memory needed */
    uint32_t total_memory = calculate_memory_requirements();
    
    /* Allocate memory */
    void *memory = allocate_buffer_memory(total_memory);
    if (!memory) {
        return -1;
    }
    
    uint8_t *mem_ptr = (uint8_t *)memory;
    
    /* Initialize small buffer pool */
    if (init_pool(&pools[BUFFER_SMALL], mem_ptr, 
                  SMALL_BUFFER_SIZE, SMALL_BUFFER_COUNT) != 0) {
        return -1;
    }
    mem_ptr += SMALL_BUFFER_COUNT * (SMALL_BUFFER_SIZE + sizeof(void*));
    
    /* Initialize medium buffer pool */
    if (init_pool(&pools[BUFFER_MEDIUM], mem_ptr, 
                  MEDIUM_BUFFER_SIZE, MEDIUM_BUFFER_COUNT) != 0) {
        return -1;
    }
    mem_ptr += MEDIUM_BUFFER_COUNT * (MEDIUM_BUFFER_SIZE + sizeof(void*));
    
    /* Initialize large buffer pool */
    if (init_pool(&pools[BUFFER_LARGE], mem_ptr, 
                  LARGE_BUFFER_SIZE, LARGE_BUFFER_COUNT) != 0) {
        return -1;
    }
    
    pools_initialized = true;
    
    LOG_INFO("Buffer pools initialized: %u small, %u medium, %u large buffers",
             SMALL_BUFFER_COUNT, MEDIUM_BUFFER_COUNT, LARGE_BUFFER_COUNT);
    
    return 0;
}

/**
 * Determine appropriate buffer type for a given size
 */
static buffer_type_t get_buffer_type(uint16_t size)
{
    if (size <= SMALL_BUFFER_SIZE) {
        return BUFFER_SMALL;
    } else if (size <= MEDIUM_BUFFER_SIZE) {
        return BUFFER_MEDIUM;
    } else {
        return BUFFER_LARGE;
    }
}

/**
 * Allocate buffer from appropriate pool
 */
void *buffer_pool_alloc(uint16_t size)
{
    if (!pools_initialized) {
        if (buffer_pool_init() != 0) {
            return NULL;
        }
    }
    
    buffer_type_t type = get_buffer_type(size);
    struct buffer_pool *pool = &pools[type];
    
    /* Check if pool is empty */
    if (pool->free_count == 0) {
        pool->failures++;
        return NULL;
    }
    
    /* Get buffer from free list */
    pool->free_count--;
    void *buffer = pool->free_list[pool->free_count];
    
    /* Update statistics */
    pool->allocations++;
    uint16_t current_usage = pool->total_count - pool->free_count;
    if (current_usage > pool->peak_usage) {
        pool->peak_usage = current_usage;
    }
    
    return buffer;
}

/**
 * Free buffer back to pool
 */
void buffer_pool_free(void *buffer)
{
    if (!buffer || !pools_initialized) {
        return;
    }
    
    /* Find which pool this buffer belongs to */
    for (int type = 0; type < BUFFER_TYPES; type++) {
        struct buffer_pool *pool = &pools[type];
        
        /* Check if buffer is within this pool's memory range */
        uint8_t *pool_start = pool->buffer_memory;
        uint8_t *pool_end = pool_start + (pool->total_count * pool->buffer_size);
        
        if ((uint8_t *)buffer >= pool_start && (uint8_t *)buffer < pool_end) {
            /* Verify buffer is properly aligned */
            uint32_t offset = (uint8_t *)buffer - pool_start;
            if (offset % pool->buffer_size != 0) {
                LOG_ERROR("Buffer %p not properly aligned for pool %d", buffer, type);
                return;
            }
            
            /* Check for double-free */
            if (pool->free_count >= pool->total_count) {
                LOG_ERROR("Double-free detected for buffer %p", buffer);
                return;
            }
            
            /* Add buffer back to free list */
            pool->free_list[pool->free_count] = buffer;
            pool->free_count++;
            pool->frees++;
            
            return;
        }
    }
    
    LOG_ERROR("Buffer %p does not belong to any pool", buffer);
}

/**
 * Allocate buffer for specific copy-break size
 * Optimized for the copy-break threshold
 */
void *buffer_pool_alloc_copybreak(uint16_t packet_size, uint16_t threshold)
{
    /* If packet is larger than threshold, don't use pool buffer */
    if (packet_size > threshold) {
        return NULL;  /* Use zero-copy instead */
    }
    
    /* Use pool buffer for small packets */
    return buffer_pool_alloc(packet_size);
}

/**
 * Check if buffer pool needs refill
 */
bool buffer_pool_needs_refill(buffer_type_t type)
{
    if (!pools_initialized || type >= BUFFER_TYPES) {
        return false;
    }
    
    return pools[type].free_count <= pools[type].low_watermark;
}

/**
 * Get buffer pool statistics
 */
void buffer_pool_get_stats(buffer_type_t type, struct buffer_pool_stats *stats)
{
    if (!pools_initialized || type >= BUFFER_TYPES || !stats) {
        return;
    }
    
    struct buffer_pool *pool = &pools[type];
    
    stats->buffer_size = pool->buffer_size;
    stats->total_count = pool->total_count;
    stats->free_count = pool->free_count;
    stats->used_count = pool->total_count - pool->free_count;
    stats->allocations = pool->allocations;
    stats->frees = pool->frees;
    stats->failures = pool->failures;
    stats->peak_usage = pool->peak_usage;
    
    /* Calculate utilization percentage */
    stats->utilization = (stats->used_count * 100) / pool->total_count;
    
    /* Calculate success rate */
    if (pool->allocations + pool->failures > 0) {
        stats->success_rate = (pool->allocations * 100) / 
                             (pool->allocations + pool->failures);
    } else {
        stats->success_rate = 100;
    }
}

/**
 * Get overall buffer pool health
 */
int buffer_pool_health_check(void)
{
    if (!pools_initialized) {
        return -1;
    }
    
    int health_score = 0;
    
    for (int type = 0; type < BUFFER_TYPES; type++) {
        struct buffer_pool *pool = &pools[type];
        
        /* Check failure rate */
        if (pool->allocations + pool->failures > 100) {  /* Enough samples */
            uint8_t failure_rate = (pool->failures * 100) / 
                                  (pool->allocations + pool->failures);
            if (failure_rate > 10) {  /* >10% failure rate */
                health_score -= 2;
            } else if (failure_rate > 5) {
                health_score -= 1;
            }
        }
        
        /* Check current utilization */
        uint8_t utilization = ((pool->total_count - pool->free_count) * 100) / 
                             pool->total_count;
        if (utilization > 90) {
            health_score -= 2;  /* Very high utilization */
        } else if (utilization > 75) {
            health_score -= 1;  /* High utilization */
        }
        
        /* Check for memory leaks */
        if (pool->allocations > pool->frees + pool->total_count) {
            health_score -= 3;  /* Possible memory leak */
        }
    }
    
    return health_score;
}

/**
 * Reset buffer pool statistics
 */
void buffer_pool_reset_stats(void)
{
    if (!pools_initialized) {
        return;
    }
    
    for (int type = 0; type < BUFFER_TYPES; type++) {
        struct buffer_pool *pool = &pools[type];
        pool->allocations = 0;
        pool->frees = 0;
        pool->failures = 0;
        pool->peak_usage = pool->total_count - pool->free_count;
    }
}

/**
 * Cleanup buffer pools
 */
void buffer_pool_cleanup(void)
{
    if (!pools_initialized) {
        return;
    }
    
    /* Free allocated memory */
    if (umb_memory) {
        xms_free_umb(umb_memory, umb_size);
        umb_memory = NULL;
        umb_size = 0;
    }
    
    if (conventional_memory) {
        free_conventional(conventional_memory);
        conventional_memory = NULL;
        conventional_size = 0;
    }
    
    /* Clear pool structures */
    memset(pools, 0, sizeof(pools));
    pools_initialized = false;
    
    LOG_INFO("Buffer pools cleaned up");
}

/**
 * Debug functions
 */

/**
 * Print buffer pool information
 */
void buffer_pool_debug_print(void)
{
    if (!pools_initialized) {
        LOG_INFO("Buffer pools not initialized");
        return;
    }
    
    LOG_INFO("Buffer Pool Status:");
    for (int type = 0; type < BUFFER_TYPES; type++) {
        struct buffer_pool *pool = &pools[type];
        const char *type_name[] = {"Small", "Medium", "Large"};
        
        LOG_INFO("  %s (%u bytes): %u/%u free, %lu allocs, %lu failures",
                type_name[type], pool->buffer_size,
                pool->free_count, pool->total_count,
                pool->allocations, pool->failures);
    }
    
    if (umb_memory) {
        LOG_INFO("  Memory: %u bytes in UMB", umb_size);
    } else {
        LOG_INFO("  Memory: %u bytes in conventional", conventional_size);
    }
}