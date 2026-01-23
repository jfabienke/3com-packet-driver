/**
 * @file tsr_defensive.h  
 * @brief TSR Defensive Programming C Interface
 *
 * C-callable wrappers for TSR defensive programming techniques
 * implemented in assembly language. Provides DOS safety checks,
 * stack management, and vector monitoring capabilities.
 */

#ifndef TSR_DEFENSIVE_H
#define TSR_DEFENSIVE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DOS Safety Functions */

/**
 * @brief Check if DOS is safe to call (InDOS flag = 0)
 * @return true if DOS is available, false if busy
 */
bool dos_is_safe(void);

/**
 * @brief Complete DOS safety check (InDOS + critical error flags)
 * @return true if completely safe, false if any flag set
 */
bool dos_is_completely_safe(void);

/**
 * @brief Initialize DOS safety monitoring
 * @return 0 on success, non-zero on error
 */
int dos_safety_init(void);

/* Stack Management Functions */

/**
 * @brief Switch to safe ISR stack for C callback
 * @note Call before invoking C functions from ISR context
 */
void tsr_switch_to_safe_stack(void);

/**
 * @brief Restore original caller stack
 * @note Call after C function completes in ISR context  
 */
void tsr_restore_caller_stack(void);

/* Vector Management Functions */

/**
 * @brief Check if we still own our interrupt vectors
 * @return Number of vectors that have been hijacked
 */
int check_vector_ownership(void);

/**
 * @brief Perform periodic vector monitoring and recovery
 * @return Number of vectors recovered
 */
int periodic_vector_monitoring(void);

/* Deferred Work Functions */

/**
 * @brief Add work item to deferred queue
 * @param work_func Function pointer to execute later
 * @return 0 on success, -1 if queue full
 */
int deferred_add_work(void (*work_func)(void));

/**
 * @brief Process pending deferred work items
 * @return Number of work items processed
 */
int deferred_process_pending(void);

/**
 * @brief Check if deferred work queue has items
 * @return Number of pending work items
 */
int deferred_work_pending(void);

/* Error Recovery Functions */

/**
 * @brief Trigger emergency TSR recovery
 * @return 0 if recovery successful, non-zero if failed
 */
int tsr_emergency_recovery(void);

/**
 * @brief Validate TSR integrity
 * @return 0 if all checks pass, non-zero for corruption
 */
int tsr_validate_integrity(void);

#ifdef __cplusplus
}
#endif

#endif /* TSR_DEFENSIVE_H */