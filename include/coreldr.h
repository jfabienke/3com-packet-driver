/**
 * @file core_loader.h
 * @brief Core Loader Architecture for 3Com Packet Driver
 * 
 * Phase 3A: Dynamic Module Loading - Stream 1 Critical Path
 * 
 * This header defines the core loader architecture that manages the
 * modular packet driver system. The core loader is the ~30KB resident
 * component that orchestrates all module loading and operation.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef CORE_LOADER_H
#define CORE_LOADER_H

#include "modapi.h"
#include "memapi.h"
#include <stdint.h>
#include <stdbool.h>

/* Core loader constants */
#define MAX_LOADED_MODULES 16      /* Maximum simultaneous modules */
#define MAX_NICS_SUPPORTED 8       /* Maximum NICs supported */
#define MAX_MODULE_SEARCH_PATHS 8  /* Maximum module search paths */
#define MODULE_SEARCH_PATH_LENGTH 128 /* Maximum path length */

/* Module loading priorities */
#define LOAD_PRIORITY_CORE     0   /* Core modules (always loaded first) */
#define LOAD_PRIORITY_HARDWARE 1   /* Hardware modules */
#define LOAD_PRIORITY_FEATURE  2   /* Feature modules */
#define LOAD_PRIORITY_OPTIONAL 3   /* Optional modules */

/* Forward declarations */
typedef struct core_loader core_loader_t;
typedef struct module_registry module_registry_t;
typedef struct nic_manager nic_manager_t;

/* ============================================================================
 * Core Services Interface
 * ============================================================================ */

/**
 * @brief Logging levels for diagnostics
 */
typedef enum {
    LOG_LEVEL_FATAL   = 0,  /**< Fatal errors (system unusable) */
    LOG_LEVEL_ERROR   = 1,  /**< Error conditions */
    LOG_LEVEL_WARNING = 2,  /**< Warning conditions */
    LOG_LEVEL_INFO    = 3,  /**< Informational messages */
    LOG_LEVEL_DEBUG   = 4,  /**< Debug-level messages */
    LOG_LEVEL_TRACE   = 5   /**< Trace-level messages */
} log_level_t;

/**
 * @brief Error types for error reporting
 */
typedef enum {
    ERROR_TYPE_HARDWARE   = 0x01,  /**< Hardware-related error */
    ERROR_TYPE_MEMORY     = 0x02,  /**< Memory-related error */
    ERROR_TYPE_NETWORK    = 0x03,  /**< Network-related error */
    ERROR_TYPE_MODULE     = 0x04,  /**< Module-related error */
    ERROR_TYPE_CONFIG     = 0x05,  /**< Configuration error */
    ERROR_TYPE_TIMEOUT    = 0x06,  /**< Timeout error */
    ERROR_TYPE_PROTOCOL   = 0x07,  /**< Protocol error */
    ERROR_TYPE_SYSTEM     = 0x08   /**< System error */
} error_type_t;

/**
 * @brief Hardware access functions for modules
 */
typedef struct {
    /* Port I/O functions */
    uint8_t  (*inb)(uint16_t port);
    uint16_t (*inw)(uint16_t port);
    uint32_t (*inl)(uint16_t port);
    void     (*outb)(uint16_t port, uint8_t value);
    void     (*outw)(uint16_t port, uint16_t value);
    void     (*outl)(uint16_t port, uint32_t value);
    
    /* String I/O functions */
    void (*insb)(uint16_t port, void* buffer, uint16_t count);
    void (*insw)(uint16_t port, void* buffer, uint16_t count);
    void (*insl)(uint16_t port, void* buffer, uint16_t count);
    void (*outsb)(uint16_t port, const void* buffer, uint16_t count);
    void (*outsw)(uint16_t port, const void* buffer, uint16_t count);
    void (*outsl)(uint16_t port, const void* buffer, uint16_t count);
    
    /* Memory mapping (for PCI devices) */
    void* (*map_memory)(uint32_t physical_addr, size_t size);
    bool  (*unmap_memory)(void* virtual_addr, size_t size);
} hardware_access_t;

