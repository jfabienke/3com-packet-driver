/**
 * @file core_loader.c
 * @brief Core Loader Framework Implementation for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 1 Day 2-3
 * 
 * This file implements the main core loader control logic that orchestrates
 * the entire modular packet driver system. ~30KB resident component.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "core_loader.h"
#include "module_manager.h"
#include "memory_manager.h"
#include "packet_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

/* Global core loader instance */
static core_loader_t g_core_loader;
static bool g_core_initialized = false;

/* Default configuration values */
static const core_config_t default_config = {
    .debug_mode = false,
    .verbose_logging = false,
    .auto_detect_nics = true,
    .load_all_features = false,
    .max_memory_usage = (256 * 1024),  /* 256KB default */
    .max_modules = 16,
    .max_nics = 8,
    .module_path = "",  /* Set by command line */
    .config_file = "3CPD.CFG",
    .log_file = "3CPD.LOG",
    .buffer_pool_size = 32,
    .interrupt_coalescing = 0,
    .enable_flow_control = false,
    .enabled_features = 0xFFFF,
    .disabled_features = 0
};

/* Command line option handlers */
static void handle_io_option(core_loader_t* core, const char* value);
static void handle_irq_option(core_loader_t* core, const char* value);
static void handle_debug_option(core_loader_t* core, const char* value);
static void handle_module_path_option(core_loader_t* core, const char* value);
static void handle_config_option(core_loader_t* core, const char* value);
static void handle_memory_option(core_loader_t* core, const char* value);

/* Command line options table */
static const command_option_t command_options[] = {
    {"IO1", "I/O base address for NIC 1", true, false, handle_io_option},
    {"IO2", "I/O base address for NIC 2", true, false, handle_io_option},
    {"IRQ1", "IRQ number for NIC 1", true, false, handle_irq_option},
    {"IRQ2", "IRQ number for NIC 2", true, false, handle_irq_option},
    {"DEBUG", "Enable debug mode", false, false, handle_debug_option},
    {"VERBOSE", "Enable verbose logging", false, false, handle_debug_option},
    {"MODPATH", "Module search path", true, false, handle_module_path_option},
    {"CONFIG", "Configuration file", true, false, handle_config_option},
    {"MAXMEM", "Maximum memory usage (KB)", true, false, handle_memory_option},
    {NULL, NULL, false, false, NULL}  /* Terminator */
};

/* Forward declarations */
static bool initialize_subsystems(core_loader_t* core);
static void shutdown_subsystems(core_loader_t* core);
static bool setup_packet_driver_interface(core_loader_t* core);
static void cleanup_packet_driver_interface(core_loader_t* core);
static void log_initialization_info(core_loader_t* core);

/* ============================================================================
 * Core Loader Main Interface Implementation
 * ============================================================================ */

/**
 * @brief Initialize the core loader system
 */
bool core_loader_initialize(core_loader_t* core, int argc, char* argv[])
{
    if (!core) {
        printf("3CPD: Critical error - NULL core loader instance\n");
        return false;
    }
    
    /* Initialize core loader signature and basic state */
    memcpy(core->signature, "3CPDCORE", 8);
    core->version = 0x0100;  /* Version 1.0 */
    core->build_timestamp = __DATE__;  /* Build timestamp */
    core->initialized = false;
    core->shutting_down = false;
    
    /* Initialize configuration with defaults */
    core->config = default_config;
    
    /* Initialize statistics */
    memset(&core->stats, 0, sizeof(core_statistics_t));
    core->stats.start_time = 0;  /* Will be set after timing services init */
    
    /* Process command line arguments */
    if (!parse_command_line(core, argc, argv)) {
        printf("3CPD: Command line parsing failed\n");
        return false;
    }
    
    /* Initialize all subsystems */
    if (!initialize_subsystems(core)) {
        printf("3CPD: Subsystem initialization failed\n");
        shutdown_subsystems(core);
        return false;
    }
    
    /* Set up packet driver interface (INT 60h) */
    if (!setup_packet_driver_interface(core)) {
        printf("3CPD: Packet driver interface setup failed\n");
        shutdown_subsystems(core);
        return false;
    }
    
    /* Set initialization timestamp */
    core->initialization_time = core->core_services.timing.get_ticks();
    core->stats.start_time = core->initialization_time;
    
    /* Mark as initialized */
    core->initialized = true;
    g_core_initialized = true;
    
    /* Log initialization information */
    log_initialization_info(core);
    
    if (core->config.verbose_logging) {
        printf("3CPD: Core loader initialized successfully\n");
        printf("3CPD: Version %d.%d, Build %lu\n", 
               (core->version >> 8) & 0xFF, core->version & 0xFF,
               core->build_timestamp);
        printf("3CPD: Memory usage: %zu bytes\n", core->stats.memory_allocated);
    }
    
    return true;
}

