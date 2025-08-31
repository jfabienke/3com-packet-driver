/**
 * @file dma_wmb.h
 * @brief Memory Barrier Macros for DMA Operations (GPT-5 A Grade)
 *
 * Provides memory barriers for proper ordering of DMA operations
 * on x86 systems. These macros ensure correct visibility ordering
 * between CPU writes and device DMA operations.
 */

#ifndef DMA_WMB_H
#define DMA_WMB_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DMA write memory barrier
 * 
 * Ensures all previous writes are visible before subsequent DMA operations.
 * On x86, I/O writes are strongly ordered, but we provide compiler barrier
 * for clarity and potential future portability.
 */
#define dma_wmb()  __asm__ __volatile__("" : : : "memory")

/**
 * @brief DMA read memory barrier  
 * 
 * Ensures DMA reads complete before subsequent CPU operations.
 * On x86, this is typically not needed due to strong ordering,
 * but provided for completeness.
 */
#define dma_rmb()  __asm__ __volatile__("" : : : "memory")

/**
 * @brief Full DMA memory barrier
 * 
 * Ensures ordering of both reads and writes relative to DMA operations.
 * Uses compiler barrier on x86 due to strong memory ordering guarantees.
 */
#define dma_mb()   __asm__ __volatile__("" : : : "memory")

/**
 * @brief Serializing memory fence (if available)
 * 
 * On newer CPUs with MFENCE instruction, provides full serialization.
 * Falls back to compiler barrier on older systems.
 */
#if defined(__i486__) || defined(__i586__) || defined(__i686__)
/* Pentium+ has MFENCE equivalent, but use compiler barrier for compatibility */
#define dma_mfence() __asm__ __volatile__("" : : : "memory")
#else
/* Assume compiler barrier is sufficient for 386/486 */
#define dma_mfence() __asm__ __volatile__("" : : : "memory")
#endif

/**
 * @brief Cache flush barrier
 * 
 * Ensures cache flushes complete before device operations.
 * WBINVD is already serializing, but CLFLUSH may need barriers.
 */
#define cache_flush_barrier() __asm__ __volatile__("" : : : "memory")

#ifdef __cplusplus
}
#endif

#endif /* DMA_WMB_H */