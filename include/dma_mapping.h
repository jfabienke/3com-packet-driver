/**
 * @file dma_mapping.h
 * @brief Centralized DMA Mapping Layer - GPT-5 Integration
 *
 * This module provides a unified API that combines:
 * - DMA boundary checking and bounce buffers
 * - Cache coherency management
 * - Direction-specific operations
 * - Automatic cleanup and error handling
 *
 * All DMA operations should go through this layer for safety.
 */

#ifndef DMA_MAPPING_H
#define DMA_MAPPING_H

/* Define this to confirm DMA safety is integrated */
#define DMA_SAFETY_INTEGRATED 1

#include <stdint.h>
#include <stdbool.h>
#include "dma_boundary.h"
#include "cache_coherency_enhanced.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DMA mapping handle - opaque structure */
typedef struct dma_mapping dma_mapping_t;

/* DMA mapping flags */
#define DMA_MAP_READ            0x01    /* DMA device reads (TX) */
#define DMA_MAP_WRITE           0x02    /* DMA device writes (RX) */
#define DMA_MAP_COHERENT        0x04    /* Skip cache operations */
#define DMA_MAP_FORCE_BOUNCE    0x08    /* Force bounce buffer use */
#define DMA_MAP_NO_CACHE_SYNC   0x10    /* Skip cache synchronization */
#define DMA_MAP_VDS_ZEROCOPY    0x20    /* Use VDS zero-copy with page locking */

/* DMA mapping results */
typedef enum {
    DMA_MAP_SUCCESS = 0,
    DMA_MAP_ERROR_INVALID_PARAM = -1,
    DMA_MAP_ERROR_NO_MEMORY = -2,
    DMA_MAP_ERROR_NO_BOUNCE = -3,
    DMA_MAP_ERROR_BOUNDARY = -4,
    DMA_MAP_ERROR_CACHE = -5,
    DMA_MAP_ERROR_NOT_MAPPED = -6
} dma_map_result_t;

/* DMA mapping statistics */
typedef struct {
    uint32_t total_mappings;        /* Total mappings created */
    uint32_t active_mappings;       /* Currently active mappings */
    uint32_t direct_mappings;       /* Direct (no bounce) mappings */
    uint32_t bounce_mappings;       /* Bounce buffer mappings */
    uint32_t cache_syncs;           /* Cache synchronizations performed */
    uint32_t mapping_errors;        /* Mapping failures */
    uint32_t tx_mappings;           /* TX (read) mappings */
    uint32_t rx_mappings;           /* RX (write) mappings */
} dma_mapping_stats_t;

/* Core DMA mapping functions */
int dma_mapping_init(void);
void dma_mapping_shutdown(void);

/* TX (CPU -> Device) mapping functions */
dma_mapping_t* dma_map_tx(void *buffer, size_t len);
dma_mapping_t* dma_map_tx_flags(void *buffer, size_t len, uint32_t flags);
void dma_unmap_tx(dma_mapping_t *mapping);

/* RX (Device -> CPU) mapping functions */
dma_mapping_t* dma_map_rx(void *buffer, size_t len);
dma_mapping_t* dma_map_rx_flags(void *buffer, size_t len, uint32_t flags);
void dma_unmap_rx(dma_mapping_t *mapping);

/* Generic mapping functions */
dma_mapping_t* dma_map_buffer(void *buffer, size_t len, dma_sync_direction_t direction);
dma_mapping_t* dma_map_buffer_flags(void *buffer, size_t len, dma_sync_direction_t direction, uint32_t flags);
void dma_unmap_buffer(dma_mapping_t *mapping);

/* Mapping information access */
void* dma_mapping_get_address(const dma_mapping_t *mapping);
uint32_t dma_mapping_get_phys_addr(const dma_mapping_t *mapping);
size_t dma_mapping_get_length(const dma_mapping_t *mapping);
bool dma_mapping_uses_bounce(const dma_mapping_t *mapping);
bool dma_mapping_is_coherent(const dma_mapping_t *mapping);
bool dma_mapping_uses_vds(const dma_mapping_t *mapping);

/* Synchronization functions */
int dma_mapping_sync_for_device(dma_mapping_t *mapping);
int dma_mapping_sync_for_cpu(dma_mapping_t *mapping);

/* Per-NIC device constraint mapping */
dma_mapping_t* dma_map_with_device_constraints(void *buffer, size_t length, 
                                              dma_sync_direction_t direction,
                                              const char *device_name);

