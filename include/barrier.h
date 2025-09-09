/**
 * @file barrier.h
 * @brief Memory barrier primitives for DMA descriptor coherency
 *
 * Ensures descriptor writes reach memory before doorbell writes.
 * Critical for x86 real mode DMA operations.
 */

#ifndef _BARRIER_H_
#define _BARRIER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * dma_wmb(): Write memory barrier for DMA descriptors
 * 
 * Ensures all prior memory writes (descriptor updates) are visible
 * to the device before we ring a doorbell (IO write).
 * On 386/486 real mode, an out to port 0x80 is widely used as serialize/delay.
 */
static inline void dma_wmb(void) {
#if defined(__WATCOMC__)
    /* Watcom C inline assembly */
    _asm {
        mov dx, 0x80
        xor al, al
        out dx, al
    }
#elif defined(_MSC_VER) || defined(__TURBOC__)
    /* Microsoft C / Turbo C inline assembly */
    __asm {
        mov dx, 0x80
        xor al, al
        out dx, al
    }
#else
    /* GCC inline assembly */
    __asm__ __volatile__ ("outb %%al, $0x80" : : "a"(0) : "memory");
#endif
}

/**
 * dma_rmb(): Read memory barrier (for completeness)
 * 
 * Ensures all prior reads complete before subsequent reads.
 * Less critical than wmb but included for symmetry.
 */
static inline void dma_rmb(void) {
    /* On x86, reads are not reordered with other reads */
    /* But we can use the same technique for consistency */
    dma_wmb();
}

#ifdef __cplusplus
}
#endif

#endif /* _BARRIER_H_ */