/**
 * @file init.c
 * @brief Driver initialization and setup
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "init.h"
#include "hardware.h"
#include "xmsdet.h"
#include "bufaloc.h"
#include "routing.h"
#include "stats.h"
#include "logging.h"
#include "nic_init.h"
#include "cpudet.h"
#include "config.h"
#include "prod.h"
#include "dmamap.h"  // GPT-5: Centralized DMA mapping layer
#include "pltprob.h" // Platform detection and VDS support
#include "vds.h"          // GPT-5 A+: Virtual DMA Services
#include "telemetr.h"    // GPT-5 A+: Production telemetry
#include "smc_safety_patches.h" // SMC safety detection and patching
#include "pciintg.h"     // PCI subsystem integration

/* Global initialization state */
static init_state_t init_state = {0};

/* All functions in this file go to cold section (discarded after init) */
#pragma code_seg("COLD_TEXT", "CODE")

/**
 * @brief Detect CPU type and capabilities
 * @return CPU type or negative on error
 */
int detect_cpu_type(void) {
    /* Use Group 1A CPU detection implementation */
    int result = cpu_detect_init();
    if (result < 0) {
        log_error("CPU detection initialization failed: %d", result);
        return result;
    }
    
    log_info("CPU detected: %s", cpu_type_to_string(g_cpu_info.type));
    log_info("CPU features: 0x%08X", g_cpu_info.features);
    
    return (int)g_cpu_info.type;
}

