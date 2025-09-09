/**
 * @file module_dispatch.c  
 * @brief Multi-Module Dispatch System - Agent 12 Implementation
 *
 * 3Com Packet Driver - Multi-Module Dispatch for PTASK/CORKSCRW/BOOMTEX
 * Implements intelligent routing and load balancing between different
 * NIC driver modules with unified coordination and statistics.
 * 
 * Agent 12: Driver API
 * Week 1 Deliverable - Multi-module dispatch system
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "unified_api.h"
#include "metrics_core.h"
#include "../include/logging.h"
#include "../include/hardware.h"
#include "../modules/common/module_bridge.h"
#include "../../docs/agents/shared/error-codes.h"

/* Module Dispatch Constants */
#define MAX_DISPATCH_MODULES        8
#define MODULE_NAME_LENGTH         12
#define DISPATCH_SIGNATURE         "MDSP"
#define DISPATCH_VERSION           0x0100

/* Module Load Balancing Strategies */
#define LOAD_BALANCE_ROUND_ROBIN   0
#define LOAD_BALANCE_WEIGHTED      1  
#define LOAD_BALANCE_PERFORMANCE   2
#define LOAD_BALANCE_CAPABILITY    3
#define LOAD_BALANCE_ADAPTIVE      4

/* Module Capability Flags */
#define MODULE_CAP_BASIC_ETHERNET   0x0001
#define MODULE_CAP_FAST_ETHERNET    0x0002  
#define MODULE_CAP_FULL_DUPLEX      0x0004
#define MODULE_CAP_PROMISCUOUS      0x0008
#define MODULE_CAP_MULTICAST        0x0010
#define MODULE_CAP_HARDWARE_CSUM    0x0020
#define MODULE_CAP_DMA_CAPABLE      0x0040
#define MODULE_CAP_INTERRUPT_COAL   0x0080

/* Module State */
typedef enum {
    MODULE_STATE_UNLOADED = 0,
    MODULE_STATE_LOADING,
    MODULE_STATE_ACTIVE, 
    MODULE_STATE_ERROR,
    MODULE_STATE_DEGRADED,
    MODULE_STATE_UNLOADING
} module_state_t;

/* Dispatch Entry Structure */
typedef struct {
    char module_name[MODULE_NAME_LENGTH];
    uint8_t module_id;
    module_state_t state;
    uint16_t capabilities;
    uint16_t base_segment;
    
    /* Function Pointers */
    module_function_table_t functions;
    
    /* Load Balancing Metrics */
    uint32_t packets_processed;
    uint32_t bytes_processed;
    uint32_t processing_time;
    uint32_t error_count;
    uint32_t last_activity_time;
    
    /* Performance Metrics */
    uint32_t avg_processing_time;
    uint32_t peak_processing_time;
    uint32_t load_percentage;
    uint32_t success_rate;
    
    /* Module Configuration */
    uint8_t priority;
    uint8_t weight;
    bool enabled;
    bool preferred;
    
} dispatch_entry_t;

/* Dispatch Manager State */
typedef struct {
    char signature[4];
    uint16_t version;
    uint8_t active_modules;
    uint8_t load_balance_strategy;
    uint32_t total_dispatches;
    uint32_t dispatch_errors;
    uint8_t last_selected_module;
    dispatch_entry_t modules[MAX_DISPATCH_MODULES];
} dispatch_manager_t;

/* Global Dispatch Manager */
static dispatch_manager_t g_dispatch_manager;
static bool dispatch_initialized = false;

/* Forward Declarations */
static int select_module_for_packet(const void *packet, uint16_t packet_type, uint8_t *selected_module);
static int select_module_round_robin(uint8_t *selected_module);
static int select_module_weighted(uint8_t *selected_module);
static int select_module_performance(uint8_t *selected_module);
static int select_module_capability(uint16_t packet_type, uint8_t *selected_module);
static int select_module_adaptive(const void *packet, uint16_t packet_type, uint8_t *selected_module);
static int validate_module_id(uint8_t module_id);
static void update_module_metrics(uint8_t module_id, uint32_t processing_time, bool success);
static uint32_t calculate_module_load(uint8_t module_id);

/**
 * @brief Initialize Multi-Module Dispatch System
 * @return SUCCESS on success, error code on failure
 */
