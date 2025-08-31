/**
 * @file portability.h
 * @brief DOS Compiler Portability Layer
 * 
 * Provides macros and definitions for cross-compiler compatibility
 * on DOS systems. Supports Borland C, Open Watcom, and Microsoft C.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef PORTABILITY_H
#define PORTABILITY_H

/* Include standard headers for size_t */
#include <stddef.h>

/* Compiler detection and setup */
#ifdef __TURBOC__
    /* Borland Turbo C/C++ */
    #define COMPILER_BORLAND
    #define FAR far
    #define NEAR near
    #define HUGE huge
    #define CDECL cdecl
    #define PASCAL pascal
    #define INTERRUPT interrupt
    #define INLINE  /* Not supported */
    #define HAS_FARMEM 1
    
#elif defined(__WATCOMC__)
    /* Open Watcom C/C++ */
    #define COMPILER_WATCOM
    #define FAR __far
    #define NEAR __near
    #define HUGE __huge
    #define CDECL __cdecl
    #define PASCAL __pascal
    #define INTERRUPT __interrupt
    #define INLINE  /* Not supported in C mode */
    #define HAS_FARMEM 1
    
#elif defined(_MSC_VER)
    /* Microsoft C/C++ */
    #define COMPILER_MSC
    #if _MSC_VER <= 600
        /* MSC 6.0 and earlier */
        #define FAR _far
        #define NEAR _near
        #define HUGE _huge
        #define CDECL _cdecl
        #define PASCAL _pascal
        #define INTERRUPT _interrupt
        #define INLINE  /* Not supported */
    #else
        /* MSC 7.0+ */
        #define FAR __far
        #define NEAR __near
        #define HUGE __huge
        #define CDECL __cdecl
        #define PASCAL __pascal
        #define INTERRUPT __interrupt
        #define INLINE __inline
    #endif
    #define HAS_FARMEM 1
    
#else
    /* Unknown compiler - use safe defaults */
    #define COMPILER_UNKNOWN
    #define FAR
    #define NEAR
    #define HUGE
    #define CDECL
    #define PASCAL
    #define INTERRUPT
    #define INLINE
    #define HAS_FARMEM 0
#endif

/* Standard integer types for DOS compilers that lack stdint.h */
#if !defined(_STDINT_H_) && !defined(_STDINT_H) && !defined(__STDINT_H)
    typedef unsigned char       uint8_t;
    typedef signed char         int8_t;
    typedef unsigned short      uint16_t;
    typedef signed short        int16_t;
    typedef unsigned long       uint32_t;
    typedef signed long         int32_t;
#endif

/* Additional types for DOS compatibility */
#ifndef _SSIZE_T_DEFINED
    typedef int                 ssize_t;
    #define _SSIZE_T_DEFINED
#endif

#ifndef _UINTPTR_T_DEFINED
    typedef unsigned int        uintptr_t;
    #define _UINTPTR_T_DEFINED
#endif

/* Boolean type for DOS C89 compatibility */
#ifndef __cplusplus
    #ifndef _BOOL_T_DEFINED
        typedef int bool;
        #define true 1
        #define false 0
        #define _BOOL_T_DEFINED
    #endif
#endif

/* Critical section helper functions - compiler specific implementations */

#ifdef COMPILER_BORLAND
    /* Borland helper functions */
    static unsigned short save_flags_cli(void) {
        unsigned short f;
        asm {
            pushf
            cli              /* Disable interrupts immediately after saving flags */
            pop word ptr f   /* Then pop the saved flags */
        }
        return f;
    }
    
    static void restore_flags(unsigned short f) {
        asm {
            push word ptr f
            popf
        }
    }
    
#elif defined(COMPILER_WATCOM)
    /* Watcom uses pragma aux for inline assembly - make functions static */
    static unsigned short save_flags_cli(void);
    #pragma aux save_flags_cli = \
        "pushf" \
        "cli" \
        "pop ax" \
        value [ax] \
        modify [ax];
    
    static void restore_flags(unsigned short f);
    #pragma aux restore_flags = \
        "push ax" \
        "popf" \
        parm [ax];
        
