/**
 * @file runtime_config.c
 * @brief Runtime Configuration API Implementation
 * 
 * Phase 5 Enhancement: Dynamic reconfiguration without restart
 * Allows real-time adjustment of driver parameters
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/runtime_config.h"
#include "../../include/logging.h"
#include "../../include/api.h"
#include "../../include/memory.h"
#include "../../include/routing.h"
#include "../../include/stats.h"
#include <string.h>
#include <stdio.h>

/* Global configuration manager */
static runtime_config_manager_t g_config_manager = {0};
static bool g_initialized = false;

/* Configuration parameter definitions */
static const config_param_def_t g_param_definitions[] = {
    /* Logging Parameters */
    {CONFIG_PARAM_LOG_LEVEL, CONFIG_TYPE_UINT8, CONFIG_CAT_LOGGING,
     "log_level", "Logging verbosity (0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG)",
     0, 4, 0, CONFIG_FLAG_DYNAMIC},
    
    {CONFIG_PARAM_LOG_DESTINATION, CONFIG_TYPE_UINT8, CONFIG_CAT_LOGGING,
     "log_dest", "Log destination (0=NONE, 1=CONSOLE, 2=FILE, 3=BOTH)",
     0, 3, 0, CONFIG_FLAG_DYNAMIC},
    
    /* Memory Parameters */
    {CONFIG_PARAM_BUFFER_SIZE, CONFIG_TYPE_UINT16, CONFIG_CAT_MEMORY,
     "buffer_size", "Packet buffer size in bytes",
     256, 8192, 0, CONFIG_FLAG_REQUIRES_RESET},
    
    {CONFIG_PARAM_BUFFER_COUNT, CONFIG_TYPE_UINT16, CONFIG_CAT_MEMORY,
     "buffer_count", "Number of packet buffers",
     4, 256, 0, CONFIG_FLAG_REQUIRES_RESET},
    
    {CONFIG_PARAM_XMS_ENABLE, CONFIG_TYPE_BOOL, CONFIG_CAT_MEMORY,
     "xms_enable", "Enable XMS memory usage",
     0, 1, 0, CONFIG_FLAG_DYNAMIC},
    
    {CONFIG_PARAM_XMS_THRESHOLD, CONFIG_TYPE_UINT32, CONFIG_CAT_MEMORY,
     "xms_threshold", "XMS migration threshold in bytes",
     1024, 65536, 0, CONFIG_FLAG_DYNAMIC},
    
    /* Network Parameters */
    {CONFIG_PARAM_PROMISCUOUS, CONFIG_TYPE_BOOL, CONFIG_CAT_NETWORK,
     "promiscuous", "Enable promiscuous mode",
     0, 1, 0, CONFIG_FLAG_DYNAMIC | CONFIG_FLAG_PER_NIC},
    
    {CONFIG_PARAM_MULTICAST, CONFIG_TYPE_BOOL, CONFIG_CAT_NETWORK,
     "multicast", "Enable multicast reception",
     0, 1, 0, CONFIG_FLAG_DYNAMIC | CONFIG_FLAG_PER_NIC},
    
    {CONFIG_PARAM_MTU, CONFIG_TYPE_UINT16, CONFIG_CAT_NETWORK,
     "mtu", "Maximum transmission unit",
     64, 1518, 0, CONFIG_FLAG_DYNAMIC | CONFIG_FLAG_PER_NIC},
    
    /* Performance Parameters */
    {CONFIG_PARAM_IRQ_COALESCE, CONFIG_TYPE_UINT16, CONFIG_CAT_PERFORMANCE,
     "irq_coalesce", "Interrupt coalescing in microseconds",
     0, 10000, 0, CONFIG_FLAG_DYNAMIC | CONFIG_FLAG_PER_NIC},
    
    {CONFIG_PARAM_TX_QUEUE_SIZE, CONFIG_TYPE_UINT16, CONFIG_CAT_PERFORMANCE,
     "tx_queue", "Transmit queue size",
     1, 64, 0, CONFIG_FLAG_REQUIRES_RESET | CONFIG_FLAG_PER_NIC},
    
    {CONFIG_PARAM_RX_QUEUE_SIZE, CONFIG_TYPE_UINT16, CONFIG_CAT_PERFORMANCE,
     "rx_queue", "Receive queue size",
     1, 64, 0, CONFIG_FLAG_REQUIRES_RESET | CONFIG_FLAG_PER_NIC},
    
    /* Routing Parameters */
    {CONFIG_PARAM_ROUTING_MODE, CONFIG_TYPE_UINT8, CONFIG_CAT_ROUTING,
     "route_mode", "Routing mode (0=STATIC, 1=FLOW, 2=LOAD_BALANCE)",
     0, 2, 0, CONFIG_FLAG_DYNAMIC},
    
    {CONFIG_PARAM_DEFAULT_ROUTE, CONFIG_TYPE_UINT8, CONFIG_CAT_ROUTING,
     "default_route", "Default NIC index for routing",
     0, 3, 0, CONFIG_FLAG_DYNAMIC},
    
    /* Diagnostics Parameters */
    {CONFIG_PARAM_STATS_INTERVAL, CONFIG_TYPE_UINT16, CONFIG_CAT_DIAGNOSTICS,
     "stats_interval", "Statistics update interval in seconds",
     1, 3600, 0, CONFIG_FLAG_DYNAMIC},
    
    {CONFIG_PARAM_DIAG_MODE, CONFIG_TYPE_BOOL, CONFIG_CAT_DIAGNOSTICS,
     "diag_mode", "Enable diagnostic mode",
     0, 1, 0, CONFIG_FLAG_DYNAMIC},
    
    /* End marker */
    {0, 0, 0, NULL, NULL, 0, 0, 0, 0}
};

