/**
 * @file ptask_module.c
 * @brief PTASK.MOD - 3C509B ISA and 3C589 PCMCIA Driver Module
 * 
 * PTASK.MOD Implementation for 3Com Packet Driver Modular Architecture
 * 
 * Team A (Agents 05-06) - Week 1 Day 4-5 Critical Deliverable
 * Supports 3C509B ISA PnP and 3C589 PCMCIA with shared PIO logic
 * Uses NE2000 compatibility layer for Week 1 emulator validation
 */

#include "../../include/module_abi.h"
#include "../../include/memory_api.h"
#include "../../include/timing_measurement.h"
#include "../../include/cpu_detect.h"
#include "../../include/3c509b.h"
#include "ptask_internal.h"

/* Module header must be first in binary layout */
static const module_header_t ptask_module_header = {
    .signature = MODULE_SIGNATURE,
    .abi_version = MODULE_ABI_VERSION,
    .module_type = MODULE_TYPE_NIC,
    .flags = MODULE_FLAG_DISCARD_COLD | MODULE_FLAG_HAS_ISR | 
             MODULE_FLAG_NEEDS_DMA_SAFE | MODULE_FLAG_SMC_USED,
    
    /* Memory layout - designed for <5KB resident */
    .total_size_para = 320,        /* 5KB total */
    .resident_size_para = 256,     /* 4KB resident */
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

/* Global module instance data */
static ptask_context_t g_ptask_context;
static memory_services_t *g_memory_services = NULL;

/* Week 1 NE2000 compatibility flag */
#ifdef WEEK1_EMULATOR_TESTING
static bool g_use_ne2000_compat = true;
#else
static bool g_use_ne2000_compat = false;
#endif

/* Forward declarations */
static int ptask_init_hardware(nic_info_t *nic);
static int ptask_setup_shared_pio(void);
static void ptask_apply_cpu_optimizations(void);

/**
 * @brief Module initialization entry point
 * 
 * Called by core loader after module loading and relocation.
 * Must complete within 100ms per Module ABI requirements.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int far ptask_module_init(void) {
    timing_context_t timing;
    uint16_t init_time_us;
    int result;
    
    /* Start timing measurement for performance validation */
    TIMING_START(timing);
    
    /* Initialize module context */
    memset(&g_ptask_context, 0, sizeof(ptask_context_t));
    g_ptask_context.module_id = MODULE_ID_PTASK;
    g_ptask_context.state = PTASK_STATE_INITIALIZING;
    
    /* Detect hardware environment */
    result = ptask_detect_target_hardware();
    if (result < 0) {
        LOG_ERROR("PTASK: Hardware detection failed: %d", result);
        return result;
    }
    
    /* Initialize shared PIO library with CPU optimization */
    result = ptask_setup_shared_pio();
    if (result < 0) {
        LOG_ERROR("PTASK: Shared PIO setup failed: %d", result);
        return result;
    }
    
    /* Apply CPU-specific optimizations */
    ptask_apply_cpu_optimizations();
    
    /* Initialize Week 1 NE2000 compatibility if enabled */
    if (g_use_ne2000_compat) {
        result = ptask_init_ne2000_compat();
        if (result < 0) {
            LOG_ERROR("PTASK: NE2000 compatibility init failed: %d", result);
            return result;
        }
        LOG_INFO("PTASK: Week 1 NE2000 compatibility mode enabled");
    }
    
    /* Register module with core systems */
    g_ptask_context.state = PTASK_STATE_ACTIVE;
    
    /* Measure initialization time */
    TIMING_END(timing);
    init_time_us = TIMING_GET_MICROSECONDS(timing);
    
    if (init_time_us > 100000) { /* 100ms limit */
        LOG_WARNING("PTASK: Init time %d μs exceeds 100ms limit", init_time_us);
    }
    
    LOG_INFO("PTASK.MOD initialized successfully in %d μs", init_time_us);
    return SUCCESS;
}

/**
 * @brief Module API entry point
 * 
 * Handles all module API calls from core loader and other modules.
 * 
 * @param function Function number
 * @param params Parameter structure (ES:DI in real mode)
 * @return Function-specific return value
 */
