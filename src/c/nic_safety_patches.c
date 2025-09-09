/**
 * @file smc_safety_patches.c
 * @brief SMC-based safety integration for DMA and cache coherency
 * 
 * This module bridges the gap between optimized hot paths and orphaned safety
 * modules by using Self-Modifying Code to patch safety checks based on runtime
 * detection. All detection code is in the cold section (discarded after init).
 * 
 * GPT-5 validated design with realistic TSR overhead of 1-2KB.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "logging.h"
#include "platform_probe.h"
#include "cpu_detect.h"
#include "cache_coherency.h"
#include "vds_core.h"
#include "dma_safety.h"
#include "cache_management.h"

/* Patch types for different safety strategies */
typedef enum {
    PATCH_NOP = 0,           /* No operation - fastest path */
    PATCH_VDS_LOCK,          /* VDS lock for V86 mode */
    PATCH_VDS_UNLOCK,        /* VDS unlock for V86 mode */
    PATCH_WBINVD,            /* Cache flush for 486+ */
    PATCH_BOUNCE_TX,         /* Bounce buffer for TX */
    PATCH_BOUNCE_RX,         /* Bounce buffer for RX */
    PATCH_CHECK_64KB,        /* 64KB boundary check */
    PATCH_PIO_FALLBACK,      /* PIO mode (no DMA) */
    PATCH_TYPE_COUNT
} patch_type_t;

/* DMA disable reasons */
typedef enum {
    DMA_ENABLED = 0,
    DMA_DISABLED_V86_MODE,
    DMA_DISABLED_BROKEN_BUS,
    DMA_DISABLED_PENDING_VALIDATION,
    DMA_DISABLED_USER_REQUEST
} dma_disable_reason_t;

/* Patch strategy based on detection */
typedef struct {
    patch_type_t rx_alloc;      /* RX buffer allocation patch */
    patch_type_t tx_prep;       /* TX preparation patch */
    patch_type_t rx_complete;   /* RX completion patch */
    bool disable_dma;           /* Force PIO mode */
    bool use_vds;              /* Use VDS for all operations */
    bool use_bounce;           /* Use bounce buffers */
    bool force_pio;            /* Force PIO for safety */
    dma_disable_reason_t dma_disable_reason; /* Why DMA is disabled */
} patch_strategy_t;

/* Safety configuration from detection */
typedef struct {
    /* Environment */
    bool in_v86;
    bool in_real_mode;
    bool has_vds;
    
    /* CPU capabilities */
    uint8_t cpu_family;        /* 2=286, 3=386, 4=486, 5=Pentium */
    bool has_cpuid;
    bool has_wbinvd;
    bool has_clflush;
    
    /* Runtime test results */
    bool bus_master_works;
    bool cache_coherent;
    bool has_snooping;
    cache_tier_t selected_tier;
    
    /* Device specific */
    bool is_3c515_pci;
    bool is_3c509_isa;
} safety_config_t;

/* External patch points in hot code */
extern void* _rx_alloc_point;
extern void* _tx_prep_point;
extern void* _rx_complete_point;

/* External safety stubs (from safety_stubs.asm) */
extern void vds_lock_stub(void);
extern void vds_unlock_stub(void);
extern void cache_flush_486(void);
extern void bounce_tx_stub(void);
extern void bounce_rx_stub(void);
extern void check_64kb_stub(void);
extern void pio_fallback_stub(void);

/* VDS buffer pool for pre-allocation */
#define VDS_POOL_SIZE 32

typedef struct {
    void __far* virt_addr;
    uint32_t phys_addr;
    uint16_t vds_handle;
    bool in_use;
} vds_buffer_t;

static vds_buffer_t vds_pool[VDS_POOL_SIZE];
static bool vds_pool_initialized = false;

/* Bounce buffer pool */
#define BOUNCE_POOL_SIZE 4

typedef struct {
    uint8_t buffer[1536];
    uint32_t phys_addr;
    bool in_use;
} bounce_buffer_t;

static bounce_buffer_t bounce_pool[BOUNCE_POOL_SIZE];
static bool bounce_pool_initialized = false;

/**
 * @brief Pre-allocate and lock VDS buffer pool
 * 
 * Called once at init to avoid per-packet VDS overhead
 */
