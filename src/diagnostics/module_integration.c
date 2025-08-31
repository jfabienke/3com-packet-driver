/**
 * @file module_integration.c
 * @brief Integration with all modules (PTASK/CORKSCRW/BOOMTEX) and Driver API
 * 
 * 3Com Packet Driver - Diagnostics Agent - Week 1
 * Implements comprehensive diagnostic integration with all system modules
 */

#include "../../include/diagnostics.h"
#include "../../include/common.h"
#include "../../include/api.h"
#include "../modules/common/module_bridge.h"
#include "../api/unified_api.h"
#include "../api/module_dispatch.h"
#include "../../docs/agents/shared/error-codes.h"
#include "../../docs/agents/shared/timing-measurement.h"
#include <string.h>
#include <stdio.h>

/* Module integration configuration */
#define MAX_MODULE_CALLBACKS        16
#define MODULE_HEALTH_CHECK_INTERVAL 5000   /* 5 seconds */
#define MODULE_RESPONSE_TIMEOUT     1000    /* 1 second */

/* Module types for integration */
typedef enum {
    MODULE_TYPE_PTASK = 0,
    MODULE_TYPE_CORKSCRW,
    MODULE_TYPE_BOOMTEX,
    MODULE_TYPE_DRIVER_API,
    MODULE_TYPE_MEMORY_MANAGER,
    MODULE_TYPE_PERFORMANCE_MONITOR,
    MODULE_TYPE_UNKNOWN
} module_type_t;

/* Module health status */
typedef enum {
    MODULE_STATUS_UNKNOWN = 0,
    MODULE_STATUS_INITIALIZING,
    MODULE_STATUS_HEALTHY,
    MODULE_STATUS_WARNING,
    MODULE_STATUS_ERROR,
    MODULE_STATUS_UNRESPONSIVE
} module_status_t;

/* Module diagnostic callback function type */
typedef int (*module_diag_callback_t)(uint8_t module_id, uint32_t *metrics, uint32_t metric_count);

/* Module integration entry */
typedef struct module_entry {
    uint8_t module_id;
    module_type_t module_type;
    module_status_t status;
    char module_name[32];
    
    /* Health monitoring */
    uint32_t last_health_check;
    uint32_t response_time_us;
    uint32_t consecutive_failures;
    uint32_t total_health_checks;
    uint32_t failed_health_checks;
    
    /* Performance metrics */
    uint32_t api_calls_total;
    uint32_t api_calls_failed;
    uint32_t avg_call_time_us;
    uint32_t max_call_time_us;
    
    /* Diagnostic callback */
    module_diag_callback_t diag_callback;
    uint32_t callback_metrics[16];
    
    /* Module-specific data */
    void *module_data;
    uint32_t data_size;
    
    struct module_entry *next;
} module_entry_t;

/* Integration system state */
typedef struct module_integration {
    bool initialized;
    bool health_monitoring_enabled;
    bool performance_tracking_enabled;
    
    /* Module registry */
    module_entry_t *modules_head;
    uint8_t module_count;
    uint8_t next_module_id;
    
    /* Health check scheduling */
    uint32_t health_check_interval;
    uint32_t last_global_health_check;
    
    /* Integration statistics */
    uint32_t total_integrations;
    uint32_t active_integrations;
    uint32_t failed_integrations;
    uint32_t health_check_cycles;
    
    /* Performance aggregation */
    uint32_t total_api_calls;
    uint32_t total_api_failures;
    uint32_t total_response_time_us;
    
} module_integration_t;

static module_integration_t g_module_integration = {0};

/* Function prototypes for module-specific integration */
static int integrate_ptask_module(module_entry_t *module);
static int integrate_corkscrw_module(module_entry_t *module);
static int integrate_boomtex_module(module_entry_t *module);
static int integrate_driver_api(module_entry_t *module);