/* Batch operations for scatter-gather */
typedef struct {
    dma_mapping_t **mappings;       /* Array of mappings */
    uint16_t count;                 /* Number of mappings */
    uint16_t capacity;              /* Array capacity */
    size_t total_length;            /* Total mapped length */
} dma_mapping_batch_t;

dma_mapping_batch_t* dma_create_mapping_batch(uint16_t max_segments);
int dma_batch_add_mapping(dma_mapping_batch_t *batch, dma_mapping_t *mapping);
void dma_unmap_batch(dma_mapping_batch_t *batch);
void dma_free_mapping_batch(dma_mapping_batch_t *batch);

/* Statistics and debugging */
void dma_mapping_get_stats(dma_mapping_stats_t *stats);
void dma_mapping_print_stats(void);
void dma_mapping_reset_stats(void);

/* Validation and testing */
bool dma_mapping_validate(const dma_mapping_t *mapping);
int dma_mapping_test_coherency(void *buffer, size_t len);

/* Helper macros for common operations */

/**
 * @brief Map buffer for TX DMA with automatic cleanup
 * Usage: DMA_MAP_TX_AUTO(buffer, len, mapping) { ... use mapping ... }
 */
#define DMA_MAP_TX_AUTO(buf, len, map_var) \
    for (dma_mapping_t *map_var = dma_map_tx(buf, len); \
         map_var != NULL; \
         dma_unmap_tx(map_var), map_var = NULL)

/**
 * @brief Map buffer for RX DMA with automatic cleanup
 */
#define DMA_MAP_RX_AUTO(buf, len, map_var) \
    for (dma_mapping_t *map_var = dma_map_rx(buf, len); \
         map_var != NULL; \
         dma_unmap_rx(map_var), map_var = NULL)

/**
 * @brief Safe TX DMA mapping with error handling
 */
#define DMA_TX_SAFE(buf, len, error_label) \
    dma_mapping_t *_tx_map = dma_map_tx(buf, len); \
    if (!_tx_map) { \
        LOG_ERROR("TX DMA mapping failed for %p len=%zu", buf, len); \
        goto error_label; \
    } \
    void *_tx_dma_addr = dma_mapping_get_address(_tx_map);

/**
 * @brief Safe RX DMA mapping with error handling
 */
#define DMA_RX_SAFE(buf, len, error_label) \
    dma_mapping_t *_rx_map = dma_map_rx(buf, len); \
    if (!_rx_map) { \
        LOG_ERROR("RX DMA mapping failed for %p len=%zu", buf, len); \
        goto error_label; \
    } \
    void *_rx_dma_addr = dma_mapping_get_address(_rx_map);

/**
 * @brief Cleanup TX DMA mapping
 */
#define DMA_TX_CLEANUP() \
    do { if (_tx_map) { dma_unmap_tx(_tx_map); _tx_map = NULL; } } while(0)

/**
 * @brief Cleanup RX DMA mapping
 */
#define DMA_RX_CLEANUP() \
    do { if (_rx_map) { dma_unmap_rx(_rx_map); _rx_map = NULL; } } while(0)

/* Error handling helpers */
const char* dma_map_result_to_string(dma_map_result_t result);
void dma_mapping_log_error(dma_map_result_t result, const char *operation);

/* Advanced features */
int dma_mapping_set_cache_policy(dma_mapping_t *mapping, bool coherent);
int dma_mapping_prefault(dma_mapping_t *mapping);
int dma_mapping_pin_pages(dma_mapping_t *mapping);

/* GPT-5 A+: Cacheable memory allocation for descriptor rings (requires sync) */
void* dma_alloc(size_t size, size_t alignment, uint32_t *phys_addr);
void dma_free(void *addr, size_t size);
int dma_sync_for_device_explicit(void *buffer, size_t len, dma_sync_direction_t direction);
int dma_sync_for_cpu_explicit(void *buffer, size_t len, dma_sync_direction_t direction);

/* Integration with existing systems */
dma_mapping_t* dma_map_from_sg_descriptor(dma_sg_descriptor_t *sg_desc, dma_sync_direction_t direction);
int dma_mapping_to_sg_list(const dma_mapping_t *mapping, dma_sg_descriptor_t **sg_desc);

/* Performance optimization */
void dma_mapping_enable_fast_path(bool enable);
bool dma_mapping_is_fast_path_enabled(void);
uint32_t dma_mapping_get_cache_hit_rate(void);

#ifdef __cplusplus
}
#endif

#endif /* DMA_MAPPING_H */