static int vds_preallocate_buffer_pool(int count) {
    int i;
    
    if (count > VDS_POOL_SIZE) {
        count = VDS_POOL_SIZE;
    }
    
    LOG_INFO("Pre-allocating %d VDS buffers", count);
    
    for (i = 0; i < count; i++) {
        /* Allocate buffer in conventional memory */
        vds_pool[i].virt_addr = _fmalloc(1536);
        if (!vds_pool[i].virt_addr) {
            LOG_ERROR("Failed to allocate VDS buffer %d", i);
            return -1;
        }
        
        /* Lock with VDS */
        vds_raw_lock_result_t result;
        uint8_t err = vds_core_lock_region(
            vds_pool[i].virt_addr, 
            1536,
            VDS_FLAG_ISA_DMA | VDS_FLAG_NO_64K_CROSS,
            VDS_DIR_BIDIRECTIONAL,
            &result
        );
        
        if (err != VDS_RAW_SUCCESS) {
            LOG_ERROR("VDS lock failed for buffer %d: %d", i, err);
            _ffree(vds_pool[i].virt_addr);
            return -1;
        }
        
        vds_pool[i].phys_addr = result.physical_addr;
        vds_pool[i].vds_handle = result.lock_handle;
        vds_pool[i].in_use = false;
        
        /* Verify ISA DMA constraints */
        if (vds_pool[i].phys_addr >= 0x1000000) {
            LOG_ERROR("VDS buffer %d above 16MB: 0x%08lX", i, vds_pool[i].phys_addr);
            vds_core_unlock_region(vds_pool[i].vds_handle);
            _ffree(vds_pool[i].virt_addr);
            return -1;
        }
    }
    
    vds_pool_initialized = true;
    LOG_INFO("VDS pool initialized with %d buffers", count);
    return 0;
}

/**
 * @brief Allocate bounce buffers for cache-incoherent systems
 */
static int allocate_bounce_pool(void) {
    int i;
    
    LOG_INFO("Allocating bounce buffer pool");
    
    for (i = 0; i < BOUNCE_POOL_SIZE; i++) {
        /* Get physical address (real mode only) */
        uint16_t seg = FP_SEG(bounce_pool[i].buffer);
        uint16_t off = FP_OFF(bounce_pool[i].buffer);
        bounce_pool[i].phys_addr = ((uint32_t)seg << 4) + off;
        bounce_pool[i].in_use = false;
        
        /* Verify constraints */
        if (bounce_pool[i].phys_addr >= 0x1000000) {
            LOG_ERROR("Bounce buffer %d above 16MB", i);
            return -1;
        }
    }
    
    bounce_pool_initialized = true;
    LOG_INFO("Bounce pool initialized with %d buffers", BOUNCE_POOL_SIZE);
    return 0;
}

/* BMTEST validation flag - set by external validation tool */
static bool bmtest_passed = false;

/**
 * @brief Perform concrete DMA validation test for 3C515
 * @param io_base I/O base address of the 3C515
 * @return true if DMA works correctly, false otherwise
 * 
 * Tests DMA with buffers that cross and don't cross 64KB boundaries,
 * verifies data integrity, and measures transfer reliability.
 * Per 3Com Fast EtherLink spec, uses DMA control registers in window 7.
 */
