/**
 * @file cache_coherency.h
 * @brief Cache coherency testing and analysis declarations
 *
 * 3Com Packet Driver - Cache Coherency Testing Framework
 *
 * This header defines the interface for runtime cache coherency testing,
 * which forms the foundation of our revolutionary approach to DMA safety
 * in DOS environments.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef CACHE_COHERENCY_H
#define CACHE_COHERENCY_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu_detect.h"

/* Forward declaration for device capabilities */
typedef struct device_caps device_caps_t;

/* Cache management tiers */
typedef enum {
    CACHE_TIER_1_CLFLUSH = 1,    /* Pentium 4+ - Surgical cache line management */
    CACHE_TIER_2_WBINVD = 2,     /* 486+ - Complete cache flush/invalidate */
    CACHE_TIER_3_SOFTWARE = 3,   /* 386+ - Software cache barriers */
    CACHE_TIER_4_FALLBACK = 4,   /* 286+ - No cache management needed */
    TIER_DISABLE_BUS_MASTER = 0  /* Disable DMA, use PIO only */
} cache_tier_t;

/* Bus master functionality test results */
typedef enum {
    BUS_MASTER_OK,        /* DMA works correctly */
    BUS_MASTER_PARTIAL,   /* DMA works with limitations */
    BUS_MASTER_BROKEN     /* DMA doesn't work - use PIO only */
} bus_master_result_t;

/* Cache coherency test results */
typedef enum {
    COHERENCY_OK,         /* No cache problems detected */
    COHERENCY_PROBLEM,    /* Cache causes corruption */
    COHERENCY_UNKNOWN     /* Test inconclusive */
} coherency_result_t;

/* Hardware snooping detection results */
typedef enum {
    SNOOPING_NONE,        /* No hardware snooping detected */
    SNOOPING_PARTIAL,     /* Unreliable snooping */
    SNOOPING_FULL,        /* Reliable hardware snooping */
    SNOOPING_UNKNOWN      /* Cannot determine */
} snooping_result_t;

/* Cache mode detection */
typedef enum {
    CACHE_DISABLED,       /* Cache is disabled */
    CACHE_WRITE_THROUGH,  /* Write-through cache mode */
    CACHE_WRITE_BACK      /* Write-back cache mode */
} cache_mode_t;

/* Complete coherency analysis results */
typedef struct {
    /* Test results */
    bus_master_result_t bus_master;
    coherency_result_t coherency;
    snooping_result_t snooping;
    
    /* System configuration */
    bool cache_enabled;
    bool write_back_cache;
    cpu_info_t cpu;
    
    /* Final decision */
    cache_tier_t selected_tier;
    uint8_t confidence;              /* 0-100% confidence in results */
    char explanation[256];           /* Human-readable explanation */
} coherency_analysis_t;

/* Enhanced coherency analysis with VDS support - GPT-5 recommendation */
typedef struct {
    coherency_analysis_t base_analysis; /* Use existing analysis framework */
    
    /* VDS (Virtual DMA Services) detection and capabilities */
    bool vds_available;                  /* VDS driver detected (INT 4Bh) */
    bool vds_required_for_device;        /* Device needs VDS (ISA bus mastering) */
    bool vds_supports_scatter_gather;    /* VDS can handle S/G lists */
    bool vds_supports_cache_coherency;   /* VDS provides cache operations */
    uint8_t vds_version_major;           /* VDS version information */
    uint8_t vds_version_minor;
    
    /* Environment detection */
    bool running_in_v86_mode;            /* V86 or Windows DOS box */
    bool emm386_detected;                /* EMM386 memory manager */
    bool qemm_detected;                  /* QEMM memory manager */
    bool windows_enhanced_mode;          /* Windows 3.x enhanced mode */
    
    /* Direction-specific cache strategies - GPT-5 recommendation */
    cache_tier_t rx_cache_tier;          /* FROM_DEVICE cache strategy */
    cache_tier_t tx_cache_tier;          /* TO_DEVICE cache strategy */
    
    /* Device-specific recommendations */
    bool requires_staging;               /* XMS staging required */
    bool pre_lock_rx_buffers;           /* Pre-lock RX for ISR safety */
    uint16_t recommended_rx_copybreak;   /* Device-specific threshold */
    uint16_t recommended_tx_copybreak;   /* Device-specific threshold */
    
    /* Performance and reliability metrics */
    uint8_t dma_reliability_score;       /* 0-100, based on testing */
    uint8_t cache_performance_score;     /* 0-100, cache efficiency */
    char detailed_recommendation[512];   /* Extended explanation */
} enhanced_coherency_analysis_t;

