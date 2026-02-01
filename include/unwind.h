/**
 * @file unwind.h
 * @brief Error unwind and cleanup management definitions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * GPT-5: Comprehensive error recovery system to ensure clean
 * shutdown and resource deallocation on initialization failure.
 */

#ifndef _UNWIND_H_
#define _UNWIND_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/* Unwind phase definitions - must match initialization order */
typedef enum {
    UNWIND_PHASE_NONE,            /* 0 */
    UNWIND_PHASE_LOGGING,         /* 1 */
    UNWIND_PHASE_CPU_DETECT,      /* 2 */
    UNWIND_PHASE_PLATFORM_PROBE,  /* 3 */
    UNWIND_PHASE_CONFIG,          /* 4 */
    UNWIND_PHASE_CHIPSET,         /* 5 */
    UNWIND_PHASE_VDS,             /* 6 */
    UNWIND_PHASE_MEMORY_CORE,     /* 7 */
    UNWIND_PHASE_PACKET_OPS,      /* 8: packet operations init */
    UNWIND_PHASE_HARDWARE,        /* 9 */
    UNWIND_PHASE_MEMORY_DMA,      /* 10 */
    UNWIND_PHASE_TSR,             /* 11 */
    UNWIND_PHASE_API_HOOKS,       /* 12 */
    UNWIND_PHASE_INTERRUPTS,      /* 13 */
    UNWIND_PHASE_API_ACTIVE,      /* 14 */
    UNWIND_PHASE_COMPLETE         /* 15 */
} unwind_phase_t;

/* Function prototypes */

/**
 * @brief Initialize unwind system
 * 
 * Captures initial system state for restoration.
 * Must be called at the very beginning of initialization.
 */
void unwind_init(void);

/**
 * @brief Mark successful completion of a phase
 * 
 * Updates the unwind system to track which phases have
 * been successfully initialized and need cleanup on error.
 * 
 * @param phase Phase that completed successfully
 */
void unwind_mark_phase_complete(unwind_phase_t phase);

/**
 * @brief Execute error unwind from current phase
 * 
 * Performs complete cleanup of all initialized subsystems
 * in reverse order, restoring the system to pre-init state.
 * 
 * @param error_code Error code that triggered unwind
 * @param error_msg Error message to display (can be NULL)
 */
void unwind_execute(int error_code, const char *error_msg);

/**
 * @brief Register allocated memory for cleanup
 * 
 * Tracks dynamically allocated memory that needs to be
 * freed during unwind if not already freed by subsystems.
 * 
 * @param ptr Memory pointer to track
 */
void unwind_register_memory(void *ptr);

/**
 * @brief Register PnP resource for cleanup
 * 
 * Tracks PnP resources that need to be released during unwind.
 * 
 * @param resource PnP resource ID or handle
 */
void unwind_register_pnp(uint16_t resource);

/**
 * @brief Get current unwind phase
 * 
 * Returns the current phase for diagnostic purposes.
 * 
 * @return Current unwind phase
 */
unwind_phase_t unwind_get_phase(void);

/**
 * @brief Check if unwind system is initialized
 * 
 * @return true if unwind system is ready
 */
bool unwind_is_initialized(void);

/* Convenience macros for error handling */

/**
 * @brief Check result and unwind on error
 * 
 * If result is negative, triggers unwind with given message.
 */
#define CHECK_RESULT_UNWIND(result, msg) \
    do { \
        if ((result) < 0) { \
            unwind_execute(result, msg); \
            return result; \
        } \
    } while(0)

/**
 * @brief Check pointer and unwind if NULL
 * 
 * If pointer is NULL, triggers unwind with given message.
 */
#define CHECK_PTR_UNWIND(ptr, msg) \
    do { \
        if (!(ptr)) { \
            unwind_execute(-1, msg); \
            return -1; \
        } \
    } while(0)

/**
 * @brief Mark phase and continue
 * 
 * Marks phase complete for unwind tracking.
 */
#define MARK_PHASE_COMPLETE(phase) \
    unwind_mark_phase_complete(phase)

#ifdef __cplusplus
}
#endif

#endif /* _UNWIND_H_ */
