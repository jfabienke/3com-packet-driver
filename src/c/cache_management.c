/**
 * @file cache_management.c
 * @brief 4-Tier cache management implementation for DMA safety
 *
 * 3Com Packet Driver - Cache Management Implementation
 *
 * This module implements the 4-tier cache management system that ensures
 * DMA safety across all x86 processors from 286 through modern CPUs.
 * The system automatically selects the optimal strategy based on CPU
 * capabilities and runtime testing results.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "../include/cache_management.h"
#include "../include/cache_coherency.h"
#include "../include/cpu_detect.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/memory.h"
#include <string.h>
#include <stdint.h>

/* Global cache management configuration */
static cache_management_config_t cache_config = {0};
static cache_tier_t active_tier = CACHE_TIER_4_FALLBACK;
static bool cache_management_initialized = false;

/* Performance metrics */
static cache_management_metrics_t metrics = {0};

/* Cache line size detection - will be set during initialization */
static size_t detected_cache_line_size = 32; /* Default fallback */

/* Forward declarations */
static void cache_tier1_clflush_management(void *buffer, size_t length, cache_operation_t operation);
static void cache_tier2_wbinvd_management(void *buffer, size_t length, cache_operation_t operation);
static void cache_tier3_software_management(void *buffer, size_t length, cache_operation_t operation);
static void cache_tier4_fallback_management(void *buffer, size_t length, cache_operation_t operation);

static void force_cache_line_touch(void *buffer, size_t length);
static void memory_barrier_inline(void);
static void io_delay_microseconds(uint32_t microseconds);
static size_t detect_cache_line_size(void);

/* External assembly functions */
extern void cache_clflush_line(void *addr);
extern void cache_wbinvd(void);
extern uint32_t read_cr0_register(void);
extern void write_cr0_register(uint32_t value);

/**
 * Initialize cache management system
 */
bool initialize_cache_management(const coherency_analysis_t *analysis) {
    cpu_info_t cpu_info;
    
    log_info("Initializing cache management system...");
    
    if (!analysis) {
        log_error("Invalid coherency analysis provided");
        return false;
    }
    
    /* Set up configuration based on analysis */
    cache_config.selected_tier = analysis->selected_tier;
    cache_config.confidence_level = analysis->confidence;
    cache_config.write_back_cache = analysis->write_back_cache;
    cache_config.hardware_snooping = (analysis->snooping == SNOOPING_FULL);
    
    /* Detect CPU capabilities */
    cpu_info = detect_cpu_info();
    cache_config.has_clflush = cpu_info.has_clflush;
    cache_config.has_wbinvd = cpu_info.has_wbinvd;
    
    /* Detect cache line size */
    detected_cache_line_size = detect_cache_line_size();
    cache_config.cache_line_size = detected_cache_line_size;
    
    /* Set active tier */
    active_tier = analysis->selected_tier;
    
    /* Initialize metrics */
    memset(&metrics, 0, sizeof(metrics));
    metrics.initialization_time = get_current_timestamp();
    
    cache_management_initialized = true;
    
    log_info("Cache management initialized: %s", 
             get_cache_tier_description(active_tier));
    log_info("Cache line size: %u bytes", detected_cache_line_size);
    
    return true;
}

/**
 * Execute cache management for DMA operations
 */