/**
 * @brief Initialize hardware subsystem and detect NICs
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int hardware_init_all(const config_t *config) {
    int result = 0;
    int num_nics = 0;
    int i;
    int active_nics;

    if (!config) {
        log_error("hardware_init_all: NULL config parameter");
        return INIT_ERR_INVALID_PARAM;
    }
    
    log_info("Initializing hardware subsystem with guided detection");
    log_info("Config: IO1=0x%X, IO2=0x%X, IRQ1=%d, IRQ2=%d, Busmaster=%d",
             config->io1_base, config->io2_base, config->irq1, config->irq2, config->busmaster);
    
    /* Initialize basic hardware layer */
    result = hardware_init();
    if (result != 0) {
        log_error("Hardware layer initialization failed: %d", result);
        return INIT_ERR_HARDWARE;
    }
    
    /* GPT-5 A+ Enhancement: Initialize VDS before DMA mapping */
    log_info("Initializing VDS (Virtual DMA Services) for EMM386/QEMM compatibility");
    result = vds_init();
    if (result != 0) {
        log_warning("VDS initialization returned: %d (non-fatal)", result);
        /* VDS is optional - continue without it */
    } else {
        log_info("VDS subsystem initialized successfully");
    }
    
    /* GPT-5 Enhancement: Initialize centralized DMA mapping layer */
    log_info("Initializing centralized DMA mapping layer");
    result = dma_mapping_init();
    if (result != 0) {
        log_error("DMA mapping layer initialization failed: %d", result);
        return INIT_ERR_HARDWARE;
    }
    log_info("Centralized DMA mapping layer initialized successfully");
    
    /* GPT-5 A+ Enhancement: Initialize telemetry system */
    log_info("Initializing production telemetry system");
    telemetry_init();
    log_info("Telemetry system initialized");
    
    /* Initialize NIC detection system */
    result = nic_init_system();
    if (result < 0) {
        log_error("NIC system initialization failed: %d", result);
        return INIT_ERR_NIC_INIT;
    }
    
    /* Phase 1: Detect 3C509B NICs (simpler PIO-based) */
    log_info("Phase 1: Detecting 3C509B NICs (PIO-based)");
    nic_detect_info_t detect_info[MAX_NICS];
    int detected_3c509b = nic_detect_3c509b(detect_info, MAX_NICS);
    if (detected_3c509b > 0) {
        log_info("Found %d 3C509B NIC(s)", detected_3c509b);
        /* Initialize detected 3C509B NICs */
        for (i = 0; i < detected_3c509b && num_nics < MAX_NICS; i++) {
            nic_info_t *nic = hardware_get_nic(num_nics);
            result = nic_init_from_detection(nic, &detect_info[i]);
            if (result == 0) {
                num_nics++;
                log_info("3C509B NIC %d initialized at IO=0x%X, IRQ=%d", 
                        num_nics, detect_info[i].io_base, detect_info[i].irq);
            } else {
                log_warning("Failed to initialize 3C509B NIC at IO=0x%X: %d", 
                           detect_info[i].io_base, result);
            }
        }
    }
    
    /* Phase 2: Detect 3C515-TX NICs (complex bus mastering) */
    /* SKIP on 8086: 3C515-TX requires 16-bit ISA slot and bus mastering */
    if (g_cpu_info.type < CPU_TYPE_80286) {
        log_info("Phase 2: Skipped (8086 mode - 3C515-TX requires 286+ and bus mastering)");
    } else {
        log_info("Phase 2: Detecting 3C515-TX NICs (bus mastering)");

        /* Check CPU capability for bus mastering support */
        bool cpu_supports_busmaster = (g_cpu_info.type >= CPU_TYPE_80286) &&
                                     cpu_has_feature(CPU_FEATURE_CX8);
        if (!cpu_supports_busmaster && config->busmaster != BUSMASTER_OFF) {
            log_warning("CPU does not support bus mastering, disabling 3C515-TX detection");
        } else {
            int detected_3c515 = nic_detect_3c515(detect_info, MAX_NICS - num_nics);
            if (detected_3c515 > 0) {
                log_info("Found %d 3C515-TX NIC(s)", detected_3c515);
                /* Initialize detected 3C515-TX NICs */
                for (i = 0; i < detected_3c515 && num_nics < MAX_NICS; i++) {
                    nic_info_t *nic = hardware_get_nic(num_nics);
                    result = nic_init_from_detection(nic, &detect_info[i]);
                    if (result == 0) {
                        num_nics++;
                        log_info("3C515-TX NIC %d initialized at IO=0x%X, IRQ=%d",
                                num_nics, detect_info[i].io_base, detect_info[i].irq);
                    } else {
                        log_warning("Failed to initialize 3C515-TX NIC at IO=0x%X: %d",
                                   detect_info[i].io_base, result);
                    }
                }
            }
        }
    }

    /* Phase 3: Detect 3Com PCI NICs (Vortex, Boomerang, Cyclone, Tornado) */
    /* SKIP on 8086: PCI didn't exist in the 8086 era */
    if (g_cpu_info.type < CPU_TYPE_80286) {
        log_info("Phase 3: Skipped (8086 mode - PCI not available)");
    } else {
        log_info("Phase 3: Detecting 3Com PCI NICs");

        /* Check if PCI is available and enabled */
        if (config->pci != PCI_DISABLED && is_pci_available()) {
            int pci_nics = detect_and_init_pci_nics(config, MAX_NICS - num_nics);
            if (pci_nics > 0) {
                log_info("Initialized %d PCI NIC(s)", pci_nics);
                num_nics += pci_nics;
            } else if (pci_nics == 0) {
                log_info("No 3Com PCI NICs detected");
            } else {
                log_warning("PCI detection failed with error: %d", pci_nics);
            }
        } else if (config->pci == PCI_REQUIRED) {
            log_error("PCI support required but not available");
            return INIT_ERR_NO_PCI;
        } else {
            log_info("PCI support disabled or not available");
        }
    }
    
    /* Validate against configuration constraints and integrate with TSR */
    if (num_nics > 0) {
        log_info("Validating NIC configuration against parameters");
        
        /* Validate detected NICs against configuration parameters */
        for (i = 0; i < num_nics; i++) {
            nic_info_t *nic = hardware_get_nic(i);
            if (!nic) continue;
            
            /* Check for configured I/O addresses */
            if (config->io1_base != 0 && i == 0 && nic->io_base != config->io1_base) {
                log_warning("NIC 0 detected at I/O 0x%X but configured for 0x%X", 
                           nic->io_base, config->io1_base);
            }
            if (config->io2_base != 0 && i == 1 && nic->io_base != config->io2_base) {
                log_warning("NIC 1 detected at I/O 0x%X but configured for 0x%X", 
                           nic->io_base, config->io2_base);
            }
            
            /* Check IRQ assignments */
            if (config->irq1 != 0 && i == 0 && nic->irq != config->irq1) {
                log_warning("NIC 0 detected with IRQ %d but configured for IRQ %d", 
                           nic->irq, config->irq1);
            }
            if (config->irq2 != 0 && i == 1 && nic->irq != config->irq2) {
                log_warning("NIC 1 detected with IRQ %d but configured for IRQ %d", 
                           nic->irq, config->irq2);
            }
            
            /* Validate bus mastering capability for 3C515-TX */
            if (nic->type == NIC_TYPE_3C515_TX && config->busmaster != BUSMASTER_OFF) {
                if (!cpu_supports_busmaster) {
                    log_error("3C515-TX requires bus mastering but CPU doesn't support it");
                    nic->capabilities &= ~(HW_CAP_DMA | HW_CAP_BUS_MASTER);
                    log_warning("Disabling DMA/bus mastering for 3C515-TX");
                }
            }
            
            /* Set NIC status based on validation */
            nic->status |= NIC_STATUS_PRESENT;
            if (nic->ops && nic->ops->init && nic->ops->init(nic) == SUCCESS) {
                nic->status |= NIC_STATUS_ACTIVE;
                log_info("NIC %d validated and activated", i);
            } else {
                log_error("Failed to activate NIC %d", i);
            }
        }
        
        /* Log final hardware detection summary for TSR integration */
        log_info("=== Hardware Detection Summary ===");
        log_info("CPU: %s (features: 0x%08X)", 
                cpu_type_to_string(g_cpu_info.type), g_cpu_info.features);
        log_info("Total NICs detected: %d", num_nics);
        for (i = 0; i < num_nics; i++) {
            nic_info_t *nic = hardware_get_nic(i);
            if (nic) {
                log_info("NIC %d: %s at I/O 0x%X, IRQ %d, Status: %s", 
                        i, hardware_nic_type_to_string(nic->type),
                        nic->io_base, nic->irq, 
                        (nic->status & NIC_STATUS_ACTIVE) ? "Active" : "Detected");
                if (nic->mac[0] | nic->mac[1] | nic->mac[2] | nic->mac[3] | nic->mac[4] | nic->mac[5]) {
                    log_info("    MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
                            nic->mac[0], nic->mac[1], nic->mac[2],
                            nic->mac[3], nic->mac[4], nic->mac[5]);
                }
            }
        }
        log_info("=== End Detection Summary ===");
    }
    
    if (num_nics <= 0) {
        log_error("No supported NICs detected - driver cannot function");
        return INIT_ERR_NO_NICS;
    }
    
    /* Count active NICs */
    active_nics = 0;
    for (i = 0; i < num_nics; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_ACTIVE)) {
            active_nics++;
        }
    }
    
    if (active_nics == 0) {
        log_error("No NICs could be activated - hardware initialization failed");
        return INIT_ERR_HARDWARE;
    }
    
    log_info("Hardware initialization completed successfully");
    log_info("Summary: %d NICs detected, %d activated", num_nics, active_nics);
    
    /* Update initialization state for TSR integration */
    init_state.num_nics = active_nics;
    init_state.hardware_ready = 1;
    
    /* Integration with Group 1A TSR framework is now complete */
    /* - CPU detection and capabilities are available in g_cpu_info */
    /* - Hardware abstraction layer with vtables is operational */
    /* - Configuration validation ensures parameter compliance */
    
    return 0;
}

