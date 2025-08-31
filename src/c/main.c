/**
 * @file main.c
 * @brief Main driver entry point and initialization
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/main.h"
#include "../include/common.h"
#include "../include/init.h"
#include "../include/config.h"
#include "../include/hardware.h"
#include "../include/api.h"
#include "../include/diagnostics.h"
#include "../include/logging.h"
#include "../include/memory.h"
#include "../include/cpu_detect.h"
#include "../include/entry_validation.h"
#include "../include/platform_probe.h"
#include "../include/dma_capability_test.h"
#include "../include/chipset_detect.h"
#include "../include/busmaster_test.h"
#include "../include/vds.h"
#include "../include/unwind.h"

/* Global driver state */
static driver_state_t driver_state = {0};
static int driver_initialized = 0;

/* CPU information structure - initialized by ASM detection */
cpu_info_t g_cpu_info = {0};

/* Vendor name strings */
static const char* vendor_names[] = {
    "Intel",          /* VENDOR_INTEL */
    "AMD",            /* VENDOR_AMD */
    "Cyrix",          /* VENDOR_CYRIX */
    "NexGen",         /* VENDOR_NEXGEN */
    "UMC",            /* VENDOR_UMC */
    "Transmeta",      /* VENDOR_TRANSMETA */
    "Rise",           /* VENDOR_RISE */
    "Unknown"         /* Default/Unknown */
};

/* ASM function declarations */
extern int driver_entry(void);           /* Main ASM entry point */
extern int cpu_detect_main(void);        /* CPU detection from ASM */
extern int asm_detect_cpu_type(void);    /* Get detected CPU type from ASM */
extern uint32_t asm_get_cpu_flags(void); /* Get detected CPU features from ASM */
extern uint8_t asm_get_cpu_family(void); /* Get CPU family ID from CPUID */
extern uint32_t asm_get_cpuid_max_level(void); /* Get max CPUID level */
extern int asm_is_v86_mode(void);        /* Check if running in V86 mode */

/* Bus detection functions from ASM */
extern int is_mca_system(void);          /* Check for MCA bus */
extern int is_eisa_system(void);         /* Check for EISA bus */

/* Error messages */
static const char *error_messages[] = {
    "Success",
    "Initialization failed",
    "No NICs detected", 
    "Memory allocation failed",
    "Configuration error",
    "Hardware error",
    "API error"
};

/**
 * @brief Get error message string
 * @param error_code Error code
 * @return Error message string
 */
const char* get_error_message(int error_code) {
    if (error_code < 0) error_code = -error_code;
    if (error_code >= sizeof(error_messages)/sizeof(error_messages[0])) {
        return "Unknown error";
    }
    return error_messages[error_code];
}

/**
 * @brief Get vendor name string
 * @param vendor Vendor identifier
 * @return Vendor name string
 */
const char* get_vendor_name(cpu_vendor_t vendor) {
    if (vendor <= VENDOR_RISE) {
        return vendor_names[vendor];
    }
    return vendor_names[7]; /* "Unknown" */
}

/**
 * @brief Initialize CPU detection and get system information
 * @return 0 on success, negative on error
 */
