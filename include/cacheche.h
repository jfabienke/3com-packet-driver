/**
 * @file cache_coherency_enhanced.h
 * @brief Enhanced Cache Coherency Detection - GPT-5 Improvements
 *
 * This module implements GPT-5's recommended improvements:
 * - Proper CPUID-based CLFLUSH detection (not CPU family)
 * - Direction-specific cache operations (dma_sync_for_device/cpu)
 * - One-time coherency probing at initialization
 * - Performance-safe WBINVD usage (opt-in only)
 * - 4-tier cache management with runtime detection
 */

#ifndef CACHE_COHERENCY_ENHANCED_H
#define CACHE_COHERENCY_ENHANCED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CPUID feature bits - GPT-5 Correction */
#define CPUID_FEAT_CLFLUSH      (1UL << 19)    /* CLFLUSH instruction support */
#define CPUID_FEAT_SSE2         (1UL << 26)    /* SSE2 support */
#define CPUID_FEAT_MFENCE       (1UL << 26)    /* Memory fence (with SSE2) */

/* Cache management tiers - GPT-5 Enhanced */
typedef enum {
    CACHE_TIER_1_CLFLUSH,       /* Pentium 4+ with CLFLUSH instruction */
    CACHE_TIER_2_WBINVD,        /* 486+ with WBINVD (emergency use only) */
    CACHE_TIER_3_SOFTWARE,      /* 386 with software coherency tricks */
    CACHE_TIER_4_DISABLED       /* 286 or broken cache - no cache ops */
} cache_tier_t;

/* DMA direction for cache operations - GPT-5 Critical */
typedef enum {
    DMA_SYNC_FOR_DEVICE,        /* CPU → Device (TX): Flush dirty lines */
    DMA_SYNC_FOR_CPU            /* Device → CPU (RX): Invalidate stale lines */
} dma_sync_direction_t;

/* Cache line size constants - GPT-5 Critical Alignment Fix */
#define CACHE_LINE_SIZE_DEFAULT     32      /* Conservative default (bytes) */
#define CACHE_LINE_SIZE_MIN         16      /* Minimum cache line size */
#define CACHE_LINE_SIZE_MAX         128     /* Maximum cache line size */
#define CACHE_LINE_SIZE_486         16      /* 486 cache line size */
#define CACHE_LINE_SIZE_PENTIUM     32      /* Pentium cache line size */
#define CACHE_LINE_SIZE_P6          32      /* Pentium Pro/II/III */

/* Cacheline alignment macros - GPT-5 Critical Safety */
#define CACHE_LINE_ALIGN_MASK(size) ((size) - 1)
#define CACHE_LINE_ALIGN_UP(addr, size) (((uintptr_t)(addr) + (size) - 1) & ~CACHE_LINE_ALIGN_MASK(size))
#define CACHE_LINE_ALIGN_DOWN(addr, size) ((uintptr_t)(addr) & ~CACHE_LINE_ALIGN_MASK(size))
#define IS_CACHE_LINE_ALIGNED(addr, size) (((uintptr_t)(addr) & CACHE_LINE_ALIGN_MASK(size)) == 0)
#define CACHE_LINE_ROUND_UP(len, size) (((len) + (size) - 1) & ~CACHE_LINE_ALIGN_MASK(size))

/* Cache coherency probe results */
typedef struct {
    bool tx_needs_flush;        /* TX DMA needs explicit cache flush */
    bool rx_needs_invalidate;   /* RX DMA needs cache invalidation */
    bool hardware_coherent;     /* Hardware maintains coherency */
    cache_tier_t recommended_tier; /* Recommended cache management tier */
    uint16_t cache_line_size;   /* Detected cache line size */
    bool probe_successful;      /* Probe completed successfully */
    char chipset_name[32];      /* Detected chipset (if known) */
} coherency_probe_result_t;