/**
 * @brief Initialize memory management subsystem
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int memory_init(const config_t *config) {
    int result = 0;
    
    if (!config) {
        log_error("memory_init: NULL config parameter");
        return INIT_ERR_INVALID_PARAM;
    }
    
    log_info("Initializing memory management");
    
    /* Initialize memory pools for packet buffers */
    /* Note: memory_pools_init() not yet implemented */
    /* Memory pool initialization handled by buffer_alloc_init() */
    
    /* Detect XMS if enabled */
    if (config->use_xms) {
        result = xms_detect_and_init();
        if (result < 0) {
            log_warning("XMS detection failed, falling back to conventional memory");
            init_state.xms_available = 0;
        } else {
            log_info("XMS detected and initialized");
            init_state.xms_available = 1;
        }
    }
    
    /* Initialize buffer allocation */
    result = buffer_alloc_init(config);
    if (result < 0) {
        log_error("Buffer allocation initialization failed: %d", result);
        return INIT_ERR_MEMORY;
    }
    
    log_info("Memory management initialized successfully");
    init_state.memory_ready = 1;
    
    return 0;
}

/**
 * @brief Initialize routing subsystem
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int routing_init(const config_t *config) {
    int result = 0;
    
    if (!config) {
        log_error("routing_init: NULL config parameter");
        return INIT_ERR_INVALID_PARAM;
    }
    
    log_info("Initializing routing subsystem");\n    \n    /* Initialize routing tables and bridge learning */\n    result = routing_init();\n    if (result < 0) {\n        log_warning("Routing initialization failed: %d", result);\n        /* Continue without routing - basic operation still possible */\n    }
    
    /* Initialize main routing engine */
    result = routing_engine_init(config);
    if (result < 0) {
        log_error("Routing engine initialization failed: %d", result);
        return INIT_ERR_ROUTING;
    }
    
    /* Initialize static routing if enabled */
    if (config->enable_static_routing) {
        result = static_routing_init(config);
        if (result < 0) {
            log_error("Static routing initialization failed: %d", result);
            return INIT_ERR_ROUTING;
        }
    }
    
    log_info("Routing subsystem initialized successfully");
    init_state.routing_ready = 1;
    
    return 0;
}

