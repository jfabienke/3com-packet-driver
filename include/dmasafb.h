/**
 * @file dma_safe_allocator.h
 * @brief DMA-safe buffer allocator interface
 */

#ifndef DMA_SAFE_ALLOCATOR_H
#define DMA_SAFE_ALLOCATOR_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct dma_safe_pool;
struct dma_free_block;

/* DMA allocation result */
struct dma_allocation {
    uint8_t *virtual_addr;          /* Virtual address */
    uint32_t physical_addr;         /* Physical address (VDS-locked) */
    uint32_t size;                  /* Allocation size */
    uint32_t offset;                /* Offset in pool */
    struct dma_safe_pool *pool;     /* Owning pool */
    bool is_valid;                  /* Allocation is valid */
};

/* DMA allocator statistics */
struct dma_safe_stats {
    uint8_t active_pools;           /* Number of active pools */
    uint32_t total_size;            /* Total pool size */
    uint32_t allocated_size;        /* Currently allocated */
    uint32_t peak_usage;            /* Peak allocation */
    uint32_t boundary_violations;   /* Boundary crossing attempts */
    uint32_t alignment_adjustments; /* Alignment corrections */
    uint32_t allocation_failures;   /* Failed allocations */
    uint8_t vds_locked_pools;       /* VDS-locked pools */
    uint8_t utilization;            /* Overall utilization % */
};

/* Function prototypes */

/**
 * Initialize DMA-safe allocator
 */
int dma_safe_allocator_init(void);

/**
 * Allocate DMA-safe buffer with boundary checking
 */
struct dma_allocation dma_safe_alloc(uint32_t size, uint32_t alignment);

/**
 * Free DMA-safe buffer
 */
void dma_safe_free(struct dma_allocation *allocation);

/**
 * Statistics and monitoring
 */
void dma_safe_get_stats(struct dma_safe_stats *stats);
int dma_safe_health_check(void);

/**
 * Cleanup
 */
void dma_safe_allocator_cleanup(void);

/* Convenience macros */

/* Standard alignments */
#define DMA_ALIGN_16        16
#define DMA_ALIGN_256       256
#define DMA_ALIGN_STRICT    256

/* Allocate with standard alignment */
#define ALLOC_DMA_BUFFER(size) dma_safe_alloc(size, DMA_ALIGN_16)

/* Allocate with strict alignment for descriptors */
#define ALLOC_DMA_DESCRIPTOR(size) dma_safe_alloc(size, DMA_ALIGN_STRICT)

/* Check if allocation is valid */
#define IS_VALID_ALLOCATION(alloc) ((alloc).is_valid)

/* Get physical address from allocation */
#define GET_PHYSICAL_ADDR(alloc) ((alloc).physical_addr)

/* Standard buffer sizes */
#define DMA_BUFFER_256      256
#define DMA_BUFFER_512      512
#define DMA_BUFFER_1536     1536
#define DMA_BUFFER_2048     2048

#endif /* DMA_SAFE_ALLOCATOR_H */