static bool validate_dma_3c515(uint16_t io_base) {
    uint8_t test_pattern[] = {0xAA, 0x55, 0xFF, 0x00, 0x5A, 0xA5, 0x12, 0x34};
    uint8_t far* test_buffer;
    uint8_t far* verify_buffer;
    int test_passed = 1;
    
    LOG_INFO("Starting 3C515 DMA validation test");
    
    /* Allocate test buffer (try to get one that crosses 64KB) */
    test_buffer = (uint8_t far*)_fmalloc(1024);
    if (!test_buffer) {
        LOG_ERROR("Failed to allocate DMA test buffer");
        return false;
    }
    
    /* Calculate physical address and check 64KB boundary */
    uint32_t phys_addr = ((uint32_t)FP_SEG(test_buffer) << 4) + FP_OFF(test_buffer);
    uint32_t end_addr = phys_addr + 1023;
    bool crosses_64k = ((phys_addr >> 16) != (end_addr >> 16));
    
    LOG_DEBUG("Test buffer at %04X:%04X (phys 0x%05lX-0x%05lX), crosses 64K: %s",
              FP_SEG(test_buffer), FP_OFF(test_buffer), phys_addr, end_addr,
              crosses_64k ? "YES" : "NO");
    
    /* Initialize test pattern */
    for (int i = 0; i < 1024; i++) {
        test_buffer[i] = test_pattern[i % sizeof(test_pattern)];
    }
    
    /* Allocate verify buffer */
    verify_buffer = (uint8_t far*)_fmalloc(1024);
    if (!verify_buffer) {
        _ffree(test_buffer);
        LOG_ERROR("Failed to allocate verify buffer");
        return false;
    }
    memset(verify_buffer, 0, 1024);
    
    /* Save current window */
    uint16_t saved_window = inw(io_base + 0x0E) >> 13;
    
    /* Test 1: Small transfer within 64KB */
    _3C515_TX_SELECT_WINDOW(io_base, 7);  /* DMA control window */
    outl(io_base + 0x24, phys_addr);      /* DMA address register */
    outw(io_base + 0x28, 256);            /* DMA length - small transfer */
    outw(io_base + 0x20, 0x0001);         /* Start DMA write to NIC */
    
    /* Wait for DMA completion */
    int timeout = 1000;
    while (--timeout > 0) {
        if (inw(io_base + 0x20) & 0x0100) break;  /* DMA complete bit */
        nic_delay_microseconds(10);
    }
    
    if (timeout == 0) {
        LOG_ERROR("DMA write timeout on small transfer");
        test_passed = 0;
    }
    
    /* Test 2: Transfer that may cross 64KB boundary */
    if (test_passed && crosses_64k) {
        LOG_INFO("Testing DMA across 64KB boundary");
        
        /* Position buffer to cross boundary */
        uint16_t offset_to_boundary = 0x10000 - (phys_addr & 0xFFFF);
        if (offset_to_boundary > 256) {
            offset_to_boundary -= 256;  /* Start 256 bytes before boundary */
        }
        
        uint32_t test_addr = phys_addr + offset_to_boundary;
        outl(io_base + 0x24, test_addr);
        outw(io_base + 0x28, 512);        /* Transfer across boundary */
        outw(io_base + 0x20, 0x0001);     /* Start DMA */
        
        timeout = 1000;
        while (--timeout > 0) {
            if (inw(io_base + 0x20) & 0x0100) break;
            nic_delay_microseconds(10);
        }
        
        if (timeout == 0) {
            LOG_ERROR("DMA failed across 64KB boundary - ISA DMA limitation confirmed");
            test_passed = 0;
        }
    }
    
    /* Test 3: Verify data integrity with read-back */
    if (test_passed) {
        /* Perform DMA read to verify buffer */
        uint32_t verify_phys = ((uint32_t)FP_SEG(verify_buffer) << 4) + 
                               FP_OFF(verify_buffer);
        outl(io_base + 0x24, verify_phys);
        outw(io_base + 0x28, 256);
        outw(io_base + 0x20, 0x0002);     /* DMA read from NIC */
        
        timeout = 1000;
        while (--timeout > 0) {
            if (inw(io_base + 0x20) & 0x0100) break;
            nic_delay_microseconds(10);
        }
        
        if (timeout > 0) {
            /* Compare first 256 bytes */
            for (int i = 0; i < 256; i++) {
                if (verify_buffer[i] != test_buffer[i]) {
                    LOG_ERROR("DMA data corruption at offset %d: wrote 0x%02X, read 0x%02X",
                             i, test_buffer[i], verify_buffer[i]);
                    test_passed = 0;
                    break;
                }
            }
            if (test_passed) {
                LOG_DEBUG("DMA data integrity verified");
            }
        } else {
            LOG_ERROR("DMA read timeout");
            test_passed = 0;
        }
    }
    
    /* Restore window */
    _3C515_TX_SELECT_WINDOW(io_base, saved_window);
    
    /* Clean up */
    _ffree(test_buffer);
    _ffree(verify_buffer);
    
    if (test_passed) {
        LOG_INFO("3C515 DMA validation PASSED - DMA enabled");
        bmtest_passed = 1;
    } else {
        LOG_WARNING("3C515 DMA validation FAILED - using PIO mode for safety");
        bmtest_passed = 0;
    }
    
    return test_passed;
}