/**
 * @brief Initialize statistics subsystem
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int stats_init(const config_t *config) {
    int result = 0;
    
    if (!config) {
        log_error("stats_init: NULL config parameter");
        return INIT_ERR_INVALID_PARAM;
    }
    
    log_info("Initializing statistics subsystem");\n    \n    /* Initialize global statistics counters */\n    result = stats_reset_all();\n    if (result < 0) {\n        log_warning("Statistics reset failed: %d", result);\n        /* Continue - statistics are not critical for operation */\n    }
    
    result = stats_subsystem_init(config);
    if (result < 0) {
        log_error("Statistics subsystem initialization failed: %d", result);
        return INIT_ERR_STATS;
    }
    
    log_info("Statistics subsystem initialized successfully");
    init_state.stats_ready = 1;
    
    return 0;
}

/**
 * @brief Complete initialization sequence
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int init_complete_sequence(const config_t *config) {
    int result = 0;
    
    if (!config) {
        log_error("init_complete_sequence: NULL config parameter");
        return INIT_ERR_INVALID_PARAM;
    }
    
    log_info("Starting complete initialization sequence");
    
    /* Detect platform and VDS capabilities FIRST for optimal memory allocation */
    init_state.platform = platform_detect();
    init_state.dma_policy = init_state.platform.recommended_policy;
    
    log_info("Platform detection complete:");
    log_info("  Memory manager: %s", init_state.platform.memory_manager);
    log_info("  Virtualizer: %s", init_state.platform.virtualizer);
    log_info("  VDS available: %s", init_state.platform.vds_available ? "Yes" : "No");
    log_info("  DMA policy: %s", 
             init_state.dma_policy == DMA_POLICY_DIRECT ? "DIRECT" :
             init_state.dma_policy == DMA_POLICY_COMMONBUF ? "COMMONBUF" : "FORBID");
    
    /* Initialize global platform state for other modules */
    platform_set_global_policy(init_state.dma_policy);
    
    /* Detect CPU capabilities */
    result = detect_cpu_type();
    if (result < 0) {
        log_error("CPU detection failed: %d", result);
        return INIT_ERR_CPU_DETECT;
    }
    init_state.cpu_type = result;
    
    /* Initialize memory management */
    result = memory_init(config);
    if (result < 0) {
        log_error("Memory initialization failed: %d", result);
        return result;
    }
    
    /* Initialize DMA safety framework BEFORE hardware initialization */
    /* GPT-5 CRITICAL: DMA safety must be initialized before any DMA operations */
    extern int dma_safety_init(void);
    extern int register_3com_device_constraints(void);
    
    result = dma_safety_init();
    if (result < 0) {
        log_error("DMA safety framework initialization failed: %d", result);
        return result;
    }
    
    /* Register 3Com device constraints for all supported NICs */
    result = register_3com_device_constraints();
    if (result < 0) {
        log_error("Failed to register 3Com device constraints: %d", result);
        return result;
    }
    
    log_info("DMA safety framework initialized with 3Com device constraints");
    
    /* GPT-5 CRITICAL: Initialize SMC safety detection before hardware initialization */
    /* This bridges the gap between "live" optimized code and "orphaned" safety modules */
    extern int init_complete_safety_detection(void);
    
    log_info("Initializing SMC safety detection and patching system");
    result = init_complete_safety_detection();
    if (result < 0) {
        log_error("SMC safety detection initialization failed: %d", result);
        log_error("This is critical - optimized paths cannot be safely used");
        return result;
    }
    log_info("SMC safety detection completed - hot paths patched successfully");
    
    /* Initialize hardware */
    result = hardware_init_all(config);
    if (result < 0) {
        log_error("Hardware initialization failed: %d", result);
        return result;
    }
    
    /* Initialize routing */
    result = routing_init(config);
    if (result < 0) {
        log_error("Routing initialization failed: %d", result);
        return result;
    }
    
    /* Initialize statistics */
    result = stats_init(config);
    if (result < 0) {
        log_error("Statistics initialization failed: %d", result);
        return result;
    }
    
    init_state.fully_initialized = 1;
    log_info("Complete initialization sequence finished successfully");
    
    return 0;
}