/**
 * @brief Shutdown the core loader system
 */
void core_loader_shutdown(core_loader_t* core)
{
    if (!core || !core->initialized) {
        return;
    }
    
    core->shutting_down = true;
    
    if (core->config.verbose_logging) {
        printf("3CPD: Shutting down core loader...\n");
    }
    
    /* Unload all modules */
    if (core->unload_all_modules) {
        core->unload_all_modules(core);
    }
    
    /* Clean up packet driver interface */
    cleanup_packet_driver_interface(core);
    
    /* Shutdown all subsystems */
    shutdown_subsystems(core);
    
    /* Reset state */
    core->initialized = false;
    g_core_initialized = false;
    
    if (core->config.verbose_logging) {
        printf("3CPD: Core loader shutdown complete\n");
    }
}

/**
 * @brief Get the global core loader instance
 */
core_loader_t* get_core_loader(void)
{
    if (!g_core_initialized) {
        return NULL;
    }
    return &g_core_loader;
}

/* ============================================================================
 * Command Line Processing Implementation
 * ============================================================================ */

/**
 * @brief Parse command line arguments
 */
bool parse_command_line(core_loader_t* core, int argc, char* argv[])
{
    int i;
    
    if (!core) return false;
    
    /* Process each command line argument */
    for (i = 1; i < argc; i++) {
        char* arg = argv[i];
        char* equals_pos;
        char option_name[32];
        char* option_value = NULL;
        const command_option_t* opt;
        bool option_found = false;
        
        /* Skip if not an option */
        if (arg[0] != '/' && arg[0] != '-') {
            continue;
        }
        
        /* Skip the option prefix */
        arg++;
        
        /* Find the equals sign if present */
        equals_pos = strchr(arg, '=');
        if (equals_pos) {
            /* Extract option name and value */
            size_t name_len = equals_pos - arg;
            if (name_len >= sizeof(option_name)) {
                printf("3CPD: Option name too long: %s\n", arg);
                return false;
            }
            strncpy(option_name, arg, name_len);
            option_name[name_len] = '\0';
            option_value = equals_pos + 1;
        } else {
            /* Option without value */
            strcpy(option_name, arg);
        }
        
        /* Convert to uppercase for case-insensitive matching */
        strupr(option_name);
        
        /* Find matching option */
        for (opt = command_options; opt->option; opt++) {
            if (strcmp(option_name, opt->option) == 0) {
                option_found = true;
                
                /* Check if option requires a value */
                if (opt->has_value && !option_value) {
                    printf("3CPD: Option %s requires a value\n", opt->option);
                    return false;
                }
                
                /* Call option handler */
                if (opt->handler) {
                    opt->handler(core, option_value);
                }
                break;
            }
        }
        
        if (!option_found) {
            printf("3CPD: Unknown option: %s\n", option_name);
            display_usage(argv[0]);
            return false;
        }
    }
    
    /* Validate required options */
    for (const command_option_t* opt = command_options; opt->option; opt++) {
        if (opt->required) {
            /* Check if required option was provided */
            /* Implementation depends on how we track option provision */
            /* For now, we assume no options are strictly required */
        }
    }
    
    return true;
}

/**
 * @brief Display usage information
 */
