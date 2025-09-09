/**
 * @file ptask_module.c
 * @brief PTASK.MOD - 3C509B Driver Module (Wrapper Implementation)
 * 
 * REFACTORED IMPLEMENTATION - Wraps existing 3C509B driver
 * 
 * This module serves as a thin wrapper around the existing, tested
 * 3C509B driver implementation in /src/c/3c509b.c. Instead of
 * duplicating hardware code, it bridges the Module ABI to the
 * existing driver, preserving all features and optimizations.
 * 
 * ARCHITECTURE BENEFITS:
 * - Uses proven, tested 3C509B driver code
 * - Preserves cache coherency management
 * - Maintains chipset compatibility database
 * - Eliminates code duplication
 * - Single maintenance point for 3C509B support
 */

#include "../common/module_bridge.h"
#include "../../include/module_abi.h"
#include "../../include/memory_api.h"
#include "../../include/timing_measurement.h"
#include "../../include/logging.h"
#include "../../include/nic_init.h"
#include "ptask_internal.h"

/* Module header must be first in binary layout */
static const module_header_t ptask_module_header = {
    .signature = MODULE_SIGNATURE,
    .abi_version = MODULE_ABI_VERSION,
    .module_type = MODULE_TYPE_NIC,
    .flags = MODULE_FLAG_DISCARD_COLD | MODULE_FLAG_HAS_ISR,
    
    /* Memory layout - much smaller as wrapper */
    .total_size_para = 256,        /* 4KB total */
    .resident_size_para = 192,     /* 3KB resident */
    .cold_size_para = 64,          /* 1KB cold section */
    .alignment_para = 1,           /* 16-byte alignment */
    
    /* Entry points - will be filled during build */
    .init_offset = 0,              /* Set by linker */
    .api_offset = 0,               /* Set by linker */
    .isr_offset = 0,               /* Set by linker */
    .unload_offset = 0,            /* Set by linker */
    
    /* Symbol resolution */
    .export_table_offset = 0,      /* Set by linker */
    .export_count = 4,             /* init, api, isr, cleanup */
    .reloc_table_offset = 0,       /* Set by linker */
    .reloc_count = 0,              /* No relocations needed */
    
    /* BSS and requirements */
    .bss_size_para = 16,           /* 256 bytes BSS */
    .required_cpu = CPU_TYPE_80286, /* Minimum 80286 */
    .required_features = FEATURE_NONE,
    .module_id = MODULE_ID_PTASK,
    
    /* Module identification */
    .module_name = "PTASK   MOD", /* 8.3 format */
    .name_padding = 0,
    
    /* Integrity */
    .header_checksum = 0,          /* Calculated during build */
    .image_checksum = 0,           /* Calculated during build */
    .vendor_id = 0x3COM,           /* 3Com vendor ID */
    .build_timestamp = 0,          /* Set during build */
    .reserved = {0, 0}
};

/* Global bridge instance for PTASK module */
static module_bridge_t g_ptask_bridge;
static module_init_context_t g_ptask_init_context;
static memory_services_t *g_memory_services = NULL;

/* NE2000 compatibility removed - not needed for production */

/* Forward declarations for cold section functions */
static int ptask_detect_hardware_cold(void);

/**
 * @brief PTASK Module initialization entry point
 * 
 * Called by core loader after module loading and relocation.
 * Uses bridge infrastructure to connect to existing 3C509B driver.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int far ptask_module_init(void) {
    pit_timing_t timing;
    int result;
    
    /* Start timing measurement for performance validation */
    PIT_START_TIMING(&timing);
    
    LOG_INFO("PTASK: Module initialization starting (wrapper mode)");
    
    /* Get hardware context from centralized detection */
    module_init_context_t *context = module_get_context_from_detection(MODULE_ID_PTASK, NIC_TYPE_3C509B);
    if (!context) {
        LOG_ERROR("PTASK: No 3C509B hardware available from centralized detection");
        
        /* Fallback to manual detection */
        result = ptask_detect_hardware_cold();
        if (result < 0) {
            LOG_ERROR("PTASK: Fallback hardware detection failed: %d", result);
            return result;
        }
        context = &g_ptask_init_context;
    } else {
        /* Copy context from centralized detection */
        memcpy(&g_ptask_init_context, context, sizeof(module_init_context_t));
        LOG_INFO("PTASK: Using centralized detection results - I/O 0x%X, IRQ %d",
                 context->detected_io_base, context->detected_irq);
    }
    
    /* Initialize bridge infrastructure */
    result = module_bridge_init(&g_ptask_bridge, 
                               (module_header_t *)&ptask_module_header,
                               &g_ptask_init_context);
    if (result < 0) {
        LOG_ERROR("PTASK: Bridge initialization failed: %d", result);
        return result;
    }
    
    /* Connect to existing 3C509B driver */
    result = module_bridge_connect_driver(&g_ptask_bridge, NIC_TYPE_3C509B);
    if (result < 0) {
        LOG_ERROR("PTASK: Driver connection failed: %d", result);
        module_bridge_cleanup(&g_ptask_bridge);
        return result;
    }
    
    /* NE2000 compatibility removed - focus on actual hardware */
    
    /* Measure initialization time */
    PIT_END_TIMING(&timing);
    
    if (!VALIDATE_INIT_TIMING(&timing)) {
        LOG_WARNING("PTASK: Init time %lu μs exceeds 100ms limit", timing.elapsed_us);
    }
    
    LOG_INFO("PTASK: Module initialized successfully in %lu μs (wrapper mode)", 
             timing.elapsed_us);
    LOG_INFO("PTASK: Connected to existing 3C509B driver at I/O 0x%X, IRQ %d",
             g_ptask_init_context.detected_io_base,
             g_ptask_init_context.detected_irq);
    
    return SUCCESS;
}

