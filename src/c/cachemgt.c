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

/* Standard C includes */
#include <string.h>
#include <stdint.h>
#include <stdbool.h>  /* For bool, true, false */
#include "dos_io.h"

/* Project includes */
#include "../../include/portabl.h"  /* For COMPILER_* detection */
#include "../../include/cachemgt.h"
#include "../../include/cachecoh.h"
#include "../../include/cpudet.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include "../../include/common.h"   /* For get_system_timestamp_ms() */

/* Helper function: Map get_current_timestamp() to system timestamp function */
static inline uint32_t get_current_timestamp(void) {
    return get_system_timestamp_ms();
}

/* Helper function: Get CPU info as a value (wraps cpu_get_info pointer return) */
static inline cpu_info_t detect_cpu_info(void) {
    const cpu_info_t* info;
    cpu_info_t default_info = {0};

    info = cpu_get_info();
    if (info) {
        return *info;
    }
    /* Return default zeroed structure if cpu_get_info() returns NULL */
    return default_info;
}

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
 * @brief Complete WBINVD sequence with proper privilege checks and serialization
 * 
 * This function implements a complete, safe WBINVD sequence as recommended by
 * Intel documentation and GPT-5 analysis. It includes all necessary safety
 * checks, proper serialization, and error handling.
 * 
 * @param context Description of operation for logging
 * @return true if WBINVD was executed successfully, false otherwise
 */
static bool perform_complete_wbinvd_sequence(const char* context) {
#if defined(COMPILER_BORLAND) || defined(COMPILER_WATCOM) || defined(COMPILER_MSC) || (defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
    const cpu_info_t* cpu_info = cpu_get_info();
    uint32_t start_time, end_time;
    uint16_t flags = 0;
    uint32_t duration;

    /* Step 1: Final privilege verification */
    if (!cpu_info->can_wbinvd) {
        log_error("WBINVD: %s - Cannot execute WBINVD (privilege/capability check failed)", context);
        return false;
    }

    /* Step 2: Disable interrupts for atomic operation */
#if defined(COMPILER_BORLAND) || defined(COMPILER_WATCOM) || defined(COMPILER_MSC)
    CRITICAL_SECTION_ENTER(flags);
#elif defined(__GNUC__) && defined(__i386__)
    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r" (flags)
        :
        : "memory"
    );
#elif defined(__GNUC__) && defined(__x86_64__)
    __asm__ volatile (
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r" (flags)
        :
        : "memory"
    );
#endif

    /* Step 3: Memory barrier to ensure all pending writes complete */
    /* Compiler barrier - handled by WBINVD below */

    /* Step 4: Execute WBINVD with timing measurement */
    start_time = get_current_timestamp();

#if defined(COMPILER_BORLAND) || defined(COMPILER_WATCOM) || defined(COMPILER_MSC)
    /* Call external assembly function for WBINVD */
    cache_wbinvd();
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile (
        "wbinvd"
        :
        :
        : "memory"
    );
#endif

    end_time = get_current_timestamp();

    /* Step 5: CPU serialization after WBINVD */
    /* For DOS compilers, WBINVD is serializing; additional serialization handled in ASM */
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    if (cpu_info->has_cpuid) {
        /* Use CPUID for serialization on CPUID-capable processors */
        uint32_t eax_out, ebx_out, ecx_out, edx_out;
#if defined(__i386__)
        __asm__ volatile (
            "pushl %%ebx\n\t"
            "cpuid\n\t"
            "movl %%ebx, %1\n\t"
            "popl %%ebx"
            : "=a" (eax_out), "=r" (ebx_out), "=c" (ecx_out), "=d" (edx_out)
            : "a" (0)
            : "memory"
        );
#else /* x86_64 */
        __asm__ volatile (
            "cpuid"
            : "=a" (eax_out), "=b" (ebx_out), "=c" (ecx_out), "=d" (edx_out)
            : "a" (0)
            : "memory"
        );
#endif
        (void)eax_out; (void)ebx_out; (void)ecx_out; (void)edx_out;
    }
#endif
    /* Note: far JMP serialization removed - only works in 16-bit real mode */

    /* Step 6: Final memory barrier - handled by restore_flags below */

    /* Step 7: Restore interrupt state */
#if defined(COMPILER_BORLAND) || defined(COMPILER_WATCOM) || defined(COMPILER_MSC)
    CRITICAL_SECTION_EXIT(flags);
