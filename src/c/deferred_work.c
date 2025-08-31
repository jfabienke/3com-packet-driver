/**
 * @file deferred_work.c
 * @brief Deferred Work Queue Management
 *
 * Provides C interface to the deferred work queue system for deferring
 * operations from interrupt context to safe DOS-idle processing.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/tsr_defensive.h"
#include "../include/logging.h"

/* Maximum number of work items that can be queued */
#define MAX_DEFERRED_WORK_ITEMS 32

/* Work item priorities */
typedef enum {
    DEFERRED_PRIORITY_LOW = 0,
    DEFERRED_PRIORITY_NORMAL = 1, 
    DEFERRED_PRIORITY_HIGH = 2,
    DEFERRED_PRIORITY_URGENT = 3
} deferred_priority_t;

/* Work item structure for C code tracking */
typedef struct {
    void (*work_func)(void);
    deferred_priority_t priority;
    uint16_t flags;
    uint32_t timestamp;
} deferred_work_item_t;

/* Static work item tracking (for diagnostics) 
 * All counters marked volatile for ISR/mainline concurrency safety */
static deferred_work_item_t work_items[MAX_DEFERRED_WORK_ITEMS];
static volatile uint16_t next_item_index = 0;
static volatile uint32_t work_items_queued = 0;
static volatile uint32_t work_items_processed = 0;
static volatile uint32_t queue_full_errors = 0;

/**
 * @brief Initialize deferred work system
 */
int deferred_work_init(void) {
    int i;
    
    /* Clear work item tracking */
    for (i = 0; i < MAX_DEFERRED_WORK_ITEMS; i++) {
        work_items[i].work_func = NULL;
        work_items[i].priority = DEFERRED_PRIORITY_NORMAL;
        work_items[i].flags = 0;
        work_items[i].timestamp = 0;
    }
    
    next_item_index = 0;
    work_items_queued = 0;
    work_items_processed = 0;
    queue_full_errors = 0;
    
    #ifdef DEBUG_BUILD
    log_debug("Deferred work system initialized");
    #endif
    
    return 0;
}

/**
 * @brief Add work item to deferred queue (ISR-safe)
 * @note Uses atomic operations for counter updates to prevent ISR corruption
 */
int deferred_add_work_priority(void (*work_func)(void), deferred_priority_t priority) {
    int result;
    uint16_t item_index;
    
    /* Validate input parameters */
    if (work_func == NULL) {
        /* Atomic increment of error counter - preserve IF state */
        unsigned short flags;
        _asm { pushf; pop flags }       /* Save current IF state */
        _disable();                     /* CLI for atomic 32-bit update */
        queue_full_errors++;
        if (flags & 0x0200) _enable();  /* Restore IF if it was set */
        return -1; /* Invalid function pointer */
    }
    
    /* Add to assembly queue first (this is the critical operation) */
    result = deferred_add_work(work_func);
    if (result != 0) {
        /* Atomic increment of error counter - preserve IF state */
        unsigned short flags;
        _asm { pushf; pop flags }       /* Save current IF state */
        _disable();                     /* CLI for atomic 32-bit update */
        queue_full_errors++;
        if (flags & 0x0200) _enable();  /* Restore IF if it was set */
        return result; /* Queue full or error */
    }
    
    /* Track in C structure for diagnostics (atomic updates for ISR safety) */
    {
        unsigned short flags;
        _asm { pushf; pop flags }       /* Save current IF state */
        _disable();                     /* CLI for atomic multi-operation update */
        item_index = next_item_index;
        next_item_index = (next_item_index + 1) % MAX_DEFERRED_WORK_ITEMS;
        work_items_queued++;
        if (flags & 0x0200) _enable();  /* Restore IF if it was set */
    }
    
    /* Update tracking structure (safe now that indices are committed) */
    work_items[item_index].work_func = work_func;
    work_items[item_index].priority = priority;
    work_items[item_index].flags = 0;
    work_items[item_index].timestamp = 0; /* Would use real timestamp in production */
    
    return 0;
}

/**
 * @brief Add normal priority work item  
 */
