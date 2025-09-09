/**
 * @file patch_apply.c
 * @brief SMC patch application framework (cold section)
 *
 * Applies CPU-specific patches to hot code during initialization.
 * This entire module is discarded after TSR installation.
 *
 * Constraints:
 * - <8μs CLI sections (PIT-measured)
 * - Atomic patching with interrupt safety
 * - Near JMP serialization for prefetch flush
 * - All patching done before TSR installation
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "../include/module_header.h"
#include "../include/cpu_detect.h"
#include "../include/logging.h"
#include "../include/production.h"
#include "../include/platform_probe.h"
#include "../include/config.h"
#include "../include/vds.h"

/* Mark entire file for cold section */
#pragma code_seg("COLD_TEXT", "CODE")

/* PIT frequency for timing measurements */
#define PIT_FREQUENCY   1193182L   /* Hz */
#define MAX_CLI_TICKS   10          /* ~8.4μs at 1.193MHz */

/* Assembly functions for atomic patching */
extern void asm_atomic_patch(void far* dest, const void far* src, uint16_t size);
extern void asm_flush_prefetch(void);
extern uint16_t asm_measure_pit_ticks(void);

/* Statistics for patch operations */
static struct {
    uint16_t patches_applied;
    uint16_t patches_skipped;
    uint16_t max_cli_ticks;
    uint16_t total_patch_time;
} patch_stats = {0};

/**
 * @brief Measure PIT ticks for a code section
 * @param start_ticks Starting PIT counter value
 * @return Number of PIT ticks elapsed
 */
static uint16_t measure_cli_duration(uint16_t start_ticks) {
    uint16_t end_ticks;
    uint16_t duration;
    
    /* Read current PIT counter */
    _disable();
    outp(0x43, 0x00);  /* Latch counter 0 */
    end_ticks = inp(0x40);
    end_ticks |= inp(0x40) << 8;
    _enable();
    
    /* Calculate duration */
    if (end_ticks > start_ticks) {
        duration = (0xFFFF - end_ticks) + start_ticks;
    } else {
        duration = start_ticks - end_ticks;
    }
    
    return duration;
}

/**
 * @brief Apply a single patch with timing validation
 * @param dest Destination address in hot code
 * @param patch Patch data to apply
 * @param size Size of patch in bytes
 * @return SUCCESS or error code
 */
static int apply_single_patch(void far* dest, const uint8_t* patch, uint8_t size) {
    uint16_t start_ticks;
    uint16_t duration;
    
    /* Validate patch size */
    if (size > 5) {
        LOG_ERROR("Patch size %d exceeds maximum of 5 bytes", size);
        return -1;
    }
    
    /* Measure CLI duration */
    _disable();
    outp(0x43, 0x00);  /* Latch PIT counter */
    start_ticks = inp(0x40);
    start_ticks |= inp(0x40) << 8;
    
    /* Apply patch atomically */
    _fmemcpy(dest, patch, size);
    
    /* Flush prefetch queue with near JMP */
    __asm {
        jmp short $+2   ; Near jump to flush prefetch
    }
    
    _enable();
    
    /* Measure duration */
    duration = measure_cli_duration(start_ticks);
    
    /* Update statistics */
    if (duration > patch_stats.max_cli_ticks) {
        patch_stats.max_cli_ticks = duration;
    }
    
    /* Validate timing constraint */
    if (duration > MAX_CLI_TICKS) {
        LOG_WARNING("CLI duration %d ticks exceeds limit of %d", 
                   duration, MAX_CLI_TICKS);
    }
    
    patch_stats.patches_applied++;
    return SUCCESS;
}

/**
 * @brief Dynamically select DMA vs PIO for 3C515 and patch hot path
 *
 * Decision gates:
 * - Global PIO override (global_force_pio_mode)
 * - Platform policy (platform_allow_busmaster_dma)
 * - Config.busmaster not OFF
 * - If platform requires VDS, ensure vds_available()
 */
