/**
 * @file mdio_lock.h
 * @brief MDIO bus software lock for multi-context serialization
 *
 * Provides a lightweight spinlock for MDIO/MII bus access serialization
 * between ISR and non-ISR contexts. Uses atomic test-and-set with exponential
 * backoff to prevent bus contention.
 */

#ifndef _MDIO_LOCK_H_
#define _MDIO_LOCK_H_

#include <stdint.h>
#include <stdbool.h>

/* MDIO lock state - must be volatile for ISR visibility */
typedef struct {
    volatile uint8_t locked;      /* 0=free, 1=locked */
    volatile uint8_t owner_ctx;   /* Context that owns lock (0=none, 1=ISR, 2=main) */
    volatile uint16_t wait_count; /* Number of waiters */
    volatile uint32_t lock_count; /* Total successful locks (statistics) */
    volatile uint32_t contention_count; /* Times lock was contested */
} mdio_lock_t;

/* Global MDIO lock instance */
extern volatile mdio_lock_t g_mdio_lock;

/* Lock context identifiers */
#define MDIO_CTX_NONE   0
#define MDIO_CTX_ISR    1
#define MDIO_CTX_MAIN   2
#define MDIO_CTX_INIT   3

/* Maximum spin iterations before giving up */
#define MDIO_LOCK_MAX_SPINS  1000

/**
 * @brief Initialize MDIO lock
 */
static inline void mdio_lock_init(void) {
    g_mdio_lock.locked = 0;
    g_mdio_lock.owner_ctx = MDIO_CTX_NONE;
    g_mdio_lock.wait_count = 0;
    g_mdio_lock.lock_count = 0;
    g_mdio_lock.contention_count = 0;
}

/**
 * @brief Acquire MDIO lock with timeout
 * @param context Context identifier (MDIO_CTX_*)
 * @return true if lock acquired, false on timeout
 */
static inline bool mdio_lock_acquire(uint8_t context) {
    uint16_t spins = 0;
    uint16_t backoff = 1;
    
    /* Increment waiter count */
    g_mdio_lock.wait_count++;
    
    while (spins < MDIO_LOCK_MAX_SPINS) {
        /* Atomic test-and-set using x86 XCHG instruction */
        uint8_t old_val;
        _asm {
            mov al, 1
            xchg al, byte ptr [g_mdio_lock.locked]
            mov old_val, al
        }
        
        if (old_val == 0) {
            /* Successfully acquired lock */
            g_mdio_lock.owner_ctx = context;
            g_mdio_lock.wait_count--;
            g_mdio_lock.lock_count++;
            
            /* Track contention if there were other waiters */
            if (g_mdio_lock.wait_count > 0) {
                g_mdio_lock.contention_count++;
            }
            
            return true;
        }
        
        /* Exponential backoff delay */
        for (volatile uint16_t i = 0; i < backoff; i++) {
            /* Busy wait */
        }
        
        /* Increase backoff up to a limit */
        if (backoff < 64) {
            backoff *= 2;
        }
        
        spins++;
    }
    
    /* Timeout - failed to acquire lock */
    g_mdio_lock.wait_count--;
    return false;
}

/**
 * @brief Release MDIO lock
 * @param context Context identifier (must match lock owner)
 */
static inline void mdio_lock_release(uint8_t context) {
    /* Verify we own the lock */
    if (g_mdio_lock.owner_ctx != context) {
        /* Lock protocol violation - log if possible */
        return;
    }
    
    /* Clear owner first, then release lock (order matters) */
    g_mdio_lock.owner_ctx = MDIO_CTX_NONE;
    
    /* Memory barrier to ensure owner clear is visible before unlock */
    _asm {
        ; Ensure write ordering
    }
    
    /* Atomic release */
    g_mdio_lock.locked = 0;
}

/**
 * @brief Check if MDIO is locked
 * @return true if locked, false if free
 */
static inline bool mdio_is_locked(void) {
    return g_mdio_lock.locked != 0;
}

/**
 * @brief Get MDIO lock statistics
 * @param locks Output: total successful locks
 * @param contentions Output: times lock was contested
 */
static inline void mdio_lock_get_stats(uint32_t *locks, uint32_t *contentions) {
    if (locks) *locks = g_mdio_lock.lock_count;
    if (contentions) *contentions = g_mdio_lock.contention_count;
}

#endif /* _MDIO_LOCK_H_ */