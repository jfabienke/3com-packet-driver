/**
 * @file memory_barriers.h
 * @brief Memory ordering and barrier primitives for concurrent access
 *
 * Provides READ_ONCE/WRITE_ONCE macros and memory barriers to ensure
 * proper ordering between ISR and mainline code without excessive volatile usage.
 */

#ifndef _MEMORY_BARRIERS_H_
#define _MEMORY_BARRIERS_H_

#include "portabl.h"
#include <stdint.h>

/* Static variables for barrier operations */
static volatile uint8_t __compiler_barrier_dummy = 0;
static volatile uint8_t __memory_barrier_dummy = 0;

/**
 * @brief Compiler memory barrier
 * 
 * Prevents compiler from reordering memory operations across this point.
 * Uses compiler-specific pragmas where available, volatile access fallback.
 */
#ifdef __WATCOMC__
    /* Open Watcom - use volatile memory access as compiler barrier */
    /* Note: Watcom doesn't support 'memory' clobber like GCC - use volatile access */
    static void __compiler_barrier_func(void) { (void)__compiler_barrier_dummy; }
    #define COMPILER_BARRIER() __compiler_barrier_func()
#elif defined(__GNUC__)
    /* GCC/Clang */
    #define COMPILER_BARRIER() __asm__ __volatile__("" : : : "memory")
#else
    /* MS C, Borland, others - volatile access fallback */
    static void __compiler_barrier_func(void) { (void)__compiler_barrier_dummy; }
    #define COMPILER_BARRIER() __compiler_barrier_func()
#endif

/**
 * @brief Full memory barrier (compiler + CPU)
 * 
 * Uses XCHG instruction which is atomic and provides full ordering.
 * Safe and legal on all 286+ systems.
 */
#define MEMORY_BARRIER() do { \
    COMPILER_BARRIER(); \
    _asm { \
        mov al, __memory_barrier_dummy \
        xchg al, __memory_barrier_dummy \
    } \
    COMPILER_BARRIER(); \
} while (0)

/**
 * @brief Read memory location once without compiler refetching
 * 
 * Ensures single read of volatile location, preventing compiler from
 * re-reading the value multiple times or optimizing away the read.
 */
#define READ_ONCE(x) ({ \
    union { typeof(x) __val; char __c[sizeof(x)]; } __u; \
    volatile typeof(x) *__p = &(x); \
    __u.__val = *__p; \
    COMPILER_BARRIER(); \
    __u.__val; \
})

/* Simplified version for compilers without typeof */
#define READ_ONCE_U8(x)  (*((volatile uint8_t *)&(x)))
#define READ_ONCE_U16(x) (*((volatile uint16_t *)&(x)))
#define READ_ONCE_U32(x) (*((volatile uint32_t *)&(x)))

/**
 * @brief Write memory location once without compiler duplicating
 * 
 * Ensures single write to volatile location, preventing compiler from
 * duplicating the write or reordering with other operations.
 */
#define WRITE_ONCE(x, val) do { \
    union { typeof(x) __val; char __c[sizeof(x)]; } __u = \
        { .__val = (val) }; \
    volatile typeof(x) *__p = &(x); \
    COMPILER_BARRIER(); \
    *__p = __u.__val; \
} while (0)

/* Simplified version for compilers without typeof */
#define WRITE_ONCE_U8(x, val)  do { *((volatile uint8_t *)&(x)) = (val); } while(0)
#define WRITE_ONCE_U16(x, val) do { *((volatile uint16_t *)&(x)) = (val); } while(0)
#define WRITE_ONCE_U32(x, val) do { *((volatile uint32_t *)&(x)) = (val); } while(0)

/**
 * @brief Acquire barrier (loads cannot move before this point)
 * 
 * Used after lock acquisition or reading a synchronization variable.
 * On x86, loads are not reordered with other loads, so only compiler barrier needed.
 */
#define ACQUIRE_BARRIER() COMPILER_BARRIER()

/**
 * @brief Release barrier (stores cannot move after this point)
 * 
 * Used before lock release or writing a synchronization variable.
 * On x86, stores are not reordered with other stores, so only compiler barrier needed.
 */
#define RELEASE_BARRIER() COMPILER_BARRIER()

/**
 * @brief IRQ-safe state save/restore helpers
 * 
 * Properly saves and restores interrupt flag state for critical sections.
 * Width-correct for both 16-bit and 32-bit builds.
 */
#ifdef __386__
    typedef uint32_t irq_flags_t;
#else
    typedef uint16_t irq_flags_t;
#endif

static inline irq_flags_t irq_save(void) {
    irq_flags_t flags = 0;  /* Initialized to suppress W200; asm block assigns actual value */
    COMPILER_BARRIER();
#ifdef __386__
    _asm {
        pushfd
        pop eax
        mov flags, eax
        cli
    }
#else
    _asm {
        pushf
        pop ax
        mov flags, ax
        cli
    }
#endif
    COMPILER_BARRIER();
    return flags;
}

static inline void irq_restore(irq_flags_t flags) {
    COMPILER_BARRIER();
#ifdef __386__
    _asm {
        mov eax, flags
        push eax
        popfd
    }
#else
    _asm {
        mov ax, flags
        push ax
        popf
    }
#endif
    COMPILER_BARRIER();
}

/**
 * @brief Atomic compare-and-swap for lock-free algorithms
 * @param ptr Pointer to variable to update
 * @param old_val Expected old value
 * @param new_val New value to write if old matches
 * @return true if swap occurred, false if old value didn't match
 */
static inline bool atomic_cmpxchg_u8(volatile uint8_t *ptr, uint8_t old_val, uint8_t new_val) {
    uint8_t result;
    
#ifdef __386__
    _asm {
        mov al, old_val
        mov dl, new_val
        mov bx, ptr
        lock cmpxchg [bx], dl
        setz result
    }
#else
    /* 286 doesn't have CMPXCHG - use CLI/STI */
    irq_flags_t flags = irq_save();
    if (*ptr == old_val) {
        *ptr = new_val;
        result = 1;
    } else {
        result = 0;
    }
    irq_restore(flags);
#endif
    
    return result;
}

/**
 * @brief Atomic increment with return value
 * @param ptr Pointer to counter
 * @return New value after increment
 */
static inline uint16_t atomic_inc_u16(volatile uint16_t *ptr) {
    uint16_t result;
    
#ifdef __386__
    _asm {
        mov bx, ptr
        mov ax, 1
        lock xadd [bx], ax
        inc ax
        mov result, ax
    }
#else
    /* 286 uses CLI/STI */
    irq_flags_t flags = irq_save();
    (*ptr)++;
    result = *ptr;
    irq_restore(flags);
#endif
    
    return result;
}

#endif /* _MEMORY_BARRIERS_H_ */