/**
 * @brief Interrupt management functions
 */
typedef struct {
    /* Interrupt installation */
    bool (*install_handler)(uint8_t irq, void (*handler)(void), uint8_t nic_id);
    bool (*remove_handler)(uint8_t irq, uint8_t nic_id);
    
    /* Interrupt control */
    bool (*enable_irq)(uint8_t irq);
    bool (*disable_irq)(uint8_t irq);
    bool (*acknowledge_irq)(uint8_t irq);
    
    /* Interrupt sharing */
    bool (*share_irq)(uint8_t irq, uint8_t primary_nic, uint8_t secondary_nic);
    bool (*unshare_irq)(uint8_t irq, uint8_t nic_id);
} interrupt_services_t;

/**
 * @brief Timer and timing services
 */
typedef struct {
    /* High-resolution timing */
    uint32_t (*get_ticks)(void);        /**< Get system ticks */
    uint32_t (*get_milliseconds)(void); /**< Get milliseconds */
    uint64_t (*get_microseconds)(void); /**< Get microseconds (if available) */
    
    /* Delay functions */
    void (*delay_ms)(uint16_t milliseconds);
    void (*delay_us)(uint16_t microseconds);
    
    /* Timer management */
    bool (*install_timer)(uint16_t interval_ms, void (*callback)(void), uint8_t timer_id);
    bool (*remove_timer)(uint8_t timer_id);
} timing_services_t;

/**
 * @brief Configuration management services
 */
typedef struct {
    /* Configuration access */
    bool (*get_config_string)(const char* section, const char* key, char* buffer, size_t size);
    bool (*get_config_int)(const char* section, const char* key, int* value);
    bool (*get_config_bool)(const char* section, const char* key, bool* value);
    
    /* Configuration modification */
    bool (*set_config_string)(const char* section, const char* key, const char* value);
    bool (*set_config_int)(const char* section, const char* key, int value);
    bool (*set_config_bool)(const char* section, const char* key, bool value);
    
    /* Configuration persistence */
    bool (*save_config)(void);
    bool (*reload_config)(void);
} config_services_t;

/**
 * @brief Complete core services interface for modules
 * 
 * This is the main interface that modules receive from the core loader.
 * It provides access to all system services in a controlled manner.
 */
typedef struct core_services {
    /* Memory management */
    memory_services_t memory;
    
    /* Hardware access */
    hardware_access_t hardware;
    
    /* Interrupt management */
    interrupt_services_t interrupts;
    
    /* Timing services */
    timing_services_t timing;
    
    /* Configuration management */
    config_services_t config;
    
    /* Logging and diagnostics */
    void (*log_message)(log_level_t level, const char* module, const char* format, ...);
    void (*record_error)(error_type_t type, uint8_t severity, const char* description);
    
    /* Module management (for feature modules) */
    bool (*register_apis)(const char* module_name, const api_registration_t* apis);
    bool (*unregister_apis)(const char* module_name);
    void* (*get_api)(const char* module_name, const char* api_name);
    
    /* Packet driver integration */
    bool (*register_packet_handler)(uint16_t packet_type, void (*handler)(packet_t*));
    bool (*unregister_packet_handler)(uint16_t packet_type);
    bool (*send_packet)(uint8_t nic_id, const packet_t* packet);
    
    /* System information */
    uint16_t (*get_dos_version)(void);
    uint16_t (*get_cpu_type)(void);
    const char* (*get_driver_version)(void);
} core_services_t;

/* ============================================================================
 * Module Registry System
 * ============================================================================ */

/**
 * @brief Module state enumeration
 */
typedef enum {
    MODULE_STATE_UNLOADED  = 0,  /**< Module not loaded */
    MODULE_STATE_LOADING   = 1,  /**< Module being loaded */
    MODULE_STATE_LOADED    = 2,  /**< Module loaded successfully */
    MODULE_STATE_ACTIVE    = 3,  /**< Module active and operational */
    MODULE_STATE_ERROR     = 4,  /**< Module in error state */
    MODULE_STATE_UNLOADING = 5   /**< Module being unloaded */
} module_state_t;