/**
 * @brief Initialize runtime configuration system
 */
int runtime_config_init(void) {
    if (g_initialized) {
        log_warning("Runtime config already initialized");
        return SUCCESS;
    }
    
    log_info("Initializing runtime configuration system");
    
    /* Clear manager structure */
    memset(&g_config_manager, 0, sizeof(runtime_config_manager_t));
    
    /* Count parameter definitions */
    const config_param_def_t *def = g_param_definitions;
    while (def->param_id != 0) {
        g_config_manager.param_count++;
        def++;
    }
    
    /* Allocate parameter value storage */
    g_config_manager.param_values = (config_param_value_t*)memory_allocate(
        g_config_manager.param_count * sizeof(config_param_value_t),
        MEMORY_TYPE_KERNEL
    );
    
    if (!g_config_manager.param_values) {
        log_error("Failed to allocate parameter storage");
        return ERROR_MEMORY;
    }
    
    /* Initialize parameter values to defaults */
    for (int i = 0; i < g_config_manager.param_count; i++) {
        g_config_manager.param_values[i].param_id = g_param_definitions[i].param_id;
        g_config_manager.param_values[i].current_value = 0;
        g_config_manager.param_values[i].pending_value = 0;
        g_config_manager.param_values[i].has_pending = false;
        g_config_manager.param_values[i].nic_index = 0xFF; /* Global */
    }
    
    /* Set default configuration */
    runtime_config_set_defaults();
    
    g_config_manager.flags = CONFIG_FLAG_INITIALIZED;
    g_initialized = true;
    
    log_info("Runtime configuration initialized with %u parameters", 
             g_config_manager.param_count);
    
    return SUCCESS;
}

/**
 * @brief Clean up runtime configuration system
 */
int runtime_config_cleanup(void) {
    if (!g_initialized) {
        return SUCCESS;
    }
    
    log_info("Cleaning up runtime configuration");
    
    /* Free parameter storage */
    if (g_config_manager.param_values) {
        memory_free(g_config_manager.param_values);
        g_config_manager.param_values = NULL;
    }
    
    /* Free change callbacks */
    config_callback_t *cb = g_config_manager.callbacks;
    while (cb) {
        config_callback_t *next = cb->next;
        memory_free(cb);
        cb = next;
    }
    
    memset(&g_config_manager, 0, sizeof(runtime_config_manager_t));
    g_initialized = false;
    
    return SUCCESS;
}

/**
 * @brief Set parameter value
 */