/* PTASK diagnostic callback */
static int ptask_diagnostic_callback(uint8_t module_id, uint32_t *metrics, uint32_t metric_count) {
    /* Simulate PTASK diagnostic data collection */
    if (metrics && metric_count >= 4) {
        metrics[0] = 100; /* Packet processing rate */
        metrics[1] = 50;  /* Queue utilization */
        metrics[2] = 10;  /* Error count */
        metrics[3] = 95;  /* Health score */
    }
    return SUCCESS;
}

/* CORKSCRW diagnostic callback */
static int corkscrw_diagnostic_callback(uint8_t module_id, uint32_t *metrics, uint32_t metric_count) {
    /* Simulate CORKSCRW diagnostic data collection */
    if (metrics && metric_count >= 4) {
        metrics[0] = 85;  /* DMA efficiency */
        metrics[1] = 32;  /* Ring buffer usage */
        metrics[2] = 3;   /* DMA errors */
        metrics[3] = 90;  /* Health score */
    }
    return SUCCESS;
}

/* BOOMTEX diagnostic callback */
static int boomtex_diagnostic_callback(uint8_t module_id, uint32_t *metrics, uint32_t metric_count) {
    /* Simulate BOOMTEX diagnostic data collection */
    if (metrics && metric_count >= 4) {
        metrics[0] = 150; /* Throughput (Mbps) */
        metrics[1] = 75;  /* Bus utilization */
        metrics[2] = 1;   /* Bus master errors */
        metrics[3] = 98;  /* Health score */
    }
    return SUCCESS;
}

/* Driver API diagnostic callback */
static int driver_api_diagnostic_callback(uint8_t module_id, uint32_t *metrics, uint32_t metric_count) {
    /* Simulate Driver API diagnostic data collection */
    if (metrics && metric_count >= 4) {
        metrics[0] = 200; /* API calls per second */
        metrics[1] = 5;   /* Average response time (us) */
        metrics[2] = 2;   /* API errors */
        metrics[3] = 92;  /* Health score */
    }
    return SUCCESS;
}

/* Helper functions */
static const char* get_module_type_string(module_type_t type) {
    switch (type) {
        case MODULE_TYPE_PTASK: return "PTASK";
        case MODULE_TYPE_CORKSCRW: return "CORKSCRW";
        case MODULE_TYPE_BOOMTEX: return "BOOMTEX";
        case MODULE_TYPE_DRIVER_API: return "DRIVER_API";
        case MODULE_TYPE_MEMORY_MANAGER: return "MEMORY_MGR";
        case MODULE_TYPE_PERFORMANCE_MONITOR: return "PERF_MON";
        default: return "UNKNOWN";
    }
}

static const char* get_module_status_string(module_status_t status) {
    switch (status) {
        case MODULE_STATUS_INITIALIZING: return "INIT";
        case MODULE_STATUS_HEALTHY: return "HEALTHY";
        case MODULE_STATUS_WARNING: return "WARNING";
        case MODULE_STATUS_ERROR: return "ERROR";
        case MODULE_STATUS_UNRESPONSIVE: return "UNRESPONSIVE";
        default: return "UNKNOWN";
    }
}

/* Initialize module integration system */
int module_integration_init(void) {
    if (g_module_integration.initialized) {
        return SUCCESS;
    }
    
    /* Initialize configuration */
    g_module_integration.health_monitoring_enabled = true;
    g_module_integration.performance_tracking_enabled = true;
    g_module_integration.health_check_interval = MODULE_HEALTH_CHECK_INTERVAL;
    
    /* Initialize module registry */
    g_module_integration.modules_head = NULL;
    g_module_integration.module_count = 0;
    g_module_integration.next_module_id = 1;
    
    /* Initialize statistics */
    g_module_integration.total_integrations = 0;
    g_module_integration.active_integrations = 0;
    g_module_integration.failed_integrations = 0;
    g_module_integration.health_check_cycles = 0;
    
    g_module_integration.last_global_health_check = diag_get_timestamp();
    g_module_integration.initialized = true;
    
    debug_log_info("Module integration system initialized");
    return SUCCESS;
}