#elif defined(COMPILER_MSC)
    /* Microsoft C helper functions */
    static unsigned short save_flags_cli(void) {
        unsigned short f;
        _asm {           /* Use _asm for classic 16-bit MSC */
            pushf
            cli              /* Disable interrupts immediately after saving flags */
            pop word ptr f   /* Then pop the saved flags */
        }
        return f;
    }
    
    static void restore_flags(unsigned short f) {
        _asm {           /* Use _asm for classic 16-bit MSC */
            push word ptr f
            popf
        }
    }
    
#else
    /* Fallback for unknown compilers - basic implementation */
    #include <dos.h>
    static unsigned short save_flags_cli(void) {
        /* Save current state - this is approximate */
        unsigned short f = 0;
        disable();  /* or _disable() depending on compiler */
        return f;
    }
    
    static void restore_flags(unsigned short f) {
        /* Basic restoration - doesn't preserve original IF state */
        if (f == 0) {
            enable();  /* or _enable() depending on compiler */
        }
    }
#endif

/* Critical section macros using helper functions 
 * WARNING: Only nest if exited in LIFO order with the matching flags variable.
 * Each ENTER must have a matching EXIT using the saved flags for that scope. */
#define CRITICAL_SECTION_ENTER(flags) \
    do { (flags) = save_flags_cli(); } while(0)

#define CRITICAL_SECTION_EXIT(flags) \
    do { restore_flags(flags); } while(0)

/* Atomic 32-bit operations for 16-bit systems */
#define ATOMIC32_READ(var, value, flags) \
    do { \
        CRITICAL_SECTION_ENTER(flags); \
        (value) = (var); \
        CRITICAL_SECTION_EXIT(flags); \
    } while(0)

#define ATOMIC32_WRITE(var, value, flags) \
    do { \
        CRITICAL_SECTION_ENTER(flags); \
        (var) = (value); \
        CRITICAL_SECTION_EXIT(flags); \
    } while(0)

#define ATOMIC32_ADD(var, delta, flags) \
    do { \
        CRITICAL_SECTION_ENTER(flags); \
        (var) += (delta); \
        CRITICAL_SECTION_EXIT(flags); \
    } while(0)

/* Far memory operations */
#ifdef HAS_FARMEM
    #ifdef COMPILER_BORLAND
        #include <mem.h>
        #define FARMEMCPY(d,s,n) _fmemcpy(d,s,n)
        #define FARMEMSET(d,v,n) _fmemset(d,v,n)
        #define FARMEMCMP(s1,s2,n) _fmemcmp(s1,s2,n)
    #elif defined(COMPILER_WATCOM)
        #include <string.h>
        #define FARMEMCPY(d,s,n) _fmemcpy(d,s,n)
        #define FARMEMSET(d,v,n) _fmemset(d,v,n)
        #define FARMEMCMP(s1,s2,n) _fmemcmp(s1,s2,n)
    #elif defined(COMPILER_MSC)
        #include <memory.h>
        #define FARMEMCPY(d,s,n) _fmemcpy(d,s,n)
        #define FARMEMSET(d,v,n) _fmemset(d,v,n)
        #define FARMEMCMP(s1,s2,n) _fmemcmp(s1,s2,n)
    #endif
#else
    /* No far memory support */
    #define FARMEMCPY(d,s,n) memcpy(d,s,n)
    #define FARMEMSET(d,v,n) memset(d,v,n)
    #define FARMEMCMP(s1,s2,n) memcmp(s1,s2,n)
#endif

/* Make far pointer from segment:offset */
#ifdef COMPILER_BORLAND
    #define MK_FP(seg,off) MK_FP(seg,off)
#elif defined(COMPILER_WATCOM)
    #define MK_FP(seg,off) MK_FP(seg,off)
#elif defined(COMPILER_MSC)
    #define MK_FP(seg,off) ((void FAR *)(((unsigned long)(seg) << 16) | (off)))
#else
    #define MK_FP(seg,off) ((void *)(((unsigned long)(seg) << 16) | (off)))
#endif

/* Get segment and offset from far pointer */
#ifdef COMPILER_BORLAND
    #define FP_SEG(fp) FP_SEG(fp)
    #define FP_OFF(fp) FP_OFF(fp)
