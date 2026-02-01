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

/* Include portability header for types and bool */
#include "portabl.h"

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

/*
 * Atomic test-and-set using x86 XCHG instruction
 * For Watcom: use pragma aux for inline assembly
 * For other compilers: use inline assembly or fallback
 */
#if defined(__WATCOMC__)
/* Watcom: declare function and use pragma aux for inline assembly */
static uint8_t mdio_atomic_test_and_set(volatile uint8_t *lock_ptr);
#pragma aux mdio_atomic_test_and_set = \
    "mov al, 1" \
    "xchg al, [bx]" \
    parm [bx] \
    value [al] \
    modify [al];

/* Memory barrier for Watcom - no-op but forces compiler ordering */
static void mdio_memory_barrier(void);
#pragma aux mdio_memory_barrier = "" parm [] modify exact [];

#elif defined(__TURBOC__) || defined(__BORLANDC__)
/* Borland: use asm keyword */
static uint8_t mdio_atomic_test_and_set(volatile uint8_t *lock_ptr) {
    uint8_t result;
    asm {
        mov bx, lock_ptr
        mov al, 1
        xchg al, [bx]
        mov result, al
    }
    return result;
}

static void mdio_memory_barrier(void) {
    /* No-op for Borland */
}

#else
/* Fallback: non-atomic (for compilation testing only) */
static uint8_t mdio_atomic_test_and_set(volatile uint8_t *lock_ptr) {
    uint8_t old_val = *lock_ptr;
    *lock_ptr = 1;
    return old_val;
}

static void mdio_memory_barrier(void) {
    /* No-op fallback */
}
#endif

/**
 * @brief Initialize MDIO lock
 */
static void mdio_lock_init(void) {
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
static int mdio_lock_acquire(uint8_t context) {
    uint16_t spins = 0;
    uint16_t backoff = 1;
    volatile uint16_t i;  /* C89: declare at block start */
    uint8_t old_val;

    /* Increment waiter count */
    g_mdio_lock.wait_count++;

    while (spins < MDIO_LOCK_MAX_SPINS) {
        /* Atomic test-and-set using portable function */
        old_val = mdio_atomic_test_and_set(&g_mdio_lock.locked);

        if (old_val == 0) {
            /* Successfully acquired lock */
            g_mdio_lock.owner_ctx = context;
            g_mdio_lock.wait_count--;
            g_mdio_lock.lock_count++;

            /* Track contention if there were other waiters */
            if (g_mdio_lock.wait_count > 0) {
                g_mdio_lock.contention_count++;
            }

            return 1;  /* true */
        }

        /* Exponential backoff delay - C89 style loop */
        for (i = 0; i < backoff; i++) {
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
    return 0;  /* false */
}

/**
 * @brief Release MDIO lock
 * @param context Context identifier (must match lock owner)
 */
static void mdio_lock_release(uint8_t context) {
    /* Verify we own the lock */
    if (g_mdio_lock.owner_ctx != context) {
        /* Lock protocol violation - log if possible */
        return;
    }

    /* Clear owner first, then release lock (order matters) */
    g_mdio_lock.owner_ctx = MDIO_CTX_NONE;

    /* Memory barrier to ensure owner clear is visible before unlock */
    mdio_memory_barrier();

    /* Atomic release */
    g_mdio_lock.locked = 0;
}

/**
 * @brief Check if MDIO is locked
 * @return true if locked, false if free
 */
static int mdio_is_locked(void) {
    return g_mdio_lock.locked != 0;
}

/**
 * @brief Get MDIO lock statistics
 * @param locks Output: total successful locks
 * @param contentions Output: times lock was contested
 */
static void mdio_lock_get_stats(uint32_t *locks, uint32_t *contentions) {
    if (locks) *locks = g_mdio_lock.lock_count;
    if (contentions) *contentions = g_mdio_lock.contention_count;
}

#endif /* _MDIO_LOCK_H_ */