int deferred_add_work_simple(void (*work_func)(void)) {
    return deferred_add_work_priority(work_func, DEFERRED_PRIORITY_NORMAL);
}

/**
 * @brief Process pending deferred work (called from INT 28h context)
 * @note Only processes work when DOS is completely safe, with error recovery
 */
int deferred_process_all_work(void) {
    int processed = 0;
    static volatile uint16_t consecutive_failures = 0;
    static volatile uint16_t safety_check_failures = 0;
    
    /* Only process if DOS is completely safe */
    if (!dos_is_completely_safe()) {
        /* Track consecutive safety failures for diagnostics - preserve IF state */
        unsigned short flags;
        _asm { pushf; pop flags }       /* Save current IF state */
        _disable();                     /* Atomic increment */
        safety_check_failures++;
        if (flags & 0x0200) _enable();  /* Restore IF if it was set */
        return 0; /* Not safe to process */
    }
    
    /* Reset failure counter on successful safety check */
    safety_check_failures = 0;
    
    /* Process deferred work with error handling */
    processed = deferred_process_pending();
    
    if (processed >= 0) {
        /* Success - update counter atomically - preserve IF state */
        unsigned short flags;
        _asm { pushf; pop flags }       /* Save current IF state */
        _disable();                     /* CLI for atomic 32-bit update */
        work_items_processed += processed;
        consecutive_failures = 0;       /* Reset failure counter */
        if (flags & 0x0200) _enable();  /* Restore IF if it was set */
        
        #ifdef DEBUG_BUILD
        if (processed > 0) {
            log_debug("Processed %d deferred work items", processed);
        }
        #endif
    } else {
        /* Error in processing - preserve IF state */
        unsigned short flags;
        _asm { pushf; pop flags }       /* Save current IF state */
        _disable();                     /* Atomic increment */
        consecutive_failures++;
        if (flags & 0x0200) _enable();  /* Restore IF if it was set */
        
        #ifdef DEBUG_BUILD
        log_warning("Deferred work processing failed, consecutive failures: %d", 
                    consecutive_failures);
        #endif
        
        /* Emergency flush if too many consecutive failures */
        if (consecutive_failures >= 10) {
            #ifdef DEBUG_BUILD
            log_error("Too many deferred work failures, initiating emergency flush");
            #endif
            deferred_work_emergency_flush();
            consecutive_failures = 0;
        }
    }
    
    return processed;
}

/**
 * @brief Get deferred work queue statistics
 */
void get_deferred_work_stats(uint32_t *queued, uint32_t *processed, 
                            uint32_t *queue_full_errors, uint16_t *pending) {
    if (queued != NULL) {
        *queued = work_items_queued;
    }
    
    if (processed != NULL) {
        *processed = work_items_processed;
    }
    
    if (queue_full_errors != NULL) {
        *queue_full_errors = queue_full_errors;
    }
    
    if (pending != NULL) {
        *pending = (uint16_t)deferred_work_pending();
    }
}

/**
 * @brief Check if deferred work system is healthy
 */
bool deferred_work_is_healthy(void) {
    uint16_t pending = (uint16_t)deferred_work_pending();
    
    /* System is unhealthy if queue is nearly full */
    if (pending >= (MAX_DEFERRED_WORK_ITEMS - 2)) {
        return false;
    }
    
    /* System is unhealthy if too many queue full errors */
    if (queue_full_errors > 10) {
        return false;  
    }
    
    return true;
}

/**
 * @brief Emergency flush of deferred work queue
 * @note Only call this during shutdown or recovery
 */
void deferred_work_emergency_flush(void) {
    int i;
    
    #ifdef DEBUG_BUILD
    log_warning("Emergency flush of deferred work queue");
    #endif
    
    /* Try to process remaining work if DOS is safe */
    if (dos_is_completely_safe()) {
        deferred_process_all_work();
    }
    
    /* Clear tracking structures */
    for (i = 0; i < MAX_DEFERRED_WORK_ITEMS; i++) {
        work_items[i].work_func = NULL;
    }
    
    next_item_index = 0;
}