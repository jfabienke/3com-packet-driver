/**
 * @file dma_aware_buffer_pool.h
 * @brief DMA-aware buffer pool management interface
 * 
 * Enhanced buffer management that uses memory manager detection to make
 * intelligent decisions about DMA safety and UMB usage.
 */

#ifndef DMA_AWARE_BUFFER_POOL_H
#define DMA_AWARE_BUFFER_POOL_H

#include <stdint.h>
#include <stdbool.h>

/* Buffer pool statistics with DMA awareness */
struct dma_buffer_stats {
    /* Allocation statistics */
    uint32_t total_allocations;     /* Total buffer allocations */
    uint32_t total_failures;       /* Total allocation failures */
    
    /* DMA-safe buffer statistics */
    uint32_t dma_allocations;       /* DMA buffer allocations */
    uint32_t dma_failures;          /* DMA allocation failures */
    uint16_t dma_buffers_total;     /* Total DMA buffers */
    uint16_t dma_buffers_free;      /* Free DMA buffers */
    uint8_t dma_utilization;        /* DMA buffer utilization % */
    
    /* Copy-only buffer statistics */
    uint32_t copy_allocations;      /* Copy buffer allocations */
    uint16_t copy_buffers_total;    /* Total copy buffers */
    uint16_t copy_buffers_free;     /* Free copy buffers */
    uint8_t copy_utilization;       /* Copy buffer utilization % */
    
    /* Memory manager information */
    char memory_manager[32];        /* Detected memory manager */
    bool vds_available;             /* VDS services available */
    bool umb_in_use;               /* UMB being used */
};

/* Function prototypes */

/**
 * Initialize DMA-aware buffer pools
 * Analyzes memory manager environment and creates appropriate pools
 */
int dma_buffer_pools_init(void);

/**
 * Buffer allocation functions
 */

/**
 * Allocate DMA-safe buffer
 * Guaranteed to be safe for bus-master DMA operations
 */
void *alloc_dma_buffer(uint16_t size);

/**
 * Allocate copy-only buffer
 * May be allocated in UMB - NOT safe for DMA
 */
void *alloc_copy_buffer(uint16_t size);

/**
 * Free buffer back to appropriate pool
 * Automatically determines buffer type and returns to correct pool
 */
void free_dma_aware_buffer(void *buffer);

/**
 * Get physical address for DMA buffer
 * Returns VDS-locked physical address if available, otherwise linear address
 */
uint32_t get_buffer_physical_address(void *buffer);

/**
 * Statistics and monitoring
 */
void get_dma_buffer_stats(struct dma_buffer_stats *stats);
void print_memory_usage_summary(void);

/**
 * Integration with copy-break optimization
 */

/**
 * Allocate buffer for copy-break operation
 * Uses copy-only pool (may be UMB) for small packets
 */
static inline void *alloc_copybreak_buffer(uint16_t packet_size, uint16_t threshold)
{
    if (packet_size > threshold) {
        return NULL;  /* Use zero-copy instead */
    }
    return alloc_copy_buffer(packet_size);
}

/**
 * Check if buffer is DMA-safe
 * Returns true if buffer was allocated from DMA-safe pool
 */
bool is_buffer_dma_safe(void *buffer);

/**
 * Memory manager detection integration
 */
extern bool detect_xms_services(void);
extern void *alloc_umb_dos_api(uint32_t size);
extern void *malloc_conventional(uint32_t size);
extern void free_conventional(void *ptr);
extern void free_memory(void *ptr, bool is_umb);

/* Convenience macros */

/* Allocate specific buffer types */
#define ALLOC_DMA_SMALL()       alloc_dma_buffer(256)
#define ALLOC_DMA_MEDIUM()      alloc_dma_buffer(512)
#define ALLOC_DMA_LARGE()       alloc_dma_buffer(1536)
#define ALLOC_DMA_JUMBO()       alloc_dma_buffer(2048)

#define ALLOC_COPY_SMALL()      alloc_copy_buffer(256)
#define ALLOC_COPY_MEDIUM()     alloc_copy_buffer(512)
#define ALLOC_COPY_LARGE()      alloc_copy_buffer(1536)

/* Free any buffer type */
#define FREE_BUFFER(buf)        free_dma_aware_buffer(buf)

/* Buffer size limits */
#define DMA_BUFFER_MAX_SIZE     2048
#define COPY_BUFFER_MAX_SIZE    1536

/**
 * Memory manager compatibility matrix for reference:
 * 
 * | Memory Manager | UMB DMA Safe | VDS Required | Notes |
 * |----------------|--------------|--------------|-------|
 * | Pure DOS       | N/A          | No           | No UMB available |
 * | HIMEM only     | No           | No           | Conservative policy |
 * | EMM386         | No           | No           | Paged UMB unsafe |
 * | QEMM           | No           | No           | Conservative policy |
 * | Windows Enh.   | No           | No           | System controlled |
 * | VDS enabled    | No           | Yes          | Use conventional+VDS |
 * 
 * This implementation follows GPT-5's recommendation to never use UMB for
 * DMA operations regardless of memory manager.
 */

#endif /* DMA_AWARE_BUFFER_POOL_H */