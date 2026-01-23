/**
 * @file discardable.h
 * @brief Discardable Memory Segment Definitions
 * 
 * Provides macros and utilities for marking code and data as discardable
 * after initialization. This saves memory by allowing the DOS memory
 * manager to reclaim initialization code after boot.
 * 
 * ARCHITECTURE: DOS TSR with discardable INIT segment
 * - INIT segment: Discarded after initialization complete
 * - RESIDENT segment: Remains in memory for TSR operation
 */

#ifndef DISCARDABLE_H
#define DISCARDABLE_H

/* Compiler-specific segment declarations for DOS */
#ifdef __WATCOMC__

/* Watcom C specific pragmas for segment control */
#pragma aux (__cdecl) __init_start;
#pragma aux (__cdecl) __init_end;

/* INIT segment - discarded after initialization */
#define INIT_SEGMENT        __based(__segname("_INIT"))
#define INIT_CODE           __based(__segname("_INITCODE"))
#define INIT_DATA           __based(__segname("_INITDATA"))

/* Function/data placement macros */
#define __init              __based(__segname("_INITCODE"))
#define __initdata          __based(__segname("_INITDATA"))

/* Segment boundary markers */
extern void __init_start(void);
extern void __init_end(void);

#else

/* Generic compiler - no segment control */
#define INIT_SEGMENT
#define INIT_CODE
#define INIT_DATA
#define __init
#define __initdata

#endif

/* Memory management functions */

/**
 * @brief Calculate size of discardable INIT segment
 * 
 * @return Size of INIT segment in bytes
 */
uint32_t discardable_get_init_segment_size(void);

/**
 * @brief Free INIT segment memory
 * 
 * Releases INIT segment memory back to DOS after initialization
 * is complete. This saves memory for the TSR.
 * 
 * @return Bytes freed, or 0 if not supported
 */
uint32_t discardable_free_init_segment(void);

/**
 * @brief Check if INIT segment is still available
 * 
 * @return 1 if INIT segment available, 0 if discarded
 */
int discardable_init_available(void);

/* Initialization phase tracking */

/**
 * @brief Mark initialization phase as complete
 * 
 * Signals that initialization is finished and INIT segment
 * can be safely discarded.
 */
void discardable_mark_init_complete(void);

/**
 * @brief Check if initialization is complete
 * 
 * @return 1 if initialization complete, 0 otherwise
 */
int discardable_is_init_complete(void);

/* Utility macros for common patterns */

/**
 * @brief Declare initialization-only function
 * 
 * Example:
 * INIT_FUNCTION int my_init_function(void) {
 *     // This code will be discarded after init
 * }
 */
#define INIT_FUNCTION       __init

/**
 * @brief Declare initialization-only data
 * 
 * Example:
 * INIT_DATA char init_message[] = "Initializing...";
 */
#define INIT_DATA_DECL      __initdata

/**
 * @brief Mark function as discardable after init
 */
#define DISCARDABLE         __init

/* Memory layout constants */
#define INIT_SEGMENT_ALIGNMENT  16      /* Paragraph alignment */
#define MAX_INIT_SEGMENT_SIZE   32768   /* Maximum INIT segment size */

/* Feature detection */
#ifdef __WATCOMC__
#define HAS_DISCARDABLE_SEGMENTS    1
#else
#define HAS_DISCARDABLE_SEGMENTS    0
#endif

#endif /* DISCARDABLE_H */