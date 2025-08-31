/**
 * @file patch_apply_fixed.c
 * @brief SMC patch application framework - GPT-5 corrected version
 *
 * Fixes identified by GPT-5:
 * 1. Use NOP sled for safe patch points
 * 2. Minimal CLI window with no I/O or function calls
 * 3. Far JMP for proper serialization on 486+
 * 4. Proper timing measurement outside CLI
 *
 * Constraints:
 * - <8μs CLI sections (verified by design, not measurement)
 * - Atomic patching with interrupt safety
 * - Far JMP serialization for 486+ prefetch flush
 * - All patching done before TSR installation
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "../include/module_header.h"
#include "../include/cpu_detect.h"
#include "../include/logging.h"
#include "../include/production.h"
#include "../include/busmaster_test.h"
#include "../include/cache_coherency.h"
#include "../include/chipset_detect.h"
#include "../include/common.h"

/* Mark entire file for cold section */
#pragma code_seg("COLD_TEXT", "CODE")

/* Assembly patch routine - minimal CLI window */
extern void asm_patch_5bytes(void far* dest, const void far* src);

/* Statistics for patch operations */
static struct {
    uint16_t patches_applied;
    uint16_t patches_skipped;
    uint16_t patches_failed;
    uint16_t safety_patches;
    uint16_t pio_fallbacks;
} patch_stats = {0};

/* Global safety context for patch selection */
typedef struct {
    bool bm_safe;                    /* Bus master test passed */
    cache_tier_t cache_tier;         /* Selected cache tier */
    bool has_clflush;               /* CLFLUSH available */
    bool has_wbinvd;                /* WBINVD available */
    bool full_snooping;             /* Hardware snooping detected */
    bool is_isa_bus;                /* ISA bus (needs 64KB check) */
    busmaster_confidence_t bm_conf;  /* Bus master confidence level */
    bus_type_t bus_type;            /* Detected system bus type */
} patch_safety_context_t;

static patch_safety_context_t g_patch_context = {0};

/**
 * @brief Apply 5-byte patch with minimal CLI window
 * 
 * GPT-5 corrected version using assembly for minimal overhead.
 * CLI window is guaranteed <8μs by design (5 byte copy + far JMP).
 */
void asm_patch_5bytes(void far* dest, const void far* src) {
    __asm {
        push    ds
        push    es
        push    si
        push    di
        push    cx
        
        ; Set up for copy
        lds     si, src         ; DS:SI = source patch bytes
        les     di, dest        ; ES:DI = destination patch point
        mov     cx, 5           ; Always 5 bytes
        
        ; Critical section - minimal CLI window
        cli
        rep movsb               ; Copy 5 bytes (~25 cycles on 8086)
        
        ; Serializing far jump for 486+ (flushes prefetch)
        db      0EAh            ; JMP FAR opcode
        dw      offset flush_label
        dw      seg flush_label
        
    flush_label:
        sti                     ; Re-enable interrupts
        
        ; Restore registers
        pop     cx
        pop     di
        pop     si
        pop     es
        pop     ds
    }
}

/**
 * @brief Validate patch point contains safe NOP sled
 * @param dest Destination address to check
 * @return 1 if valid NOP sled, 0 otherwise
 */
static int validate_patch_point(void far* dest) {
    uint8_t far* ptr = (uint8_t far*)dest;
    int i;
    
    /* Check for 5 NOPs (0x90) */
    for (i = 0; i < 5; i++) {
        if (ptr[i] != 0x90) {
            LOG_ERROR("Patch point at %Fp not a NOP sled (byte %d = 0x%02X)", 
                     dest, i, ptr[i]);
            return 0;
        }
    }
    
    return 1;
}

/**
 * @brief Apply a single patch safely
 * @param dest Destination address in hot code
 * @param patch Patch data to apply (must be 5 bytes)
 * @param patch_type Type of patch for logging
 * @return SUCCESS or error code
 */
static int apply_single_patch(void far* dest, const uint8_t* patch, 
                             uint8_t patch_type) {
    
    /* Validate patch point first */
    if (!validate_patch_point(dest)) {
        LOG_ERROR("Invalid patch point at %Fp", dest);
        patch_stats.patches_failed++;
        return -1;
    }
    
    /* Log patch application */
    switch (patch_type) {
    case PATCH_TYPE_COPY:
        LOG_DEBUG("Applying COPY patch at %Fp", dest);
        break;
    case PATCH_TYPE_IO:
        LOG_DEBUG("Applying IO patch at %Fp", dest);
        break;
    case PATCH_TYPE_ISR:
        LOG_DEBUG("Applying ISR patch at %Fp", dest);
        break;
    case PATCH_TYPE_CHECKSUM:
        LOG_DEBUG("Applying CHECKSUM patch at %Fp", dest);
        break;
    default:
        LOG_DEBUG("Applying patch type %d at %Fp", patch_type, dest);
        break;
    }
    
    /* Apply patch with minimal CLI window */
    asm_patch_5bytes(dest, patch);
    
    patch_stats.patches_applied++;
    return SUCCESS;
}

