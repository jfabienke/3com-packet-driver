/**
 * @file tsr_defensive.c
 * @brief TSR Defensive Programming C Implementation
 *
 * C-callable wrappers for TSR defensive programming techniques.
 * Interfaces with assembly language defensive routines.
 */

#include <stdint.h>
#include <stddef.h>
#include <dos.h>
#include "../include/tsr_defensive.h"
#include "../include/logging.h"

/* External assembly function prototypes */
extern int asm_check_dos_safe(void);
extern int asm_check_dos_completely_safe(void);
extern int asm_dos_safety_init(void);
extern int asm_check_vector_ownership(void);
extern int asm_periodic_vector_monitoring(void);
extern int asm_deferred_add_work(void (*work_func)(void));
extern int asm_deferred_process_pending(void);
extern int asm_deferred_work_pending(void);
extern int asm_tsr_emergency_recovery(void);
extern int asm_tsr_validate_integrity(void);

/* Static variables for DOS safety state */
static bool dos_safety_initialized = false;
static uint16_t dos_version = 0;

/**
 * @brief Initialize DOS safety monitoring
 */
int dos_safety_init(void) {
    int result;
    
    if (dos_safety_initialized) {
        return 0; /* Already initialized */
    }
    
    /* Get DOS version for compatibility checks */
    _asm {
        mov ah, 30h
        int 21h
        mov dos_version, ax
    }
    
    /* Call assembly initialization routine */
    result = asm_dos_safety_init();
    if (result == 0) {
        dos_safety_initialized = true;
        
        #ifdef DEBUG_BUILD
        log_debug("DOS safety monitoring initialized (DOS version %d.%d)", 
                  dos_version & 0xFF, (dos_version >> 8) & 0xFF);
        #endif
    }
    
    return result;
}

/**
 * @brief Check if DOS is safe to call (InDOS flag = 0)
 */
bool dos_is_safe(void) {
    if (!dos_safety_initialized) {
        /* If not initialized, assume unsafe */
        return false;
    }
    
    return (asm_check_dos_safe() == 0);
}

/**
 * @brief Complete DOS safety check (InDOS + critical error flags)
 */
bool dos_is_completely_safe(void) {
    if (!dos_safety_initialized) {
        /* If not initialized, assume unsafe */
        return false;
    }
    
    return (asm_check_dos_completely_safe() == 0);
}

/**
 * @brief Switch to safe ISR stack for C callback
 * @note This is a placeholder - actual switching done in assembly
 */
void tsr_switch_to_safe_stack(void) {
    /* Stack switching is handled by assembly macros in ISR handlers */
    /* This function exists for API completeness */
}

/**
 * @brief Restore original caller stack  
 * @note This is a placeholder - actual restoration done in assembly
 */
void tsr_restore_caller_stack(void) {
    /* Stack restoration is handled by assembly macros in ISR handlers */
    /* This function exists for API completeness */
}

/**
 * @brief Check if we still own our interrupt vectors
 */
int check_vector_ownership(void) {
    return asm_check_vector_ownership();
}

/**
 * @brief Perform periodic vector monitoring and recovery
 */
int periodic_vector_monitoring(void) {
    int recovered;
    
    recovered = asm_periodic_vector_monitoring();
    
    #ifdef DEBUG_BUILD
    if (recovered > 0) {
        log_warning("Vector monitoring recovered %d hijacked vectors", recovered);
    }
    #endif
    
    return recovered;
}

/**
 * @brief Add work item to deferred queue
 */
int deferred_add_work(void (*work_func)(void)) {
    if (work_func == NULL) {
        return -1; /* Invalid function pointer */
    }
    
    return asm_deferred_add_work(work_func);
}

/**
 * @brief Process pending deferred work items
 */
int deferred_process_pending(void) {
    int processed = 0;
    
    /* Only process if DOS is completely safe */
    if (!dos_is_completely_safe()) {
        return 0; /* Not safe to process work */
    }
    
    processed = asm_deferred_process_pending();
    
    #ifdef DEBUG_BUILD
    if (processed > 0) {
        log_debug("Processed %d deferred work items", processed);
    }
    #endif
    
    return processed;
}

/**
 * @brief Check if deferred work queue has items  
 */
int deferred_work_pending(void) {
    return asm_deferred_work_pending();
}

/**
 * @brief Trigger emergency TSR recovery
 */
int tsr_emergency_recovery(void) {
    int result;
    
    #ifdef DEBUG_BUILD
    log_warning("Triggering emergency TSR recovery");
    #endif
    
    result = asm_tsr_emergency_recovery();
    
    #ifdef DEBUG_BUILD
    if (result == 0) {
        log_info("Emergency recovery successful");
    } else {
        log_error("Emergency recovery failed (code %d)", result);
    }
    #endif
    
    return result;
}

/**
 * @brief Validate TSR integrity
 */
int tsr_validate_integrity(void) {
    int result;
    
    result = asm_tsr_validate_integrity();
    
    #ifdef DEBUG_BUILD  
    if (result != 0) {
        log_warning("TSR integrity validation failed (code %d)", result);
    }
    #endif
    
    return result;
}

/**
 * @brief Get DOS safety status for diagnostics
 * @return Bitmask of safety flags
 */
uint16_t get_dos_safety_status(void) {
    uint16_t status = 0;
    
    if (!dos_safety_initialized) {
        return 0x8000; /* Not initialized flag */
    }
    
    if (!dos_is_safe()) {
        status |= 0x01; /* InDOS flag set */
    }
    
    if (!dos_is_completely_safe()) {
        status |= 0x02; /* Critical error flag set */
    }
    
    return status;
}