/**
 * @brief Loaded module information
 */
typedef struct {
    /* Module identification */
    char             name[MODULE_NAME_LENGTH];
    char             filename[64];
    module_header_t* header;
    
    /* Module state */
    module_state_t   state;
    uint32_t         load_time;
    uint32_t         last_activity;
    
    /* Memory management */
    void*            base_address;
    size_t           memory_size;
    memory_type_t    memory_type;
    
    /* Module interface */
    void*            init_function;
    void*            cleanup_function;
    void*            vtable;  /* For hardware modules */
    
    /* Usage statistics */
    uint32_t         call_count;
    uint32_t         error_count;
    uint32_t         last_error_time;
    
    /* Dependencies */
    uint8_t          dependency_count;
    uint8_t          dependent_modules[8];  /* Modules that depend on this one */
} loaded_module_t;

/**
 * @brief Module registry structure
 */
typedef struct module_registry {
    loaded_module_t  modules[MAX_LOADED_MODULES];
    uint8_t          module_count;
    uint8_t          next_module_id;
    
    /* Search paths */
    char             search_paths[MAX_MODULE_SEARCH_PATHS][MODULE_SEARCH_PATH_LENGTH];
    uint8_t          search_path_count;
    
    /* Loading statistics */
    uint32_t         total_loads;
    uint32_t         total_unloads;
    uint32_t         load_failures;
    
    /* Module operations */
    int (*load_module)(module_registry_t* registry, const char* module_name, bool required);
    bool (*unload_module)(module_registry_t* registry, uint8_t module_id);
    loaded_module_t* (*find_module)(module_registry_t* registry, const char* name);
    bool (*verify_dependencies)(module_registry_t* registry, const module_header_t* header);
} module_registry_t;

/* ============================================================================
 * NIC Management System
 * ============================================================================ */

/**
 * @brief NIC operational state
 */
typedef enum {
    NIC_STATE_UNINITIALIZED = 0,  /**< NIC not initialized */
    NIC_STATE_DETECTED       = 1,  /**< NIC detected but not configured */
    NIC_STATE_CONFIGURING    = 2,  /**< NIC being configured */
    NIC_STATE_READY          = 3,  /**< NIC ready for operation */
    NIC_STATE_ACTIVE         = 4,  /**< NIC actively transmitting/receiving */
    NIC_STATE_ERROR          = 5,  /**< NIC in error state */
    NIC_STATE_DISABLED       = 6   /**< NIC disabled */
} nic_state_t;

/**
 * @brief NIC context information
 */
typedef struct {
    /* Hardware information */
    uint8_t          nic_id;
    hardware_info_t  hw_info;
    nic_state_t      state;
    
    /* Module binding */
    uint8_t          module_id;      /* Associated hardware module */
    nic_ops_t*       operations;     /* Operations vtable */
    
    /* Configuration */
    char             config_name[32];
    uint16_t         mtu;
    uint8_t          mac_address[6];
    
    /* Statistics */
    nic_stats_t      stats;
    uint32_t         last_activity;
    uint32_t         error_count;
    
    /* Buffer management */
    uint8_t          tx_buffers_used;
    uint8_t          rx_buffers_used;
    uint16_t         max_tx_buffers;
    uint16_t         max_rx_buffers;
} nic_context_t;

/**
 * @brief NIC manager structure
 */