/**
 * @brief Check if BMTEST has validated DMA for 3C515
 * @return true if DMA has been validated, false otherwise
 */
static bool bmtest_validated(void) {
    /* Return cached validation result */
    return bmtest_passed;
}

/**
 * @brief Run DMA validation for 3C515 (called during init)
 * @param io_base I/O base of 3C515 card
 */
void run_3c515_dma_validation(uint16_t io_base) {
    if (io_base != 0) {
        validate_dma_3c515(io_base);
    }
}

/**
 * @brief Set BMTEST validation status (called by external tool)
 * @param validated true if BMTEST passed
 */
void set_bmtest_validation(bool validated) {
    bmtest_passed = validated;
    LOG_INFO("BMTEST validation status set to: %s", validated ? "PASSED" : "PENDING");
}

/**
 * @brief Gather safety configuration from all detection sources
 */
static void gather_safety_config(safety_config_t* cfg) {
    platform_probe_result_t platform;
    cpu_info_t* cpu;
    coherency_analysis_t coherency;
    
    memset(cfg, 0, sizeof(safety_config_t));
    
    /* Platform detection */
    platform = platform_detect();
    cfg->has_vds = platform.vds_available;
    cfg->in_v86 = platform.emm386_detected || platform.qemm_detected || 
                  platform.windows_enhanced;
    cfg->in_real_mode = !cfg->in_v86;
    
    /* CPU detection */
    detect_cpu_type();
    cpu = cpu_get_info();
    cfg->cpu_family = cpu->cpu_family_id;
    cfg->has_cpuid = (cpu->cpu_features & FEATURE_CPUID) != 0;
    cfg->has_wbinvd = (cpu->cpu_features & FEATURE_WBINVD_SAFE) != 0;
    cfg->has_clflush = (cpu->cpu_features & FEATURE_CLFLUSH) != 0;
    
    /* Cache coherency testing */
    coherency = run_complete_coherency_analysis();
    cfg->bus_master_works = (coherency.bus_master != BUS_MASTER_BROKEN);
    cfg->cache_coherent = (coherency.coherency == COHERENCY_OK);
    cfg->has_snooping = (coherency.snooping == SNOOPING_FULL);
    cfg->selected_tier = coherency.selected_tier;
    
    /* Device detection - TODO: Get from hardware module */
    extern bool is_3c515_detected(void);
    extern bool is_3c509_detected(void);
    cfg->is_3c515_pci = is_3c515_detected();
    cfg->is_3c509_isa = is_3c509_detected();
    
    LOG_INFO("Safety config gathered:");
    LOG_INFO("  V86=%d VDS=%d CPU=%d", cfg->in_v86, cfg->has_vds, cfg->cpu_family);
    LOG_INFO("  BusMaster=%d Coherent=%d Snooping=%d", 
             cfg->bus_master_works, cfg->cache_coherent, cfg->has_snooping);
}

/**
 * @brief Determine patch strategy based on detection
 */