int initialize_cpu_detection(void) {
    int result;
    
    log_info("Performing CPU detection...");
    
    /* Call ASM CPU detection routine */
    result = cpu_detect_main();
    if (result != 0) {
        log_error("CPU detection failed or CPU not supported (requires 286+)");
        return MAIN_ERR_HARDWARE;
    }
    
    /* Get CPU information and populate global structure */
    g_cpu_info.type = (cpu_type_t)asm_detect_cpu_type();
    g_cpu_info.features = asm_get_cpu_flags();
    g_cpu_info.vendor = (cpu_vendor_t)asm_get_cpu_vendor();
    
    /* Get vendor string if CPUID is available */
    if (g_cpu_info.features & CPU_FEATURE_CPUID) {
        char far* vendor_str = asm_get_cpu_vendor_string();
        if (vendor_str) {
            /* Copy vendor string from far pointer */
            int i;
            for (i = 0; i < 12 && vendor_str[i]; i++) {
                g_cpu_info.vendor_string[i] = vendor_str[i];
            }
            g_cpu_info.vendor_string[i] = '\0';
        }
    }
    
    /* Check for Cyrix extensions */
    g_cpu_info.has_cyrix_ext = asm_has_cyrix_extensions() != 0;
    
    /* Get CPU family/model if CPUID is available */
    if (g_cpu_info.type == CPU_TYPE_CPUID_CAPABLE || 
        (g_cpu_info.type == CPU_TYPE_80486 && (g_cpu_info.features & CPU_FEATURE_CPUID))) {
        
        /* Get family and model information */
        uint8_t family = asm_get_cpu_family();
        uint32_t max_level = asm_get_cpuid_max_level();
        
        /* Handle extended family (family 15 = 0x0F) */
        if (family == 0x0F) {
            /* Extended family already added in ASM module */
            log_info("CPU family 0x%02X (extended)", family);
        } else {
            log_info("CPU family %d", family);
        }
        
        /* Store family for future use */
        g_cpu_info.cpu_family = family;
        
        /* Note: cpu_model would be retrieved similarly if needed */
    }
    
    /* Set boolean flags based on detected features */
    g_cpu_info.has_cpuid = (g_cpu_info.features & CPU_FEATURE_CPUID) != 0;
    g_cpu_info.has_clflush = (g_cpu_info.features & CPU_FEATURE_CLFLUSH) != 0;
    g_cpu_info.has_wbinvd = (g_cpu_info.features & CPU_FEATURE_WBINVD) != 0;
    g_cpu_info.in_v86_mode = (g_cpu_info.features & CPU_FEATURE_V86_MODE) != 0;
    
    /* Check for V86 mode */
    if (asm_is_v86_mode()) {
        log_warning("Running in Virtual 8086 mode - certain instructions restricted");
        g_cpu_info.features |= CPU_FEATURE_V86_MODE;
        g_cpu_info.in_v86_mode = 1;
    }
    
    /* Log CPU information */
    log_info("Detected CPU: %s (%s)", cpu_type_to_string(g_cpu_info.type), 
             get_vendor_name(g_cpu_info.vendor));
    
    /* Log vendor string if available */
    if (g_cpu_info.vendor_string[0] != '\0') {
        log_info("CPU Vendor ID: %s", g_cpu_info.vendor_string);
    }
    
    /* Log special vendor cases and warnings */
    if (g_cpu_info.vendor == VENDOR_NEXGEN) {
        log_warning("NexGen Nx586 detected - CPUID without ID flag support");
        log_warning("Special handling enabled for this processor");
    }
    
    if (g_cpu_info.vendor == VENDOR_CYRIX) {
        if (g_cpu_info.has_cyrix_ext) {
            log_info("Cyrix CPU with DIR0 extensions detected");
        }
        if (g_cpu_info.type == CPU_TYPE_CPUID_CAPABLE) {
            log_warning("Cyrix 6x86 may report as 486 for compatibility");
        }
    }
    
    if (g_cpu_info.vendor == VENDOR_AMD && g_cpu_info.type == CPU_TYPE_80486) {
        log_info("AMD 486 detected - no CPUID support on Am486 series");
    }
    
    /* Log Intel 486 specific information */
    if (g_cpu_info.vendor == VENDOR_INTEL && g_cpu_info.type == CPU_TYPE_80486) {
        if (g_cpu_info.features & CPU_FEATURE_CPUID) {
            log_info("Intel 486 with CPUID detected (DX4 or SL Enhanced)");
        } else {
            log_info("Early Intel 486 detected (no CPUID support)");
        }
    }
    
    if (g_cpu_info.features & CPU_FEATURE_FPU) {
        log_info("FPU detected");
    }
    
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        log_info("32-bit operations enabled (386+ CPU)");
    }
    
    /* Log CPU database information for known quirks */
    log_cpu_database_info(&g_cpu_info);
    
    /* Check for specific quirks that need handling */
    if (g_cpu_info.vendor == VENDOR_CYRIX && cyrix_needs_cpuid_enable(&g_cpu_info)) {
        log_warning("Cyrix CPUID may need manual enabling via CCR4 register");
    }
    
    if (g_cpu_info.vendor == VENDOR_AMD && g_cpu_info.cpu_family == 5 && g_cpu_info.cpu_model == 0) {
        if (amd_k5_has_pge_bug(g_cpu_info.cpu_model)) {
            log_warning("AMD K5 Model 0 PGE feature bit is unreliable");
        }
    }
    
    return 0;
}