typedef struct nic_manager {
    nic_context_t    nics[MAX_NICS_SUPPORTED];
    uint8_t          nic_count;
    uint8_t          active_nics;
    
    /* Hardware detection */
    uint8_t (*detect_nics)(nic_manager_t* mgr);
    bool (*configure_nic)(nic_manager_t* mgr, uint8_t nic_id);
    
    /* Module binding */
    bool (*bind_module)(nic_manager_t* mgr, uint8_t nic_id, const char* module_name);
    bool (*unbind_module)(nic_manager_t* mgr, uint8_t nic_id);
    
    /* NIC operations */
    bool (*start_nic)(nic_manager_t* mgr, uint8_t nic_id);
    bool (*stop_nic)(nic_manager_t* mgr, uint8_t nic_id);
    bool (*reset_nic)(nic_manager_t* mgr, uint8_t nic_id);
    
    /* Statistics and monitoring */
    bool (*get_nic_stats)(nic_manager_t* mgr, uint8_t nic_id, nic_stats_t* stats);
    bool (*reset_nic_stats)(nic_manager_t* mgr, uint8_t nic_id);
} nic_manager_t;

/* ============================================================================
 * Core Loader Main Structure
 * ============================================================================ */

/**
 * @brief Core loader configuration
 */
typedef struct {
    /* Command line options */
    bool             debug_mode;
    bool             verbose_logging;
    bool             auto_detect_nics;
    bool             load_all_features;
    
    /* Resource limits */
    size_t           max_memory_usage;
    uint16_t         max_modules;
    uint16_t         max_nics;
    
    /* Paths and files */
    char             module_path[MODULE_SEARCH_PATH_LENGTH];
    char             config_file[64];
    char             log_file[64];
    
    /* Performance tuning */
    uint16_t         buffer_pool_size;
    uint16_t         interrupt_coalescing;
    bool             enable_flow_control;
    
    /* Feature flags */
    uint32_t         enabled_features;
    uint32_t         disabled_features;
} core_config_t;

/**
 * @brief Core loader runtime statistics
 */
typedef struct {
    /* Uptime and activity */
    uint32_t         start_time;
    uint32_t         packets_processed;
    uint32_t         interrupts_handled;
    
    /* Module statistics */
    uint8_t          modules_loaded;
    uint8_t          modules_active;
    uint32_t         module_load_time;
    
    /* Memory usage */
    size_t           memory_allocated;
    size_t           peak_memory_usage;
    uint16_t         memory_fragmentation;
    
    /* Error tracking */
    uint32_t         total_errors;
    uint32_t         critical_errors;
    uint32_t         last_error_time;
    
    /* Performance metrics */
    uint32_t         avg_packet_processing_time;
    uint32_t         max_packet_processing_time;
    uint16_t         cpu_utilization_percent;
} core_statistics_t;

/**
 * @brief Main core loader structure
 * 
 * This is the central control structure for the entire modular driver system.
 * It coordinates module loading, NIC management, and provides services.
 */
typedef struct core_loader {
    /* Core identification */
    char             signature[8];      /**< "3CPDCORE" signature */
    uint16_t         version;           /**< Core loader version */
    uint32_t         build_timestamp;   /**< Build timestamp */
    
    /* Configuration and state */
    core_config_t    config;
    core_statistics_t stats;
    uint32_t         initialization_time;
    bool             initialized;
    bool             shutting_down;
    
    /* Sub-systems */
    module_registry_t module_registry;
    nic_manager_t     nic_manager;
    memory_services_t memory_services;
    core_services_t   core_services;
    
    /* Packet driver interface */
    bool             packet_driver_active;
    uint8_t          packet_driver_interrupt; /**< INT 60h by default */
    void*            original_interrupt_handler;
    
    /* Core operations */
    bool (*initialize)(core_loader_t* core, int argc, char* argv[]);
    void (*shutdown)(core_loader_t* core);
    bool (*process_command_line)(core_loader_t* core, int argc, char* argv[]);
    
    /* Module management operations */
    int  (*load_required_modules)(core_loader_t* core);
    int  (*load_optional_modules)(core_loader_t* core);
    void (*unload_all_modules)(core_loader_t* core);
    
    /* NIC management operations */
    int  (*detect_and_configure_nics)(core_loader_t* core);
    bool (*bind_nics_to_modules)(core_loader_t* core);
    
    /* Runtime operations */
    void (*main_loop)(core_loader_t* core);
    void (*packet_interrupt_handler)(void);
    void (*timer_callback)(void);
    
    /* Diagnostics and monitoring */
    void (*dump_statistics)(core_loader_t* core);
    void (*dump_module_info)(core_loader_t* core);
    void (*dump_nic_info)(core_loader_t* core);
    
    /* Error handling */
    void (*handle_critical_error)(core_loader_t* core, error_type_t type, const char* msg);
    void (*emergency_shutdown)(core_loader_t* core);
} core_loader_t;

