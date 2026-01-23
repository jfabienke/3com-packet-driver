/**
 * @file compile_checks.h
 * @brief Compile-time validation of critical constants
 * 
 * Ensures ring sizes, masks, and thresholds are power-of-two
 * and other critical constraints are met at build time.
 */

#ifndef _COMPILE_CHECKS_H_
#define _COMPILE_CHECKS_H_

/* Import configuration constants */
#include "txlazy.h"
#include "rxbatch.h"

/* Compile-time assertion macro for C */
#define COMPILE_ASSERT(expr, msg) \
    typedef char compile_assert_##msg[(expr) ? 1 : -1]

/* Power-of-two check macro */
#define IS_POWER_OF_TWO(n) (((n) > 0) && (((n) & ((n) - 1)) == 0))

/* Validate K_PKTS is power of two for lazy TX */
#if !defined(K_PKTS)
    #error "K_PKTS not defined"
#elif !IS_POWER_OF_TWO(K_PKTS)
    #error "K_PKTS must be a power of two for bitmask optimization"
#endif

/* Validate TX ring size and mask */
#if !defined(TX_RING_SIZE)
    #error "TX_RING_SIZE not defined"
#elif !IS_POWER_OF_TWO(TX_RING_SIZE)
    #error "TX_RING_SIZE must be a power of two"
#elif !defined(TX_RING_MASK)
    #error "TX_RING_MASK not defined"
#elif (TX_RING_MASK != (TX_RING_SIZE - 1))
    #error "TX_RING_MASK must equal TX_RING_SIZE - 1"
#endif

/* Validate RX ring size and mask */
#if !defined(RX_RING_SIZE)
    #error "RX_RING_SIZE not defined"
#elif !IS_POWER_OF_TWO(RX_RING_SIZE)
    #error "RX_RING_SIZE must be a power of two"
#elif !defined(RX_RING_MASK)
    #error "RX_RING_MASK not defined"
#elif (RX_RING_MASK != (RX_RING_SIZE - 1))
    #error "RX_RING_MASK must equal RX_RING_SIZE - 1"
#endif

/* Validate copy-break threshold */
#if !defined(COPY_BREAK_THRESHOLD)
    #error "COPY_BREAK_THRESHOLD not defined"
#elif (COPY_BREAK_THRESHOLD > 256)
    #error "COPY_BREAK_THRESHOLD exceeds small buffer size (256)"
#elif (COPY_BREAK_THRESHOLD < 64)
    #warning "COPY_BREAK_THRESHOLD < 64 may hurt performance"
#endif

/* Validate buffer sizes */
#if !defined(RX_BUF_SIZE)
    #define RX_BUF_SIZE 1536
#elif (RX_BUF_SIZE < 1536)
    #error "RX_BUF_SIZE must be at least 1536 for Ethernet MTU"
#endif

#if !defined(RX_SMALL_BUF_SIZE)
    #define RX_SMALL_BUF_SIZE 256
#elif (RX_SMALL_BUF_SIZE < COPY_BREAK_THRESHOLD)
    #error "RX_SMALL_BUF_SIZE must be >= COPY_BREAK_THRESHOLD"
#endif

/* Validate refill threshold */
#if !defined(RX_REFILL_THRESHOLD)
    #error "RX_REFILL_THRESHOLD not defined"
#elif (RX_REFILL_THRESHOLD >= RX_RING_SIZE)
    #error "RX_REFILL_THRESHOLD must be less than RX_RING_SIZE"
#elif (RX_REFILL_THRESHOLD < 4)
    #warning "RX_REFILL_THRESHOLD < 4 may cause frequent refills"
#endif

/* Validate NIC limits */
#if !defined(MAX_NICS)
    #define MAX_NICS 4
#elif (MAX_NICS > 4)
    #error "MAX_NICS exceeds supported limit of 4"
#elif (MAX_NICS < 1)
    #error "MAX_NICS must be at least 1"
#endif

/* Runtime assertions for values that can't be checked at compile time */
static inline void validate_runtime_constants(void) {
    /* Add runtime checks here if needed */
}

#endif /* _COMPILE_CHECKS_H_ */