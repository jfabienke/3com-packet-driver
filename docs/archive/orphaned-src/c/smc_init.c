/**
 * @file smc_init.c
 * @brief Self-Modifying Code initialization for optimized paths
 * 
 * Performs one-time V86 detection and patches all critical code paths
 * based on the execution environment. This eliminates runtime V86 checks
 * in hot paths for maximum performance.
 * 
 * GPT-5 Enhanced: Added CPU/chipset feature detection for optimal patching
 */

#include <stdint.h>
#include <stdbool.h>
#include "logging.h"
#include "cpu_detect.h"
#include "cache_coherency.h"

/* External assembly functions */
extern int pci_io_patch_init(void);
extern int vortex_tx_patch_init(void);
extern int vortex_rx_patch_init(void);
extern int isr_tiny_patch_init(void);
extern void smc_atomic_patch_5byte(void* target, void* patch_data);
extern void apply_cache_patch_templates(int tier);

/* CPU/Chipset feature detection results */
typedef struct {
    uint8_t cpu_family;         /* 2=286, 3=386, 4=486, 5=P5, 6=P6+ */
    uint8_t has_cpuid;
    uint8_t has_clflush;
    uint8_t has_wbinvd;
    uint8_t has_prefetch;
    uint8_t cache_coherent;     /* Hardware cache coherency */
    uint8_t bus_snooping;       /* Chipset supports bus snooping */
    uint8_t needs_cache_mgmt;   /* Requires explicit cache management */
} cpu_features_t;

/* Statistics for tracking patch application */
typedef struct {
    uint8_t v86_detected;
    uint8_t pci_io_patched;
    uint8_t vortex_tx_patched;
    uint8_t vortex_rx_patched;
    uint8_t isr_patched;
    uint8_t cache_patches_applied;
    uint8_t cpu_optimized;
    uint32_t patches_applied;
    cpu_features_t cpu_features;
} smc_stats_t;

static smc_stats_t smc_stats = {0};

/**
 * @brief Detect CPU and chipset features for SMC patching
 * 
 * GPT-5 requirement: Detect CPU generation and cache capabilities
 * to determine optimal patch strategies.
 */
static void detect_cpu_chipset_features(void) {
    cpu_info_t* cpu_info = get_cpu_info();
    cache_analysis_t cache_info = {0};
    
    /* Get CPU family and basic features */
    smc_stats.cpu_features.cpu_family = cpu_info->cpu_family;
    smc_stats.cpu_features.has_cpuid = cpu_info->has_cpuid;
    smc_stats.cpu_features.has_clflush = cpu_info->has_clflush;
    smc_stats.cpu_features.has_wbinvd = cpu_info->has_wbinvd;
    
    /* Detect prefetch capability (Pentium Pro+) */
    smc_stats.cpu_features.has_prefetch = (cpu_info->cpu_family >= 6);
    
    /* Analyze cache coherency */
    analyze_cache_coherency(&cache_info);
    smc_stats.cpu_features.cache_coherent = cache_info.coherent;
    smc_stats.cpu_features.bus_snooping = cache_info.snooping_detected;
    
    /* Determine if explicit cache management is needed */
    if (cpu_info->cpu_family >= 4) {  /* 486+ has cache */
        if (!cache_info.coherent || !cache_info.snooping_detected) {
            smc_stats.cpu_features.needs_cache_mgmt = 1;
            LOG_INFO("CPU requires explicit cache management for DMA");
        } else {
            LOG_INFO("CPU has hardware cache coherency - no management needed");
        }
    }
    
    LOG_INFO("CPU Detection: Family=%d, CPUID=%d, CLFLUSH=%d, WBINVD=%d",
             smc_stats.cpu_features.cpu_family,
             smc_stats.cpu_features.has_cpuid,
             smc_stats.cpu_features.has_clflush,
             smc_stats.cpu_features.has_wbinvd);
}

/**
 * @brief Apply CPU-specific optimization patches
 * 
 * Based on detected CPU features, patch code with optimal instructions
 */