/* ============================================================================
 * Command Line Processing
 * ============================================================================ */

/**
 * @brief Command line option structure
 */
typedef struct {
    const char* option;      /**< Option name (e.g., "IO1", "IRQ1") */
    const char* description; /**< Option description */
    bool        has_value;   /**< Option takes a value */
    bool        required;    /**< Option is required */
    void (*handler)(core_loader_t* core, const char* value);
} command_option_t;

/**
 * @brief Parse command line arguments
 * 
 * @param core Core loader instance
 * @param argc Argument count
 * @param argv Argument vector
 * @return true on success, false on error
 */
bool parse_command_line(core_loader_t* core, int argc, char* argv[]);

/**
 * @brief Display usage information
 * 
 * @param program_name Program name for usage display
 */
void display_usage(const char* program_name);

/* ============================================================================
 * Initialization and Shutdown
 * ============================================================================ */

/**
 * @brief Initialize the core loader system
 * 
 * @param core Core loader instance
 * @param argc Command line argument count
 * @param argv Command line arguments
 * @return true on success, false on failure
 */
bool core_loader_initialize(core_loader_t* core, int argc, char* argv[]);

/**
 * @brief Shutdown the core loader system
 * 
 * @param core Core loader instance
 */
void core_loader_shutdown(core_loader_t* core);

/**
 * @brief Get the global core loader instance
 * 
 * @return Pointer to global core loader
 */
core_loader_t* get_core_loader(void);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Convert module state to string
 * 
 * @param state Module state
 * @return Human-readable state string
 */
static inline const char* module_state_string(module_state_t state) {
    switch (state) {
        case MODULE_STATE_UNLOADED:  return "Unloaded";
        case MODULE_STATE_LOADING:   return "Loading";
        case MODULE_STATE_LOADED:    return "Loaded";
        case MODULE_STATE_ACTIVE:    return "Active";
        case MODULE_STATE_ERROR:     return "Error";
        case MODULE_STATE_UNLOADING: return "Unloading";
        default:                     return "Unknown";
    }
}

/**
 * @brief Convert NIC state to string
 * 
 * @param state NIC state
 * @return Human-readable state string
 */
static inline const char* nic_state_string(nic_state_t state) {
    switch (state) {
        case NIC_STATE_UNINITIALIZED: return "Uninitialized";
        case NIC_STATE_DETECTED:      return "Detected";
        case NIC_STATE_CONFIGURING:   return "Configuring";
        case NIC_STATE_READY:         return "Ready";
        case NIC_STATE_ACTIVE:        return "Active";
        case NIC_STATE_ERROR:         return "Error";
        case NIC_STATE_DISABLED:      return "Disabled";
        default:                      return "Unknown";
    }
}

/**
 * @brief Check if core loader is properly initialized
 * 
 * @param core Core loader instance
 * @return true if initialized, false otherwise
 */
static inline bool core_loader_is_initialized(const core_loader_t* core) {
    return core && core->initialized && !core->shutting_down;
}

/**
 * @brief Get uptime in seconds
 * 
 * @param core Core loader instance
 * @return Uptime in seconds
 */
static inline uint32_t core_loader_uptime(const core_loader_t* core) {
    if (!core || !core->initialized) return 0;
    return (core->core_services.timing.get_ticks() - core->stats.start_time) / 18; /* 18.2 ticks per second */
}

#endif /* CORE_LOADER_H */