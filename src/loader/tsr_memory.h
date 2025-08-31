/**
 * @file tsr_memory.h
 * @brief TSR Memory Management Interface
 * 
 * Provides interrupt-safe memory allocation for TSR context
 * using a pre-allocated heap pool to avoid DOS INT 21h calls.
 */

#ifndef TSR_MEMORY_H
#define TSR_MEMORY_H

#include <stdint.h>

/**
 * @brief TSR memory usage statistics
 */
typedef struct {
    uint16_t total_size;        /* Total heap size */
    uint16_t allocated_bytes;   /* Currently allocated bytes */
    uint16_t free_bytes;        /* Free bytes available */
    uint16_t peak_allocated;    /* Peak allocation */
    uint16_t allocation_count;  /* Total allocations made */
    uint8_t fragmentation_pct;  /* Fragmentation percentage */
} tsr_memory_stats_t;

/**
 * @brief Initialize TSR memory heap
 */
void tsr_heap_init(void);

/**
 * @brief Allocate memory from TSR heap
 * 
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory or NULL if failed
 */
void *tsr_malloc(uint16_t size);

/**
 * @brief Free memory back to TSR heap
 * 
 * @param ptr Pointer to memory to free
 */
void tsr_free(void *ptr);

/**
 * @brief Get memory usage statistics
 * 
 * @param stats Pointer to structure to fill with statistics
 */
void tsr_get_memory_stats(tsr_memory_stats_t *stats);

/**
 * @brief Check available memory
 * 
 * @return Number of free bytes available
 */
uint16_t tsr_get_free_memory(void);

/**
 * @brief Perform garbage collection and defragmentation
 * 
 * @return Number of bytes recovered
 */
uint16_t tsr_garbage_collect(void);

/**
 * @brief Check memory integrity
 * 
 * @return 1 if heap is intact, 0 if corrupted
 */
int tsr_check_heap_integrity(void);

/**
 * @brief Allocate memory with alignment
 * 
 * @param size Size in bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
 * @return Aligned pointer or NULL if failed
 */
void *tsr_malloc_aligned(uint16_t size, uint16_t alignment);

#endif /* TSR_MEMORY_H */