/* Register a module for diagnostic integration */
int module_integration_register(module_type_t module_type, const char *module_name, 
                                module_diag_callback_t callback, void *module_data, uint32_t data_size) {
    if (!g_module_integration.initialized || !module_name) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create module entry */
    module_entry_t *module = (module_entry_t*)malloc(sizeof(module_entry_t));
    if (!module) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    /* Initialize module entry */
    memset(module, 0, sizeof(module_entry_t));
    module->module_id = g_module_integration.next_module_id++;
    module->module_type = module_type;
    module->status = MODULE_STATUS_INITIALIZING;
    strncpy(module->module_name, module_name, sizeof(module->module_name) - 1);
    
    module->last_health_check = diag_get_timestamp();
    module->diag_callback = callback;
    
    /* Store module data if provided */
    if (module_data && data_size > 0) {
        module->module_data = malloc(data_size);
        if (module->module_data) {
            memcpy(module->module_data, module_data, data_size);
            module->data_size = data_size;
        }
    }
    
    /* Add to module registry */
    module->next = g_module_integration.modules_head;
    g_module_integration.modules_head = module;
    g_module_integration.module_count++;
    g_module_integration.total_integrations++;
    g_module_integration.active_integrations++;
    
    /* Perform module-specific integration */
    int result = SUCCESS;
    switch (module_type) {
        case MODULE_TYPE_PTASK:
            result = integrate_ptask_module(module);
            break;
        case MODULE_TYPE_CORKSCRW:
            result = integrate_corkscrw_module(module);
            break;
        case MODULE_TYPE_BOOMTEX:
            result = integrate_boomtex_module(module);
            break;
        case MODULE_TYPE_DRIVER_API:
            result = integrate_driver_api(module);
            break;
        default:
            debug_log_warning("Unknown module type for integration: %d", module_type);
            break;
    }
    
    if (result == SUCCESS) {
        module->status = MODULE_STATUS_HEALTHY;
        debug_log_info("Module integrated successfully: %s (ID=%d)", module_name, module->module_id);
    } else {
        module->status = MODULE_STATUS_ERROR;
        g_module_integration.failed_integrations++;
        debug_log_error("Module integration failed: %s (error=0x%04X)", module_name, result);
    }
    
    return result;
}