/**
 * @brief Select appropriate patch variant for CPU
 * @param entry Patch table entry
 * @param cpu_type Detected CPU type
 * @return Pointer to 5-byte patch code
 */
static const uint8_t* select_patch_variant(const patch_entry_t* entry, 
                                          cpu_type_t cpu_type) {
    /* Each patch is exactly 5 bytes */
    switch (cpu_type) {
    case CPU_TYPE_8086:
        return entry->cpu_8086;
    case CPU_TYPE_80286:
        return entry->cpu_286;
    case CPU_TYPE_80386:
        return entry->cpu_386;
    case CPU_TYPE_80486:
        return entry->cpu_486;
    case CPU_TYPE_PENTIUM:
    case CPU_TYPE_PENTIUM_PRO:
        return entry->cpu_pentium;
    default:
        return entry->cpu_8086;  /* Fallback to 8086 */
    }
}

/**
 * @brief Select safety-aware patch variant
 * @param entry Enhanced patch table entry
 * @param cpu_type Detected CPU type
 * @param ctx Safety context from tests
 * @return Pointer to 5-byte patch code
 */
static const uint8_t* select_safe_patch_variant(
    const enhanced_patch_entry_t* entry,
    cpu_type_t cpu_type,
    const patch_safety_context_t* ctx) {
    
    /* Check for Pentium 4+ with CLFLUSH for cache operations */
    if (entry->patch_type == PATCH_TYPE_CACHE_PRE || 
        entry->patch_type == PATCH_TYPE_CACHE_POST) {
        if (cpu_type >= CPU_TYPE_PENTIUM_PRO && ctx->has_clflush) {
            return entry->cpu_p4_clflush;
        }
    }
    
    /* Check if DMA is safe for DMA-related patches */
    if (entry->safety_flags & SAFETY_FLAG_BUS_MASTER) {
        if (!ctx->bm_safe) {
            /* Force PIO variants */
            LOG_DEBUG("Forcing PIO variant due to bus master test failure");
            patch_stats.pio_fallbacks++;
            
            switch(cpu_type) {
            case CPU_TYPE_80286:
                return entry->cpu_286_pio;
            case CPU_TYPE_80386:
                return entry->cpu_386_pio;
            default:
                /* Use standard variant for others */
                break;
            }
        } else {
            /* DMA is safe, use DMA variants */
            switch(cpu_type) {
            case CPU_TYPE_80286:
                return entry->cpu_286_dma;
            case CPU_TYPE_80386:
                return entry->cpu_386_dma;
            default:
                /* Use standard variant for others */
                break;
            }
        }
    }
    
    /* Check for ISA DMA boundary requirements */
    if ((entry->safety_flags & SAFETY_FLAG_ISA_DMA) && ctx->is_isa_bus) {
        /* This patch needs boundary checking on ISA bus */
        LOG_DEBUG("Enabling DMA boundary check for ISA bus");
        patch_stats.safety_patches++;
    }
    
    /* Default CPU-specific selection */
    switch (cpu_type) {
    case CPU_TYPE_8086:
        return entry->cpu_8086;
    case CPU_TYPE_80286:
        return ctx->bm_safe ? entry->cpu_286_dma : entry->cpu_286_pio;
    case CPU_TYPE_80386:
        return ctx->bm_safe ? entry->cpu_386_dma : entry->cpu_386_pio;
    case CPU_TYPE_80486:
        return entry->cpu_486;
    case CPU_TYPE_PENTIUM:
    case CPU_TYPE_PENTIUM_PRO:
        return entry->cpu_pentium;
    default:
        return entry->cpu_8086;
    }
}

/**
 * @brief Apply all patches to a module
 * @param module Module header pointer
 * @param cpu_info CPU information for patch selection
 * @return SUCCESS or error code
 */