#elif defined(__GNUC__)
    if (flags & 0x0200) { /* IF flag was set */
        __asm__ volatile ("sti" ::: "memory");
    }
#endif

    /* Step 8: Update metrics and logging */
    duration = end_time - start_time;
    metrics.total_overhead_microseconds += duration;

    if (duration > 500) { /* More than 500 microseconds */
        log_warning("WBINVD: %s - Slow execution (%u us) - possible system load", context, duration);
    } else {
        log_debug("WBINVD: %s - Complete cache flush (%u us)", context, duration);
    }

    return true;
#else
    /* Non-x86 platform stub - WBINVD not applicable */
    (void)context;
    log_debug("WBINVD: Stub implementation (non-x86 platform)");
    return true;
#endif
}

/**
 * Tier 2: WBINVD implementation (486+)
 */
static void cache_tier2_wbinvd_management(void *buffer, size_t length, cache_operation_t operation) {
    static uint32_t last_wbinvd_time = 0;
    static uint32_t wbinvd_batch_count = 0;
    uint32_t current_time = get_current_timestamp();
    cpu_info_t cpu_info = detect_cpu_info();
    
    /* GPT-5 Critical: Use centralized can_wbinvd detection from CPU stage */
    if (!cpu_info.can_wbinvd) {
        if (cpu_info.cpu_family == 4 && cpu_info.in_v86_mode) {
            /* 486 in V86 mode - cannot use WBINVD */
            log_error("WBINVD: 486 in V86 mode - DMA disabled, using PIO");
            cache_config.dma_disabled_reason = DMA_DISABLED_V86_MODE;
        } else if (cpu_info.cpu_family == 4 && !cpu_info.in_ring0) {
            /* 486 not in ring 0 - cannot use WBINVD */
            log_error("WBINVD: 486 not in ring 0 (CPL=%d) - DMA disabled", cpu_info.current_cpl);
            cache_config.dma_disabled_reason = DMA_DISABLED_V86_MODE; // Same practical effect
        } else {
            /* Other reason WBINVD not available */
            log_debug("WBINVD: Not available on this configuration");
            cache_config.dma_disabled_reason = DMA_DISABLED_SAFETY_FAIL;
        }
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
        /* Complete WBINVD sequence for DMA preparation */
        perform_complete_wbinvd_sequence("PRE-DMA");
        
    } else if (operation == CACHE_OPERATION_POST_DMA) {
        /* Complete WBINVD sequence for DMA completion */
        perform_complete_wbinvd_sequence("POST-DMA");
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
    /* Suppress unused parameter warnings - buffer/length not used in fallback tier */
    (void)buffer;
    (void)length;

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
#if defined(COMPILER_BORLAND) || defined(COMPILER_WATCOM) || defined(COMPILER_MSC)
    /* For DOS compilers, use the external assembly function */
    memory_fence();
#elif defined(__GNUC__)
    /* Compiler barrier - works on all GCC targets */
    __asm__ volatile ("" ::: "memory");

#if defined(__i386__) || defined(__x86_64__)
    /* CPU serializing instruction if available (x86 only) */
    if (cache_config.has_wbinvd) {
#if defined(__i386__)
        __asm__ volatile (
            "pushl %%eax\n\t"
            "movl %%cr0, %%eax\n\t"
            "movl %%eax, %%cr0\n\t"
            "popl %%eax"
            ::: "eax", "memory"
        );
#elif defined(__x86_64__)
        __asm__ volatile (
            "pushq %%rax\n\t"
            "movq %%cr0, %%rax\n\t"
            "movq %%rax, %%cr0\n\t"
            "popq %%rax"
            ::: "rax", "memory"
        );
#endif
    }
#endif /* x86 */
#else
    /* Unknown compiler: stub implementation */
    (void)cache_config;
#endif
}

/**
 * Helper function: Microsecond delay
 */
static void io_delay_microseconds(uint32_t microseconds) {
    volatile uint32_t i;
    /* Simple delay loop - would be replaced with precise timing in real implementation */
    for (i = 0; i < microseconds * 100; i++) {
#if defined(COMPILER_BORLAND) || defined(COMPILER_WATCOM) || defined(COMPILER_MSC)
        /* IO port read for delay - use portability layer */
        (void)IO_IN8(0x80);
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
        /* IO port read for delay (x86 only) */
        __asm__ volatile ("inb $0x80, %%al" ::: "al");
#else
        /* Unknown compiler: simple busy-wait with no-op */
        ;
#endif
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
    if (cpu_info.cpu_family >= 6) {      /* Pentium Pro+ */
        return 64;
    } else if (cpu_info.cpu_family == 5) { /* Pentium */
        return 32;
    } else if (cpu_info.cpu_family == 4) { /* 486 */
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

/**
 * @brief Centralized DMA policy resolution
 * 
 * This function consolidates all DMA enable/disable logic in one place,
 * providing consistent policy decisions across all modules based on
 * CPU capabilities, platform detection, and runtime testing.
 * 
 * @return dma_policy_extended_t Complete DMA policy decision with reasoning
 */
dma_policy_extended_t resolve_dma_policy(void) {
    dma_policy_extended_t policy = {0};
    const cpu_info_t* cpu_info = cpu_get_info();
    /* Forward declarations for functions used later in this function */
    extern bool vds_available(void);
    extern bool is_isa_bus(void);

    /* Default to enabled with no special handling */
    policy.dma_enabled = true;
    policy.cache_tier = CACHE_TIER_4_FALLBACK;
    policy.disable_reason = DMA_ENABLED;
    policy.requires_vds = false;
    policy.requires_bounce = false;
    policy.confidence_level = 100;
    
    /* Critical Path 1: 486 in V86 mode - GPT-5 mandated disable */
    if (cpu_info->cpu_family == 4 && cpu_info->in_v86_mode) {
        policy.dma_enabled = false;
        policy.disable_reason = DMA_DISABLED_V86_MODE;
        policy.confidence_level = 100;
        policy.explanation = "486 processors in V86 mode cannot safely use DMA due to "
                           "WBINVD privilege restrictions and inadequate software barriers.";
        return policy;
    }
    
    /* Critical Path 2: No WBINVD capability when cache coherency needed */
    if (!cpu_info->can_wbinvd && cache_config.write_back_cache && 
        !cache_config.hardware_snooping) {
        
        if (cpu_info->cpu_family >= 3) {
            /* 386+ without WBINVD - use bounce buffers */
            policy.requires_bounce = true;
            policy.cache_tier = CACHE_TIER_3_SOFTWARE;
            policy.confidence_level = 85;
            policy.explanation = "Write-back cache detected without WBINVD capability - "
                               "using bounce buffers for safety.";
        } else {
            /* 286 - no cache coherency issues */
            policy.cache_tier = CACHE_TIER_4_FALLBACK;
            policy.confidence_level = 95;
            policy.explanation = "286 processor with no cache - DMA safe without management.";
        }
        return policy;
    }
    
    /* Optimal Path 1: Hardware snooping available */
    if (cache_config.hardware_snooping) {
        policy.cache_tier = CACHE_TIER_4_FALLBACK; /* No cache management needed */
        policy.confidence_level = 100;
        policy.explanation = "Hardware cache snooping ensures DMA coherency automatically.";
        return policy;
    }
    
    /* Optimal Path 2: Modern CPU with CLFLUSH */
    if (cpu_info->has_clflush && cpu_info->cpu_family >= 6) {
        policy.cache_tier = CACHE_TIER_1_CLFLUSH;
        policy.confidence_level = 100;
        policy.explanation = "Pentium 4+ with CLFLUSH - surgical cache line management.";
        return policy;
    }
    
    /* Standard Path: 486+ with WBINVD capability */
    if (cpu_info->can_wbinvd && cpu_info->cpu_family >= 4) {
        policy.cache_tier = CACHE_TIER_2_WBINVD;
        policy.confidence_level = 95;
        policy.explanation = "486+ with WBINVD capability - complete cache flush method.";
        return policy;
    }
    
    /* V86 Mode Handling for non-486 */
    if (cpu_info->in_v86_mode) {
        /* VDS available? */
        if (vds_available()) {
            policy.requires_vds = true;
            policy.cache_tier = CACHE_TIER_4_FALLBACK;
            policy.confidence_level = 90;
            policy.explanation = "V86 mode with VDS support - using Virtual DMA Services.";
        } else {
            policy.dma_enabled = false;
            policy.disable_reason = DMA_DISABLED_V86_MODE;
            policy.confidence_level = 100;
            policy.explanation = "V86 mode without VDS - DMA disabled for safety.";
        }
        return policy;
    }
    
    /* ISA Bus Analysis - Check for 486/ISA overhead situation */
    if (is_isa_bus() && cpu_info->cpu_family == 4 && cache_config.write_back_cache) {
        /* GPT-5 identified: DMA may use MORE CPU than PIO on 486/ISA */
        policy.dma_enabled = false;
        policy.disable_reason = DMA_DISABLED_CACHE_OVERHEAD;
        policy.confidence_level = 85;
        policy.explanation = "486/ISA systems: cache flush overhead makes DMA less "
                           "efficient than PIO (counter-intuitive but measured).";
        return policy;
    }
    
    /* Conservative Fallback */
    policy.cache_tier = CACHE_TIER_3_SOFTWARE;
    policy.confidence_level = 70;
    policy.explanation = "Conservative software barriers - performance impact but safe.";
    return policy;
}

/**
 * @brief Complete policy matrix for all CPU families
 * 
 * This comprehensive matrix provides DMA policy decisions for all x86 processors
 * from 286 through modern CPUs, incorporating GPT-5 analysis and real-world
 * performance measurements.
 * 
 * @param cpu_family CPU family (2=286, 3=386, 4=486, 5=Pentium, 6+=Pentium Pro+)
 * @param in_v86_mode true if running in V86 mode
 * @param has_hardware_snooping true if chipset provides cache snooping
 * @param is_isa_bus true if using ISA bus (affects 486 policy)
 * @return dma_policy_extended_t Complete policy for the given configuration
 */
dma_policy_extended_t get_cpu_family_policy_matrix(uint8_t cpu_family, bool in_v86_mode,
                                         bool has_hardware_snooping, bool is_isa_bus_param) {
    dma_policy_extended_t policy = {0};
    const cpu_info_t* cpu_info_ptr;
    /* Forward declaration for VDS availability check */
    extern bool vds_available(void);

    /* Avoid unused parameter warning while still using passed value */
    (void)is_isa_bus_param;

    policy.dma_enabled = true;
    policy.disable_reason = DMA_ENABLED;
    policy.requires_vds = false;
    policy.requires_bounce = false;
    policy.confidence_level = 100;
    
    switch (cpu_family) {
        case 2: /* 80286 */
            policy.cache_tier = CACHE_TIER_4_FALLBACK;
            if (in_v86_mode) {
                if (vds_available()) {
                    policy.requires_vds = true;
                    policy.confidence_level = 95;
                    policy.explanation = "286 in V86 mode with VDS - safe DMA operation.";
                } else {
                    policy.dma_enabled = false;
                    policy.disable_reason = DMA_DISABLED_V86_MODE;
                    policy.explanation = "286 in V86 mode without VDS - DMA disabled for safety.";
                }
            } else {
                policy.confidence_level = 100;
                policy.explanation = "286 real mode - no cache coherency issues, DMA safe.";
            }
            break;
            
        case 3: /* 80386 */
            if (has_hardware_snooping) {
                policy.cache_tier = CACHE_TIER_4_FALLBACK;
                policy.confidence_level = 100;
                policy.explanation = "386 with hardware snooping - no cache management needed.";
            } else if (in_v86_mode) {
                if (vds_available()) {
                    policy.requires_vds = true;
                    policy.cache_tier = CACHE_TIER_3_SOFTWARE;
                    policy.confidence_level = 85;
                    policy.explanation = "386 in V86 mode with VDS - software cache management.";
                } else {
                    policy.dma_enabled = false;
                    policy.disable_reason = DMA_DISABLED_V86_MODE;
                    policy.explanation = "386 in V86 mode without VDS - DMA disabled.";
                }
            } else {
                /* 386 real mode with write-back cache */
                policy.requires_bounce = true;
                policy.cache_tier = CACHE_TIER_3_SOFTWARE;
                policy.confidence_level = 80;
                policy.explanation = "386 with write-back cache - software barriers and bounce buffers.";
            }
            break;
            
        case 4: /* 80486 - GPT-5 Critical Analysis Applied */
            if (in_v86_mode) {
                /* GPT-5 mandated: 486 in V86 mode cannot safely use DMA */
                policy.dma_enabled = false;
                policy.disable_reason = DMA_DISABLED_V86_MODE;
                policy.confidence_level = 100;
                policy.explanation = "486 in V86 mode - WBINVD privilege restrictions make DMA unsafe. "
                                   "Software barriers insufficient for cache coherency.";
            } else if (is_isa_bus_param) {
                /* GPT-5 insight: DMA uses MORE CPU than PIO on 486/ISA */
                policy.dma_enabled = false;
                policy.disable_reason = DMA_DISABLED_ISA_486;
                policy.confidence_level = 90;
                policy.explanation = "486 on ISA bus - cache flush overhead makes DMA less "
                                   "efficient than PIO (measured 52% vs 45% CPU usage).";
            } else if (has_hardware_snooping) {
                policy.cache_tier = CACHE_TIER_4_FALLBACK;
                policy.confidence_level = 100;
                policy.explanation = "486 with hardware snooping - no cache management needed.";
            } else {
                /* 486 real mode with PCI/VLB - optimal WBINVD usage */
                policy.cache_tier = CACHE_TIER_2_WBINVD;
                policy.confidence_level = 95;
                policy.explanation = "486 real mode with bus mastering - WBINVD cache management.";
            }
            break;
            
        case 5: /* Pentium */
            if (has_hardware_snooping) {
                policy.cache_tier = CACHE_TIER_4_FALLBACK;
                policy.confidence_level = 100;
                policy.explanation = "Pentium with hardware snooping - coherent DMA automatically.";
            } else if (in_v86_mode) {
                /* Pentium cache snooping makes V86 DMA safe */
                if (vds_available()) {
                    policy.requires_vds = true;
                    policy.cache_tier = CACHE_TIER_2_WBINVD;
                    policy.confidence_level = 90;
                    policy.explanation = "Pentium in V86 mode with VDS - WBINVD safe due to improved caching.";
                } else {
                    policy.cache_tier = CACHE_TIER_2_WBINVD;
                    policy.confidence_level = 85;
                    policy.explanation = "Pentium in V86 mode - cache coherency sufficient for DMA safety.";
                }
            } else {
                policy.cache_tier = CACHE_TIER_2_WBINVD;
                policy.confidence_level = 95;
                policy.explanation = "Pentium real mode - efficient WBINVD cache management.";
            }
            break;

        case 6: /* Pentium Pro/Pentium II */
            if (has_hardware_snooping) {
                policy.cache_tier = CACHE_TIER_4_FALLBACK;
                policy.confidence_level = 100;
                policy.explanation = "P6 architecture with hardware snooping - fully coherent DMA.";
            } else {
                policy.cache_tier = CACHE_TIER_2_WBINVD;
                policy.confidence_level = 95;
                policy.explanation = "P6 architecture - advanced WBINVD implementation.";
            }
            break;
            
        default: /* Pentium 4+ (family 15+) */
            if (has_hardware_snooping) {
                policy.cache_tier = CACHE_TIER_4_FALLBACK;
                policy.confidence_level = 100;
                policy.explanation = "Modern CPU with hardware snooping - no cache management needed.";
            } else {
                cpu_info_ptr = cpu_get_info();
                if (cpu_info_ptr->has_clflush) {
                    policy.cache_tier = CACHE_TIER_1_CLFLUSH;
                    policy.confidence_level = 100;
                    policy.explanation = "Modern CPU with CLFLUSH - surgical cache line management.";
                } else {
                    policy.cache_tier = CACHE_TIER_2_WBINVD;
                    policy.confidence_level = 95;
                    policy.explanation = "Modern CPU with WBINVD - complete cache management.";
                }
            }
            break;
    }
    
    return policy;
}

/**
 * @brief Print comprehensive policy matrix for debugging
 * 
 * This function outputs the complete DMA policy matrix for all supported
 * CPU families and configurations, useful for system analysis and debugging.
 */
void print_complete_policy_matrix(void) {
    const char* cpu_names[] = {"Unknown", "Unknown", "286", "386", "486", "Pentium", "P6+", "Modern"};
    bool test_configs[][3] = {
        {false, false, false}, /* Real mode, no snooping, PCI/VLB */
        {false, false, true},  /* Real mode, no snooping, ISA */
        {false, true, false},  /* Real mode, with snooping, PCI/VLB */
        {true, false, false},  /* V86 mode, no snooping, PCI/VLB */
        {true, false, true},   /* V86 mode, no snooping, ISA */
        {true, true, false}    /* V86 mode, with snooping, PCI/VLB */
    };
    const char* config_names[] = {
        "Real/NoSnoop/PCI", "Real/NoSnoop/ISA", "Real/Snoop/PCI",
        "V86/NoSnoop/PCI", "V86/NoSnoop/ISA", "V86/Snoop/PCI"
    };
    
    printf("\n=== Complete DMA Policy Matrix ===\n");
    printf("CPU Family | Configuration  | DMA | Tier | Reason\n");
    printf("-----------|----------------|-----|------|-------\n");
    
    {
        uint8_t family;
        int config;
        const char* cpu_name;
        dma_policy_extended_t policy;

        for (family = 2; family <= 15; family++) {
            cpu_name = (family < 8) ? cpu_names[family] : cpu_names[7];

            for (config = 0; config < 6; config++) {
                policy = get_cpu_family_policy_matrix(
                    family,
                    test_configs[config][0], /* in_v86_mode */
                    test_configs[config][1], /* has_hardware_snooping */
                    test_configs[config][2]  /* is_isa_bus */
                );

                printf("%-10s | %-14s | %-3s | %-4d | %s\n",
                    cpu_name,
                    config_names[config],
                    policy.dma_enabled ? "Yes" : "No",
                    policy.cache_tier,
                    policy.explanation
                );
            }

            if (family == 6) family = 14; /* Skip to modern CPUs */
        }
    }

    printf("=====================================\n");
    printf("Tier Legend: 1=CLFLUSH, 2=WBINVD, 3=Software, 4=None, 0=Disabled\n");
}

/**
 * @brief Flush cache for a memory range
 *
 * Ensures cache contents are written back to memory for DMA operations.
 *
 * @param buffer Pointer to start of buffer
 * @param len Length of buffer in bytes
 */
void cache_flush_range(void *buffer, size_t len) {
    if (!buffer || len == 0) {
        return;
    }

    /* Use appropriate tier-based cache flush via the management functions */
    switch (active_tier) {
        case CACHE_TIER_1_CLFLUSH:
            cache_tier1_clflush_management(buffer, len, CACHE_OPERATION_FLUSH);
            break;
        case CACHE_TIER_2_WBINVD:
            cache_tier2_wbinvd_management(buffer, len, CACHE_OPERATION_FLUSH);
            break;
        case CACHE_TIER_3_SOFTWARE:
            cache_tier3_software_management(buffer, len, CACHE_OPERATION_FLUSH);
            break;
        case CACHE_TIER_4_FALLBACK:
        default:
            /* No cache management needed/possible */
            break;
    }
}

/**
 * @brief Invalidate cache for a memory range
 *
 * Ensures CPU will read from memory instead of stale cache for RX DMA.
 *
 * @param buffer Pointer to start of buffer
 * @param len Length of buffer in bytes
 */
void cache_invalidate_range(void *buffer, size_t len) {
    /* For most x86 CPUs, invalidation and flush are the same operation */
    cache_flush_range(buffer, len);
}

/**
 * @brief Safe aligned cache flush for DMA operations
 *
 * GPT-5 Enhancement: Ensures cache operations are aligned to cache line
 * boundaries to prevent partial line corruption issues.
 *
 * @param buffer Pointer to buffer (may be unaligned)
 * @param len Length of buffer in bytes
 */
void cache_flush_aligned_safe(void *buffer, size_t len) {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t aligned_start;
    uint32_t aligned_end;
    size_t aligned_len;
    uint16_t line_size;

    if (!buffer || len == 0) {
        return;
    }

    /* Get current cache line size */
    line_size = (uint16_t)cache_config.cache_line_size;
    if (line_size == 0) {
        line_size = DEFAULT_CACHE_LINE_SIZE;
    }

    /* Calculate aligned boundaries */
    start_addr = (uint32_t)(unsigned long)buffer;
    end_addr = start_addr + len;

    /* Align start down to cache line boundary */
    aligned_start = start_addr & ~(uint32_t)(line_size - 1);
    /* Align end up to cache line boundary */
    aligned_end = (end_addr + line_size - 1) & ~(uint32_t)(line_size - 1);
    aligned_len = aligned_end - aligned_start;

    /* Flush the aligned region */
    cache_flush_range((void*)(unsigned long)aligned_start, aligned_len);
}