/**
 * @brief Initialize driver subsystems
 * @param config_params Configuration parameters from CONFIG.SYS
 * @return 0 on success, negative on error
 */
int driver_init(const char *config_params) {
    int result = 0;
    dma_test_config_t dma_test_cfg = {0};
    dma_capabilities_t *dma_caps;
    nic_info_t *primary_nic = NULL;
    
    log_info("Initializing 3Com packet driver");
    
    /* Note: Phases 0-2 already completed in main() before this */
    /* Unwind system already initialized in main() */
    
    /* PHASE 3: Parse configuration parameters */
    log_info("Phase 3: Configuration parsing");
    result = config_parse_params(config_params, &driver_state.config);
    if (result < 0) {
        log_error("Configuration parsing failed: %s", get_error_message(result));
        unwind_execute(result, "Configuration parsing failed");
        return MAIN_ERR_CONFIG;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_CONFIG);
    
    /* =================================================================
     * PHASE 4: Chipset & Bus Detection
     * Detect system architecture to inform memory allocation strategy
     * =================================================================
     */
    log_info("Phase 4: Chipset and bus detection");
    
    /* 4.1: Detect chipset */
    chipset_detection_result_t chipset_result = detect_system_chipset();
    if (chipset_result.chipset.found) {
        log_info("  Chipset: %s (vendor:%04X device:%04X)", 
                 chipset_result.chipset.name,
                 chipset_result.chipset.vendor_id,
                 chipset_result.chipset.device_id);
    } else {
        log_info("  Chipset: Unknown (pre-PCI system)");
    }
    
    /* 4.2: Detect bus type */
    bus_type_t bus_type = BUS_ISA;  /* Default */
    if (is_mca_system()) {
        bus_type = BUS_MCA;
        log_info("  Bus type: MicroChannel (MCA)");
    } else if (is_eisa_system()) {
        bus_type = BUS_EISA;
        log_info("  Bus type: EISA");
    } else if (chipset_result.chipset.era == CHIPSET_ERA_PCI) {
        bus_type = BUS_PCI;
        log_info("  Bus type: PCI");
    } else {
        log_info("  Bus type: ISA");
    }
    
    /* 4.3: Bus master testing (conditional) */
    if (g_cpu_info.type == CPU_TYPE_80286 || !chipset_result.chipset.found) {
        log_info("  Running bus master capability test (286 or unknown chipset)");
        busmaster_test_results_t bm_results = {0};
        busmaster_test_config_t bm_config = {
            .test_mode = BM_TEST_MODE_QUICK,
            .timeout_ms = 200
        };
        
        /* Note: busmaster_test_init would need a NIC context, skip for now */
        /* This would be refined in Phase 4.5 with actual NIC */
    }
    
    /* Store results for later phases */
    driver_state.chipset_result = chipset_result;
    driver_state.bus_type = bus_type;
    MARK_PHASE_COMPLETE(UNWIND_PHASE_CHIPSET);
    
    /* =================================================================
     * PHASE 4.5: VDS Detection and DMA Policy Refinement
     * GPT-5: Critical for V86 mode DMA support
     * =================================================================
     */
    log_info("Phase 4.5: VDS detection and DMA policy refinement");
    
    /* Initialize VDS support (checks for V86 mode internally) */
    result = vds_init();
    if (result == 0 && vds_available()) {
        log_info("  VDS available - DMA safe in V86 mode");
        MARK_PHASE_COMPLETE(UNWIND_PHASE_VDS);
    } else if (vds_in_v86_mode()) {
        log_warning("  V86 mode detected but VDS not available!");
        log_warning("  Forcing DMA_POLICY_FORBID for safety");
        g_dma_policy = DMA_POLICY_FORBID;
    }
    
    /* Additional DMA tests for 286 or unknown chipsets */
    if (g_cpu_info.type == CPU_TYPE_80286 || !chipset_result.chipset.found) {
        log_info("  Running DMA capability tests (286 or unknown chipset)");
        
        /* Configure DMA tests for early detection */
        dma_test_cfg.skip_destructive_tests = true;  /* Non-destructive only */
        dma_test_cfg.verbose_output = false;
        dma_test_cfg.test_iterations = 1;
        dma_test_cfg.test_buffer_size = 512;  /* Small test buffer */
        dma_test_cfg.timeout_ms = 1000;
        
        /* Run basic DMA tests without NIC context */
        result = test_dma_cache_coherency(&dma_test_cfg);
        if (result < 0) {
            log_warning("  Cache coherency test failed - DMA may be unreliable");
            if (g_dma_policy != DMA_POLICY_FORBID) {
                g_dma_policy = DMA_POLICY_COMMONBUF;  /* Use bounce buffers */
            }
        } else {
            log_info("  Cache coherency test passed");
        }
        
        result = test_bus_snooping(&dma_test_cfg);
        if (result < 0) {
            log_warning("  Bus snooping test failed - using bounce buffers");
            if (g_dma_policy != DMA_POLICY_FORBID) {
                g_dma_policy = DMA_POLICY_COMMONBUF;
            }
        } else {
            log_info("  Bus snooping test passed");
        }
    }
    
    /* PHASE 5: Initialize memory management (core only) */
    log_info("Phase 5: Memory subsystem initialization (core)");
    result = memory_init_core(&driver_state.config);
    if (result < 0) {
        log_error("Core memory initialization failed: %s", get_error_message(result));
        unwind_execute(result, "Core memory initialization failed");
        return MAIN_ERR_MEMORY;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_MEMORY_CORE);
    
    /* PHASE 6-8: Initialize hardware detection and NICs */
    log_info("Phase 6-8: Hardware detection and initialization");
    result = hardware_init_all(&driver_state.config);
    if (result < 0) {
        log_error("Hardware initialization failed: %s", get_error_message(result));
        unwind_execute(result, "Hardware initialization failed");
        return MAIN_ERR_HARDWARE;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_HARDWARE);
    
    /* Get primary NIC for DMA testing */
    primary_nic = hardware_get_primary_nic();
    
    /* =================================================================
     * PHASE 9: Memory subsystem initialization (DMA buffers)
     * Allocate DMA buffers based on refined capabilities
     * =================================================================
     */
    log_info("Phase 9: Memory subsystem initialization (DMA buffers)");
    result = memory_init_dma(&driver_state.config);
    if (result < 0) {
        log_error("DMA memory initialization failed: %s", get_error_message(result));
        unwind_execute(result, "DMA memory initialization failed");
        return MAIN_ERR_MEMORY;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_MEMORY_DMA);
    
    /* =================================================================
     * PHASE 10: TSR Relocation (BEFORE hooking vectors)
     * Move driver to final memory location before installing any hooks
     * GPT-5: Critical to relocate before vectors are installed
     * =================================================================
     */
    log_info("Phase 10: TSR relocation");
    result = tsr_relocate();
    if (result < 0) {
        log_warning("TSR relocation failed, continuing in current location");
    } else {
        log_info("TSR relocated successfully");
        MARK_PHASE_COMPLETE(UNWIND_PHASE_TSR);
    }
    
    /* =================================================================
     * PHASE 11: Packet Driver API Installation (hooks only)
     * Install minimal API hooks without enabling interrupts
     * Now safe after TSR relocation is complete
     * =================================================================
     */
    log_info("Phase 11: Packet driver API installation (hooks only)");
    result = api_install_hooks(&driver_state.config);
    if (result < 0) {
        log_error("API hook installation failed: %s", get_error_message(result));
        unwind_execute(result, "API hook installation failed");
        return MAIN_ERR_API;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_API_HOOKS);
    
    /* =================================================================
     * PHASE 12: Enable Interrupts (precise control)
     * Enable only necessary interrupts at precise points
     * =================================================================
     */
    log_info("Phase 12: Enabling interrupts");
    result = enable_driver_interrupts();
    if (result < 0) {
        log_error("Failed to enable interrupts: %s", get_error_message(result));
        unwind_execute(result, "Interrupt enablement failed");
        return MAIN_ERR_HARDWARE;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_INTERRUPTS);
    
    /* =================================================================
     * PHASE 13: Final API Activation
     * Activate full packet driver API functionality
     * =================================================================
     */
    log_info("Phase 13: Final API activation");
    result = api_activate(&driver_state.config);
    if (result < 0) {
        log_error("API activation failed: %s", get_error_message(result));
        unwind_execute(result, "API activation failed");
        return MAIN_ERR_API;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_API_ACTIVE);
    
    /* =================================================================
     * PHASE 14: Complete Boot
     * Final validation and status reporting
     * =================================================================
     */
    log_info("Phase 14: Boot completion");
    driver_initialized = 1;
    MARK_PHASE_COMPLETE(UNWIND_PHASE_COMPLETE);
    
    /* Report final status */
    log_info("=================================================");
    log_info("3Com Packet Driver Boot Sequence Complete");
    log_info("  CPU: %s", cpu_type_to_string(g_cpu_info.type));
    log_info("  Chipset: %s", chipset_result.chipset.found ? 
             chipset_result.chipset.name : "Unknown");
    log_info("  Bus: %s", bus_type == BUS_MCA ? "MCA" :
             bus_type == BUS_EISA ? "EISA" :
             bus_type == BUS_PCI ? "PCI" : "ISA");
    log_info("  DMA Policy: %s", g_dma_policy == DMA_POLICY_FORBID ? "Disabled" :
             g_dma_policy == DMA_POLICY_DIRECT ? "Direct" : "Common Buffer");
    log_info("  NICs detected: %d", driver_state.num_nics);
    log_info("=================================================");
    
    return 0;
}