int dispatch_init(void) {
    if (dispatch_initialized) {
        return SUCCESS;
    }
    
    log_info("Initializing Multi-Module Dispatch System");
    
    /* Initialize dispatch manager */
    memset(&g_dispatch_manager, 0, sizeof(dispatch_manager_t));
    strncpy(g_dispatch_manager.signature, DISPATCH_SIGNATURE, 4);
    g_dispatch_manager.version = DISPATCH_VERSION;
    g_dispatch_manager.active_modules = 0;
    g_dispatch_manager.load_balance_strategy = LOAD_BALANCE_ADAPTIVE;
    g_dispatch_manager.last_selected_module = 0;
    
    /* Initialize all module entries */
    for (int i = 0; i < MAX_DISPATCH_MODULES; i++) {
        dispatch_entry_t *module = &g_dispatch_manager.modules[i];
        module->module_id = i;
        module->state = MODULE_STATE_UNLOADED;
        module->enabled = false;
        module->priority = 128; /* Default priority */
        module->weight = 100;   /* Default weight */
    }
    
    dispatch_initialized = true;
    log_info("Multi-Module Dispatch System initialized");
    
    return SUCCESS;
}

/**
 * @brief Cleanup Multi-Module Dispatch System
 * @return SUCCESS on success, error code on failure
 */
int dispatch_cleanup(void) {
    if (!dispatch_initialized) {
        return SUCCESS;
    }
    
    log_info("Cleaning up Multi-Module Dispatch System");
    
    /* Unload all active modules */
    for (int i = 0; i < MAX_DISPATCH_MODULES; i++) {
        if (g_dispatch_manager.modules[i].state == MODULE_STATE_ACTIVE) {
            dispatch_unregister_module(i);
        }
    }
    
    dispatch_initialized = false;
    log_info("Multi-Module Dispatch System cleanup completed");
    
    return SUCCESS;
}

/**
 * @brief Register a module with the dispatch system
 * @param module_id Module identifier
 * @param module_name Module name
 * @param capabilities Module capability flags
 * @param functions Function table
 * @return SUCCESS on success, error code on failure
 */
int dispatch_register_module(uint8_t module_id, const char *module_name, 
                            uint16_t capabilities, const module_function_table_t *functions) {
    
    if (!dispatch_initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (!validate_module_id(module_id)) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!module_name || !functions) {
        return ERROR_INVALID_PARAM;
    }
    
    dispatch_entry_t *module = &g_dispatch_manager.modules[module_id];
    
    /* Check if module is already registered */
    if (module->state != MODULE_STATE_UNLOADED) {
        log_warning("Module %d (%s) already registered", module_id, module_name);
        return ERROR_ALREADY_EXISTS;
    }
    
    log_info("Registering module: %s (ID=%d, caps=0x%04X)", module_name, module_id, capabilities);
    
    /* Initialize module entry */
    strncpy(module->module_name, module_name, MODULE_NAME_LENGTH - 1);
    module->module_name[MODULE_NAME_LENGTH - 1] = '\0';
    module->state = MODULE_STATE_LOADING;
    module->capabilities = capabilities;
    
    /* Copy function table */
    memcpy(&module->functions, functions, sizeof(module_function_table_t));
    
    /* Initialize metrics */
    module->packets_processed = 0;
    module->bytes_processed = 0;
    module->processing_time = 0;
    module->error_count = 0;
    module->avg_processing_time = 0;
    module->peak_processing_time = 0;
    module->load_percentage = 0;
    module->success_rate = 100; /* Start with 100% success rate */
    
    /* Call module initialization function */
    if (module->functions.init_func) {
        /* Create module configuration context */
        module_init_context_t *context = module_get_context_from_detection(module->id, 0);
        int result;
        
        if (context) {
            log_debug("Initializing module %s with centralized detection context", module_name);
            result = module->functions.init_func(context);
        } else {
            log_debug("Initializing module %s without detection context", module_name);
            result = module->functions.init_func(NULL);
        }
        
        if (result != SUCCESS) {
            log_error("Module %s initialization failed: %d", module_name, result);
            module->state = MODULE_STATE_ERROR;
            return result;
        }
    }
    
    /* Mark module as active */
    module->state = MODULE_STATE_ACTIVE;
    module->enabled = true;
    g_dispatch_manager.active_modules++;
    
    log_info("Module %s registered successfully (active modules: %d)", 
             module_name, g_dispatch_manager.active_modules);
    
    return SUCCESS;
}

/**
 * @brief Unregister a module from the dispatch system
 * @param module_id Module identifier  
 * @return SUCCESS on success, error code on failure
 */