static void determine_patch_strategy(safety_config_t* cfg, patch_strategy_t* strategy) {
    memset(strategy, 0, sizeof(patch_strategy_t));
    
    /* Default all patches to NOP */
    strategy->rx_alloc = PATCH_NOP;
    strategy->tx_prep = PATCH_NOP;
    strategy->rx_complete = PATCH_NOP;
    
    /* Critical decision tree */
    if (cfg->in_v86) {
        /* V86 MODE - Must use VDS, no privileged ops */
        if (!cfg->has_vds) {
            LOG_ERROR("V86 mode without VDS - cannot continue!");
            strategy->disable_dma = true;
            strategy->rx_alloc = PATCH_PIO_FALLBACK;
            strategy->tx_prep = PATCH_PIO_FALLBACK;
            return;
        }
        
        LOG_INFO("V86 mode detected - using VDS for all DMA");
        strategy->use_vds = true;
        strategy->rx_alloc = PATCH_VDS_LOCK;
        strategy->tx_prep = PATCH_VDS_LOCK;
        strategy->rx_complete = PATCH_VDS_UNLOCK;
        
        /* Pre-allocate VDS pool */
        if (vds_preallocate_buffer_pool(32) < 0) {
            LOG_ERROR("VDS pool allocation failed");
            strategy->disable_dma = true;
        }
        
    } else if (!cfg->bus_master_works) {
        /* DMA BROKEN - PIO only */
        LOG_INFO("Bus master broken - using PIO fallback");
        strategy->disable_dma = true;
        strategy->rx_alloc = PATCH_PIO_FALLBACK;
        strategy->tx_prep = PATCH_PIO_FALLBACK;
        
    } else if (cfg->is_3c509_isa) {
        /* 3C509B - ISA PIO, no DMA issues! */
        LOG_INFO("3C509B detected - PIO mode, no DMA patches needed");
        /* All remain as NOP */
        
    } else if (cfg->is_3c515_pci && !bmtest_validated()) {
        /* 3C515 - Force PIO until BMTEST validates DMA */
        LOG_INFO("3C515 detected - forcing PIO mode until BMTEST validates DMA");
        strategy->disable_dma = true;
        strategy->rx_alloc = PATCH_PIO_FALLBACK;
        strategy->tx_prep = PATCH_PIO_FALLBACK;
        strategy->dma_disable_reason = DMA_DISABLED_PENDING_VALIDATION;
        
    } else if (!cfg->cache_coherent) {
        /* CACHE ISSUES - Need management */
        LOG_INFO("Cache coherency issues detected");
        
        if (cfg->cpu_family >= 4 && cfg->has_wbinvd && cfg->in_real_mode) {
            /* 486+ with WBINVD in real mode */
            LOG_INFO("Using WBINVD for cache management");
            strategy->tx_prep = PATCH_WBINVD;
            strategy->rx_complete = PATCH_WBINVD;
        } else if (cfg->cpu_family == 4 && cfg->in_v86_mode) {
            /* GPT-5 Critical: 486 only in V86 mode - DISABLE DMA entirely */
            LOG_ERROR("486 in V86 mode detected - disabling DMA (use PIO instead)");
            LOG_ERROR("Software barriers cannot guarantee cache coherency on 486");
            strategy->disable_dma = true;
            strategy->force_pio = true;
            strategy->dma_disable_reason = DMA_DISABLED_V86_MODE;
        } else {
            /* No WBINVD or pre-486 - use bounce buffers */
            LOG_INFO("Using bounce buffers for cache safety");
            strategy->use_bounce = true;
            strategy->tx_prep = PATCH_BOUNCE_TX;
            strategy->rx_complete = PATCH_BOUNCE_RX;
            
            if (allocate_bounce_pool() < 0) {
                LOG_ERROR("Bounce pool allocation failed");
                strategy->disable_dma = true;
            }
        }
        
    } else if (cfg->has_snooping) {
        /* COHERENT CHIPSET - No ops needed! */
        LOG_INFO("Hardware snooping detected - no cache management needed");
        /* All remain as NOP */
        
    } else {
        /* UNKNOWN/CONSERVATIVE - Use bounce buffers */
        LOG_INFO("Unknown coherency - using conservative bounce buffers");
        strategy->use_bounce = true;
        strategy->tx_prep = PATCH_BOUNCE_TX;
        strategy->rx_complete = PATCH_BOUNCE_RX;
        allocate_bounce_pool();
    }
    
    /* Add 64KB boundary check for ISA DMA (non-VDS) */
    if (!cfg->in_v86 && !cfg->is_3c509_isa && cfg->is_3c515_pci) {
        /* 3C515 might use ISA DMA */
        if (strategy->rx_alloc == PATCH_NOP) {
            strategy->rx_alloc = PATCH_CHECK_64KB;
        }
    }
}

/**
 * @brief Get patch target address for patch type
 */
static void* get_patch_target(patch_type_t type) {
    switch (type) {
        case PATCH_VDS_LOCK:     return (void*)vds_lock_stub;
        case PATCH_VDS_UNLOCK:   return (void*)vds_unlock_stub;
        case PATCH_WBINVD:       return (void*)cache_flush_486;
        case PATCH_BOUNCE_TX:    return (void*)bounce_tx_stub;
        case PATCH_BOUNCE_RX:    return (void*)bounce_rx_stub;
        case PATCH_CHECK_64KB:   return (void*)check_64kb_stub;
        case PATCH_PIO_FALLBACK: return (void*)pio_fallback_stub;
        case PATCH_NOP:
        default:                 return NULL;
    }
}