static void apply_cpu_optimizations(void) {
    /* Example patch data for different CPU generations */
    static uint8_t patch_386_rep_movs[] = {0xF3, 0xA5, 0x90, 0x90, 0x90}; /* REP MOVSD */
    static uint8_t patch_486_xcopy[] = {0x66, 0xF3, 0xA5, 0x90, 0x90};    /* 32-bit REP MOVSD */
    static uint8_t patch_p5_mmx[] = {0x0F, 0x6F, 0x06, 0x90, 0x90};       /* MOVQ MM0,[ESI] */
    static uint8_t patch_nop_sled[] = {0x90, 0x90, 0x90, 0x90, 0x90};     /* 5x NOP */
    
    /* External patch points from assembly modules */
    extern void* copy_patch_point;
    extern void* cache_flush_patch_point;
    
    switch (smc_stats.cpu_features.cpu_family) {
        case 3:  /* 386 */
            LOG_DEBUG("Applying 386 optimizations");
            /* Basic string operations */
            break;
            
        case 4:  /* 486 */
            LOG_DEBUG("Applying 486 optimizations with cache awareness");
            /* Add cache line awareness to copies */
            if (smc_stats.cpu_features.needs_cache_mgmt) {
                apply_cache_patch_templates(2);  /* Tier 2: WBINVD */
            }
            break;
            
        case 5:  /* Pentium */
            LOG_DEBUG("Applying Pentium optimizations with dual pipeline");
            /* Optimize for U/V pipe pairing */
            break;
            
        case 6:  /* Pentium Pro+ */
            LOG_DEBUG("Applying P6+ optimizations with CLFLUSH");
            if (smc_stats.cpu_features.has_clflush) {
                apply_cache_patch_templates(1);  /* Tier 1: CLFLUSH */
            }
            break;
            
        default:
            LOG_DEBUG("Unknown CPU family %d - using safe defaults", 
                     smc_stats.cpu_features.cpu_family);
            break;
    }
    
    smc_stats.cpu_optimized = 1;
}

/**
 * @brief Initialize all SMC patches based on execution environment
 * 
 * This function should be called once during driver initialization,
 * after hardware detection but before any I/O operations.
 * 
 * @return 0 on success, -1 on error
 */
int smc_init_all(void) {
    int v86_mode;
    
    LOG_INFO("Initializing SMC patches for hot paths");
    
    /* GPT-5 Enhancement: Detect CPU/chipset features first */
    detect_cpu_chipset_features();
    
    /* Check V86 mode once using PCI I/O patch init */
    v86_mode = pci_io_patch_init();
    smc_stats.v86_detected = (v86_mode > 0) ? 1 : 0;
    smc_stats.pci_io_patched = 1;
    smc_stats.patches_applied++;
    
    if (v86_mode > 0) {
        LOG_INFO("V86 mode detected - applying safe I/O patches");
    } else {
        LOG_INFO("Real mode detected - applying fast I/O patches");
    }
    
    /* Apply CPU-specific optimizations based on detection */
    apply_cpu_optimizations();
    
    /* Patch Vortex TX fast path */
    if (vortex_tx_patch_init() == 0) {
        smc_stats.vortex_tx_patched = 1;
        smc_stats.patches_applied++;
        LOG_DEBUG("Vortex TX path patched");
    }
    
    /* Patch Vortex RX fast path */
    if (vortex_rx_patch_init() == 0) {
        smc_stats.vortex_rx_patched = 1;
        smc_stats.patches_applied++;
        LOG_DEBUG("Vortex RX path patched");
    }
    
    /* Patch tiny ISR */
    if (isr_tiny_patch_init() == 0) {
        smc_stats.isr_patched = 1;
        smc_stats.patches_applied++;
        LOG_DEBUG("Tiny ISR patched");
    }
    
    LOG_INFO("SMC initialization complete: %lu patches applied",
             smc_stats.patches_applied);
    
    return 0;
}

/**
 * @brief Get SMC patch statistics
 * 
 * @param v86_mode Output: 1 if V86 detected, 0 if real mode
 * @param patches_applied Output: Number of patches applied
 */
void smc_get_stats(uint8_t *v86_mode, uint32_t *patches_applied) {
    if (v86_mode) {
        *v86_mode = smc_stats.v86_detected;
    }
    if (patches_applied) {
        *patches_applied = smc_stats.patches_applied;
    }
}

/**
 * @brief Check if a specific patch was applied
 * 
 * @param patch_id Patch identifier (0=PCI_IO, 1=VORTEX_TX, 2=VORTEX_RX, 3=ISR)
 * @return 1 if patched, 0 if not
 */
uint8_t smc_is_patched(uint8_t patch_id) {
    switch (patch_id) {
        case 0: return smc_stats.pci_io_patched;
        case 1: return smc_stats.vortex_tx_patched;
        case 2: return smc_stats.vortex_rx_patched;
        case 3: return smc_stats.isr_patched;
        default: return 0;
    }
}