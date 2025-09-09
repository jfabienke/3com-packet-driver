/**
 * @file tsr_memory.c
 * @brief TSR Memory Management Implementation
 * 
 * Provides interrupt-safe memory allocation for TSR context
 * using a pre-allocated heap pool to avoid DOS INT 21h calls.
 */

#include <i86.h>
#include <string.h>
#include <stdint.h>
#include "tsr_memory.h"
#include "timer_services.h"
#include "../api/metrics_core.h"

#define TSR_HEAP_SIZE 4096        /* 4KB heap for TSR operations */

static uint8_t tsr_heap[TSR_HEAP_SIZE];

typedef struct blk {
    uint16_t size;                /* payload size in bytes */
    struct blk __far *next;
} blk_t;

static blk_t __far *free_list = 0;
static uint16_t total_allocated = 0;
static uint16_t peak_allocated = 0;
static uint16_t allocation_count = 0;
static uint32_t last_gc_time = 0;

/**
 * @brief Initialize TSR memory heap
 */
void tsr_heap_init(void)
{
    blk_t __far *b = (blk_t __far *)tsr_heap;
    b->size = TSR_HEAP_SIZE - sizeof(blk_t);
    b->next = 0;
    free_list = b;
    
    total_allocated = 0;
    peak_allocated = 0;
    allocation_count = 0;
    last_gc_time = get_millisecond_timestamp();
}

static void split_block(blk_t __far *b, uint16_t needed)
{
    uint16_t remain = b->size - needed;
    if (remain > sizeof(blk_t) + 8) {
        blk_t __far *nb = (blk_t __far *)((uint8_t __far *)b + sizeof(blk_t) + needed);
        nb->size = remain - sizeof(blk_t);
        nb->next = b->next;
        b->size = needed;
        b->next = nb;
    }
}

/**
 * @brief Allocate memory from TSR heap
 * 
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory or NULL if failed
 */
void *tsr_malloc(uint16_t size)
{
    blk_t __far *prev = 0, __far *p = free_list;

    if (!size) return 0;
    /* align to 2 bytes */
    size = (size + 1) & ~1;

    _disable();
    while (p) {
        if (p->size >= size) {
            split_block(p, size);
            /* unlink p */
            if (prev) prev->next = p->next;
            else free_list = p->next;
            
            /* Update statistics */
            total_allocated += size + sizeof(blk_t);
            if (total_allocated > peak_allocated) {
                peak_allocated = total_allocated;
            }
            allocation_count++;
            
            _enable();
            
            /* Update metrics core - assume module 0 for TSR allocations */
            metrics_memory_allocated(size + sizeof(blk_t), 0);
            
            return (uint8_t __far *)p + sizeof(blk_t);
        }
        prev = p;
        p = p->next;
    }
    _enable();
    return 0;
}

/**
 * @brief Free memory back to TSR heap
 * 
 * @param ptr Pointer to memory to free
 */
void tsr_free(void *ptr)
{
    blk_t __far *b;
    if (!ptr) return;

    b = (blk_t __far *)((uint8_t __far *)ptr - sizeof(blk_t));

    _disable();
    
    /* Update metrics core before updating local stats */
    metrics_memory_freed(b->size + sizeof(blk_t), 0);
    
    /* Update statistics */
    total_allocated -= (b->size + sizeof(blk_t));
    
    /* Simple LIFO insert; could implement coalescing here */
    b->next = free_list;
    free_list = b;
    _enable();
}

/**
 * @brief Get memory usage statistics
 * 
 * @param stats Pointer to structure to fill with statistics
 */
void tsr_get_memory_stats(tsr_memory_stats_t *stats)
{
    if (!stats) return;
    
    _disable();
    stats->total_size = TSR_HEAP_SIZE;
    stats->allocated_bytes = total_allocated;
    stats->free_bytes = TSR_HEAP_SIZE - total_allocated;
    stats->peak_allocated = peak_allocated;
    stats->allocation_count = allocation_count;
    stats->fragmentation_pct = 0; /* Could implement fragmentation analysis */
    _enable();
}

/**
 * @brief Check available memory
 * 
 * @return Number of free bytes available
 */
uint16_t tsr_get_free_memory(void)
{
    blk_t __far *p = free_list;
    uint16_t total_free = 0;
    
    _disable();
    while (p) {
        total_free += p->size;
        p = p->next;
    }
    _enable();
    
    return total_free;
}

/**
 * @brief Perform garbage collection and defragmentation
 * 
 * Simple coalescing of adjacent free blocks.
 * 
 * @return Number of bytes recovered
 */
uint16_t tsr_garbage_collect(void)
{
    blk_t __far *p = free_list;
    blk_t __far *next;
    uint16_t recovered = 0;
    uint32_t current_time = get_millisecond_timestamp();
    
    /* Only run GC every 5 seconds to avoid overhead */
    if (current_time - last_gc_time < 5000) {
        return 0;
    }
    last_gc_time = current_time;
    
    _disable();
    /* Simple adjacent block coalescing */
    while (p && p->next) {
        next = p->next;
        
        /* Check if blocks are adjacent */
        uint8_t __far *p_end = (uint8_t __far *)p + sizeof(blk_t) + p->size;
        if (p_end == (uint8_t __far *)next) {
            /* Coalesce blocks */
            p->size += sizeof(blk_t) + next->size;
            p->next = next->next;
            recovered += sizeof(blk_t);
        } else {
            p = p->next;
        }
    }
    _enable();
    
    return recovered;
}

/**
 * @brief Check memory integrity
 * 
 * @return 1 if heap is intact, 0 if corrupted
 */
int tsr_check_heap_integrity(void)
{
    blk_t __far *p = free_list;
    uint16_t total_checked = 0;
    
    _disable();
    while (p) {
        /* Check if block is within heap bounds */
        if ((uint8_t __far *)p < (uint8_t __far *)tsr_heap ||
            (uint8_t __far *)p >= (uint8_t __far *)tsr_heap + TSR_HEAP_SIZE) {
            _enable();
            return 0; /* Corruption detected */
        }
        
        /* Check if size is reasonable */
        if (p->size == 0 || p->size > TSR_HEAP_SIZE) {
            _enable();
            return 0; /* Corruption detected */
        }
        
        total_checked += p->size + sizeof(blk_t);
        if (total_checked > TSR_HEAP_SIZE) {
            _enable();
            return 0; /* Corruption detected */
        }
        
        p = p->next;
    }
    _enable();
    
    return 1; /* Heap is intact */
}

/**
 * @brief Allocate memory with alignment
 * 
 * @param size Size in bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
 * @return Aligned pointer or NULL if failed
 */
void *tsr_malloc_aligned(uint16_t size, uint16_t alignment)
{
    void *ptr;
    uintptr_t aligned_ptr;
    
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL; /* Invalid alignment */
    }
    
    /* Allocate extra space for alignment */
    ptr = tsr_malloc(size + alignment - 1);
    if (!ptr) {
        return NULL;
    }
    
    /* Note: metrics already updated by tsr_malloc() call above */
    
    aligned_ptr = ((uintptr_t)ptr + alignment - 1) & ~(alignment - 1);
    
    /* For simplicity, we return the aligned pointer but this wastes some space */
    return (void *)aligned_ptr;
}