int dispatch_unregister_module(uint8_t module_id) {
    if (!dispatch_initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (!validate_module_id(module_id)) {
        return ERROR_INVALID_PARAM;
    }
    
    dispatch_entry_t *module = &g_dispatch_manager.modules[module_id];
    
    if (module->state == MODULE_STATE_UNLOADED) {
        return ERROR_NOT_FOUND;
    }
    
    log_info("Unregistering module: %s (ID=%d)", module->module_name, module_id);
    
    /* Mark as unloading */
    module->state = MODULE_STATE_UNLOADING;
    
    /* Call module cleanup function */
    if (module->functions.cleanup_func) {
        module->functions.cleanup_func();
    }
    
    /* Clear module entry */
    memset(module, 0, sizeof(dispatch_entry_t));
    module->module_id = module_id;
    module->state = MODULE_STATE_UNLOADED;
    
    g_dispatch_manager.active_modules--;
    
    log_info("Module unregistered (active modules: %d)", g_dispatch_manager.active_modules);
    
    return SUCCESS;
}

/**
 * @brief Dispatch function call to appropriate module
 * @param function Function code
 * @param handle Handle parameter
 * @param params Function parameters  
 * @param selected_module Preferred module (0xFF = auto-select)
 * @return Function result
 */
int dispatch_call_module(uint8_t function, uint16_t handle, void *params, uint8_t selected_module) {
    uint32_t start_time, processing_time;
    int result;
    uint8_t target_module;
    
    if (!dispatch_initialized) {
        return ERROR_INVALID_STATE;
    }
    
    g_dispatch_manager.total_dispatches++;
    
    /* Select target module */
    if (selected_module == 0xFF) {
        /* Auto-select based on strategy */
        result = select_module_for_packet(params, 0, &target_module);
        if (result != SUCCESS) {
            g_dispatch_manager.dispatch_errors++;
            return result;
        }
    } else {
        if (!validate_module_id(selected_module) || 
            g_dispatch_manager.modules[selected_module].state != MODULE_STATE_ACTIVE) {
            g_dispatch_manager.dispatch_errors++;
            return ERROR_MODULE_NOT_FOUND;
        }
        target_module = selected_module;
    }
    
    dispatch_entry_t *module = &g_dispatch_manager.modules[target_module];
    
    log_debug("Dispatching function %02X to module %s (handle=%04X)", 
              function, module->module_name, handle);
    
    /* Record start time for performance measurement */
    start_time = get_system_time();
    
    /* Dispatch to appropriate function */
    switch (function) {
        case 0x02: /* access_type */
            if (module->functions.handle_access_type) {
                result = module->functions.handle_access_type(params);
            } else {
                result = ERROR_NOT_IMPLEMENTED;
            }
            break;
            
        case 0x03: /* release_handle */
            if (module->functions.release_handle) {
                result = module->functions.release_handle(handle);
            } else {
                result = ERROR_NOT_IMPLEMENTED;
            }
            break;
            
        case 0x04: /* send_packet */
            if (module->functions.send_packet) {
                result = module->functions.send_packet(handle, params);
            } else {
                result = ERROR_NOT_IMPLEMENTED;
            }
            break;
            
        case 0x19: /* get_statistics */
            if (module->functions.get_statistics) {
                result = module->functions.get_statistics(handle, params);
            } else {
                result = ERROR_NOT_IMPLEMENTED;
            }
            break;
            
        default:
            result = ERROR_NOT_IMPLEMENTED;
            break;
    }
    
    /* Calculate processing time */
    processing_time = get_system_time() - start_time;
    
    /* Update module metrics */
    update_module_metrics(target_module, processing_time, (result == SUCCESS));
    
    if (result != SUCCESS) {
        log_debug("Module %s function %02X failed: %d", module->module_name, function, result);
    }
    
    return result;
}

/**
 * @brief Get module status information
 * @param module_id Module identifier
 * @param status Status structure to fill
 * @return SUCCESS on success, error code on failure
 */
int dispatch_get_module_status(uint8_t module_id, unified_module_status_t *status) {
    if (!dispatch_initialized || !status) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!validate_module_id(module_id)) {
        return ERROR_INVALID_PARAM;
    }
    
    dispatch_entry_t *module = &g_dispatch_manager.modules[module_id];
    
    /* Fill status structure */
    strncpy(status->module_name, module->module_name, sizeof(status->module_name) - 1);
    status->module_name[sizeof(status->module_name) - 1] = '\0';
    status->module_id = module_id;
    
    /* Convert state to status */
    switch (module->state) {
        case MODULE_STATE_UNLOADED:
            status->status = MODULE_STATUS_INACTIVE;
            break;
        case MODULE_STATE_ACTIVE:
            status->status = MODULE_STATUS_ACTIVE;
            break;
        case MODULE_STATE_ERROR:
            status->status = MODULE_STATUS_ERROR;
            break;
        case MODULE_STATE_DEGRADED:
            status->status = MODULE_STATUS_DEGRADED;
            break;
        default:
            status->status = MODULE_STATUS_INACTIVE;
            break;
    }
    
    status->active_handles = (uint16_t)metrics_get_module_handles(module_id);
    status->packets_processed = module->packets_processed;
    status->errors = module->error_count;
    status->last_activity_time = module->last_activity_time;
    /* Get memory usage from metrics - stored in a 32-bit value but truncated to 16-bit for compatibility */
    uint32_t mem_usage = metrics_get_memory_usage();
    status->memory_usage = (mem_usage > 0xFFFF) ? 0xFFFF : (uint16_t)mem_usage;
    status->cpu_usage = module->load_percentage;
    
    return SUCCESS;
}