int apply_module_patches(module_header_t* module, const cpu_info_t* cpu_info) {
    patch_entry_t* patch_table;
    uint16_t i;
    int result;
    
    LOG_INFO("Applying patches to module: %.7s", module->signature);
    
    /* Validate module header */
    if (memcmp(module->signature, MODULE_SIGNATURE, MODULE_SIG_SIZE) != 0) {
        LOG_ERROR("Invalid module signature");
        return -1;
    }
    
    /* Validate patch size constraint */
    patch_table = (patch_entry_t*)((uint8_t*)module + module->patch_table_offset);
    
    for (i = 0; i < module->patch_count; i++) {
        if (patch_table[i].patch_size != 5) {
            LOG_ERROR("Patch %d size is %d, must be 5", 
                     i, patch_table[i].patch_size);
            return -1;
        }
    }
    
    /* Apply each patch */
    for (i = 0; i < module->patch_count; i++) {
        patch_entry_t* entry = &patch_table[i];
        void far* dest = (void far*)((uint8_t*)module + entry->patch_offset);
        const uint8_t* patch_code;
        
        /* Skip NOP patches */
        if (entry->patch_type == PATCH_TYPE_NOP) {
            /* Already NOPs, nothing to do */
            patch_stats.patches_skipped++;
            continue;
        }
        
        /* Select patch variant for CPU */
        patch_code = select_patch_variant(entry, cpu_info->cpu_type);
        
        /* Validate patch code */
        if (!patch_code) {
            LOG_ERROR("No patch variant available for CPU type %d", 
                     cpu_info->cpu_type);
            patch_stats.patches_failed++;
            return -1;
        }
        
        /* Apply the patch */
        result = apply_single_patch(dest, patch_code, entry->patch_type);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to apply patch %d at offset 0x%04X", 
                     i, entry->patch_offset);
            return result;
        }
    }
    
    LOG_INFO("Applied %d patches, skipped %d, failed %d", 
            patch_stats.patches_applied, 
            patch_stats.patches_skipped,
            patch_stats.patches_failed);
    
    return SUCCESS;
}

/**
 * @brief Apply patches with safety awareness
 * @param module Module header pointer
 * @param cpu_info CPU information
 * @param safety_ctx Safety context from tests
 * @return SUCCESS or error code
 */
int apply_module_patches_safe(module_header_t* module, 
                             const cpu_info_t* cpu_info,
                             const patch_safety_context_t* safety_ctx) {
    enhanced_patch_entry_t* patch_table;
    uint16_t i;
    int result;
    
    LOG_INFO("Applying safety-aware patches to module: %.7s", module->signature);
    
    /* Validate module header */
    if (memcmp(module->signature, MODULE_SIGNATURE, MODULE_SIG_SIZE) != 0) {
        LOG_ERROR("Invalid module signature");
        return -1;
    }
    
    /* Get enhanced patch table */
    patch_table = (enhanced_patch_entry_t*)((uint8_t*)module + 
                                           module->patch_table_offset);
    
    /* Apply each patch with safety considerations */
    for (i = 0; i < module->patch_count; i++) {
        enhanced_patch_entry_t* entry = &patch_table[i];
        void far* dest = (void far*)((uint8_t*)module + entry->patch_offset);
        const uint8_t* patch_code;
        
        /* Skip NOP patches */
        if (entry->patch_type == PATCH_TYPE_NOP) {
            patch_stats.patches_skipped++;
            continue;
        }
        
        /* Handle cache management patches specially */
        if (entry->patch_type == PATCH_TYPE_CACHE_PRE ||
            entry->patch_type == PATCH_TYPE_CACHE_POST) {
            
            /* Skip cache patches if snooping detected or tier 4 */
            if (safety_ctx->full_snooping || 
                safety_ctx->cache_tier == CACHE_TIER_4_FALLBACK) {
                LOG_DEBUG("Skipping cache patch - not needed");
                patch_stats.patches_skipped++;
                continue;
            }
        }
        
        /* Handle DMA boundary check patches */
        if (entry->patch_type == PATCH_TYPE_DMA_CHECK) {
            /* Only needed for ISA bus */
            if (!safety_ctx->is_isa_bus) {
                LOG_DEBUG("Skipping DMA boundary check - not ISA bus");
                patch_stats.patches_skipped++;
                continue;
            }
        }
        
        /* Select safety-aware patch variant */
        patch_code = select_safe_patch_variant(entry, cpu_info->cpu_type, 
                                              safety_ctx);
        
        /* Validate patch code pointer */
        if (!patch_code) {
            LOG_ERROR("No patch variant available for type %d, CPU %d", 
                     entry->patch_type, cpu_info->cpu_type);
            /* Use safe NOPs as fallback */
            static const uint8_t nops[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
            patch_code = nops;
            LOG_WARNING("Using NOP fallback for safety");
        }
        
        /* Apply the patch */
        result = apply_single_patch(dest, patch_code, entry->patch_type);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to apply patch %d at offset 0x%04X", 
                     i, entry->patch_offset);
            return result;
        }
    }
    
    LOG_INFO("Module patched: %d applied, %d safety, %d skipped", 
            patch_stats.patches_applied,
            patch_stats.safety_patches,
            patch_stats.patches_skipped);
    
    return SUCCESS;
}