void display_usage(const char* program_name)
{
    const command_option_t* opt;
    
    printf("3Com Packet Driver - Modular Architecture\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    
    for (opt = command_options; opt->option; opt++) {
        if (opt->has_value) {
            printf("  /%s=<value>    %s\n", opt->option, opt->description);
        } else {
            printf("  /%s           %s\n", opt->option, opt->description);
        }
    }
    
    printf("\nExamples:\n");
    printf("  %s /IO1=0x300 /IRQ1=10\n", program_name);
    printf("  %s /DEBUG /MODPATH=C:\\DRIVERS\\MODULES\n", program_name);
    printf("  %s /CONFIG=MYNET.CFG /MAXMEM=512\n", program_name);
}

/* ============================================================================
 * Command Line Option Handlers
 * ============================================================================ */

static void handle_io_option(core_loader_t* core, const char* value)
{
    uint16_t io_base;
    int nic_num;
    
    if (!value) return;
    
    /* Parse I/O address (hex format supported) */
    if (strncmp(value, "0x", 2) == 0 || strncmp(value, "0X", 2) == 0) {
        io_base = (uint16_t)strtol(value, NULL, 16);
    } else {
        io_base = (uint16_t)strtol(value, NULL, 10);
    }
    
    /* Determine NIC number from current parsing context */
    /* This is a simplified implementation - real version would track context */
    nic_num = 0;  /* Default to first NIC */
    
    if (core->config.verbose_logging) {
        printf("3CPD: NIC %d I/O base set to 0x%04X\n", nic_num + 1, io_base);
    }
    
    /* Store in configuration - would be part of NIC-specific config */
}

static void handle_irq_option(core_loader_t* core, const char* value)
{
    uint8_t irq_num;
    int nic_num;
    
    if (!value) return;
    
    irq_num = (uint8_t)atoi(value);
    
    /* Validate IRQ range */
    if (irq_num < 2 || irq_num > 15) {
        printf("3CPD: Invalid IRQ number: %d (must be 2-15)\n", irq_num);
        return;
    }
    
    nic_num = 0;  /* Default to first NIC */
    
    if (core->config.verbose_logging) {
        printf("3CPD: NIC %d IRQ set to %d\n", nic_num + 1, irq_num);
    }
}

static void handle_debug_option(core_loader_t* core, const char* value)
{
    /* Enable debug mode */
    core->config.debug_mode = true;
    core->config.verbose_logging = true;
    
    printf("3CPD: Debug mode enabled\n");
}

static void handle_module_path_option(core_loader_t* core, const char* value)
{
    if (!value) return;
    
    /* Copy module path, ensuring null termination */
    strncpy(core->config.module_path, value, sizeof(core->config.module_path) - 1);
    core->config.module_path[sizeof(core->config.module_path) - 1] = '\0';
    
    if (core->config.verbose_logging) {
        printf("3CPD: Module path set to: %s\n", core->config.module_path);
    }
}

static void handle_config_option(core_loader_t* core, const char* value)
{
    if (!value) return;
    
    /* Copy config file name */
    strncpy(core->config.config_file, value, sizeof(core->config.config_file) - 1);
    core->config.config_file[sizeof(core->config.config_file) - 1] = '\0';
    
    if (core->config.verbose_logging) {
        printf("3CPD: Configuration file set to: %s\n", core->config.config_file);
    }
}

static void handle_memory_option(core_loader_t* core, const char* value)
{
    size_t max_memory_kb;
    
    if (!value) return;
    
    max_memory_kb = strtoul(value, NULL, 10);
    
    /* Convert KB to bytes */
    core->config.max_memory_usage = max_memory_kb * 1024;
    
    if (core->config.verbose_logging) {
        printf("3CPD: Maximum memory usage set to %zu KB\n", max_memory_kb);
    }
}

/* ============================================================================
 * Subsystem Management
 * ============================================================================ */

/**
 * @brief Initialize all core subsystems
 */
static bool initialize_subsystems(core_loader_t* core)
{
    /* Initialize memory management first */
    if (!memory_manager_initialize(&core->memory_services, &core->config)) {
        printf("3CPD: Memory manager initialization failed\n");
        return false;
    }
    
    /* Initialize module registry */
    if (!module_registry_initialize(&core->module_registry, &core->memory_services)) {
        printf("3CPD: Module registry initialization failed\n");
        return false;
    }
    
    /* Initialize NIC manager */
    if (!nic_manager_initialize(&core->nic_manager, &core->memory_services)) {
        printf("3CPD: NIC manager initialization failed\n");
        return false;
    }
    
    /* Initialize core services interface */
    if (!core_services_initialize(&core->core_services, core)) {
        printf("3CPD: Core services initialization failed\n");
        return false;
    }
    
    /* Bind operation function pointers */
    core->initialize = core_loader_initialize;
    core->shutdown = core_loader_shutdown;
    core->process_command_line = parse_command_line;
    core->load_required_modules = load_required_modules;
    core->load_optional_modules = load_optional_modules;
    core->unload_all_modules = unload_all_modules;
    core->detect_and_configure_nics = detect_and_configure_nics;
    core->bind_nics_to_modules = bind_nics_to_modules;
    core->main_loop = core_main_loop;
    core->packet_interrupt_handler = packet_interrupt_handler;
    core->timer_callback = timer_callback;
    core->dump_statistics = dump_statistics;
    core->dump_module_info = dump_module_info;
    core->dump_nic_info = dump_nic_info;
    core->handle_critical_error = handle_critical_error;
    core->emergency_shutdown = emergency_shutdown;
    
    return true;
}

/**
 * @brief Shutdown all core subsystems
 */
static void shutdown_subsystems(core_loader_t* core)
{
    /* Shutdown in reverse order of initialization */
    core_services_shutdown(&core->core_services);
    nic_manager_shutdown(&core->nic_manager);
    module_registry_shutdown(&core->module_registry);
    memory_manager_shutdown(&core->memory_services);
    
    /* Clear operation function pointers */
    memset(&core->initialize, 0, sizeof(void*) * 16);  /* Clear function pointers */
}

/**
 * @brief Set up packet driver interface
 */
static bool setup_packet_driver_interface(core_loader_t* core)
{
    /* Set default interrupt vector (INT 60h) */
    core->packet_driver_interrupt = 0x60;
    
    /* Install packet driver interrupt handler */
    if (!packet_api_install_handler(core->packet_driver_interrupt, core)) {
        printf("3CPD: Failed to install packet driver interrupt handler\n");
        return false;
    }
    
    core->packet_driver_active = true;
    
    if (core->config.verbose_logging) {
        printf("3CPD: Packet driver interface installed at INT %02Xh\n", 
               core->packet_driver_interrupt);
    }
    
    return true;
}

/**
 * @brief Clean up packet driver interface
 */
static void cleanup_packet_driver_interface(core_loader_t* core)
{
    if (core->packet_driver_active) {
        packet_api_remove_handler(core->packet_driver_interrupt);
        core->packet_driver_active = false;
        
        if (core->config.verbose_logging) {
            printf("3CPD: Packet driver interface removed\n");
        }
    }
}

/**
 * @brief Log initialization information
 */
static void log_initialization_info(core_loader_t* core)
{
    memory_stats_t mem_stats;
    
    /* Get memory statistics */
    if (core->memory_services.get_stats(&mem_stats)) {
        core->stats.memory_allocated = mem_stats.current_usage;
        core->stats.peak_memory_usage = mem_stats.peak_usage;
    }
    
    /* Log core information */
    core->core_services.log_message(LOG_LEVEL_INFO, "CORE", 
        "3Com Packet Driver Core Loader v%d.%d initialized",
        (core->version >> 8) & 0xFF, core->version & 0xFF);
    
    core->core_services.log_message(LOG_LEVEL_INFO, "CORE",
        "Memory allocated: %zu bytes, Configuration: %s",
        core->stats.memory_allocated,
        core->config.debug_mode ? "DEBUG" : "RELEASE");
}

/* ============================================================================
 * Core Operation Stubs (Implemented in separate modules)
 * ============================================================================ */

/* These functions are implemented in module_manager.c */
extern int load_required_modules(core_loader_t* core);
extern int load_optional_modules(core_loader_t* core);
extern void unload_all_modules(core_loader_t* core);

/* These functions are implemented in nic_manager.c */
extern int detect_and_configure_nics(core_loader_t* core);
extern bool bind_nics_to_modules(core_loader_t* core);

/* These functions are implemented in packet_api.c */
extern void core_main_loop(core_loader_t* core);
extern void packet_interrupt_handler(void);
extern void timer_callback(void);

/* These functions are implemented in diagnostics.c */
extern void dump_statistics(core_loader_t* core);
extern void dump_module_info(core_loader_t* core);
extern void dump_nic_info(core_loader_t* core);

/* These functions are implemented in error_handler.c */
extern void handle_critical_error(core_loader_t* core, error_type_t type, const char* msg);
extern void emergency_shutdown(core_loader_t* core);