static int apply_dma_pio_selection(const config_t* cfg) {
    int use_dma = 0;
    uint8_t patch_bytes[5];
    uint16_t rel;

    /* Hard override to PIO if critical patches failed */
    extern int global_force_pio_mode;
    if (global_force_pio_mode) {
        LOG_WARNING("Forcing PIO: global override active");
        use_dma = 0;
        goto do_patch;
    }

    /* Respect user configuration */
    if (cfg && cfg->busmaster == BUSMASTER_OFF) {
        LOG_INFO("Bus mastering disabled by configuration");
        use_dma = 0;
        goto do_patch;
    }

    /* Platform policy gate */
    if (!platform_allow_busmaster_dma()) {
        LOG_INFO("Platform policy forbids bus-master DMA; using PIO");
        use_dma = 0;
        goto do_patch;
    }

    /* If policy requires VDS, verify it's available */
    if (g_platform.requires_vds && !vds_available()) {
        LOG_INFO("VDS required but not available; using PIO");
        use_dma = 0;
        goto do_patch;
    }

    /* All gates passed: allow DMA */
    use_dma = 1;

do_patch:
    /* Build 5-byte sled: E8 rel16, 90, 90 */
    patch_bytes[0] = 0xE8;
    patch_bytes[3] = 0x90;
    patch_bytes[4] = 0x90;

    /* Compute rel16 to transfer_{dma|pio} from PATCH_3c515_transfer+3 */
    {
        extern void far PATCH_3c515_transfer(void);
        extern void transfer_dma(void);
        extern void transfer_pio(void);

        uint16_t target_off = (uint16_t)(use_dma ? FP_OFF(transfer_dma) : FP_OFF(transfer_pio));
        uint16_t site_off = FP_OFF(PATCH_3c515_transfer);
        rel = (uint16_t)(target_off - (site_off + 3));
        patch_bytes[1] = (uint8_t)(rel & 0xFF);
        patch_bytes[2] = (uint8_t)((rel >> 8) & 0xFF);
    }

    /* Apply patch atomically */
    {
        extern uint8_t far PATCH_3c515_transfer;
        int rc = apply_single_patch(&PATCH_3c515_transfer, patch_bytes, 5);
        if (rc == SUCCESS) {
            LOG_INFO("3C515 transfer method: %s", use_dma ? "DMA" : "PIO");
        } else {
            LOG_ERROR("Failed to patch 3C515 transfer method");
            return rc;
        }
    }

    return SUCCESS;
}

/**
 * @brief Select appropriate patch variant for CPU
 * @param entry Patch table entry
 * @param cpu_type Detected CPU type
 * @return Pointer to patch code or NULL
 */
