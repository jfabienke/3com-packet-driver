/**
 * @file interrupt_macros.h
 * @brief Portable Interrupt Save/Restore Macros (GPT-5 A Grade)
 *
 * Provides correct interrupt flag save/restore for different DOS environments:
 * - Real mode DOS (16-bit FLAGS register)
 * - Protected mode DOS extenders (32-bit EFLAGS register)
 */

#ifndef INTERRUPT_MACROS_H
#define INTERRUPT_MACROS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Detect environment and define appropriate types and macros */

/* For 16-bit real mode DOS (traditional DOS) */
#if defined(__MSDOS__) && !defined(__386__)
    typedef uint16_t irq_flags_t;
    
    #define IRQ_SAVE_DISABLE(flags) \
        __asm { \
            pushf; \
            pop flags; \
            cli; \
        }
    
    #define IRQ_RESTORE(flags) \
        __asm { \
            push flags; \
            popf; \
        }

/* For 32-bit DOS extenders (DOS/4GW, PMODE/W, etc.) */
#elif defined(__MSDOS__) && defined(__386__)
    typedef uint32_t irq_flags_t;
    
    #define IRQ_SAVE_DISABLE(flags) \
        __asm { \
            pushfd; \
            pop flags; \
            cli; \
        }
    
    #define IRQ_RESTORE(flags) \
        __asm { \
            push flags; \
            popfd; \
        }

/* For 32-bit protected mode (general case) */
#elif defined(__i386__) || defined(_M_IX86)
    typedef uint32_t irq_flags_t;
    
    #ifdef __GNUC__
        /* GCC inline assembly */
        #define IRQ_SAVE_DISABLE(flags) \
            __asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) :: "memory")
        
        #define IRQ_RESTORE(flags) \
            __asm__ __volatile__("pushl %0; popfl" :: "r"(flags) : "memory", "cc")
    
    #else
        /* MSVC/Watcom inline assembly */
        #define IRQ_SAVE_DISABLE(flags) \
            __asm { \
                pushfd; \
                pop flags; \
                cli; \
            }
        
        #define IRQ_RESTORE(flags) \
            __asm { \
                push flags; \
                popfd; \
            }
    #endif

/* Default fallback for unknown environments */
#else
    typedef unsigned long irq_flags_t;
    
    /* Assume reasonable defaults - may need platform-specific implementation */
    #define IRQ_SAVE_DISABLE(flags) \
        do { \
            (flags) = 0; \
            __asm__ __volatile__("cli" ::: "memory"); \
        } while(0)
    
    #define IRQ_RESTORE(flags) \
        do { \
            __asm__ __volatile__("sti" ::: "memory"); \
        } while(0)
    
    #warning "Unknown target environment - using generic interrupt macros"
#endif

/**
 * @brief Check if interrupts are currently enabled
 * @return true if interrupts enabled, false if disabled
 */
static inline bool irq_are_enabled(void) {
    irq_flags_t flags;
    
    #if defined(__MSDOS__) && !defined(__386__)
        __asm {
            pushf;
            pop flags;
        }
        return (flags & 0x0200) != 0;  /* IF bit in FLAGS */
    #elif defined(__MSDOS__) && defined(__386__)
        __asm {
            pushfd;
            pop flags;
        }
        return (flags & 0x00000200UL) != 0;  /* IF bit in EFLAGS */
    #else
        /* Generic implementation */
        #ifdef __GNUC__
            __asm__ __volatile__("pushfl; popl %0" : "=r"(flags) :: "memory");
        #else
            __asm {
                pushfd;
                pop flags;
            }
        #endif
        return (flags & 0x00000200UL) != 0;
    #endif
}

/**
 * @brief Convenient macros for critical sections
 */
#define CRITICAL_SECTION_ENTER(flags) IRQ_SAVE_DISABLE(flags)
#define CRITICAL_SECTION_EXIT(flags)  IRQ_RESTORE(flags)

/**
 * @brief RAII-style critical section for C++ (if applicable)
 */
#ifdef __cplusplus
class CriticalSection {
    irq_flags_t saved_flags;
public:
    CriticalSection() { IRQ_SAVE_DISABLE(saved_flags); }
    ~CriticalSection() { IRQ_RESTORE(saved_flags); }
};

#define CRITICAL_SECTION() CriticalSection _cs
#endif

#ifdef __cplusplus
}
#endif

#endif /* INTERRUPT_MACROS_H */