/**
 * @brief Cleanup driver resources
 * @return 0 on success, negative on error
 */
int driver_cleanup(void) {
    int result = 0;
    
    if (!driver_initialized) {
        return 0;
    }
    
    /* Comprehensive cleanup sequence for Phase 3 implementation */
    log_info("Cleaning up driver resources");
    
    /* Cleanup API */
    result = api_cleanup();
    if (result < 0) {
        log_error("API cleanup failed: %s", get_error_message(result));
    }
    
    /* Cleanup hardware */
    result = hardware_cleanup();
    if (result < 0) {
        log_error("Hardware cleanup failed: %s", get_error_message(result));
    }
    
    /* Cleanup memory */
    result = memory_cleanup();
    if (result < 0) {
        log_error("Memory cleanup failed: %s", get_error_message(result));
    }
    
    driver_initialized = 0;
    log_info("Driver cleanup completed");
    return 0;
}

/**
 * @brief Get current driver state
 * @return Pointer to driver state structure
 */
driver_state_t* get_driver_state(void) {
    return &driver_state;
}

/**
 * @brief Check if driver is initialized
 * @return 1 if initialized, 0 otherwise
 */
int is_driver_initialized(void) {
    return driver_initialized;
}

/**
 * @brief Get CPU information for diagnostic purposes
 * @param info Pointer to CPU info structure to fill
 */