#elif defined(COMPILER_WATCOM)
    #define FP_SEG(fp) FP_SEG(fp)
    #define FP_OFF(fp) FP_OFF(fp)
#elif defined(COMPILER_MSC)
    #define FP_SEG(fp) ((unsigned)((unsigned long)(fp) >> 16))
    #define FP_OFF(fp) ((unsigned)(fp))
#else
    #define FP_SEG(fp) 0
    #define FP_OFF(fp) ((unsigned)(fp))
#endif

/* NULL definition */
#ifndef NULL
    #ifdef __cplusplus
        #define NULL 0
    #else
        #define NULL ((void *)0)
    #endif
#endif

/* Function attributes */
#ifndef __GNUC__
    #define __attribute__(x)  /* Nothing */
#endif

/* Packing pragmas for structures */
#ifdef COMPILER_BORLAND
    #define PACK_STRUCT_BEGIN
    #define PACK_STRUCT_END
    #define PACK_STRUCT_FIELD(x) x
#elif defined(COMPILER_WATCOM)
    #define PACK_STRUCT_BEGIN _Packed
    #define PACK_STRUCT_END
    #define PACK_STRUCT_FIELD(x) x
#elif defined(COMPILER_MSC)
    #define PACK_STRUCT_BEGIN
    #define PACK_STRUCT_END
    #define PACK_STRUCT_FIELD(x) x
#else
    #define PACK_STRUCT_BEGIN
    #define PACK_STRUCT_END
    #define PACK_STRUCT_FIELD(x) x
#endif

/* Compiler-specific optimizations */
#ifdef COMPILER_WATCOM
    #pragma aux default parm caller []
#endif

/* DOS-specific defines */
#define DOS_MAX_PATH 128
#define DOS_PARAGRAPH_SIZE 16
#define DOS_CONVENTIONAL_MEMORY_LIMIT 0xA0000

/* I/O Port Access Macros - Portable across DOS compilers */
#ifdef COMPILER_BORLAND
    /* Borland uses outportb(port, value) */
    #include <dos.h>
    #define IO_OUT8(port, value)  outportb((port), (value))
    #define IO_IN8(port)          inportb(port)
    #define IO_OUT16(port, value) outport((port), (value))
    #define IO_IN16(port)         inport(port)
    
#elif defined(COMPILER_WATCOM)
    /* Watcom uses outp(port, value) */
    #include <conio.h>
    #define IO_OUT8(port, value)  outp((port), (value))
    #define IO_IN8(port)          inp(port)
    #define IO_OUT16(port, value) outpw((port), (value))
    #define IO_IN16(port)         inpw(port)
    
#elif defined(COMPILER_MSC)
    /* Microsoft C uses outp(port, value) */
    #include <conio.h>
    #define IO_OUT8(port, value)  outp((port), (value))
    #define IO_IN8(port)          inp(port)
    #define IO_OUT16(port, value) outpw((port), (value))
    #define IO_IN16(port)         inpw(port)
    
#else
    /* Fallback - assume Linux-style for testing */
    #define IO_OUT8(port, value)  outb((value), (port))
    #define IO_IN8(port)          inb(port)
    #define IO_OUT16(port, value) outw((value), (port))
    #define IO_IN16(port)         inw(port)
#endif

/* Compatibility aliases for existing code */
#define outb(port, value) IO_OUT8((port), (value))
#define inb(port)         IO_IN8(port)
#define outw(port, value) IO_OUT16((port), (value))
#define inw(port)         IO_IN16(port)

/* Error codes compatibility */
#ifndef SUCCESS
    #define SUCCESS 0
#endif
#ifndef ERROR_INVALID_PARAM
    #define ERROR_INVALID_PARAM -1
#endif
#ifndef ERROR_NO_MEMORY
    #define ERROR_NO_MEMORY -2
#endif
#ifndef ERROR_INVALID_STATE
    #define ERROR_INVALID_STATE -3
#endif
#ifndef ERROR_TIMEOUT
    #define ERROR_TIMEOUT -4
#endif
#ifndef ERROR_BOUNDS
    #define ERROR_BOUNDS -5
#endif

#endif /* PORTABILITY_H */