/* Performance opportunity analysis */
typedef enum {
    PERFORMANCE_OPPORTUNITY_NONE,           /* Already optimized */
    PERFORMANCE_OPPORTUNITY_ENABLE_WB,      /* Major gain available */
    PERFORMANCE_OPPORTUNITY_OPTIMIZE_WB,    /* Already write-back, optimize */
    PERFORMANCE_OPPORTUNITY_OPTIMAL         /* Perfect configuration */
} performance_opportunity_t;

/* Cache recommendation for performance optimization */
typedef enum {
    CACHE_RECOMMENDATION_NONE,              /* No action needed */
    CACHE_RECOMMENDATION_ENABLE_WB,         /* Encourage write-back */
    CACHE_RECOMMENDATION_OPTIMIZE_WB,       /* Optimize write-back settings */
    CACHE_RECOMMENDATION_CHECK_BIOS         /* Check BIOS configuration */
} cache_recommendation_t;

/* Function declarations */

/* Core testing functions */
bus_master_result_t test_basic_bus_master(void);
coherency_result_t test_cache_coherency(void);
snooping_result_t test_hardware_snooping(void);

/* Main analysis function */
coherency_analysis_t perform_complete_coherency_analysis(void);

/* Enhanced analysis with VDS and device awareness - GPT-5 recommendation */
enhanced_coherency_analysis_t perform_enhanced_coherency_analysis(device_caps_t* device_caps);

/* VDS-specific detection and testing functions */
bool test_vds_availability(void);
snooping_result_t test_vds_functionality(void);
bool detect_v86_environment(void);
bool detect_memory_manager_type(char* manager_name, size_t name_len);

/* Validation and utility functions */
bool validate_coherency_test_results(const coherency_analysis_t *analysis);
const char* get_cache_tier_description(cache_tier_t tier);
void print_detailed_coherency_results(const coherency_analysis_t *analysis);

/* Cache mode detection */
cache_mode_t detect_cache_mode(void);
bool is_cache_enabled(void);

/* Performance opportunity analysis */
performance_opportunity_t analyze_performance_opportunity(const coherency_analysis_t *coherency);
cache_recommendation_t generate_cache_recommendation(const coherency_analysis_t *coherency, 
                                                    performance_opportunity_t opportunity);

/* Result descriptions for user interface */
const char* get_bus_master_result_description(bus_master_result_t result);
const char* get_coherency_result_description(coherency_result_t result);
const char* get_snooping_result_description(snooping_result_t result);
const char* get_cache_mode_description(cache_mode_t mode);

/* Test configuration constants */
#define COHERENCY_TEST_TIMEOUT_MS    5000    /* Maximum test duration */
#define COHERENCY_MIN_CONFIDENCE     70      /* Minimum confidence for tier selection */
#define COHERENCY_HIGH_CONFIDENCE    90      /* High confidence threshold */

/* Test result validation macros */
#define IS_COHERENCY_SAFE(analysis) \
    ((analysis)->coherency == COHERENCY_OK || \
     (analysis)->selected_tier != CACHE_TIER_4_FALLBACK)

#define REQUIRES_CACHE_MANAGEMENT(analysis) \
    ((analysis)->coherency == COHERENCY_PROBLEM)

#define HAS_HARDWARE_SNOOPING(analysis) \
    ((analysis)->snooping == SNOOPING_FULL)

#endif /* CACHE_COHERENCY_H */