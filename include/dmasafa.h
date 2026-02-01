/**
 * @file dma_safe_alloc.h
 * @brief DMA-safe memory allocator interface
 * 
 * Provides physically contiguous memory allocation with alignment guarantees
 * for DMA operations in DOS environment.
 */

#ifndef _DMA_SAFE_ALLOC_H_
#define _DMA_SAFE_ALLOC_H_

#include <stdint.h>
#include <stdbool.h>

/* DMA memory allocation flags */
#define DMAMEM_BELOW_1M         0x01    /* Allocate below 1MB (real mode) */
#define DMAMEM_BELOW_16M        0x02    /* Allocate below 16MB (ISA DMA) */
#define DMAMEM_CONTIGUOUS       0x04    /* Must be physically contiguous */
#define DMAMEM_ALIGNED          0x08    /* Must meet alignment requirement */
#define DMAMEM_NO_CROSS_4K      0x10    /* Cannot cross 4KB boundary */
#define DMAMEM_NO_CROSS_64K     0x20    /* Cannot cross 64KB boundary */
#define DMAMEM_CACHED           0x40    /* Can be cached (coherent) */
#define DMAMEM_UNCACHED         0x80    /* Must be uncached */

/* DMA allocation information */
typedef struct {
    void far *virt_addr;        /* Virtual address for CPU access (far pointer) */
    uint32_t phys_addr;         /* Physical address for DMA */
    uint32_t size;              /* Allocated size */
    uint32_t alignment;         /* Alignment used */
    uint32_t flags;             /* Allocation flags */
} dma_alloc_info_t;

/* Function prototypes */

/**
 * @brief Allocate physically contiguous DMA-safe memory
 * 
 * @param size Size in bytes
 * @param alignment Required alignment (must be power of 2)
 * @param flags Allocation flags (DMAMEM_*)
 * @return DMA allocation info, or NULL on failure
 */
dma_alloc_info_t* dma_alloc_coherent(uint32_t size, uint32_t alignment, uint32_t flags);

/**
 * @brief Free DMA-safe memory
 * 
 * @param info DMA allocation info from dma_alloc_coherent()
 */
void dma_free_coherent(dma_alloc_info_t *info);

/**
 * @brief Allocate DMA descriptor ring
 * 
 * Specialized allocator for descriptor rings with strict alignment.
 * 
 * @param num_descriptors Number of descriptors
 * @param descriptor_size Size of each descriptor
 * @param alignment Required alignment (typically 16 or 32)
 * @return DMA allocation info, or NULL on failure
 */
dma_alloc_info_t* dma_alloc_ring(uint32_t num_descriptors, uint32_t descriptor_size,
                                 uint32_t alignment);

/**
 * @brief Allocate DMA packet buffer
 * 
 * @param size Buffer size (typically 1518 for Ethernet)
 * @return DMA allocation info, or NULL on failure
 */
dma_alloc_info_t* dma_alloc_packet_buffer(uint32_t size);

/**
 * @brief Check if DMA address is valid for device
 * 
 * @param phys_addr Physical address
 * @param dma_mask Device DMA mask (e.g., 0xFFFFFFFF for 32-bit)
 * @return true if valid, false if bounce buffer needed
 */
bool dma_addr_valid(uint32_t phys_addr, uint32_t dma_mask);

/**
 * @brief Get DMA allocator statistics
 * 
 * @param total_allocs Output: Total allocations
 * @param active_allocs Output: Currently active allocations
 * @param total_bytes Output: Total bytes allocated
 */
void dma_get_stats(uint32_t *total_allocs, uint32_t *active_allocs, uint32_t *total_bytes);

/* Bounce buffer functions for memory-constrained environments */

/**
 * @brief Allocate bounce buffer for DMA operation
 * 
 * @param original_addr Original buffer address
 * @param size Data size
 * @param tx_direction true for TX, false for RX
 * @return Bounce buffer physical address, or 0xFFFFFFFF on failure
 */
uint32_t dma_alloc_bounce_buffer(void *original_addr, uint32_t size, bool tx_direction);

/**
 * @brief Free bounce buffer and copy data back if needed
 * 
 * @param phys_addr Physical address returned by dma_alloc_bounce_buffer()
 * @return true on success, false on error
 */
bool dma_free_bounce_buffer(uint32_t phys_addr);

/**
 * @brief Check if address needs bounce buffer
 * 
 * @param virt_addr Virtual address
 * @param phys_addr Physical address
 * @param size Data size
 * @param dma_mask Device DMA capability mask
 * @return true if bounce buffer needed, false if direct DMA ok
 */
bool dma_needs_bounce_buffer(void *virt_addr, uint32_t phys_addr, 
                            uint32_t size, uint32_t dma_mask);

/**
 * @brief Get bounce buffer pool statistics
 * 
 * @param total_buffers Output: Total bounce buffers
 * @param free_buffers Output: Available bounce buffers
 * @param buffer_size Output: Size of each bounce buffer
 */
void dma_get_bounce_stats(uint32_t *total_buffers, uint32_t *free_buffers, 
                         uint32_t *buffer_size);

#endif /* _DMA_SAFE_ALLOC_H_ */