void get_cpu_info(cpu_info_t* info) {
    if (info) {
        *info = g_cpu_info;
    }
}

/**
 * @brief Print CPU information to console
 */
void print_cpu_info(void) {
    printf("CPU Information:\n");
    printf("  Type: %s\n", cpu_type_to_string(g_cpu_info.type));
    printf("  Features: 0x%04X\n", g_cpu_info.features);
    
    if (g_cpu_info.features & CPU_FEATURE_FPU) {
        printf("    - Floating Point Unit\n");
    }
    if (cpu_supports_32bit()) {
        printf("    - 32-bit Operations\n");
    }
    if (g_cpu_info.has_cpuid) {
        printf("    - CPUID Instruction\n");
        if (g_cpu_info.vendor[0] != '\0') {
            printf("  Vendor: %.12s\n", g_cpu_info.vendor);
        }
        printf("  Stepping: %d\n", g_cpu_info.stepping);
    }
}

/**
 * @brief Main entry point for DOS device driver  
 * @param argc Command line argument count
 * @param argv Command line arguments
 * @return Exit code
 */
int main(int argc, char *argv[]) {
    int result = 0;
    entry_validation_t entry_result = {0};
    platform_probe_result_t *platform;
    dma_test_config_t dma_test_cfg = {0};
    const char *config_params = "";
    
    /* Parse command line arguments for CONFIG.SYS parameters */
    if (argc > 1) {
        config_params = argv[1];
    }
    
    /* Initialize unwind system FIRST */
    unwind_init();
    
    /* =================================================================
     * PHASE 0: Entry Validation (FIRST - before ANY initialization)
     * =================================================================
     */
    result = entry_validate(argc, argv, &entry_result);
    if (result != ENTRY_SUCCESS) {
        printf("Entry validation failed: %s\n", entry_error_string(result));
        if (entry_result.conflict_desc[0]) {
            printf("Conflict: %s\n", entry_result.conflict_desc);
        }
        return 1;
    }
    
    /* =================================================================
     * PHASE 1: CPU Detection (MUST come before V86 detection)
     * GPT-5: Need 386+ to safely read EFLAGS.VM
     * =================================================================
     */
    printf("Phase 1: CPU detection and identification\n");
    result = initialize_cpu_detection();
    if (result < 0) {
        printf("CPU detection failed - requires 286 or higher\n");
        return 1;
    }
    printf("  CPU: %s\n", cpu_type_to_string(g_cpu_info.type));
    
    /* =================================================================
     * PHASE 2: Platform Probe Early (determine DMA policy)
     * Now safe to check V86 mode with CPU known
     * =================================================================
     */
    result = platform_probe_early();
    if (result != 0) {
        printf("Early platform probe failed\n");
        return 1;
    }
    
    platform = get_early_platform_results();
    printf("DMA Policy: %s\n", get_dma_policy_description(platform->recommended_policy));
    printf("Environment: %s\n", platform->environment_desc);
    
    /* Check if we can continue with this hardware */
    if (platform->recommended_policy == DMA_POLICY_FORBID) {
        printf("\n!!! WARNING: Bus-master DMA is FORBIDDEN !!!\n");
        printf("3C515-TX will be disabled, only 3C509B (PIO) will work\n");
        if (!platform->pio_fallback_ok) {
            printf("No PIO fallback available - cannot continue\n");
            return 1;
        }
    }
    
    /* Initialize logging after basic validation */
    result = logging_init();
    if (result < 0) {
        printf("Failed to initialize logging system\n");
        unwind_execute(result, "Logging initialization failed");
        return 1;
    }
    MARK_PHASE_COMPLETE(UNWIND_PHASE_LOGGING);
    
    log_info("=== 3Com Packet Driver Boot Sequence ===");
    log_info("Phase 0: Entry validation complete");
    log_info("Phase 1: CPU detection complete - %s", cpu_type_to_string(g_cpu_info.type));
    MARK_PHASE_COMPLETE(UNWIND_PHASE_CPU_DETECT);
    
    log_info("Phase 2: DMA policy set to %s", 
             get_dma_policy_description(platform->recommended_policy));
    MARK_PHASE_COMPLETE(UNWIND_PHASE_PLATFORM_PROBE);
    
    /* For TSR operation, call the ASM entry point which handles:
     * - CPU detection and validation
     * - Hardware initialization 
     * - Interrupt vector installation
     * - Memory resident installation
     */
    log_info("Starting 3Com Packet Driver installation...");
    
    /* Call the main ASM entry point - this may not return if going TSR */
    result = driver_entry();
    
    /* If we reach here, either installation failed or we're in test mode */
    if (result != 0) {
        printf("Driver installation failed with error code %d\n", result);
        
        switch(result) {
            case 2:  /* ERROR_ALREADY_LOADED */
                printf("Driver is already loaded\n");
                break;
            case 3:  /* ERROR_CPU_UNSUPPORTED */
                printf("CPU not supported - requires 286 or higher\n");
                break;
            case 4:  /* ERROR_HARDWARE */
                printf("Hardware initialization failed\n");
                break;
            case 5:  /* ERROR_MEMORY */
                printf("Memory allocation failed\n");
                break;
            default:
                printf("Unknown error occurred\n");
                break;
        }
        
        logging_cleanup();
        return result;
    }
    
    /* If we reach here in TSR mode, something went wrong */
    /* In normal TSR operation, the driver_entry() call should not return */
    printf("Warning: TSR installation may have failed\n");
    
    logging_cleanup();
    return 0;
}