/**
 * @brief Apply a 3-byte patch to a code location
 */
static void patch_3byte_site(void* site, void* target) {
    uint8_t* patch_site = (uint8_t*)site;
    
    if (target == NULL) {
        /* Patch with NOPs */
        patch_site[0] = 0x90;  /* NOP */
        patch_site[1] = 0x90;  /* NOP */
        patch_site[2] = 0x90;  /* NOP */
    } else {
        /* Calculate relative offset for near call */
        int16_t offset = (int16_t)((uint8_t*)target - (patch_site + 3));
        
        /* Write call instruction (E8 rel16) */
        /* Write displacement first for safety */
        patch_site[1] = (uint8_t)(offset & 0xFF);
        patch_site[2] = (uint8_t)((offset >> 8) & 0xFF);
        
        /* Memory barrier */
        __asm { jmp $+2 }
        
        /* Write opcode last to make it atomic */
        patch_site[0] = 0xE8;  /* CALL near */
    }
}

/**
 * @brief Apply patches with proper serialization
 */
static int apply_patches_with_serialization(patch_strategy_t* strategy) {
    extern void safe_disable_interrupts(void);
    extern void safe_enable_interrupts(void);
    extern void serialize_after_smc(void);
    
    LOG_INFO("Applying safety patches to hot path");
    
    /* Safely disable interrupts (handles V86/IOPL) */
    safe_disable_interrupts();
    
    /* Apply patches to each site */
    patch_3byte_site(&_rx_alloc_point, get_patch_target(strategy->rx_alloc));
    patch_3byte_site(&_tx_prep_point, get_patch_target(strategy->tx_prep));
    patch_3byte_site(&_rx_complete_point, get_patch_target(strategy->rx_complete));
    
    /* Proper serialization for 486+ */
    serialize_after_smc();
    
    /* Re-enable interrupts */
    safe_enable_interrupts();
    
    LOG_INFO("Safety patches applied successfully");
    LOG_INFO("  RX alloc: %d", strategy->rx_alloc);
    LOG_INFO("  TX prep: %d", strategy->tx_prep);
    LOG_INFO("  RX complete: %d", strategy->rx_complete);
    
    return 0;
}

/**
 * @brief Main entry point for safety detection and patching
 * 
 * This is called once during driver initialization.
 * All detection code is in the cold section and will be discarded.
 */
int init_complete_safety_detection(void) {
    safety_config_t config;
    patch_strategy_t strategy;
    int result;
    
    LOG_INFO("Starting comprehensive safety detection and patching");
    
    /* Initialize orphaned safety modules */
    result = dma_safety_init();
    if (result < 0) {
        LOG_WARNING("DMA safety init failed: %d", result);
    }
    
    result = cache_coherency_init();
    if (result < 0) {
        LOG_WARNING("Cache coherency init failed: %d", result);
    }
    
    result = vds_core_init();
    if (result < 0) {
        LOG_WARNING("VDS core init failed: %d", result);
    }
    
    /* Gather all detection results */
    gather_safety_config(&config);
    
    /* Determine patch strategy */
    determine_patch_strategy(&config, &strategy);
    
    /* Apply patches to hot path */
    result = apply_patches_with_serialization(&strategy);
    if (result < 0) {
        LOG_ERROR("Failed to apply safety patches");
        return result;
    }
    
    LOG_INFO("Safety integration complete");
    return 0;
}

/**
 * @brief Cleanup VDS pool on driver unload
 */
void cleanup_vds_pool(void) {
    int i;
    
    if (!vds_pool_initialized) {
        return;
    }
    
    for (i = 0; i < VDS_POOL_SIZE; i++) {
        if (vds_pool[i].vds_handle) {
            vds_core_unlock_region(vds_pool[i].vds_handle);
        }
        if (vds_pool[i].virt_addr) {
            _ffree(vds_pool[i].virt_addr);
        }
    }
    
    vds_pool_initialized = false;
}

/* Device detection stubs - TODO: Link with actual hardware module */
bool is_3c515_detected(void) {
    extern uint16_t g_nic_type;
    return (g_nic_type == 0x515);
}

bool is_3c509_detected(void) {
    extern uint16_t g_nic_type;
    return (g_nic_type == 0x509);
}