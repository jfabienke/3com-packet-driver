/**
 * @file atomic_time.h
 * @brief Atomic 32-bit time access for 16-bit x86 systems
 *
 * Provides atomic read/write functions for 32-bit timestamps on 16-bit systems
 * where timer ISR may update the value concurrently. Uses double-read verification
 * or CLI protection as appropriate.
 */

#ifndef _ATOMIC_TIME_H_
#define _ATOMIC_TIME_H_

#include <stdint.h>
#include "memory_barriers.h"

/**
 * @brief Atomically read 32-bit timestamp
 * 
 * On 16-bit systems, 32-bit reads are not atomic. This function uses
 * CLI protection. On 32-bit systems, checks alignment before assuming
 * atomic read capability.
 *
 * @param timestamp Pointer to 32-bit timestamp variable
 * @return Current timestamp value
 */
static inline uint32_t atomic_time_read(volatile uint32_t *timestamp) {
    uint32_t value;
    
#ifdef __386__
    /* 32-bit CPU - single read is atomic only if 4-byte aligned */
    if (((uint32_t)timestamp & 3U) == 0) {
        /* Aligned - single read is atomic */
        value = *timestamp;
    } else {
        /* Misaligned - need CLI protection */
        irq_flags_t flags = irq_save();
        value = *timestamp;
        irq_restore(flags);
    }
#else
    /* 16-bit CPU - always need atomic access */
    irq_flags_t flags = irq_save();
    value = *timestamp;
    irq_restore(flags);
    
    /* Alternative Method: Double-read verification (if CLI undesirable)
    uint32_t val1, val2;
    do {
        val1 = *timestamp;
        val2 = *timestamp;
    } while (val1 != val2);
    value = val1;
    */
#endif
    
    return value;
}

/**
 * @brief Atomically write 32-bit timestamp
 *
 * @param timestamp Pointer to 32-bit timestamp variable
 * @param value New timestamp value
 */
static inline void atomic_time_write(volatile uint32_t *timestamp, uint32_t value) {
#ifdef __386__
    /* 32-bit CPU - single write is atomic if aligned */
    if (((uint32_t)timestamp & 3U) == 0) {
        *timestamp = value;
    } else {
        irq_flags_t flags = irq_save();
        *timestamp = value;
        irq_restore(flags);
    }
#else
    /* 16-bit CPU - need atomic access */
    irq_flags_t flags = irq_save();
    *timestamp = value;
    irq_restore(flags);
#endif
}

/**
 * @brief Atomically increment 32-bit counter
 *
 * @param counter Pointer to 32-bit counter variable
 * @return New counter value after increment
 */
static inline uint32_t atomic_time_increment(volatile uint32_t *counter) {
    uint32_t new_value;
    
#ifdef __386__
    /* 32-bit CPU - use LOCK prefix for atomic increment */
    _asm {
        mov ebx, counter
        lock inc dword ptr [ebx]
        mov eax, dword ptr [ebx]
        mov new_value, eax
    }
#else
    /* 16-bit CPU - need CLI protection */
    irq_flags_t flags = irq_save();
    (*counter)++;
    new_value = *counter;
    irq_restore(flags);
#endif
    
    return new_value;
}

/**
 * @brief Get current system tick count atomically
 *
 * Reads the BIOS timer tick counter at 0000:046C atomically
 * @return Current tick count (18.2 Hz)
 */
static inline uint32_t atomic_get_ticks(void) {
    uint32_t ticks;
    
#ifdef __386__
    /* 32-bit read from BIOS data area */
    _asm {
        push ds
        xor ax, ax
        mov ds, ax
        mov eax, dword ptr ds:[046Ch]
        mov ticks, eax
        pop ds
    }
#else
    /* 16-bit - need atomic read */
    irq_flags_t flags = irq_save();
    
    _asm {
        push ds
        xor ax, ax
        mov ds, ax
        mov ax, word ptr ds:[046Ch]
        mov dx, word ptr ds:[046Eh]
        pop ds
        
        mov word ptr ticks, ax
        mov word ptr ticks+2, dx
    }
    
    irq_restore(flags);
#endif
    
    return ticks;
}

#endif /* _ATOMIC_TIME_H_ */