/* Cache management configuration - GPT-5 Performance Fix */
typedef struct {
    cache_tier_t active_tier;   /* Currently active tier */
    bool clflush_available;     /* CLFLUSH instruction available */
    bool wbinvd_enabled;        /* WBINVD operations enabled (opt-in) */
    bool software_coherency;    /* Using software coherency method */
    uint16_t cache_line_size;   /* Cache line size in bytes */
    bool initialized;           /* Cache management initialized */
    
    /* GPT-5 Critical: WBINVD coalescing for performance */
    bool coalescing_enabled;    /* Enable flush coalescing */
    uint32_t pending_flushes;   /* Number of pending flush operations */
    uint32_t flush_threshold;   /* Threshold for triggering coalesced flush */
    uint32_t last_flush_time;   /* Timestamp of last flush */
    uint32_t max_flush_delay;   /* Maximum delay before forced flush */
    bool force_flush_pending;   /* Force flush on next opportunity */
} cache_config_t;

/* Statistics for cache operations - GPT-5 Performance Tracking */
typedef struct {
    uint32_t clflush_calls;     /* CLFLUSH operations performed */
    uint32_t wbinvd_calls;      /* WBINVD operations performed */
    uint32_t software_flushes;  /* Software flush operations */
    uint32_t tx_syncs;          /* TX cache synchronizations */
    uint32_t rx_syncs;          /* RX cache synchronizations */
    uint32_t coherency_failures; /* Coherency test failures */
    
    /* GPT-5 Critical: Performance optimization statistics */
    uint32_t coalesced_flushes; /* Number of coalesced flush operations */
    uint32_t deferred_flushes;  /* Number of deferred flush operations */
    uint32_t forced_flushes;    /* Number of forced immediate flushes */
    uint32_t bounce_avoidance;  /* Times bounce was avoided via coalescing */
    uint32_t performance_saves; /* Estimated performance impact reductions */
} cache_stats_t;

/* Core cache coherency functions */
int cache_coherency_init(void);
void cache_coherency_shutdown(void);

/* CPUID-based detection - GPT-5 Enhancement */
bool detect_clflush_via_cpuid(void);
bool detect_sse2_fences_via_cpuid(void);
cache_tier_t determine_optimal_cache_tier(void);

/* One-time coherency probing - GPT-5 Critical */
coherency_probe_result_t* run_coherency_probe(void);
bool probe_tx_cache_coherency(void);
bool probe_rx_cache_coherency(void);
void free_probe_result(coherency_probe_result_t *result);

/* Direction-specific cache operations - GPT-5 API */
void dma_sync_for_device(void *buffer, size_t len);
void dma_sync_for_cpu(void *buffer, size_t len);

/* Legacy compatibility functions */
void cache_flush_range(void *buffer, size_t len);
void cache_invalidate_range(void *buffer, size_t len);

/* Tier-specific implementations */
void cache_tier1_flush_clflush(void *buffer, size_t len);
void cache_tier2_flush_wbinvd(void);
void cache_tier3_software_flush(void *buffer, size_t len);
void cache_tier4_noop(void *buffer, size_t len);

/* Cache line size detection */
uint16_t detect_cache_line_size(void);
void touch_cache_lines(void *buffer, size_t len, uint16_t line_size);

/* GPT-5 Critical: Cacheline alignment safety functions */
bool is_buffer_cacheline_aligned(const void *buffer, size_t len, uint16_t cacheline_size);
bool needs_bounce_for_alignment(const void *buffer, size_t len, uint16_t cacheline_size);
size_t get_aligned_buffer_size(size_t len, uint16_t cacheline_size);
void* align_buffer_to_cacheline(void *buffer, uint16_t cacheline_size);

/* GPT-5 Critical: WBINVD performance coalescing functions */
void cache_enable_coalescing(bool enable);
bool cache_is_coalescing_enabled(void);
void cache_defer_flush(void);
void cache_force_coalesced_flush(void);
void cache_flush_if_needed(void);
void cache_set_flush_threshold(uint32_t threshold);
void cache_set_max_flush_delay(uint32_t delay_ms);