/**
 * @brief Set load balancing strategy
 * @param strategy Load balancing strategy
 * @return SUCCESS on success, error code on failure
 */
int dispatch_set_load_balance_strategy(uint8_t strategy) {
    if (!dispatch_initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (strategy > LOAD_BALANCE_ADAPTIVE) {
        return ERROR_INVALID_PARAM;
    }
    
    g_dispatch_manager.load_balance_strategy = strategy;
    log_info("Load balancing strategy set to %d", strategy);
    
    return SUCCESS;
}

/* Internal Helper Functions */

static int select_module_for_packet(const void *packet, uint16_t packet_type, uint8_t *selected_module) {
    switch (g_dispatch_manager.load_balance_strategy) {
        case LOAD_BALANCE_ROUND_ROBIN:
            return select_module_round_robin(selected_module);
            
        case LOAD_BALANCE_WEIGHTED:
            return select_module_weighted(selected_module);
            
        case LOAD_BALANCE_PERFORMANCE:
            return select_module_performance(selected_module);
            
        case LOAD_BALANCE_CAPABILITY:
            return select_module_capability(packet_type, selected_module);
            
        case LOAD_BALANCE_ADAPTIVE:
            return select_module_adaptive(packet, packet_type, selected_module);
            
        default:
            return select_module_round_robin(selected_module);
    }
}

static int select_module_round_robin(uint8_t *selected_module) {
    uint8_t start_module = g_dispatch_manager.last_selected_module;
    
    for (int i = 0; i < MAX_DISPATCH_MODULES; i++) {
        uint8_t module_id = (start_module + i + 1) % MAX_DISPATCH_MODULES;
        dispatch_entry_t *module = &g_dispatch_manager.modules[module_id];
        
        if (module->state == MODULE_STATE_ACTIVE && module->enabled) {
            *selected_module = module_id;
            g_dispatch_manager.last_selected_module = module_id;
            return SUCCESS;
        }
    }
    
    return ERROR_MODULE_NOT_FOUND;
}

static int select_module_weighted(uint8_t *selected_module) {
    uint32_t total_weight = 0;
    uint32_t selection_point;
    uint32_t current_weight = 0;
    
    /* Calculate total weight of active modules */
    for (int i = 0; i < MAX_DISPATCH_MODULES; i++) {
        dispatch_entry_t *module = &g_dispatch_manager.modules[i];
        if (module->state == MODULE_STATE_ACTIVE && module->enabled) {
            total_weight += module->weight;
        }
    }
    
    if (total_weight == 0) {
        return ERROR_MODULE_NOT_FOUND;
    }
    
    /* Generate selection point based on dispatch count */
    selection_point = (g_dispatch_manager.total_dispatches * 137) % total_weight;
    
    /* Find module that corresponds to selection point */
    for (int i = 0; i < MAX_DISPATCH_MODULES; i++) {
        dispatch_entry_t *module = &g_dispatch_manager.modules[i];
        if (module->state == MODULE_STATE_ACTIVE && module->enabled) {
            current_weight += module->weight;
            if (current_weight > selection_point) {
                *selected_module = i;
                return SUCCESS;
            }
        }
    }
    
    return ERROR_MODULE_NOT_FOUND;
}

static int select_module_performance(uint8_t *selected_module) {
    uint8_t best_module = 0xFF;
    uint32_t best_score = 0xFFFFFFFF;
    
    for (int i = 0; i < MAX_DISPATCH_MODULES; i++) {
        dispatch_entry_t *module = &g_dispatch_manager.modules[i];
        if (module->state == MODULE_STATE_ACTIVE && module->enabled) {
            /* Calculate performance score (lower is better) */
            uint32_t score = module->load_percentage + 
                           (module->avg_processing_time / 1000) +
                           (module->error_count * 10);
            
            if (score < best_score) {
                best_score = score;
                best_module = i;
            }
        }
    }
    
    if (best_module == 0xFF) {
        return ERROR_MODULE_NOT_FOUND;
    }
    
    *selected_module = best_module;
    return SUCCESS;
}

static int select_module_capability(uint16_t packet_type, uint8_t *selected_module) {
    /* Select based on packet type and module capabilities */
    uint16_t required_caps = MODULE_CAP_BASIC_ETHERNET;
    
    /* Determine required capabilities based on packet type */
    if (packet_type == 0x0800) { /* IP packets */
        required_caps |= MODULE_CAP_HARDWARE_CSUM; /* Prefer hardware checksum */
    }
    
    /* Find best matching module */
    uint8_t best_module = 0xFF;
    uint16_t best_match = 0;
    
    for (int i = 0; i < MAX_DISPATCH_MODULES; i++) {
        dispatch_entry_t *module = &g_dispatch_manager.modules[i];
        if (module->state == MODULE_STATE_ACTIVE && module->enabled) {
            uint16_t match_score = module->capabilities & required_caps;
            if (match_score > best_match || best_module == 0xFF) {
                best_match = match_score;
                best_module = i;
            }
        }
    }
    
    if (best_module == 0xFF) {
        return ERROR_MODULE_NOT_FOUND;
    }
    
    *selected_module = best_module;
    return SUCCESS;
}

static int select_module_adaptive(const void *packet, uint16_t packet_type, uint8_t *selected_module) {
    /* Combine performance and capability-based selection */
    int result;
    
    /* First try capability-based selection */
    result = select_module_capability(packet_type, selected_module);
    if (result == SUCCESS) {
        /* Check if selected module is overloaded */
        dispatch_entry_t *module = &g_dispatch_manager.modules[*selected_module];
        if (module->load_percentage > 80) {
            /* Try performance-based selection instead */
            uint8_t perf_module;
            if (select_module_performance(&perf_module) == SUCCESS) {
                *selected_module = perf_module;
            }
        }
        return SUCCESS;
    }
    
    /* Fall back to performance-based selection */
    return select_module_performance(selected_module);
}

static int validate_module_id(uint8_t module_id) {
    return (module_id < MAX_DISPATCH_MODULES);
}

static void update_module_metrics(uint8_t module_id, uint32_t processing_time, bool success) {
    if (!validate_module_id(module_id)) {
        return;
    }
    
    dispatch_entry_t *module = &g_dispatch_manager.modules[module_id];
    
    /* Update processing time metrics */
    module->processing_time += processing_time;
    if (processing_time > module->peak_processing_time) {
        module->peak_processing_time = processing_time;
    }
    
    /* Calculate average processing time */
    if (module->packets_processed > 0) {
        module->avg_processing_time = module->processing_time / module->packets_processed;
    }
    
    /* Update success rate */
    if (!success) {
        module->error_count++;
    }
    
    if (module->packets_processed > 0) {
        module->success_rate = ((module->packets_processed - module->error_count) * 100) / 
                              module->packets_processed;
    }
    
    /* Update load percentage */
    module->load_percentage = calculate_module_load(module_id);
    
    module->last_activity_time = get_system_time();
}

static uint32_t calculate_module_load(uint8_t module_id) {
    /* Simplified load calculation based on recent activity */
    dispatch_entry_t *module = &g_dispatch_manager.modules[module_id];
    uint32_t current_time = get_system_time();
    uint32_t time_diff = current_time - module->last_activity_time;
    
    /* If no recent activity, load is low */
    if (time_diff > 1000) { /* 1 second */
        return 0;
    }
    
    /* Calculate load based on processing time ratio */
    if (time_diff > 0) {
        uint32_t load = (module->avg_processing_time * 100) / time_diff;
        return (load > 100) ? 100 : load;
    }
    
    return 50; /* Default moderate load */
}

/* External system time function */
extern uint32_t get_system_time(void);