static const uint8_t* select_patch_variant(const patch_entry_t* entry, 
                                          cpu_type_t cpu_type) {
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
 * @brief Apply all patches to a module
 * @param module Module header pointer
 * @param cpu_info CPU information for patch selection
 * @return SUCCESS or error code
 */
int apply_module_patches(module_header_t* module, const cpu_info_t* cpu_info) {
    patch_entry_t* patch_table;
    uint16_t i;
    int result = SUCCESS;
    
    LOG_INFO("Applying patches to module: %s", module->signature);
    
    /* Validate module header */
    if (memcmp(module->signature, MODULE_SIGNATURE, MODULE_SIG_SIZE) != 0) {
        LOG_ERROR("Invalid module signature");
        return -1;
    }
    
    /* Check CPU requirements */
    if (module->cpu_requirements > cpu_info->cpu_type) {
        LOG_WARNING("Module requires CPU type %d, have %d", 
                   module->cpu_requirements, cpu_info->cpu_type);
        /* Continue anyway with default patches */
    }
    
    /* Get patch table */
    patch_table = (patch_entry_t*)((uint8_t*)module + module->patch_table_offset);
    
    /* Apply each patch */
    for (i = 0; i < module->patch_count; i++) {
        patch_entry_t* entry = &patch_table[i];
        void far* dest = (void far*)((uint8_t*)module + entry->patch_offset);
        const uint8_t* patch_code;
        
        /* Select patch variant for CPU */
        patch_code = select_patch_variant(entry, cpu_info->cpu_type);
        
        /* Apply patch based on type */
        switch (entry->patch_type) {
        case PATCH_TYPE_COPY:
            LOG_DEBUG("Patching copy operation at offset 0x%04X", 
                     entry->patch_offset);
            break;
        case PATCH_TYPE_IO:
            LOG_DEBUG("Patching I/O operation at offset 0x%04X", 
                     entry->patch_offset);
            break;
        case PATCH_TYPE_CHECKSUM:
            LOG_DEBUG("Patching checksum at offset 0x%04X", 
                     entry->patch_offset);
            break;
        case PATCH_TYPE_ISR:
            LOG_DEBUG("Patching ISR at offset 0x%04X", 
                     entry->patch_offset);
            break;
        case PATCH_TYPE_NOP:
            /* Fill with NOPs */
            memset(dest, 0x90, entry->patch_size);
            patch_stats.patches_applied++;
            continue;
        default:
            LOG_WARNING("Unknown patch type %d", entry->patch_type);
            patch_stats.patches_skipped++;
            continue;
        }
        
        /* Apply the patch */
        result = apply_single_patch(dest, patch_code, entry->patch_size);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to apply patch %d", i);
            return result;
        }
    }
    
    /* Final prefetch flush for entire module */
    asm_flush_prefetch();
    
    LOG_INFO("Applied %d patches, skipped %d", 
            patch_stats.patches_applied, patch_stats.patches_skipped);
    LOG_INFO("Maximum CLI duration: %d PIT ticks (~%d μs)", 
            patch_stats.max_cli_ticks,
            (patch_stats.max_cli_ticks * 1000000L) / PIT_FREQUENCY);
    
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
    module_header_t* packet_api_module;
    module_header_t* nic_irq_module;
    module_header_t* hardware_module;
    int result;
    
    LOG_INFO("Initializing SMC patch framework");
    
    /* Get CPU information */
    cpu_info = cpu_get_info();
    if (!cpu_info) {
        LOG_ERROR("CPU information not available");
        return -1;
    }
    
    LOG_INFO("Applying patches for %s CPU", cpu_info->cpu_name);
    
    /* Apply patches to each module */
    /* Note: Module addresses would be provided by the loader */
    
    #ifdef PRODUCTION
    printf("Optimizing for %s...\n", cpu_info->cpu_name);
    #endif
    
    /* Apply patches to packet API module */
    extern module_header_t packet_api_module_header;
    result = apply_module_patches(&packet_api_module_header, cpu_info);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to patch packet_api module");
        return result;
    }
    
    /* CRITICAL FIX: Apply patches to NIC IRQ module (contains DMA/cache safety points) */
    extern module_header_t nic_irq_module_header;
    result = apply_module_patches(&nic_irq_module_header, cpu_info);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to patch nic_irq module");
        return result;
    }
    
    /* Apply patches to hardware module */
    extern module_header_t hardware_module_header;
    result = apply_module_patches(&hardware_module_header, cpu_info);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to patch hardware module");
        return result;
    }
    
    LOG_INFO("SMC patching complete: %d total patches applied", 
            patch_stats.patches_applied);
    
    return SUCCESS;
}

/**
 * @brief Validate timing constraints
 * @return SUCCESS if constraints met, error otherwise
 * 
 * Ensures all CLI sections meet the <8μs requirement.
 */