int runtime_config_set_param(uint16_t param_id, uint32_t value, uint8_t nic_index) {
    if (!g_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    /* Find parameter definition */
    const config_param_def_t *def = runtime_config_get_definition(param_id);
    if (!def) {
        log_error("Unknown parameter ID: 0x%04X", param_id);
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate value range */
    if (value < def->min_value || value > def->max_value) {
        log_error("Parameter %s value %lu out of range [%lu-%lu]",
                  def->name, value, def->min_value, def->max_value);
        return ERROR_OUT_OF_RANGE;
    }
    
    /* Check if parameter is per-NIC */
    if ((def->flags & CONFIG_FLAG_PER_NIC) && nic_index >= MAX_NICS) {
        log_error("Invalid NIC index %u for parameter %s", nic_index, def->name);
        return ERROR_INVALID_NIC;
    }
    
    /* Find or create parameter value entry */
    config_param_value_t *param_val = runtime_config_find_param(param_id, nic_index);
    if (!param_val) {
        /* Need to allocate new per-NIC entry */
        /* For simplicity, we'll use the first free slot */
        for (int i = 0; i < g_config_manager.param_count; i++) {
            if (g_config_manager.param_values[i].param_id == 0) {
                /* Initialize the entire entry to prevent stale data */
                memset(&g_config_manager.param_values[i], 0, sizeof(config_param_value_t));
                param_val = &g_config_manager.param_values[i];
                param_val->param_id = param_id;
                param_val->nic_index = nic_index;
                param_val->current_value = def->default_value;  /* Initialize to default */
                param_val->pending_value = 0;
                param_val->has_pending = 0;  /* Use int for DOS compatibility */
                break;
            }
        }
        
        if (!param_val) {
            log_error("No free parameter slots");
            return ERROR_NO_SPACE;  /* More specific error */
        }
    }
    
    /* Apply or queue the change */
    if (def->flags & CONFIG_FLAG_REQUIRES_RESET) {
        /* Queue for next reset - check for duplicate pending */
        if (!param_val->has_pending || param_val->pending_value != value) {
            param_val->pending_value = value;
            if (!param_val->has_pending) {
                g_config_manager.pending_changes++;  /* Only increment if first time */
            }
            param_val->has_pending = 1;  /* Use int for DOS */
        }
        
        log_info("Parameter %s queued for next reset (value=%lu)",
                 def->name, value);
    } else {
        /* Apply immediately */
        uint32_t old_value = param_val->current_value;
        
        /* Apply the configuration change first, before updating current_value */
        int result = runtime_config_apply_param(param_id, value, nic_index);
        if (result != SUCCESS) {
            log_error("Failed to apply parameter %s", def->name);
            g_config_manager.stats.failed_changes++;
            return result;
        }
        
        /* Only update current value after successful application */
        param_val->current_value = value;
        
        /* Notify callbacks */
        runtime_config_notify_callbacks(param_id, old_value, value, nic_index);
        
        g_config_manager.stats.immediate_changes++;
        
        log_info("Parameter %s changed from %lu to %lu",
                 def->name, old_value, value);
    }
    
    g_config_manager.stats.total_changes++;
    
    return SUCCESS;
}

/**
 * @brief Get parameter value
 */
int runtime_config_get_param(uint16_t param_id, uint32_t *value, uint8_t nic_index) {
    if (!g_initialized || !value) {
        return ERROR_INVALID_PARAM;
    }
    
    config_param_value_t *param_val = runtime_config_find_param(param_id, nic_index);
    if (!param_val) {
        /* Return default value */
        const config_param_def_t *def = runtime_config_get_definition(param_id);
        if (!def) {
            return ERROR_INVALID_PARAM;
        }
        *value = def->default_value;
    } else {
        *value = param_val->current_value;
    }
    
    return SUCCESS;
}

/**
 * @brief Apply pending configuration changes
 */
int runtime_config_apply_pending(void) {
    if (!g_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (g_config_manager.pending_changes == 0) {
        log_info("No pending configuration changes");
        return SUCCESS;
    }
    
    log_info("Applying %u pending configuration changes",
             g_config_manager.pending_changes);
    
    int applied = 0;
    int failed = 0;
    
    for (int i = 0; i < g_config_manager.param_count; i++) {
        config_param_value_t *param_val = &g_config_manager.param_values[i];
        
        if (param_val->has_pending) {
            uint32_t old_value = param_val->current_value;
            param_val->current_value = param_val->pending_value;
            param_val->has_pending = false;
            
            /* Apply the change */
            int result = runtime_config_apply_param(
                param_val->param_id,
                param_val->current_value,
                param_val->nic_index
            );
            
            if (result == SUCCESS) {
                applied++;
                
                /* Notify callbacks */
                runtime_config_notify_callbacks(
                    param_val->param_id,
                    old_value,
                    param_val->current_value,
                    param_val->nic_index
                );
            } else {
                failed++;
                /* Rollback */
                param_val->current_value = old_value;
                log_error("Failed to apply parameter 0x%04X", param_val->param_id);
            }
        }
    }
    
    g_config_manager.pending_changes = 0;
    g_config_manager.stats.reset_applied_changes += applied;
    
    log_info("Applied %d changes, %d failed", applied, failed);
    
    return (failed > 0) ? ERROR_PARTIAL : SUCCESS;
}

/**
 * @brief Register configuration change callback
 */
int runtime_config_register_callback(config_callback_func_t callback, 
                                    uint16_t param_id,
                                    void *context) {
    if (!g_initialized || !callback) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Allocate callback entry */
    config_callback_t *cb = (config_callback_t*)memory_allocate(
        sizeof(config_callback_t),
        MEMORY_TYPE_KERNEL
    );
    
    if (!cb) {
        return ERROR_MEMORY;
    }
    
    cb->callback = callback;
    cb->param_id = param_id;
    cb->context = context;
    cb->next = g_config_manager.callbacks;
    g_config_manager.callbacks = cb;
    
    log_debug("Registered callback for parameter 0x%04X", param_id);
    
    return SUCCESS;
}

/**
 * @brief Export configuration to buffer
 */
int runtime_config_export(void *buffer, uint16_t *size) {
    if (!g_initialized || !buffer || !size) {
        return ERROR_INVALID_PARAM;
    }
    
    config_export_t *export = (config_export_t*)buffer;
    uint16_t required_size = sizeof(config_export_t) + 
        g_config_manager.param_count * sizeof(config_param_export_t);
    
    if (*size < required_size) {
        *size = required_size;
        return ERROR_BUFFER_TOO_SMALL;
    }
    
    /* Fill export header */
    export->magic = CONFIG_EXPORT_MAGIC;
    export->version = CONFIG_EXPORT_VERSION;
    export->param_count = 0;
    export->checksum = 0;
    
    /* Export active parameters */
    config_param_export_t *param = (config_param_export_t*)(export + 1);
    
    for (int i = 0; i < g_config_manager.param_count; i++) {
        config_param_value_t *val = &g_config_manager.param_values[i];
        if (val->param_id != 0) {
            param->param_id = val->param_id;
            param->value = val->current_value;
            param->nic_index = val->nic_index;
            param++;
            export->param_count++;
        }
    }
    
    /* Calculate checksum */
    export->checksum = runtime_config_calculate_checksum(export, 
        sizeof(config_export_t) + export->param_count * sizeof(config_param_export_t));
    
    *size = required_size;
    g_config_manager.stats.exports++;
    
    log_info("Exported %u configuration parameters", export->param_count);
    
    return SUCCESS;
}

/**
 * @brief Import configuration from buffer
 */
int runtime_config_import(const void *buffer, uint16_t size) {
    if (!g_initialized || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    const config_export_t *import = (const config_export_t*)buffer;
    
    /* Validate magic and version */
    if (import->magic != CONFIG_EXPORT_MAGIC) {
        log_error("Invalid configuration magic: 0x%08X", import->magic);
        return ERROR_INVALID_FORMAT;
    }
    
    if (import->version != CONFIG_EXPORT_VERSION) {
        log_error("Unsupported configuration version: %u", import->version);
        return ERROR_VERSION_MISMATCH;
    }
    
    /* Verify size */
    uint16_t expected_size = sizeof(config_export_t) + 
        import->param_count * sizeof(config_param_export_t);
    
    if (size < expected_size) {
        log_error("Configuration buffer too small");
        return ERROR_BUFFER_TOO_SMALL;
    }
    
    /* Verify checksum */
    uint16_t calc_checksum = runtime_config_calculate_checksum(import, expected_size);
    if (calc_checksum != 0) {  /* Checksum should be 0 when including the checksum field */
        log_error("Configuration checksum mismatch");
        return ERROR_CHECKSUM;
    }
    
    /* Import parameters */
    const config_param_export_t *param = (const config_param_export_t*)(import + 1);
    int imported = 0;
    int failed = 0;
    
    for (int i = 0; i < import->param_count; i++) {
        int result = runtime_config_set_param(
            param->param_id,
            param->value,
            param->nic_index
        );
        
        if (result == SUCCESS) {
            imported++;
        } else {
            failed++;
            log_warning("Failed to import parameter 0x%04X", param->param_id);
        }
        
        param++;
    }
    
    g_config_manager.stats.imports++;
    
    log_info("Imported %d parameters, %d failed", imported, failed);
    
    return (failed > 0) ? ERROR_PARTIAL : SUCCESS;
}

/**
 * @brief Set default configuration values
 */
void runtime_config_set_defaults(void) {
    log_info("Setting default configuration values");
    
    /* Apply defaults from definitions */
    const config_param_def_t *def = g_param_definitions;
    while (def->param_id != 0) {
        runtime_config_set_param(def->param_id, def->default_value, 0xFF);
        def++;
    }
}

/**
 * @brief Dump configuration for debugging
 */
void runtime_config_dump(void) {
    if (!g_initialized) {
        printf("Runtime configuration not initialized\n");
        return;
    }
    
    printf("\n=== Runtime Configuration ===\n");
    printf("Parameters: %u active, %u pending\n",
           g_config_manager.param_count, g_config_manager.pending_changes);
    
    /* Dump by category */
    for (int cat = 0; cat < CONFIG_CAT_COUNT; cat++) {
        const char *cat_name[] = {
            "General", "Memory", "Network", "Performance", 
            "Routing", "Logging", "Diagnostics"
        };
        
        printf("\n%s Parameters:\n", cat_name[cat]);
        
        const config_param_def_t *def = g_param_definitions;
        while (def->param_id != 0) {
            if (def->category == cat) {
                uint32_t value;
                runtime_config_get_param(def->param_id, &value, 0xFF);
                
                printf("  %-20s: %lu", def->name, value);
                
                /* Show pending value if any */
                config_param_value_t *val = runtime_config_find_param(def->param_id, 0xFF);
                if (val && val->has_pending) {
                    printf(" (pending: %lu)", val->pending_value);
                }
                
                printf(" [%lu-%lu]\n", def->min_value, def->max_value);
            }
            def++;
        }
    }
    
    /* Dump statistics */
    printf("\nConfiguration Statistics:\n");
    printf("  Total changes: %lu\n", g_config_manager.stats.total_changes);
    printf("  Immediate changes: %lu\n", g_config_manager.stats.immediate_changes);
    printf("  Reset-applied changes: %lu\n", g_config_manager.stats.reset_applied_changes);
    printf("  Failed changes: %lu\n", g_config_manager.stats.failed_changes);
    printf("  Exports: %lu\n", g_config_manager.stats.exports);
    printf("  Imports: %lu\n", g_config_manager.stats.imports);
    printf("\n");
}

/* Internal helper functions */

static const config_param_def_t* runtime_config_get_definition(uint16_t param_id) {
    const config_param_def_t *def = g_param_definitions;
    while (def->param_id != 0) {
        if (def->param_id == param_id) {
            return def;
        }
        def++;
    }
    return NULL;
}

static config_param_value_t* runtime_config_find_param(uint16_t param_id, uint8_t nic_index) {
    for (int i = 0; i < g_config_manager.param_count; i++) {
        config_param_value_t *val = &g_config_manager.param_values[i];
        if (val->param_id == param_id) {
            if (nic_index == 0xFF || val->nic_index == 0xFF || val->nic_index == nic_index) {
                return val;
            }
        }
    }
    return NULL;
}

static int runtime_config_apply_param(uint16_t param_id, uint32_t value, uint8_t nic_index) {
    /* Apply configuration based on parameter ID */
    switch (param_id) {
        case CONFIG_PARAM_LOG_LEVEL:
            log_set_level((uint8_t)value);
            break;
            
        case CONFIG_PARAM_PROMISCUOUS:
            /* Would call NIC-specific promiscuous mode function */
            log_info("Setting promiscuous mode to %s for NIC %u",
                     value ? "ON" : "OFF", nic_index);
            break;
            
        case CONFIG_PARAM_XMS_ENABLE:
            /* Would enable/disable XMS usage */
            log_info("XMS memory %s", value ? "enabled" : "disabled");
            break;
            
        case CONFIG_PARAM_ROUTING_MODE:
            /* Would change routing mode */
            log_info("Routing mode changed to %lu", value);
            break;
            
        /* Add more parameter handlers as needed */
        
        default:
            log_debug("Parameter 0x%04X set to %lu", param_id, value);
            break;
    }
    
    return SUCCESS;
}

static void runtime_config_notify_callbacks(uint16_t param_id, uint32_t old_value, 
                                           uint32_t new_value, uint8_t nic_index) {
    config_callback_t *cb = g_config_manager.callbacks;
    
    while (cb) {
        if (cb->param_id == 0 || cb->param_id == param_id) {
            cb->callback(param_id, old_value, new_value, nic_index, cb->context);
        }
        cb = cb->next;
    }
}

static uint16_t runtime_config_calculate_checksum(const void *data, uint16_t size) {
    const uint16_t *ptr = (const uint16_t*)data;
    uint32_t sum = 0;
    
    while (size > 1) {
        sum += *ptr++;
        size -= 2;
    }
    
    if (size > 0) {
        sum += *(const uint8_t*)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}