/**
 * @brief PTASK Module API entry point
 * 
 * Delegates all API calls to the bridge infrastructure, which
 * routes them to the existing 3C509B driver implementation.
 * 
 * @param function Function number
 * @param params Parameter structure (ES:DI in real mode)
 * @return Function-specific return value
 */
int far ptask_module_api(uint16_t function, void far *params) {
    /* Validate module state */
    if (g_ptask_bridge.module_state != MODULE_STATE_ACTIVE) {
        return ERROR_MODULE_NOT_READY;
    }
    
    /* Delegate to bridge infrastructure */
    return module_bridge_api_dispatch(&g_ptask_bridge, function, params);
}

/**
 * @brief PTASK Module ISR entry point
 * 
 * Delegates interrupt handling to the existing 3C509B driver
 * through the bridge infrastructure.
 */
void far ptask_module_isr(void) {
    /* Delegate to bridge infrastructure */
    module_bridge_handle_interrupt(&g_ptask_bridge);
}

/**
 * @brief PTASK Module cleanup entry point
 * 
 * Cleans up the bridge and releases resources.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int far ptask_module_cleanup(void) {
    LOG_DEBUG("PTASK: Starting module cleanup");
    
    /* Cleanup bridge infrastructure */
    int result = module_bridge_cleanup(&g_ptask_bridge);
    if (result < 0) {
        LOG_WARNING("PTASK: Bridge cleanup failed: %d", result);
    }
    
    /* Free memory services */
    if (g_memory_services) {
        g_memory_services = NULL;
    }
    
    LOG_INFO("PTASK: Module cleanup completed");
    return result;
}

/**
 * @brief Register memory services interface
 * 
 * Called by core loader to provide memory management services.
 * 
 * @param memory_services Pointer to memory services interface
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_register_memory_services(memory_services_t *memory_services) {
    if (!memory_services) {
        return ERROR_INVALID_PARAM;
    }
    
    g_memory_services = memory_services;
    return SUCCESS;
}

/**
 * @brief Get module exports table
 * 
 * @return Pointer to exports table
 */
const export_entry_t* ptask_get_exports(void) {
    static const export_entry_t ptask_exports[] = {
        {"INIT", (uint16_t)ptask_module_init, SYMBOL_FLAG_FUNCTION | SYMBOL_FLAG_FAR_CALL},
        {"API", (uint16_t)ptask_module_api, SYMBOL_FLAG_FUNCTION | SYMBOL_FLAG_FAR_CALL},
        {"ISR", (uint16_t)ptask_module_isr, SYMBOL_FLAG_FUNCTION | SYMBOL_FLAG_ISR_SAFE},
        {"CLEANUP", (uint16_t)ptask_module_cleanup, SYMBOL_FLAG_FUNCTION | SYMBOL_FLAG_FAR_CALL}
    };
    
    return ptask_exports;
}

/* Cold section - code that can be discarded after initialization */
#pragma code_seg("COLD")

/**
 * @brief Detect 3C509B hardware (Cold section)
 * 
 * Uses existing hardware detection routines instead of duplicating code.
 * This code is discarded after initialization.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
static int ptask_detect_hardware_cold(void) {
    nic_detect_info_t detect_list[4];
    int detected_count;
    
    LOG_DEBUG("PTASK: Starting hardware detection using existing routines");
    
    /* Use existing 3C509B detection */
    detected_count = nic_detect_3c509b(detect_list, 4);
    if (detected_count <= 0) {
        LOG_ERROR("PTASK: No 3C509B cards detected");
        return ERROR_HARDWARE_NOT_FOUND;
    }
    
    /* Use first detected card */
    nic_detect_info_t *detected = &detect_list[0];
    
    LOG_INFO("PTASK: Detected 3C509B at I/O 0x%X, IRQ %d", 
             detected->io_base, detected->irq);
    
    /* Create initialization context using detected hardware */
    int result = module_create_init_context(&g_ptask_init_context,
                                           detected->io_base,
                                           detected->irq,
                                           detected->mac_address,
                                           detected->device_id);
    if (result < 0) {
        LOG_ERROR("PTASK: Failed to create init context: %d", result);
        return result;
    }
    
    g_ptask_init_context.bus_type = BUS_TYPE_ISA;
    g_ptask_init_context.vendor_id = detected->vendor_id;
    g_ptask_init_context.revision = detected->revision;
    
    LOG_DEBUG("PTASK: Hardware detection completed - using existing detection logic");
    
    return SUCCESS;
}

/* NE2000 compatibility function removed - not needed for production */

#pragma code_seg()  /* Return to default code segment */