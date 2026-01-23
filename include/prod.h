/**
 * @file production.h
 * @brief Production build configuration for size optimization
 * 
 * This header provides macros and definitions for production builds
 * that eliminate debug code, logging, and non-essential features
 * to minimize TSR memory footprint.
 */

#ifndef PRODUCTION_H
#define PRODUCTION_H

#ifdef PRODUCTION

/* ============================================================
 * Logging Macros - Compile to nothing in production
 * ============================================================ */
#define LOG_DEBUG(...)      ((void)0)
#define LOG_INFO(...)       ((void)0)
#define LOG_WARNING(...)    ((void)0)
#define LOG_ERROR(...)      ((void)0)
#define LOG_CRITICAL(...)   ((void)0)
#define LOG_TRACE(...)      ((void)0)

/* Debug logging functions become no-ops */
#define debug_log(...)      ((void)0)
#define debug_log_info(...) ((void)0)
#define debug_log_warning(...) ((void)0)
#define debug_log_error(...)   ((void)0)
#define debug_log_debug(...)   ((void)0)
#define debug_print(...)    ((void)0)
#define debug_printf(...)   ((void)0)

/* ============================================================
 * Assertion Macros - Removed in production
 * ============================================================ */
#define ASSERT(condition)           ((void)0)
#define ASSERT_MSG(condition, msg)  ((void)0)
#define DEBUG_ASSERT(condition)     ((void)0)
#define VERIFY(condition)           (condition)  /* Still evaluate but don't check */

/* ============================================================
 * Debug-Only Code Blocks
 * ============================================================ */
#define DEBUG_ONLY(code)            /* Remove debug-only code */
#define DEBUG_CODE(code)            /* Remove debug code blocks */
#define IF_DEBUG(code)              /* Remove conditional debug */
#define PRODUCTION_ONLY(code)       code  /* Include production-only code */

/* ============================================================
 * Error Messages - Use codes instead of strings
 * ============================================================ */
#define ERROR_MSG(msg)              NULL  /* Don't store error strings */
#define ERROR_STR(code, msg)        code  /* Return code only */

/* ============================================================
 * Statistics and Monitoring - Optional removal
 * ============================================================ */
#ifdef NO_STATS
  #define UPDATE_STAT(stat, value)  ((void)0)
  #define INCREMENT_STAT(stat)      ((void)0)
  #define RECORD_METRIC(metric)     ((void)0)
#endif

/* ============================================================
 * Diagnostic Features - Removed in production
 * ============================================================ */
#define DIAGNOSTIC_MODE             0
#define ENABLE_DIAGNOSTICS          0
#define DIAGNOSTIC_CHECK(...)       ((void)0)
#define DIAGNOSTIC_REPORT(...)      ((void)0)

/* ============================================================
 * Memory Debugging - Removed in production
 * ============================================================ */
#define MEM_DEBUG_ALLOC(size)       malloc(size)
#define MEM_DEBUG_FREE(ptr)         free(ptr)
#define MEM_CHECK_LEAK()            ((void)0)
#define MEM_VALIDATE()              ((void)0)

/* ============================================================
 * Performance Monitoring - Optional
 * ============================================================ */
#ifdef NO_PERF_MONITOR
  #define PERF_START(timer)         ((void)0)
  #define PERF_END(timer)           ((void)0)
  #define PERF_RECORD(metric)       ((void)0)
#endif

/* ============================================================
 * Verbose Output - Removed
 * ============================================================ */
#define VERBOSE(...)                ((void)0)
#define VERBOSE_PRINT(...)          ((void)0)
#define TRACE_ENTER(func)           ((void)0)
#define TRACE_EXIT(func)            ((void)0)

/* ============================================================
 * Size Optimization Hints - Watcom C Specific
 * ============================================================ */
/* Note: Watcom C does not support GCC-style attributes.
 * Section placement must be done with #pragma code_seg() directives
 * directly in the source files where functions are defined.
 * 
 * Usage in source files:
 *   #pragma code_seg("HOT_TEXT", "CODE")   // Start hot section
 *   int critical_function() { ... }
 *   #pragma code_seg()                      // Restore default
 *
 *   #pragma code_seg("COLD_TEXT", "CODE")  // Start cold section
 *   int init_function() { ... }
 *   #pragma code_seg()                      // Restore default
 *
 * These macros are kept for documentation purposes only.
 */
#define OPTIMIZE_SIZE               /* Use -os compiler flag */
#define COLD_SECTION                /* Use #pragma code_seg("COLD_TEXT", "CODE") */
#define HOT_SECTION                 /* Use #pragma code_seg("HOT_TEXT", "CODE") */
#define INIT_SECTION                /* Use #pragma code_seg("INIT_TEXT", "CODE") */
#define DISCARD_AFTER_INIT          /* Use #pragma code_seg("COLD_TEXT", "CODE") */

/* ============================================================
 * Production Error Codes (Compact)
 * ============================================================ */
enum {
    ERR_OK = 0,
    ERR_FAIL = 1,
    ERR_MEM = 2,
    ERR_IO = 3,
    ERR_HW = 4,
    ERR_INIT = 5,
    ERR_PARAM = 6,
    ERR_TIMEOUT = 7,
    ERR_BUSY = 8,
    ERR_UNSUP = 9
};

#else /* !PRODUCTION - Development Build */

/* Keep existing debug functionality */
#include "logging.h"
#include "diag.h"
#include "debug.h"

#define OPTIMIZE_SIZE
#define COLD_SECTION
#define HOT_SECTION
#define INIT_SECTION
#define DISCARD_AFTER_INIT
#define PRODUCTION_ONLY(code)
#define DEBUG_ONLY(code) code

#endif /* PRODUCTION */

/* ============================================================
 * Common Optimizations (Both Debug and Production)
 * ============================================================ */

/* Use smaller types where possible */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed char    s8;
typedef signed short   s16;
typedef signed long    s32;

/* Inline hints for critical functions */
#define ALWAYS_INLINE   __attribute__((always_inline)) inline
#define NEVER_INLINE    __attribute__((noinline))
#define LIKELY(x)       __builtin_expect(!!(x), 1)
#define UNLIKELY(x)     __builtin_expect(!!(x), 0)

/* Structure packing */
#define PACKED          __attribute__((packed))

#endif /* PRODUCTION_H */