void cache_management_dma_prepare(void *buffer, size_t length) {
    uint32_t start_time;
    
    if (!cache_management_initialized) {
        log_warning("Cache management not initialized - using fallback");
        active_tier = CACHE_TIER_4_FALLBACK;
    }
    
    if (!buffer || length == 0) {
        log_error("Invalid buffer parameters for cache management");
        return;
    }
    
    /* Record start time for metrics */
    start_time = get_current_timestamp();
    
    /* Execute tier-specific cache management */
    switch (active_tier) {
        case CACHE_TIER_1_CLFLUSH:
            cache_tier1_clflush_management(buffer, length, CACHE_OPERATION_PRE_DMA);
            metrics.tier1_operations++;
            break;
            
        case CACHE_TIER_2_WBINVD:
            cache_tier2_wbinvd_management(buffer, length, CACHE_OPERATION_PRE_DMA);
            metrics.tier2_operations++;
            break;
            
        case CACHE_TIER_3_SOFTWARE:
            cache_tier3_software_management(buffer, length, CACHE_OPERATION_PRE_DMA);
            metrics.tier3_operations++;
            break;
            
        case CACHE_TIER_4_FALLBACK:
            cache_tier4_fallback_management(buffer, length, CACHE_OPERATION_PRE_DMA);
            metrics.tier4_operations++;
            break;
            
        case TIER_DISABLE_BUS_MASTER:
        default:
            /* No cache management needed - bus mastering disabled */
            metrics.disabled_operations++;
            return;
    }
    
    /* Update performance metrics */
    metrics.total_operations++;
    metrics.total_overhead_microseconds += (get_current_timestamp() - start_time);
    
    log_debug("Cache prepare: %u bytes, tier %d", length, active_tier);
}

/**
 * Execute cache management after DMA completion
 */
void cache_management_dma_complete(void *buffer, size_t length) {
    uint32_t start_time;
    
    if (!cache_management_initialized || active_tier == TIER_DISABLE_BUS_MASTER) {
        return;
    }
    
    if (!buffer || length == 0) {
        return;
    }
    
    start_time = get_current_timestamp();
    
    /* Execute tier-specific post-DMA cache management */
    switch (active_tier) {
        case CACHE_TIER_1_CLFLUSH:
            cache_tier1_clflush_management(buffer, length, CACHE_OPERATION_POST_DMA);
            break;
            
        case CACHE_TIER_2_WBINVD:
            cache_tier2_wbinvd_management(buffer, length, CACHE_OPERATION_POST_DMA);
            break;
            
        case CACHE_TIER_3_SOFTWARE:
            cache_tier3_software_management(buffer, length, CACHE_OPERATION_POST_DMA);
            break;
            
        case CACHE_TIER_4_FALLBACK:
            cache_tier4_fallback_management(buffer, length, CACHE_OPERATION_POST_DMA);
            break;
    }
    
    metrics.total_overhead_microseconds += (get_current_timestamp() - start_time);
    
    log_debug("Cache complete: %u bytes, tier %d", length, active_tier);
}

/**
 * Tier 1: CLFLUSH implementation (Pentium 4+)
 */
static void cache_tier1_clflush_management(void *buffer, size_t length, cache_operation_t operation) {
    uint8_t *ptr = (uint8_t*)buffer;
    uint8_t *end = ptr + length;
    size_t cache_line_size = cache_config.cache_line_size;
    
    if (!cache_config.has_clflush) {
        log_error("CLFLUSH not available - falling back to Tier 2");
        cache_tier2_wbinvd_management(buffer, length, operation);
        return;
    }
    
    /* Align to cache line boundaries */
    ptr = (uint8_t*)((uintptr_t)ptr & ~(cache_line_size - 1));
    
    if (operation == CACHE_OPERATION_PRE_DMA) {
        /* Flush cache lines to ensure data reaches memory */
        while (ptr < end) {
            cache_clflush_line(ptr);
            ptr += cache_line_size;
        }
        memory_barrier_inline();
        
    } else if (operation == CACHE_OPERATION_POST_DMA) {
        /* Invalidate cache lines to ensure fresh data on next read */
        while (ptr < end) {
            cache_clflush_line(ptr);
            ptr += cache_line_size;
        }
        memory_barrier_inline();
    }
    
    log_debug("CLFLUSH: %u cache lines processed", 
              (length + cache_line_size - 1) / cache_line_size);
}

/**
 * Tier 2: WBINVD implementation (486+)
 */
