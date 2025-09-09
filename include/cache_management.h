/**
 * @file cache_management.h
 * @brief 4-Tier cache management system declarations
 *
 * 3Com Packet Driver - Cache Management System
 *
 * This header defines the interface for the 4-tier cache management system
 * that ensures DMA safety across all x86 processors from 286 through modern CPUs.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef CACHE_MANAGEMENT_H
#define CACHE_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "cache_coherency.h"

/* Cache operation types */
typedef enum {
    CACHE_OPERATION_PRE_DMA,     /* Before DMA operation */
    CACHE_OPERATION_POST_DMA,    /* After DMA operation */
    CACHE_OPERATION_FLUSH,       /* Force cache flush */
    CACHE_OPERATION_INVALIDATE   /* Force cache invalidation */
} cache_operation_t;

/* Cache management configuration */
typedef struct {
    cache_tier_t selected_tier;          /* Selected cache management tier */
    uint8_t confidence_level;            /* Confidence in tier selection (0-100) */
    bool write_back_cache;               /* System has write-back cache */
    bool hardware_snooping;              /* Hardware provides cache snooping */
    bool has_clflush;                    /* CPU supports CLFLUSH instruction */
    bool has_wbinvd;                     /* CPU supports WBINVD instruction */
    size_t cache_line_size;              /* Detected cache line size */
    bool allow_batching;                 /* Allow operation batching optimization */
    uint32_t batch_timeout_microseconds; /* Batching timeout */
    dma_disable_reason_t dma_disabled_reason; /* Why DMA was disabled (if applicable) */
    uint16_t config_flags;               /* Configuration flags (PREFER_PIO_ON_486_ISA, etc) */
} cache_management_config_t;

/* Cache management performance metrics */
typedef struct {
    uint32_t total_operations;              /* Total cache operations performed */
    uint32_t total_overhead_microseconds;   /* Total overhead in microseconds */
    uint32_t average_overhead_microseconds; /* Average overhead per operation */
    uint32_t tier1_operations;              /* CLFLUSH operations */
    uint32_t tier2_operations;              /* WBINVD operations */
    uint32_t tier3_operations;              /* Software barrier operations */
    uint32_t tier4_operations;              /* Fallback operations */
    uint32_t disabled_operations;           /* Operations with bus master disabled */
    uint32_t initialization_time;           /* When cache management was initialized */
} cache_management_metrics_t;

/* Function declarations */

/* Initialization and configuration */
bool initialize_cache_management(const coherency_analysis_t *analysis);
bool update_cache_management_config(const cache_management_config_t *new_config);
cache_management_config_t get_cache_management_config(void);

/* Core cache management functions */
void cache_management_dma_prepare(void *buffer, size_t length);
void cache_management_dma_complete(void *buffer, size_t length);

/* Advanced cache operations */
void cache_management_flush_buffer(void *buffer, size_t length);
void cache_management_invalidate_buffer(void *buffer, size_t length);
void cache_management_sync_buffer(void *buffer, size_t length);

/* Query functions */
bool cache_management_required(void);
cache_tier_t get_active_cache_tier(void);
bool is_cache_management_initialized(void);

/* Performance monitoring */
cache_management_metrics_t get_cache_management_metrics(void);
void reset_cache_management_metrics(void);
uint32_t get_cache_management_overhead_percent(void);

/* Diagnostics and debugging */
void print_cache_management_status(void);
bool validate_cache_management_state(void);
void cache_management_self_test(void);

/* Utility macros */
#define CACHE_ALIGN_SIZE(size, line_size) \
    (((size) + (line_size) - 1) & ~((line_size) - 1))

#define CACHE_ALIGN_POINTER(ptr, line_size) \
    ((void*)(((uintptr_t)(ptr) + (line_size) - 1) & ~((line_size) - 1)))

#define IS_CACHE_ALIGNED(ptr, line_size) \
    (((uintptr_t)(ptr) & ((line_size) - 1)) == 0)

/* Performance optimization hints */
#define CACHE_PREFETCH_HINT(ptr) \
    __builtin_prefetch((ptr), 0, 3)

#define CACHE_WRITE_HINT(ptr) \
    __builtin_prefetch((ptr), 1, 3)

/* Constants */
#define CACHE_MANAGEMENT_VERSION        0x0100  /* Version 1.0 */
#define MAX_CACHE_LINE_SIZE            128     /* Maximum supported cache line size */
#define MIN_CACHE_LINE_SIZE            16      /* Minimum cache line size */
#define DEFAULT_CACHE_LINE_SIZE        32      /* Default assumption */

#define CACHE_BATCH_TIMEOUT_DEFAULT    1000    /* Default batching timeout (microseconds) */
#define CACHE_OVERHEAD_THRESHOLD_PCT   10      /* Maximum acceptable overhead percentage */

/* Error codes */
#define CACHE_MGMT_SUCCESS             0
#define CACHE_MGMT_ERROR_INVALID_PARAM -1
#define CACHE_MGMT_ERROR_NOT_INIT      -2
#define CACHE_MGMT_ERROR_NO_MEMORY     -3
#define CACHE_MGMT_ERROR_UNSUPPORTED   -4

/* Conditional compilation for different cache implementations */
#ifdef CACHE_MANAGEMENT_DEBUG
#define CACHE_DEBUG_LOG(fmt, ...) log_debug("CACHE: " fmt, ##__VA_ARGS__)
#else
#define CACHE_DEBUG_LOG(fmt, ...) do {} while(0)
#endif

/* Assembly function prototypes (implemented in cache_ops.asm) */
extern void cache_clflush_line(void *addr);
extern void cache_wbinvd(void);
extern void cache_invd(void);
extern uint32_t read_cr0_register(void);
extern void write_cr0_register(uint32_t value);
extern void memory_fence(void);
extern void store_fence(void);
extern void load_fence(void);

#endif /* CACHE_MANAGEMENT_H */