int validate_timing_constraints(void) {
    if (patch_stats.max_cli_ticks > MAX_CLI_TICKS) {
        LOG_ERROR("Timing constraint violated: %d ticks > %d maximum", 
                 patch_stats.max_cli_ticks, MAX_CLI_TICKS);
        return -1;
    }
    
    LOG_INFO("Timing constraints validated: max CLI = %d ticks (~%d μs)", 
            patch_stats.max_cli_ticks,
            (patch_stats.max_cli_ticks * 1000000L) / PIT_FREQUENCY);
    
    return SUCCESS;
}

/**
 * @brief Verify critical patches were applied (not NOPs)
 * @return SUCCESS if patches valid, error otherwise
 * 
 * CRITICAL: Checks that DMA/cache safety patches are active.
 * If patches are still NOPs, forces PIO mode for safety.
 */
int verify_patches_applied(void) {
    uint8_t far *patch_site;
    int patches_valid = 1;
    
    /* Check critical patch points are not NOPs */
    
    /* Check DMA boundary check patch */
    extern uint8_t far PATCH_dma_boundary_check;
    patch_site = &PATCH_dma_boundary_check;
    if (patch_site[0] == 0x90 && patch_site[1] == 0x90) {  /* NOPs */
        LOG_ERROR("CRITICAL: DMA boundary check patch not applied!");
        patches_valid = 0;
    }
    
    /* Check cache flush pre patch */
    extern uint8_t far PATCH_cache_flush_pre;
    patch_site = &PATCH_cache_flush_pre;
    if (patch_site[0] == 0x90 && patch_site[1] == 0x90) {  /* NOPs */
        LOG_ERROR("CRITICAL: Cache flush pre patch not applied!");
        patches_valid = 0;
    }
    
    /* Check 3C515 transfer patch (should be PIO by default) */
    extern uint8_t far PATCH_3c515_transfer;
    patch_site = &PATCH_3c515_transfer;
    if (patch_site[0] == 0xE8) {  /* CALL instruction */
        LOG_INFO("3C515 transfer patch verified (CALL instruction present)");
    } else if (patch_site[0] == 0x90) {  /* NOP */
        LOG_ERROR("CRITICAL: 3C515 transfer patch not applied!");
        patches_valid = 0;
    }
    
    if (!patches_valid) {
        LOG_ERROR("Safety patches missing - forcing PIO mode!");
        
        /* Set global flag to force PIO for all NICs */
        extern int global_force_pio_mode;
        global_force_pio_mode = 1;
        
        #ifdef PRODUCTION
        printf("WARNING: Safety patches not active, using PIO mode\n");
        #endif
        
        return -1;
    }
    
    LOG_INFO("All critical patches verified as active");
    return SUCCESS;
}

/* Assembly implementation of prefetch flush */
void asm_flush_prefetch(void) {
    __asm {
        jmp short flush_done    ; Near jump flushes prefetch queue
    flush_done:
    }
}

/* Restore default code segment */
#pragma code_seg()

/* Public entry to initialize and apply patches including DMA/PIO selection */
int patch_init_and_apply(void) {
    int result;
    const cpu_info_t* cpu_info = cpu_get_info();
    if (!cpu_info) return -1;

    /* Apply CPU-class patches to all modules */
    extern module_header_t packet_api_module_header;
    extern module_header_t nic_irq_module_header;
    extern module_header_t hardware_module_header;
    result = apply_module_patches(&packet_api_module_header, cpu_info);
    if (result != SUCCESS) return result;
    result = apply_module_patches(&nic_irq_module_header, cpu_info);
    if (result != SUCCESS) return result;
    result = apply_module_patches(&hardware_module_header, cpu_info);
    if (result != SUCCESS) return result;

    /* Platform init (sets g_platform and policy) */
    platform_init();

    /* Verify critical patches present */
    result = verify_patches_applied();
    if (result != SUCCESS) {
        /* Proceed with PIO enforced */
    }

    /* Apply DMA vs PIO selection (3C515 only) */
    extern config_t g_config;
    apply_dma_pio_selection(&g_config);

    return SUCCESS;
}