int far ptask_module_api(uint16_t function, void far *params) {
    timing_context_t timing;
    uint16_t cli_time_us;
    int result = SUCCESS;
    
    /* Validate module state */
    if (g_ptask_context.state != PTASK_STATE_ACTIVE) {
        return ERROR_MODULE_NOT_READY;
    }
    
    /* Handle API functions */
    switch (function) {
        case PTASK_API_DETECT_HARDWARE:
            result = ptask_api_detect_hardware((ptask_detect_params_t far *)params);
            break;
            
        case PTASK_API_INITIALIZE_NIC:
            /* Start CLI timing measurement */
            TIMING_CLI_START(timing);
            result = ptask_api_initialize_nic((ptask_init_params_t far *)params);
            TIMING_CLI_END(timing);
            
            /* Validate CLI duration ≤8μs */
            cli_time_us = TIMING_GET_MICROSECONDS(timing);
            if (cli_time_us > 8) {
                LOG_WARNING("PTASK: CLI section %d μs exceeds 8μs limit", cli_time_us);
            }
            break;
            
        case PTASK_API_SEND_PACKET:
            result = ptask_api_send_packet((ptask_send_params_t far *)params);
            break;
            
        case PTASK_API_RECEIVE_PACKET:
            result = ptask_api_receive_packet((ptask_recv_params_t far *)params);
            break;
            
        case PTASK_API_GET_STATISTICS:
            result = ptask_api_get_statistics((ptask_stats_params_t far *)params);
            break;
            
        case PTASK_API_CONFIGURE:
            result = ptask_api_configure((ptask_config_params_t far *)params);
            break;
            
        default:
            LOG_WARNING("PTASK: Unknown API function: %d", function);
            result = ERROR_UNSUPPORTED_FUNCTION;
            break;
    }
    
    return result;
}

/**
 * @brief Module ISR entry point
 * 
 * Zero-branch interrupt service routine optimized for ≤60μs execution.
 * Uses computed jumps and straight-line code with self-modifying optimizations.
 */
void far ptask_module_isr(void) {
    /* This will be implemented in ptask_isr.asm for optimal performance */
    /* Assembly implementation provides zero-branch critical paths */
    ptask_isr_asm_entry();
}

/**
 * @brief Module cleanup entry point
 * 
 * Called before module unloading to free resources and restore state.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int far ptask_module_cleanup(void) {
    int result = SUCCESS;
    
    LOG_DEBUG("PTASK: Starting module cleanup");
    
    /* Disable interrupts if registered */
    if (g_ptask_context.isr_registered) {
        result = ptask_disable_interrupts();
        if (result < 0) {
            LOG_WARNING("PTASK: Failed to disable interrupts: %d", result);
        }
    }
    
    /* Cleanup hardware resources */
    if (g_ptask_context.hardware_initialized) {
        result = ptask_cleanup_hardware();
        if (result < 0) {
            LOG_WARNING("PTASK: Hardware cleanup failed: %d", result);
        }
    }
    
    /* Free allocated memory */
    if (g_memory_services) {
        ptask_free_allocated_memory();
    }
    
    /* Reset module state */
    g_ptask_context.state = PTASK_STATE_UNLOADED;
    
    LOG_INFO("PTASK: Module cleanup completed");
    return result;
}

/**
 * @brief Detect target hardware for PTASK module
 * 
 * Week 1: Uses NE2000 emulation detection
 * Week 2+: Real 3C509B/3C589 detection
 * 
 * @return Positive value = detected hardware type, negative = error
 */
static int ptask_detect_target_hardware(void) {
    int hardware_type = PTASK_HARDWARE_UNKNOWN;
    
    if (g_use_ne2000_compat) {
        /* Week 1: NE2000 compatibility detection */
        hardware_type = ptask_detect_ne2000();
        if (hardware_type > 0) {
            g_ptask_context.hardware_type = PTASK_HARDWARE_NE2000_COMPAT;
            g_ptask_context.io_base = 0x300;
            g_ptask_context.irq = 3;
            LOG_INFO("PTASK: Detected NE2000 compatibility hardware");
            return hardware_type;
        }
    } else {
        /* Week 2+: Real hardware detection */
        
        /* Try 3C509B ISA PnP detection */
        hardware_type = ptask_detect_3c509b();
        if (hardware_type > 0) {
            g_ptask_context.hardware_type = PTASK_HARDWARE_3C509B;
            LOG_INFO("PTASK: Detected 3C509B ISA hardware");
            return hardware_type;
        }
        
        /* Try 3C589 PCMCIA detection */
        hardware_type = ptask_detect_3c589();
        if (hardware_type > 0) {
            g_ptask_context.hardware_type = PTASK_HARDWARE_3C589;
            LOG_INFO("PTASK: Detected 3C589 PCMCIA hardware");
            return hardware_type;
        }
    }
    
    LOG_ERROR("PTASK: No supported hardware detected");
    return ERROR_HARDWARE_NOT_FOUND;
}

/**
 * @brief Setup shared PIO library with CPU optimization
 * 
 * Initializes CPU-optimized I/O routines based on detected processor.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
static int ptask_setup_shared_pio(void) {
    int result;
    
    /* Use global CPU info from main driver initialization */
    extern cpu_info_t g_cpu_info;
    
    /* Verify CPU detection was completed */
    if (g_cpu_info.type == CPU_TYPE_UNKNOWN) {
        LOG_ERROR("PTASK: Global CPU detection not completed - initialization failed");
        return ERROR_GENERIC;
    }
    
    /* Store CPU info in context for backward compatibility */
    g_ptask_context.cpu_type = g_cpu_info.type;
    g_ptask_context.cpu_features = (uint16_t)g_cpu_info.features;
    
    /* Initialize shared PIO library */
    result = pio_lib_init(&g_cpu_info);
    if (result < 0) {
        LOG_ERROR("PTASK: PIO library initialization failed: %d", result);
        return result;
    }
    
    LOG_DEBUG("PTASK: Shared PIO library initialized for CPU type %04X", 
              g_cpu_info.type);
    return SUCCESS;
}