static void cache_tier2_wbinvd_management(void *buffer, size_t length, cache_operation_t operation) {
    static uint32_t last_wbinvd_time = 0;
    static uint32_t wbinvd_batch_count = 0;
    uint32_t current_time = get_current_timestamp();
    cpu_info_t cpu_info = detect_cpu_info();
    
    /* GPT-5 Critical: Check for V86 mode - WBINVD is privileged */
    if (cpu_info.in_v86_mode) {
        log_debug("WBINVD: Cannot execute in V86 mode - using software barriers");
        cache_tier3_software_management(buffer, length, operation);
        return;
    }
    
    if (!cache_config.has_wbinvd) {
        log_error("WBINVD not available - falling back to Tier 3");
        cache_tier3_software_management(buffer, length, operation);
        return;
    }
    
    /* Batching optimization: avoid excessive WBINVD calls */
    if (current_time - last_wbinvd_time < 1000) { /* Less than 1ms since last WBINVD */
        wbinvd_batch_count++;
        if (wbinvd_batch_count < 4) {
            /* Skip WBINVD for small, frequent operations */
            log_debug("WBINVD: Batching optimization - skipping operation");
            return;
        }
    }
    
    if (operation == CACHE_OPERATION_PRE_DMA) {
        /* Write-back and invalidate entire cache */
        memory_barrier_inline();
        cache_wbinvd();
        memory_barrier_inline();
        
    } else if (operation == CACHE_OPERATION_POST_DMA) {
        /* Invalidate cache to ensure fresh data */
        cache_wbinvd();
        memory_barrier_inline();
    }
    
    last_wbinvd_time = current_time;
    wbinvd_batch_count = 0;
    
    log_debug("WBINVD: Complete cache flush/invalidate");
}

/**
 * Tier 3: Software cache management (386+)
 */
static void cache_tier3_software_management(void *buffer, size_t length, cache_operation_t operation) {
    if (operation == CACHE_OPERATION_PRE_DMA) {
        /* Force write completion through cache line touching */
        force_cache_line_touch(buffer, length);
        memory_barrier_inline();
        io_delay_microseconds(10); /* Allow cache settling time */
        
    } else if (operation == CACHE_OPERATION_POST_DMA) {
        /* Force cache invalidation through read operations */
        force_cache_line_touch(buffer, length);
        memory_barrier_inline();
        io_delay_microseconds(5); /* Shorter delay for post-DMA */
    }
    
    log_debug("Software cache management: %u bytes touched", length);
}

/**
 * Tier 4: Conservative fallback (286+)
 */
static void cache_tier4_fallback_management(void *buffer, size_t length, cache_operation_t operation) {
    /* Conservative approach with safety delays */
    memory_barrier_inline();
    
    if (operation == CACHE_OPERATION_PRE_DMA) {
        io_delay_microseconds(20); /* Conservative delay before DMA */
    } else if (operation == CACHE_OPERATION_POST_DMA) {
        io_delay_microseconds(15); /* Conservative delay after DMA */
    }
    
    log_debug("Fallback cache management: Conservative delays applied");
}

/**
 * Helper function: Force cache line touching
 */
static void force_cache_line_touch(void *buffer, size_t length) {
    volatile uint8_t *ptr = (volatile uint8_t*)buffer;
    volatile uint8_t *end = ptr + length;
    size_t cache_line_size = cache_config.cache_line_size;
    volatile uint8_t dummy;
    
    /* Touch every cache line to force write-back */
    while (ptr < end) {
        dummy = *ptr;  /* Force cache line access */
        ptr += cache_line_size;
    }
    
    /* Prevent compiler optimization */
    (void)dummy;
}

/**
 * Helper function: Memory barrier
 */
static void memory_barrier_inline(void) {
    /* Compiler barrier */
    __asm__ volatile ("" ::: "memory");
    
    /* CPU serializing instruction if available */
    if (cache_config.has_wbinvd) {
        __asm__ volatile (
            "push %%eax\n\t"
            "mov %%cr0, %%eax\n\t"
            "mov %%eax, %%cr0\n\t"
            "pop %%eax"
            ::: "eax", "memory"
        );
    }
}

