/**
 * @file buffer_pool.h
 * @brief Buffer pool management interface
 * 
 * Manages pre-allocated buffer pools for copy-break optimization
 */

#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <stdint.h>
#include <stdbool.h>

/* Buffer types */
typedef enum {
    BUFFER_SMALL = 0,       /* 256 bytes */
    BUFFER_MEDIUM = 1,      /* 512 bytes */
    BUFFER_LARGE = 2,       /* 1536 bytes (MTU) */
    BUFFER_TYPES = 3
} buffer_type_t;

/* Buffer pool statistics */
struct buffer_pool_stats {
    uint16_t buffer_size;       /* Size of each buffer */
    uint16_t total_count;       /* Total buffers in pool */
    uint16_t free_count;        /* Currently free buffers */
    uint16_t used_count;        /* Currently used buffers */
    uint32_t allocations;       /* Total allocations */
    uint32_t frees;             /* Total frees */
    uint32_t failures;          /* Allocation failures */
    uint32_t peak_usage;        /* Peak simultaneous usage */
    uint8_t utilization;        /* Current utilization % */
    uint8_t success_rate;       /* Allocation success rate % */
};

/* Function prototypes */

/**
 * Initialize buffer pool system
 * Allocates memory in UMB if available, otherwise conventional
 */
int buffer_pool_init(void);

/**
 * Buffer allocation/deallocation
 */
void *buffer_pool_alloc(uint16_t size);
void buffer_pool_free(void *buffer);

/**
 * Copy-break optimized allocation
 * Returns NULL if packet should use zero-copy instead
 */
void *buffer_pool_alloc_copybreak(uint16_t packet_size, uint16_t threshold);

/**
 * Pool monitoring
 */
bool buffer_pool_needs_refill(buffer_type_t type);
void buffer_pool_get_stats(buffer_type_t type, struct buffer_pool_stats *stats);
int buffer_pool_health_check(void);
void buffer_pool_reset_stats(void);

/**
 * Cleanup
 */
void buffer_pool_cleanup(void);

/**
 * Debug functions
 */
void buffer_pool_debug_print(void);

/* Convenience macros */

/* Allocate small buffer (up to 256 bytes) */
#define ALLOC_SMALL_BUFFER() buffer_pool_alloc(256)

/* Allocate medium buffer (up to 512 bytes) */
#define ALLOC_MEDIUM_BUFFER() buffer_pool_alloc(512)

/* Allocate large buffer (up to 1536 bytes) */
#define ALLOC_LARGE_BUFFER() buffer_pool_alloc(1536)

/* Free any buffer */
#define FREE_BUFFER(buf) buffer_pool_free(buf)

/* Check if copy-break should be used */
#define USE_COPY_BREAK(size, threshold) ((size) <= (threshold))

/* Get copy-break buffer or NULL for zero-copy */
#define GET_COPYBREAK_BUFFER(size, threshold) \
    buffer_pool_alloc_copybreak(size, threshold)

#endif /* BUFFER_POOL_H */