/* Configuration and control */
int set_cache_tier(cache_tier_t tier);
cache_tier_t get_active_cache_tier(void);
void enable_wbinvd_operations(bool enable);
bool is_wbinvd_enabled(void);

/* Statistics and debugging */
void get_cache_stats(cache_stats_t *stats);
void print_cache_stats(void);
void reset_cache_stats(void);
void print_cache_config(void);

/* Safety checks and validation */
bool validate_cache_tier_support(cache_tier_t tier);
bool is_cache_management_safe(void);
void emergency_disable_cache_ops(void);

/* Inline helper functions */

/**
 * @brief Check if CLFLUSH is available via CPUID
 * GPT-5: Use CPUID detection, not CPU family
 */
static inline bool has_clflush_instruction(void) {
    extern bool g_clflush_available;
    return g_clflush_available;
}

/**
 * @brief Check if memory fences are available
 */
static inline bool has_memory_fences(void) {
    extern bool g_mfence_available;
    return g_mfence_available;
}

/**
 * @brief Get cache line size for operations
 */
static inline uint16_t get_cache_line_size(void) {
    extern uint16_t g_cache_line_size;
    return g_cache_line_size ? g_cache_line_size : 32; /* Default 32 bytes */
}

/**
 * @brief Check if hardware is cache coherent
 */
static inline bool is_hardware_coherent(void) {
    extern bool g_hardware_coherent;
    return g_hardware_coherent;
}

/* External assembly functions from cache_ops.asm */
extern void asm_clflush_line(void *addr);
extern void asm_wbinvd(void);
extern void asm_mfence(void);
extern void asm_sfence(void);
extern void asm_lfence(void);

/* CPUID detection functions */
extern bool asm_has_cpuid(void);
extern uint32_t asm_cpuid_get_features_edx(void);
extern uint32_t asm_cpuid_get_features_ecx(void);

/* Memory barrier operations */
#define MEMORY_BARRIER_FULL()    do { if (has_memory_fences()) asm_mfence(); } while(0)
#define MEMORY_BARRIER_STORE()   do { if (has_memory_fences()) asm_sfence(); } while(0)
#define MEMORY_BARRIER_LOAD()    do { if (has_memory_fences()) asm_lfence(); } while(0)

/* Cache operation macros - GPT-5 Safe Usage */
#define CACHE_FLUSH_FOR_DMA_TX(buf, len) \
    do { \
        if (!is_hardware_coherent()) { \
            dma_sync_for_device((buf), (len)); \
        } \
    } while(0)

#define CACHE_INVALIDATE_FOR_DMA_RX(buf, len) \
    do { \
        if (!is_hardware_coherent()) { \
            dma_sync_for_cpu((buf), (len)); \
        } \
    } while(0)

/* Error codes */
#define CACHE_ERROR_CPUID_UNAVAILABLE   -3001
#define CACHE_ERROR_NO_CACHE_SUPPORT    -3002
#define CACHE_ERROR_PROBE_FAILED        -3003
#define CACHE_ERROR_TIER_UNSUPPORTED    -3004
#define CACHE_ERROR_WBINVD_DISABLED     -3005

/* Cache coherency test patterns */
#define CACHE_TEST_PATTERN_1    0xDEADBEEF
#define CACHE_TEST_PATTERN_2    0xCAFEBABE
#define CACHE_TEST_PATTERN_3    0x12345678
#define CACHE_TEST_PATTERN_4    0x87654321

/* Probing configuration */
#define CACHE_PROBE_BUFFER_SIZE     4096    /* 4KB test buffer */
#define CACHE_PROBE_ITERATIONS      3       /* Test iterations */
#define CACHE_PROBE_DELAY_MS        10      /* Delay between tests */

#ifdef __cplusplus
}
#endif

#endif /* CACHE_COHERENCY_ENHANCED_H */