/**
 * Helper function: Microsecond delay
 */
static void io_delay_microseconds(uint32_t microseconds) {
    /* Simple delay loop - would be replaced with precise timing in real implementation */
    for (volatile uint32_t i = 0; i < microseconds * 100; i++) {
        /* IO port read for delay */
        __asm__ volatile ("inb $0x80, %%al" ::: "al");
    }
}

/**
 * Detect cache line size
 */
static size_t detect_cache_line_size(void) {
    cpu_info_t cpu_info = detect_cpu_info();
    
    /* Use CPUID if available */
    if (cpu_info.has_cpuid && cpu_info.cache_line_size > 0) {
        return cpu_info.cache_line_size;
    }
    
    /* CPU generation-based defaults */
    if (cpu_info.family >= 6) {      /* Pentium Pro+ */
        return 64;
    } else if (cpu_info.family == 5) { /* Pentium */
        return 32;
    } else if (cpu_info.family == 4) { /* 486 */
        return 16;
    } else {
        return 32; /* Conservative default */
    }
}

/**
 * Get current cache management configuration
 */
cache_management_config_t get_cache_management_config(void) {
    return cache_config;
}

/**
 * Get cache management performance metrics
 */
cache_management_metrics_t get_cache_management_metrics(void) {
    /* Calculate derived metrics */
    if (metrics.total_operations > 0) {
        metrics.average_overhead_microseconds = 
            metrics.total_overhead_microseconds / metrics.total_operations;
    }
    
    return metrics;
}

/**
 * Check if cache management is required for current configuration
 */
bool cache_management_required(void) {
    return (active_tier != CACHE_TIER_4_FALLBACK && 
            active_tier != TIER_DISABLE_BUS_MASTER);
}

/**
 * Update cache management configuration
 */
bool update_cache_management_config(const cache_management_config_t *new_config) {
    if (!new_config) {
        return false;
    }
    
    /* Validate new configuration */
    if (new_config->selected_tier < TIER_DISABLE_BUS_MASTER ||
        new_config->selected_tier > CACHE_TIER_4_FALLBACK) {
        log_error("Invalid cache tier in new configuration");
        return false;
    }
    
    /* Update configuration */
    cache_config = *new_config;
    active_tier = new_config->selected_tier;
    
    log_info("Cache management configuration updated to tier %d", active_tier);
    return true;
}

/**
 * Reset cache management metrics
 */
void reset_cache_management_metrics(void) {
    memset(&metrics, 0, sizeof(metrics));
    metrics.initialization_time = get_current_timestamp();
    log_debug("Cache management metrics reset");
}

/**
 * Print cache management status
 */
void print_cache_management_status(void) {
    cache_management_metrics_t current_metrics = get_cache_management_metrics();
    
    printf("\n=== Cache Management Status ===\n");
    printf("Active Tier: %s\n", get_cache_tier_description(active_tier));
    printf("Cache Line Size: %u bytes\n", cache_config.cache_line_size);
    printf("Write-Back Cache: %s\n", cache_config.write_back_cache ? "Yes" : "No");
    printf("Hardware Snooping: %s\n", cache_config.hardware_snooping ? "Yes" : "No");
    printf("Confidence Level: %u%%\n", cache_config.confidence_level);
    
    printf("\nPerformance Metrics:\n");
    printf("Total Operations: %u\n", current_metrics.total_operations);
    printf("Average Overhead: %u microseconds\n", current_metrics.average_overhead_microseconds);
    printf("Tier 1 Operations: %u\n", current_metrics.tier1_operations);
    printf("Tier 2 Operations: %u\n", current_metrics.tier2_operations);
    printf("Tier 3 Operations: %u\n", current_metrics.tier3_operations);
    printf("Tier 4 Operations: %u\n", current_metrics.tier4_operations);
    printf("==============================\n");
}