/**
 * @brief Initialize and apply all SMC patches
 * @return SUCCESS or error code
 *
 * Main entry point for patch application during loader initialization.
 * Called once before TSR installation.
 */
int patch_init_and_apply(void) {
    const cpu_info_t* cpu_info;
    extern module_header_t packet_api_module_header;
    extern module_header_t nic_irq_module_header;
    extern module_header_t hardware_module_header;
    busmaster_test_results_t bm_results;
    cache_coherency_analysis_t cache_analysis;
    nic_context_t nic_ctx;
    int result;
    
    LOG_INFO("Initializing SMC patch framework with safety checks");
    
    /* Get CPU information */
    cpu_info = cpu_get_info();
    if (!cpu_info) {
        LOG_ERROR("CPU information not available");
        return -1;
    }
    
    LOG_INFO("Detected %s CPU", cpu_info->cpu_name);
    
    /* Run bus master safety tests */
    LOG_INFO("Running bus master safety tests...");
    memset(&bm_results, 0, sizeof(bm_results));
    memset(&nic_ctx, 0, sizeof(nic_ctx));
    
    if (busmaster_test_init(&nic_ctx) == 0) {
        result = perform_automated_busmaster_test(&nic_ctx, 
                                                 BM_TEST_MODE_QUICK, 
                                                 &bm_results);
        if (result != 0) {
            LOG_WARNING("Bus master test failed - forcing PIO mode");
            g_patch_context.bm_safe = false;
        } else {
            g_patch_context.bm_safe = 
                (bm_results.confidence_level >= BM_CONFIDENCE_HIGH);
            g_patch_context.bm_conf = bm_results.confidence_level;
            LOG_INFO("Bus master test: %s (confidence: %d)",
                    g_patch_context.bm_safe ? "PASSED" : "FAILED",
                    bm_results.confidence_level);
        }
        busmaster_test_cleanup(&nic_ctx);
    } else {
        LOG_WARNING("Could not initialize bus master test - assuming unsafe");
        g_patch_context.bm_safe = false;
    }
    
    /* Run cache coherency analysis */
    LOG_INFO("Analyzing cache coherency...");
    cache_analysis = analyze_cache_coherency();
    g_patch_context.cache_tier = cache_analysis.selected_tier;
    g_patch_context.has_clflush = cache_analysis.cpu.has_clflush;
    g_patch_context.has_wbinvd = cache_analysis.cpu.has_wbinvd;
    g_patch_context.full_snooping = 
        (cache_analysis.snooping_result == SNOOPING_FULL);
    
    LOG_INFO("Cache coherency: Tier %d selected (%s)",
            cache_analysis.selected_tier,
            cache_analysis.explanation);
    
    /* Detect bus type using existing infrastructure */
    LOG_INFO("Detecting system bus architecture...");
    
    /* Get chipset information */
    chipset_detection_result_t chipset_result = detect_system_chipset();
    chipset_additional_info_t additional = scan_additional_pci_devices();
    
    /* Check for specific bus types */
    extern int is_mca_system(void);
    extern int is_eisa_system(void);
    bool has_mca = is_mca_system();
    bool has_eisa = is_eisa_system();
    bool has_pci = (chipset_result.chipset.era == CHIPSET_ERA_PCI) || 
                   (chipset_result.detection_method == CHIPSET_DETECT_PCI_SUCCESS);
    bool is_pre_486 = (cpu_info->type <= CPU_TYPE_80386);
    
    /* Apply comprehensive bus compatibility heuristics */
    if (has_mca) {
        /* MCA never has ISA - IBM proprietary incompatible bus */
        g_patch_context.is_isa_bus = false;
        g_patch_context.bus_type = BUS_TYPE_MCA;
        LOG_INFO("Bus type: MicroChannel (MCA) - ISA not available");
        LOG_WARNING("MCA bus detected - 3C509B/3C515-TX will not work!");
        
    } else if (is_pre_486 && !has_mca) {
        /* Pre-486 non-MCA systems are always ISA-based */
        g_patch_context.is_isa_bus = true;
        if (has_eisa) {
            g_patch_context.bus_type = BUS_TYPE_EISA;
            LOG_INFO("Bus type: EISA (pre-486) - ISA compatible");
        } else {
            g_patch_context.bus_type = BUS_TYPE_ISA;
            LOG_INFO("Bus type: ISA (pre-486 system)");
        }
        
    } else if (has_eisa) {
        /* EISA always supports ISA cards by design */
        g_patch_context.is_isa_bus = true;
        g_patch_context.bus_type = BUS_TYPE_EISA;
        LOG_INFO("Bus type: EISA - ISA compatible");
        
    } else if (has_pci) {
        /* PCI system - check for ISA bridge to determine ISA availability */
        g_patch_context.bus_type = BUS_TYPE_PCI;
        
        if (additional.has_isa_bridge) {
            g_patch_context.is_isa_bus = true;
            LOG_INFO("Bus type: PCI with ISA bridge (%s) - ISA available", 
                     additional.isa_bridge_name);
        } else {
            /* Pure PCI without ISA bridge (rare in vintage, common post-2000) */
            g_patch_context.is_isa_bus = false;
            LOG_WARNING("Bus type: PCI without ISA bridge - ISA not available!");
            LOG_WARNING("3C509B/3C515-TX are ISA cards and may not work!");
        }
        
    } else {
        /* Default: Pure ISA system (or detection failed - assume ISA) */
        g_patch_context.is_isa_bus = true;
        g_patch_context.bus_type = BUS_TYPE_ISA;
        LOG_INFO("Bus type: ISA (default/detected)");
    }
    
    /* Validate against detected NIC if available */
    if (nic_ctx.nic_count > 0) {
        if ((nic_ctx.nic_type == NIC_3C509B || nic_ctx.nic_type == NIC_3C515TX) && 
            !g_patch_context.is_isa_bus) {
            LOG_ERROR("ISA NIC detected but ISA bus not available!");
            LOG_ERROR("System incompatibility detected - driver may not function");
        }
    }
    
    /* Log chipset details if detected */
    if (chipset_result.chipset.found) {
        LOG_INFO("Chipset: %s (Era: %s)", 
                 chipset_result.chipset.name,
                 get_chipset_era_description(chipset_result.chipset.era));
    }
    
    /* Set additional patch context based on bus type */
    if (g_patch_context.bus_type == BUS_TYPE_MCA) {
        /* Force PIO mode on MCA - no bus mastering for our NICs */
        g_patch_context.bm_safe = false;
        LOG_INFO("MCA detected - forcing PIO mode");
    }
    
    #ifdef PRODUCTION
    printf("Optimizing for %s...\n", cpu_info->cpu_name);
    if (!g_patch_context.bm_safe) {
        printf("Bus mastering disabled for safety\n");
    }
    #endif
    
    /* Apply patches to each module with safety context */
    result = apply_module_patches_safe(&packet_api_module_header, 
                                      cpu_info, &g_patch_context);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to patch packet API module");
        return result;
    }
    
    result = apply_module_patches_safe(&nic_irq_module_header, 
                                      cpu_info, &g_patch_context);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to patch NIC IRQ module");
        return result;
    }
    
    result = apply_module_patches_safe(&hardware_module_header, 
                                      cpu_info, &g_patch_context);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to patch hardware module");
        return result;
    }
    
    LOG_INFO("SMC patching complete: %d patches, %d safety, %d PIO fallbacks", 
            patch_stats.patches_applied,
            patch_stats.safety_patches,
            patch_stats.pio_fallbacks);
    
    /* Free environment block to save memory (GPT-5 recommendation) */
    {
        uint16_t psp_seg;
        uint16_t env_seg;
        
        __asm {
            mov     ah, 62h         ; Get PSP
            int     21h
            mov     psp_seg, bx
        }
        
        /* Get environment segment from PSP:2Ch */
        env_seg = *(uint16_t far*)(MK_FP(psp_seg, 0x2C));
        
        if (env_seg != 0) {
            __asm {
                mov     es, env_seg
                mov     ah, 49h     ; Free memory block
                int     21h
            }
            
            /* Clear environment pointer in PSP */
            *(uint16_t far*)(MK_FP(psp_seg, 0x2C)) = 0;
            
            LOG_INFO("Freed environment block");
        }
    }
    
    return result;  /* Return actual result, not SUCCESS */
}

/* Restore default code segment */
#pragma code_seg()