/**
 * @brief Cleanup initialization resources
 * @return 0 on success, negative on error
 */
int init_cleanup(void) {
    int result = 0;
    
    log_info("Cleaning up initialization resources");\n    \n    /* Cleanup in reverse order of initialization */\n    if (init_state.stats_ready) {\n        stats_cleanup();\n        init_state.stats_ready = 0;\n    }\n    \n    if (init_state.routing_ready) {\n        routing_cleanup();\n        init_state.routing_ready = 0;\n    }\n    \n    if (init_state.hardware_ready) {\n        hardware_cleanup();\n        init_state.hardware_ready = 0;\n    }\n    \n    if (init_state.memory_ready) {\n        /* Memory cleanup handled by buffer system */\n        init_state.memory_ready = 0;\n    }
    
    if (init_state.stats_ready) {
        result = stats_cleanup();
        if (result < 0) {
            log_error("Statistics cleanup failed: %d", result);
        }
        init_state.stats_ready = 0;
    }
    
    if (init_state.routing_ready) {
        result = routing_cleanup();
        if (result < 0) {
            log_error("Routing cleanup failed: %d", result);
        }
        init_state.routing_ready = 0;
    }
    
    if (init_state.hardware_ready) {
        result = hardware_cleanup();
        if (result < 0) {
            log_error("Hardware cleanup failed: %d", result);
        }
        init_state.hardware_ready = 0;
    }
    
    if (init_state.memory_ready) {
        result = buffer_alloc_cleanup();
        if (result < 0) {
            log_error("Buffer allocation cleanup failed: %d", result);
        }
        init_state.memory_ready = 0;
    }
    
    if (init_state.xms_available) {
        result = xms_cleanup();
        if (result < 0) {
            log_error("XMS cleanup failed: %d", result);
        }
        init_state.xms_available = 0;
    }
    
    memset(&init_state, 0, sizeof(init_state));
    log_info("Initialization cleanup completed");
    
    return 0;
}

/**
 * @brief Get current initialization state
 * @return Pointer to initialization state structure
 */
const init_state_t* get_init_state(void) {
    return &init_state;
}

/**
 * @brief Check if initialization is complete
 * @return 1 if fully initialized, 0 otherwise
 */
int is_init_complete(void) {
    return init_state.fully_initialized;
}

/* Restore default code segment */
#pragma code_seg()