/* Auto-register all known modules */
int module_integration_auto_register(void) {
    if (!g_module_integration.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    int results[4];
    int total_result = SUCCESS;
    
    /* Register PTASK module */
    results[0] = module_integration_register(MODULE_TYPE_PTASK, "PTASK", 
                                           ptask_diagnostic_callback, NULL, 0);
    
    /* Register CORKSCRW module */
    results[1] = module_integration_register(MODULE_TYPE_CORKSCRW, "CORKSCRW", 
                                           corkscrw_diagnostic_callback, NULL, 0);
    
    /* Register BOOMTEX module */
    results[2] = module_integration_register(MODULE_TYPE_BOOMTEX, "BOOMTEX", 
                                           boomtex_diagnostic_callback, NULL, 0);
    
    /* Register Driver API */
    results[3] = module_integration_register(MODULE_TYPE_DRIVER_API, "DRIVER_API", 
                                           driver_api_diagnostic_callback, NULL, 0);
    
    /* Check results */
    for (int i = 0; i < 4; i++) {
        if (results[i] != SUCCESS) {
            total_result = results[i];
            debug_log_warning("Auto-registration failed for module %d: 0x%04X", i, results[i]);
        }
    }
    
    debug_log_info("Auto-registration completed: %d modules registered", g_module_integration.module_count);
    return total_result;
}

/* Perform health check on a specific module */
static int perform_module_health_check(module_entry_t *module) {
    if (!module) {
        return ERROR_INVALID_PARAM;
    }
    
    pit_timing_t timing;
    int result = SUCCESS;
    
    /* Time the health check */
    PIT_START_TIMING(&timing);
    
    /* Call module diagnostic callback if available */
    if (module->diag_callback) {
        result = module->diag_callback(module->module_id, module->callback_metrics, 16);
    } else {
        /* Basic health check - just verify module is responsive */
        result = SUCCESS; /* Assume healthy if no callback */
    }
    
    PIT_END_TIMING(&timing);
    
    /* Update module statistics */
    module->last_health_check = diag_get_timestamp();
    module->response_time_us = timing.elapsed_us;
    module->total_health_checks++;
    
    if (result == SUCCESS) {
        module->consecutive_failures = 0;
        if (module->status == MODULE_STATUS_ERROR || module->status == MODULE_STATUS_UNRESPONSIVE) {
            module->status = MODULE_STATUS_HEALTHY;
            debug_log_info("Module %s recovered", module->module_name);
        }
    } else {
        module->consecutive_failures++;
        module->failed_health_checks++;
        
        if (module->consecutive_failures >= 3) {
            module->status = MODULE_STATUS_UNRESPONSIVE;
            debug_log_error("Module %s marked as unresponsive", module->module_name);
        } else if (result != SUCCESS) {
            module->status = MODULE_STATUS_ERROR;
            debug_log_warning("Module %s health check failed: 0x%04X", module->module_name, result);
        }
    }
    
    /* Check for performance warnings */
    if (timing.elapsed_us > MODULE_RESPONSE_TIMEOUT) {
        if (module->status == MODULE_STATUS_HEALTHY) {
            module->status = MODULE_STATUS_WARNING;
        }
        debug_log_warning("Module %s health check slow: %lu us", module->module_name, timing.elapsed_us);
    }
    
    return result;
}

/* Perform health checks on all registered modules */
int module_integration_health_check(void) {
    if (!g_module_integration.initialized || !g_module_integration.health_monitoring_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    uint32_t current_time = diag_get_timestamp();
    
    /* Check if it's time for global health check */
    if (current_time - g_module_integration.last_global_health_check < g_module_integration.health_check_interval) {
        return SUCCESS;
    }
    
    debug_log_debug("Performing global module health check");
    
    uint32_t healthy_modules = 0;
    uint32_t warning_modules = 0;
    uint32_t error_modules = 0;
    
    /* Check each registered module */
    module_entry_t *module = g_module_integration.modules_head;
    while (module) {
        perform_module_health_check(module);
        
        switch (module->status) {
            case MODULE_STATUS_HEALTHY:
                healthy_modules++;
                break;
            case MODULE_STATUS_WARNING:
                warning_modules++;
                break;
            case MODULE_STATUS_ERROR:
            case MODULE_STATUS_UNRESPONSIVE:
                error_modules++;
                break;
        }
        
        module = module->next;
    }
    
    g_module_integration.health_check_cycles++;
    g_module_integration.last_global_health_check = current_time;
    
    debug_log_info("Health check completed: %lu healthy, %lu warning, %lu error modules",
                   healthy_modules, warning_modules, error_modules);
    
    /* Generate alerts for unhealthy modules */
    if (error_modules > 0) {
        diag_generate_alert(ALERT_TYPE_NIC_FAILURE, "Module health issues detected");
    }
    
    return SUCCESS;
}

/* Collect metrics from all integrated modules */
int module_integration_collect_metrics(void) {
    if (!g_module_integration.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    debug_log_debug("Collecting metrics from all integrated modules");
    
    uint32_t total_calls = 0;
    uint32_t total_failures = 0;
    uint32_t total_response_time = 0;
    uint32_t active_modules = 0;
    
    module_entry_t *module = g_module_integration.modules_head;
    while (module) {
        if (module->status == MODULE_STATUS_HEALTHY || module->status == MODULE_STATUS_WARNING) {
            /* Update statistical analysis with module metrics */
            if (module->callback_metrics[3] > 0) { /* Health score */
                stat_analysis_add_sample(METRIC_TYPE_NIC_HEALTH, module->callback_metrics[3]);
            }
            
            /* Aggregate performance metrics */
            total_calls += module->api_calls_total;
            total_failures += module->api_calls_failed;
            total_response_time += (module->response_time_us * module->total_health_checks);
            active_modules++;
        }
        
        module = module->next;
    }
    
    /* Update global statistics */
    g_module_integration.total_api_calls = total_calls;
    g_module_integration.total_api_failures = total_failures;
    g_module_integration.total_response_time_us = (active_modules > 0) ? total_response_time / active_modules : 0;
    
    debug_log_debug("Metrics collection completed: %lu calls, %lu failures, %lu us avg response",
                   total_calls, total_failures, g_module_integration.total_response_time_us);
    
    return SUCCESS;
}

/* Module-specific integration functions */
static int integrate_ptask_module(module_entry_t *module) {
    debug_log_debug("Integrating PTASK module");
    
    /* Get PTASK module context from centralized detection */
    module_init_context_t *ptask_context = module_get_context_from_detection(MODULE_ID_PTASK, NIC_TYPE_3C509B);
    if (!ptask_context) {
        debug_log_warning("PTASK module context not available");
        module->status = MODULE_STATUS_WARNING;
        return ERROR_INVALID_STATE;
    }
    
    /* Check module bridge health */
    module_bridge_t *bridge = (module_bridge_t *)module->module_data;
    if (bridge && bridge->module_state == MODULE_STATE_ACTIVE) {
        /* Module is active - perform health checks */
        int bridge_health = module_bridge_validate_isr_safety(bridge);
        if (bridge_health == SUCCESS) {
            module->status = MODULE_STATUS_HEALTHY;
            module->callback_metrics[0] = bridge->packets_sent;
            module->callback_metrics[1] = bridge->packets_received;
            module->callback_metrics[2] = bridge->isr_entry_count;
            module->callback_metrics[3] = 100; /* Health score */
            
            /* Update performance metrics */
            module->avg_call_time_us = bridge->last_isr_time_us;
            module->max_call_time_us = bridge->isr_max_duration_us;
            
            debug_log_debug("PTASK health: %lu sent, %lu received, %lu ISR calls",
                           bridge->packets_sent, bridge->packets_received, bridge->isr_entry_count);
        } else {
            module->status = MODULE_STATUS_ERROR;
            module->consecutive_failures++;
            debug_log_error("PTASK module ISR safety validation failed: 0x%04X", bridge_health);
        }
    } else {
        module->status = MODULE_STATUS_ERROR;
        module->consecutive_failures++;
        debug_log_error("PTASK module bridge not active");
    }
    
    module->last_health_check = diag_get_timestamp();
    module->total_health_checks++;
    
    return SUCCESS;
}

static int integrate_corkscrw_module(module_entry_t *module) {
    debug_log_debug("Integrating CORKSCRW module");
    
    /* Get CORKSCRW module context from centralized detection */
    module_init_context_t *corkscrw_context = module_get_context_from_detection(MODULE_ID_CORKSCRW, NIC_TYPE_3C515_TX);
    if (!corkscrw_context) {
        debug_log_warning("CORKSCRW module context not available");
        module->status = MODULE_STATUS_WARNING;
        return ERROR_INVALID_STATE;
    }
    
    /* Check module bridge health */
    module_bridge_t *bridge = (module_bridge_t *)module->module_data;
    if (bridge && bridge->module_state == MODULE_STATE_ACTIVE) {
        /* Module is active - perform health checks */
        int bridge_health = module_bridge_validate_isr_safety(bridge);
        if (bridge_health == SUCCESS) {
            module->status = MODULE_STATUS_HEALTHY;
            module->callback_metrics[0] = bridge->packets_sent;
            module->callback_metrics[1] = bridge->packets_received;
            module->callback_metrics[2] = bridge->isr_entry_count;
            
            /* Check bus master DMA health (3C515 specific) */
            if (bridge->module_flags & MODULE_BRIDGE_FLAG_BUS_MASTER) {
                if (bridge->module_flags & MODULE_BRIDGE_FLAG_DMA_ACTIVE) {
                    module->callback_metrics[3] = 100; /* Healthy DMA */
                    debug_log_debug("CORKSCRW bus master DMA active and healthy");
                } else {
                    module->callback_metrics[3] = 75; /* DMA not active but OK */
                    debug_log_debug("CORKSCRW bus master DMA not active");
                }
            } else {
                module->callback_metrics[3] = 90; /* PIO mode */
                debug_log_debug("CORKSCRW using PIO mode");
            }
            
            /* Update performance metrics */
            module->avg_call_time_us = bridge->last_isr_time_us;
            module->max_call_time_us = bridge->isr_max_duration_us;
            
            debug_log_debug("CORKSCRW health: %lu sent, %lu received, %lu ISR calls, score %lu",
                           bridge->packets_sent, bridge->packets_received, 
                           bridge->isr_entry_count, module->callback_metrics[3]);
        } else {
            module->status = MODULE_STATUS_ERROR;
            module->consecutive_failures++;
            debug_log_error("CORKSCRW module ISR safety validation failed: 0x%04X", bridge_health);
        }
    } else {
        module->status = MODULE_STATUS_ERROR;
        module->consecutive_failures++;
        debug_log_error("CORKSCRW module bridge not active");
    }
    
    module->last_health_check = diag_get_timestamp();
    module->total_health_checks++;
    
    return SUCCESS;
}

static int integrate_boomtex_module(module_entry_t *module) {
    debug_log_debug("Integrating BOOMTEX module");
    
    /* Get BOOMTEX module context from centralized detection for any PCI NIC */
    module_init_context_t *boomtex_context = module_get_context_from_detection(MODULE_ID_BOOMTEX, NIC_TYPE_3C905B);
    if (!boomtex_context) {
        debug_log_warning("BOOMTEX module context not available");
        module->status = MODULE_STATUS_WARNING;
        return ERROR_INVALID_STATE;
    }
    
    /* Check module bridge health */
    module_bridge_t *bridge = (module_bridge_t *)module->module_data;
    if (bridge && bridge->module_state == MODULE_STATE_ACTIVE) {
        /* Module is active - perform health checks */
        int bridge_health = module_bridge_validate_isr_safety(bridge);
        if (bridge_health == SUCCESS) {
            module->status = MODULE_STATUS_HEALTHY;
            module->callback_metrics[0] = bridge->packets_sent;
            module->callback_metrics[1] = bridge->packets_received;
            module->callback_metrics[2] = bridge->isr_entry_count;
            
            /* Check PCI bus master capabilities (BOOMTEX handles all PCI NICs) */
            if (bridge->module_flags & MODULE_BRIDGE_FLAG_BUS_MASTER) {
                if (bridge->module_flags & MODULE_BRIDGE_FLAG_CACHE_COHERENT) {
                    module->callback_metrics[3] = 100; /* Perfect PCI DMA */
                    debug_log_debug("BOOMTEX PCI DMA with cache coherency");
                } else {
                    module->callback_metrics[3] = 95; /* Good PCI DMA */
                    debug_log_debug("BOOMTEX PCI DMA without cache coherency");
                }
            } else {
                module->callback_metrics[3] = 85; /* PCI PIO mode */
                debug_log_debug("BOOMTEX using PCI PIO mode");
            }
            
            /* Update performance metrics */
            module->avg_call_time_us = bridge->last_isr_time_us;
            module->max_call_time_us = bridge->isr_max_duration_us;
            
            debug_log_debug("BOOMTEX health: %lu sent, %lu received, %lu ISR calls, score %lu",
                           bridge->packets_sent, bridge->packets_received, 
                           bridge->isr_entry_count, module->callback_metrics[3]);
        } else {
            module->status = MODULE_STATUS_ERROR;
            module->consecutive_failures++;
            debug_log_error("BOOMTEX module ISR safety validation failed: 0x%04X", bridge_health);
        }
    } else {
        module->status = MODULE_STATUS_ERROR;
        module->consecutive_failures++;
        debug_log_error("BOOMTEX module bridge not active");
    }
    
    module->last_health_check = diag_get_timestamp();
    module->total_health_checks++;
    
    return SUCCESS;
}

static int integrate_driver_api(module_entry_t *module) {
    debug_log_debug("Integrating Driver API");
    
    /* Check unified API health and status */
    api_status_t api_status;
    int result = unified_api_get_status(&api_status);
    if (result == SUCCESS) {
        module->status = MODULE_STATUS_HEALTHY;
        
        /* Populate metrics from API status */
        module->callback_metrics[0] = api_status.total_calls;        /* Total API calls */
        module->callback_metrics[1] = api_status.failed_calls;       /* Failed API calls */
        module->callback_metrics[2] = api_status.active_handles;     /* Active handles */
        
        /* Calculate health score based on success rate */
        if (api_status.total_calls > 0) {
            uint32_t success_rate = ((api_status.total_calls - api_status.failed_calls) * 100) / api_status.total_calls;
            module->callback_metrics[3] = success_rate; /* Health score */
            
            if (success_rate < 90) {
                module->status = MODULE_STATUS_WARNING;
                debug_log_warning("Driver API success rate low: %lu%%", success_rate);
            } else {
                debug_log_debug("Driver API healthy: %lu%% success rate", success_rate);
            }
        } else {
            module->callback_metrics[3] = 100; /* No calls yet, assume healthy */
        }
        
        /* Update performance metrics */
        module->api_calls_total = api_status.total_calls;
        module->api_calls_failed = api_status.failed_calls;
        module->avg_call_time_us = api_status.avg_call_time_us;
        module->max_call_time_us = api_status.max_call_time_us;
        
        /* Check module dispatch system */
        module_dispatch_stats_t dispatch_stats;
        if (module_dispatch_get_stats(&dispatch_stats) == SUCCESS) {
            if (dispatch_stats.dispatch_failures > 0) {
                module->status = MODULE_STATUS_WARNING;
                debug_log_warning("Module dispatch failures detected: %lu", dispatch_stats.dispatch_failures);
            }
            
            /* Add dispatch metrics */
            module->callback_metrics[4] = dispatch_stats.total_dispatches;
            module->callback_metrics[5] = dispatch_stats.dispatch_failures;
        }
        
        debug_log_debug("Driver API health: %lu total, %lu failed, %lu active handles, score %lu",
                       api_status.total_calls, api_status.failed_calls, 
                       api_status.active_handles, module->callback_metrics[3]);
    } else {
        module->status = MODULE_STATUS_ERROR;
        module->consecutive_failures++;
        debug_log_error("Driver API health check failed: 0x%04X", result);
    }
    
    module->last_health_check = diag_get_timestamp();
    module->total_health_checks++;
    
    return SUCCESS;
}

/* Get module integration statistics */
int module_integration_get_statistics(uint32_t *total_modules, uint32_t *healthy_modules,
                                      uint32_t *failed_integrations, uint32_t *health_checks) {
    if (!g_module_integration.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (total_modules) *total_modules = g_module_integration.module_count;
    if (failed_integrations) *failed_integrations = g_module_integration.failed_integrations;
    if (health_checks) *health_checks = g_module_integration.health_check_cycles;
    
    if (healthy_modules) {
        *healthy_modules = 0;
        module_entry_t *module = g_module_integration.modules_head;
        while (module) {
            if (module->status == MODULE_STATUS_HEALTHY) {
                (*healthy_modules)++;
            }
            module = module->next;
        }
    }
    
    return SUCCESS;
}

/* Print module integration dashboard */
int module_integration_print_dashboard(void) {
    if (!g_module_integration.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== MODULE INTEGRATION DASHBOARD ===\n");
    printf("Health Monitoring: %s\n", g_module_integration.health_monitoring_enabled ? "Enabled" : "Disabled");
    printf("Performance Tracking: %s\n", g_module_integration.performance_tracking_enabled ? "Enabled" : "Disabled");
    
    printf("\nIntegration Statistics:\n");
    printf("  Total Modules: %d\n", g_module_integration.module_count);
    printf("  Active Integrations: %lu\n", g_module_integration.active_integrations);
    printf("  Failed Integrations: %lu\n", g_module_integration.failed_integrations);
    printf("  Health Check Cycles: %lu\n", g_module_integration.health_check_cycles);
    
    printf("\nRegistered Modules:\n");
    module_entry_t *module = g_module_integration.modules_head;
    while (module) {
        printf("  [%d] %s (%s): %s\n",
               module->module_id,
               module->module_name,
               get_module_type_string(module->module_type),
               get_module_status_string(module->status));
        
        printf("       Health Checks: %lu total, %lu failed, %lu consecutive failures\n",
               module->total_health_checks,
               module->failed_health_checks,
               module->consecutive_failures);
        
        printf("       Response Time: %lu us (last), API Calls: %lu total, %lu failed\n",
               module->response_time_us,
               module->api_calls_total,
               module->api_calls_failed);
        
        if (module->diag_callback && module->total_health_checks > 0) {
            printf("       Metrics: [%lu, %lu, %lu, %lu]\n",
                   module->callback_metrics[0],
                   module->callback_metrics[1],
                   module->callback_metrics[2],
                   module->callback_metrics[3]);
        }
        
        module = module->next;
    }
    
    return SUCCESS;
}

/* Week 1 specific: NE2000 emulation module integration validation */
int module_integration_validate_ne2000_emulation(void) {
    if (!g_module_integration.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    debug_log_info("Validating NE2000 emulation module integration...");
    
    /* Check if all required modules are healthy */
    bool ptask_healthy = false;
    bool corkscrw_healthy = false;
    bool boomtex_healthy = false;
    bool driver_api_healthy = false;
    
    module_entry_t *module = g_module_integration.modules_head;
    while (module) {
        if (module->status == MODULE_STATUS_HEALTHY) {
            switch (module->module_type) {
                case MODULE_TYPE_PTASK:
                    ptask_healthy = true;
                    break;
                case MODULE_TYPE_CORKSCRW:
                    corkscrw_healthy = true;
                    break;
                case MODULE_TYPE_BOOMTEX:
                    boomtex_healthy = true;
                    break;
                case MODULE_TYPE_DRIVER_API:
                    driver_api_healthy = true;
                    break;
            }
        }
        module = module->next;
    }
    
    if (!ptask_healthy) {
        debug_log_error("PTASK module not healthy for NE2000 emulation");
        return ERROR_MODULE_INIT_FAILED;
    }
    
    if (!driver_api_healthy) {
        debug_log_error("Driver API not healthy for NE2000 emulation");
        return ERROR_MODULE_INIT_FAILED;
    }
    
    debug_log_info("NE2000 emulation module integration validation passed");
    return SUCCESS;
}

/* Cleanup module integration system */
void module_integration_cleanup(void) {
    if (!g_module_integration.initialized) {
        return;
    }
    
    debug_log_info("Cleaning up module integration system");
    
    /* Free all registered modules */
    module_entry_t *module = g_module_integration.modules_head;
    while (module) {
        module_entry_t *next = module->next;
        
        if (module->module_data) {
            free(module->module_data);
        }
        
        free(module);
        module = next;
    }
    
    memset(&g_module_integration, 0, sizeof(module_integration_t));
}