/**
 * @brief Apply CPU-specific optimizations using self-modifying code
 * 
 * Patches critical path code with CPU-optimized instruction sequences.
 * Uses interrupt-safe self-modification with prefetch flush.
 */
static void ptask_apply_cpu_optimizations(void) {
    /* Apply optimization patches based on CPU type */
    extern cpu_info_t g_cpu_info;
    
    switch (g_cpu_info.type) {
        case CPU_TYPE_80286:
            /* 80286 optimizations - basic 16-bit operations */
            ptask_patch_286_optimizations();
            break;
            
        case CPU_TYPE_80386:
            /* 80386 optimizations - 32-bit operations, better addressing */
            ptask_patch_386_optimizations();
            break;
            
        case CPU_TYPE_80486:
            /* 80486 optimizations - cache-friendly code, burst transfers */
            ptask_patch_486_optimizations();
            break;
            
        case CPU_TYPE_PENTIUM:
            /* Pentium optimizations - pipeline scheduling, dual execution */
            ptask_patch_pentium_optimizations();
            break;
            
        default:
            LOG_WARNING("PTASK: Unknown CPU type %04X, using basic optimizations",
                        g_cpu_info.type);
            ptask_patch_286_optimizations();
            break;
    }
    
    /* Flush prefetch queue after self-modification */
    flush_prefetch_queue();
    
    LOG_DEBUG("PTASK: CPU-specific optimizations applied");
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
    
    /* Initialize memory pools for packet buffers */
    return ptask_init_memory_pools();
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
 * @brief Initialize Week 1 NE2000 compatibility mode
 * 
 * Sets up NE2000 emulation interface for QEMU testing.
 * This code is in the cold section and discarded after init.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
static int ptask_init_ne2000_compat(void) {
    ne2000_config_t config;
    int result;
    
    LOG_DEBUG("PTASK: Initializing NE2000 compatibility mode");
    
    /* Configure NE2000 parameters */
    config.io_base = 0x300;
    config.interrupt_line = 3;
    
    /* Read MAC address from emulated EEPROM */
    result = ne2000_read_mac_address(config.mac_address);
    if (result < 0) {
        LOG_ERROR("PTASK: Failed to read NE2000 MAC address: %d", result);
        return result;
    }
    
    /* Initialize NE2000 hardware abstraction */
    result = ne2000_init_hardware(&config);
    if (result < 0) {
        LOG_ERROR("PTASK: NE2000 hardware init failed: %d", result);
        return result;
    }
    
    /* Store configuration in context */
    g_ptask_context.io_base = config.io_base;
    g_ptask_context.irq = config.interrupt_line;
    memcpy(g_ptask_context.mac_address, config.mac_address, 6);
    
    LOG_INFO("PTASK: NE2000 compatibility initialized at I/O 0x%X, IRQ %d",
             config.io_base, config.interrupt_line);
    
    return SUCCESS;
}

/**
 * @brief Initialize memory pools for packet buffers
 * 
 * Creates DMA-safe buffer pools using memory management API.
 * This code is in the cold section and discarded after init.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
static int ptask_init_memory_pools(void) {
    buffer_pool_config_t pool_config;
    int result;
    
    if (!g_memory_services) {
        LOG_ERROR("PTASK: Memory services not available");
        return ERROR_DEPENDENCY_NOT_MET;
    }
    
    /* Configure buffer pools for packet I/O */
    pool_config.small_buffer_size = 256;    /* Small packets */
    pool_config.large_buffer_size = 1600;   /* Large packets (MTU + headers) */
    pool_config.small_buffer_count = 8;     /* 8 small buffers */
    pool_config.large_buffer_count = 4;     /* 4 large buffers */
    pool_config.memory_type = MEMORY_TYPE_BUFFER | MEMORY_TYPE_DMA_COHERENT;
    pool_config.alignment = 16;              /* 16-byte alignment for DMA safety */
    
    /* Allocate buffer pools using memory services */
    result = ptask_create_buffer_pools(&pool_config);
    if (result < 0) {
        LOG_ERROR("PTASK: Buffer pool creation failed: %d", result);
        return result;
    }
    
    LOG_DEBUG("PTASK: Memory pools initialized - %d small, %d large buffers",
              pool_config.small_buffer_count, pool_config.large_buffer_count);
    
    return SUCCESS;
}

#pragma code_seg()  /* Return to default code segment */