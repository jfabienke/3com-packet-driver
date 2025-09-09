/**
 * @file config_interface.c
 * @brief Configuration Interface with Runtime Parameter Modification - Agent 12 Implementation
 *
 * 3Com Packet Driver - Configuration Interface
 * Provides runtime configuration and parameter modification capabilities
 * for the unified driver system and all loaded modules.
 * 
 * Features:
 * - Runtime parameter modification
 * - Module-specific configuration
 * - Persistent configuration storage
 * - Configuration validation
 * - Hot-swappable settings
 * - Configuration versioning
 * 
 * Agent 12: Driver API
 * Week 1 Deliverable - Configuration interface
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "unified_api.h"
#include "../include/logging.h"
#include "../loader/dos_services.h"
#include "../include/config.h"
#include "../../docs/agents/shared/error-codes.h"

/* Configuration Constants */
#define CONFIG_SIGNATURE           "CONF"
#define CONFIG_VERSION             0x0100
#define MAX_CONFIG_PARAMETERS      128
#define MAX_PARAMETER_NAME_LEN     32
#define MAX_PARAMETER_VALUE_LEN    64
#define CONFIG_FILE_MAGIC          0x43464754  /* "CFGT" */

/* Parameter Types */
typedef enum {
    PARAM_TYPE_INVALID = 0,
    PARAM_TYPE_UINT8,
    PARAM_TYPE_UINT16,
    PARAM_TYPE_UINT32,
    PARAM_TYPE_STRING,
    PARAM_TYPE_BOOLEAN,
    PARAM_TYPE_ENUM
} parameter_type_t;

/* Parameter Scope */
typedef enum {
    PARAM_SCOPE_GLOBAL = 0,
    PARAM_SCOPE_MODULE,
    PARAM_SCOPE_HANDLE,
    PARAM_SCOPE_INTERFACE
} parameter_scope_t;

/* Parameter Flags */
#define PARAM_FLAG_READONLY        0x01  /* Read-only parameter */
#define PARAM_FLAG_RUNTIME         0x02  /* Can be modified at runtime */
#define PARAM_FLAG_PERSISTENT      0x04  /* Saved to configuration file */
#define PARAM_FLAG_RESTART_REQUIRED 0x08  /* Requires driver restart */
#define PARAM_FLAG_MODULE_SPECIFIC 0x10  /* Module-specific parameter */

/* Configuration Parameter */
typedef struct {
    char name[MAX_PARAMETER_NAME_LEN];      /* Parameter name */
    parameter_type_t type;                  /* Parameter type */
    parameter_scope_t scope;                /* Parameter scope */
    uint8_t flags;                          /* Parameter flags */
    uint8_t module_id;                      /* Module ID (if module-specific) */
    
    /* Value Storage */
    union {
        uint8_t uint8_val;
        uint16_t uint16_val;
        uint32_t uint32_val;
        char string_val[MAX_PARAMETER_VALUE_LEN];
        bool bool_val;
    } value;
    
    /* Default Value */
    union {
        uint8_t uint8_val;
        uint16_t uint16_val;
        uint32_t uint32_val;
        char string_val[MAX_PARAMETER_VALUE_LEN];
        bool bool_val;
    } default_value;
    
    /* Validation */
    uint32_t min_value;                     /* Minimum value (for numeric types) */
    uint32_t max_value;                     /* Maximum value (for numeric types) */
    char *enum_values;                      /* Enum value list (for enum type) */
    
    /* Metadata */
    char description[64];                   /* Parameter description */
    uint32_t last_modified;                 /* Last modification time */
    bool modified;                          /* Modified since last save */
    
} config_parameter_t;

/* Configuration Manager */
typedef struct {
    char signature[4];                      /* Configuration signature */
    uint16_t version;                       /* Configuration version */
    bool initialized;                       /* Initialization flag */
    
    /* Parameters */
    uint16_t parameter_count;               /* Number of active parameters */
    config_parameter_t parameters[MAX_CONFIG_PARAMETERS];
    
    /* File Management */
    char config_filename[128];              /* Configuration file name */
    uint32_t last_save_time;                /* Last save timestamp */
    bool auto_save_enabled;                 /* Auto-save enabled */
    uint32_t auto_save_interval;            /* Auto-save interval in ms */
    
    /* Change Tracking */
    uint16_t changes_pending;               /* Number of pending changes */
    bool restart_required;                  /* Restart required flag */
    
} config_manager_t;

/* Global Configuration Manager */
static config_manager_t g_config_manager;

/* Forward Declarations */
static int register_default_parameters(void);
static int validate_parameter_value(const config_parameter_t *param, const void *value);
static config_parameter_t *find_parameter(const char *name, uint8_t module_id);
static int save_configuration_to_file(const char *filename);
static int load_configuration_from_file(const char *filename);
static const char *parameter_type_to_string(parameter_type_t type);
static const char *parameter_scope_to_string(parameter_scope_t scope);

/**
 * @brief Initialize Configuration Interface
 * @param config_file Configuration file name (NULL for default)
 * @return SUCCESS on success, error code on failure
 */
int config_interface_init(const char *config_file) {
    if (g_config_manager.initialized) {
        return SUCCESS;
    }
    
    log_info("Initializing Configuration Interface");
    
    /* Initialize configuration manager */
    memset(&g_config_manager, 0, sizeof(config_manager_t));
    strncpy(g_config_manager.signature, CONFIG_SIGNATURE, 4);
    g_config_manager.version = CONFIG_VERSION;
    g_config_manager.auto_save_enabled = true;
    g_config_manager.auto_save_interval = 30000; /* 30 seconds */
    
    /* Set configuration filename */
    if (config_file) {
        strncpy(g_config_manager.config_filename, config_file, sizeof(g_config_manager.config_filename) - 1);
        g_config_manager.config_filename[sizeof(g_config_manager.config_filename) - 1] = '\0';
    } else {
        strcpy(g_config_manager.config_filename, "3CDRV.CFG");
    }
    
    /* Register default parameters */
    int result = register_default_parameters();
    if (result != SUCCESS) {
        log_error("Failed to register default parameters: %d", result);
        return result;
    }
    
    /* Try to load existing configuration */
    result = load_configuration_from_file(g_config_manager.config_filename);
    if (result != SUCCESS) {
        log_warning("Could not load configuration file, using defaults");
    }
    
    g_config_manager.initialized = true;
    log_info("Configuration Interface initialized (file: %s)", g_config_manager.config_filename);
    
    return SUCCESS;
}

/**
 * @brief Cleanup Configuration Interface
 * @return SUCCESS on success, error code on failure
 */
int config_interface_cleanup(void) {
    if (!g_config_manager.initialized) {
        return SUCCESS;
    }
    
    log_info("Cleaning up Configuration Interface");
    
    /* Save pending changes */
    if (g_config_manager.changes_pending > 0) {
        log_info("Saving %d pending configuration changes", g_config_manager.changes_pending);
        save_configuration_to_file(g_config_manager.config_filename);
    }
    
    g_config_manager.initialized = false;
    log_info("Configuration Interface cleanup completed");
    
    return SUCCESS;
}

/**
 * @brief Set configuration parameter value
 * @param name Parameter name
 * @param value Parameter value
 * @param module_id Module ID (0xFF for global parameters)
 * @return SUCCESS on success, error code on failure
 */
int config_interface_set_parameter(const char *name, const void *value, uint8_t module_id) {
    if (!g_config_manager.initialized || !name || !value) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Find parameter */
    config_parameter_t *param = find_parameter(name, module_id);
    if (!param) {
        log_error("Parameter '%s' not found", name);
        return ERROR_NOT_FOUND;
    }
    
    /* Check if parameter is read-only */
    if (param->flags & PARAM_FLAG_READONLY) {
        log_error("Parameter '%s' is read-only", name);
        return ERROR_ACCESS_DENIED;
    }
    
    /* Validate parameter value */
    int result = validate_parameter_value(param, value);
    if (result != SUCCESS) {
        log_error("Invalid value for parameter '%s'", name);
        return result;
    }
    
    log_debug("Setting parameter '%s' (type=%s, scope=%s)", 
              name, parameter_type_to_string(param->type), 
              parameter_scope_to_string(param->scope));
    
    /* Set parameter value based on type */
    switch (param->type) {
        case PARAM_TYPE_UINT8:
            param->value.uint8_val = *(uint8_t *)value;
            break;
        case PARAM_TYPE_UINT16:
            param->value.uint16_val = *(uint16_t *)value;
            break;
        case PARAM_TYPE_UINT32:
            param->value.uint32_val = *(uint32_t *)value;
            break;
        case PARAM_TYPE_STRING:
            strncpy(param->value.string_val, (char *)value, MAX_PARAMETER_VALUE_LEN - 1);
            param->value.string_val[MAX_PARAMETER_VALUE_LEN - 1] = '\0';
            break;
        case PARAM_TYPE_BOOLEAN:
            param->value.bool_val = *(bool *)value;
            break;
        case PARAM_TYPE_ENUM:
            param->value.uint32_val = *(uint32_t *)value;
            break;
        default:
            return ERROR_INVALID_PARAM;
    }
    
    /* Update metadata */
    param->last_modified = get_system_time();
    param->modified = true;
    g_config_manager.changes_pending++;
    
    /* Check if restart is required */
    if (param->flags & PARAM_FLAG_RESTART_REQUIRED) {
        g_config_manager.restart_required = true;
        log_warning("Parameter '%s' change requires driver restart", name);
    }
    
    log_info("Parameter '%s' set successfully", name);
    
    return SUCCESS;
}

/**
 * @brief Get configuration parameter value
 * @param name Parameter name
 * @param value Buffer to receive parameter value
 * @param value_size Size of value buffer
 * @param module_id Module ID (0xFF for global parameters)
 * @return SUCCESS on success, error code on failure
 */
int config_interface_get_parameter(const char *name, void *value, uint16_t value_size, uint8_t module_id) {
    if (!g_config_manager.initialized || !name || !value) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Find parameter */
    config_parameter_t *param = find_parameter(name, module_id);
    if (!param) {
        return ERROR_NOT_FOUND;
    }
    
    /* Get parameter value based on type */
    switch (param->type) {
        case PARAM_TYPE_UINT8:
            if (value_size < sizeof(uint8_t)) return ERROR_BUFFER_TOO_SMALL;
            *(uint8_t *)value = param->value.uint8_val;
            break;
        case PARAM_TYPE_UINT16:
            if (value_size < sizeof(uint16_t)) return ERROR_BUFFER_TOO_SMALL;
            *(uint16_t *)value = param->value.uint16_val;
            break;
        case PARAM_TYPE_UINT32:
            if (value_size < sizeof(uint32_t)) return ERROR_BUFFER_TOO_SMALL;
            *(uint32_t *)value = param->value.uint32_val;
            break;
        case PARAM_TYPE_STRING:
            if (value_size < strlen(param->value.string_val) + 1) return ERROR_BUFFER_TOO_SMALL;
            strcpy((char *)value, param->value.string_val);
            break;
        case PARAM_TYPE_BOOLEAN:
            if (value_size < sizeof(bool)) return ERROR_BUFFER_TOO_SMALL;
            *(bool *)value = param->value.bool_val;
            break;
        case PARAM_TYPE_ENUM:
            if (value_size < sizeof(uint32_t)) return ERROR_BUFFER_TOO_SMALL;
            *(uint32_t *)value = param->value.uint32_val;
            break;
        default:
            return ERROR_INVALID_PARAM;
    }
    
    return SUCCESS;
}

/**
 * @brief Register new configuration parameter
 * @param name Parameter name
 * @param type Parameter type
 * @param scope Parameter scope
 * @param flags Parameter flags
 * @param default_value Default value
 * @param description Parameter description
 * @return SUCCESS on success, error code on failure
 */
int config_interface_register_parameter(const char *name, parameter_type_t type, 
                                       parameter_scope_t scope, uint8_t flags,
                                       const void *default_value, const char *description) {
    
    if (!g_config_manager.initialized || !name || !default_value) {
        return ERROR_INVALID_PARAM;
    }
    
    if (g_config_manager.parameter_count >= MAX_CONFIG_PARAMETERS) {
        return ERROR_BUFFER_TOO_SMALL;
    }
    
    /* Check if parameter already exists */
    if (find_parameter(name, 0xFF)) {
        return ERROR_ALREADY_EXISTS;
    }
    
    config_parameter_t *param = &g_config_manager.parameters[g_config_manager.parameter_count];
    
    /* Initialize parameter */
    strncpy(param->name, name, MAX_PARAMETER_NAME_LEN - 1);
    param->name[MAX_PARAMETER_NAME_LEN - 1] = '\0';
    param->type = type;
    param->scope = scope;
    param->flags = flags;
    param->module_id = 0xFF; /* Global by default */
    
    /* Set default value */
    switch (type) {
        case PARAM_TYPE_UINT8:
            param->default_value.uint8_val = *(uint8_t *)default_value;
            param->value.uint8_val = param->default_value.uint8_val;
            break;
        case PARAM_TYPE_UINT16:
            param->default_value.uint16_val = *(uint16_t *)default_value;
            param->value.uint16_val = param->default_value.uint16_val;
            break;
        case PARAM_TYPE_UINT32:
            param->default_value.uint32_val = *(uint32_t *)default_value;
            param->value.uint32_val = param->default_value.uint32_val;
            break;
        case PARAM_TYPE_STRING:
            strncpy(param->default_value.string_val, (char *)default_value, MAX_PARAMETER_VALUE_LEN - 1);
            param->default_value.string_val[MAX_PARAMETER_VALUE_LEN - 1] = '\0';
            strcpy(param->value.string_val, param->default_value.string_val);
            break;
        case PARAM_TYPE_BOOLEAN:
            param->default_value.bool_val = *(bool *)default_value;
            param->value.bool_val = param->default_value.bool_val;
            break;
        case PARAM_TYPE_ENUM:
            param->default_value.uint32_val = *(uint32_t *)default_value;
            param->value.uint32_val = param->default_value.uint32_val;
            break;
        default:
            return ERROR_INVALID_PARAM;
    }
    
    /* Set description */
    if (description) {
        strncpy(param->description, description, sizeof(param->description) - 1);
        param->description[sizeof(param->description) - 1] = '\0';
    } else {
        strcpy(param->description, "No description available");
    }
    
    param->last_modified = 0;
    param->modified = false;
    
    g_config_manager.parameter_count++;
    
    log_debug("Registered parameter '%s' (type=%s, scope=%s)", 
              name, parameter_type_to_string(type), parameter_scope_to_string(scope));
    
    return SUCCESS;
}

/**
 * @brief Save configuration to file
 * @return SUCCESS on success, error code on failure
 */
int config_interface_save(void) {
    if (!g_config_manager.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    return save_configuration_to_file(g_config_manager.config_filename);
}

/**
 * @brief Load configuration from file
 * @return SUCCESS on success, error code on failure
 */
int config_interface_load(void) {
    if (!g_config_manager.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    return load_configuration_from_file(g_config_manager.config_filename);
}

/**
 * @brief Reset parameter to default value
 * @param name Parameter name
 * @param module_id Module ID (0xFF for global parameters)
 * @return SUCCESS on success, error code on failure
 */
int config_interface_reset_parameter(const char *name, uint8_t module_id) {
    if (!g_config_manager.initialized || !name) {
        return ERROR_INVALID_PARAM;
    }
    
    config_parameter_t *param = find_parameter(name, module_id);
    if (!param) {
        return ERROR_NOT_FOUND;
    }
    
    /* Reset to default value */
    switch (param->type) {
        case PARAM_TYPE_UINT8:
            param->value.uint8_val = param->default_value.uint8_val;
            break;
        case PARAM_TYPE_UINT16:
            param->value.uint16_val = param->default_value.uint16_val;
            break;
        case PARAM_TYPE_UINT32:
            param->value.uint32_val = param->default_value.uint32_val;
            break;
        case PARAM_TYPE_STRING:
            strcpy(param->value.string_val, param->default_value.string_val);
            break;
        case PARAM_TYPE_BOOLEAN:
            param->value.bool_val = param->default_value.bool_val;
            break;
        case PARAM_TYPE_ENUM:
            param->value.uint32_val = param->default_value.uint32_val;
            break;
    }
    
    param->modified = true;
    g_config_manager.changes_pending++;
    
    log_info("Reset parameter '%s' to default value", name);
    
    return SUCCESS;
}

/* Internal Helper Functions */

static int register_default_parameters(void) {
    int result = SUCCESS;
    
    /* Global Parameters */
    uint8_t debug_level = 2;
    result |= config_interface_register_parameter("debug_level", PARAM_TYPE_UINT8, PARAM_SCOPE_GLOBAL,
                                                  PARAM_FLAG_RUNTIME | PARAM_FLAG_PERSISTENT,
                                                  &debug_level, "Debug logging level (0-4)");
    
    bool auto_detect = true;
    result |= config_interface_register_parameter("auto_detect", PARAM_TYPE_BOOLEAN, PARAM_SCOPE_GLOBAL,
                                                  PARAM_FLAG_RUNTIME | PARAM_FLAG_PERSISTENT,
                                                  &auto_detect, "Enable automatic hardware detection");
    
    uint16_t max_handles = 32;
    result |= config_interface_register_parameter("max_handles", PARAM_TYPE_UINT16, PARAM_SCOPE_GLOBAL,
                                                  PARAM_FLAG_RESTART_REQUIRED | PARAM_FLAG_PERSISTENT,
                                                  &max_handles, "Maximum number of packet handles");
    
    uint32_t stats_interval = 1000;
    result |= config_interface_register_parameter("stats_interval", PARAM_TYPE_UINT32, PARAM_SCOPE_GLOBAL,
                                                  PARAM_FLAG_RUNTIME | PARAM_FLAG_PERSISTENT,
                                                  &stats_interval, "Statistics collection interval (ms)");
    
    /* Module Parameters */
    uint8_t module_priority = 128;
    result |= config_interface_register_parameter("module_priority", PARAM_TYPE_UINT8, PARAM_SCOPE_MODULE,
                                                  PARAM_FLAG_RUNTIME | PARAM_FLAG_PERSISTENT,
                                                  &module_priority, "Module scheduling priority");
    
    bool dma_enabled = true;
    result |= config_interface_register_parameter("dma_enabled", PARAM_TYPE_BOOLEAN, PARAM_SCOPE_MODULE,
                                                  PARAM_FLAG_RUNTIME | PARAM_FLAG_PERSISTENT,
                                                  &dma_enabled, "Enable DMA operations");
    
    return result;
}

static int validate_parameter_value(const config_parameter_t *param, const void *value) {
    if (!param || !value) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate based on parameter type */
    switch (param->type) {
        case PARAM_TYPE_UINT8: {
            uint8_t val = *(uint8_t *)value;
            if (val < param->min_value || val > param->max_value) {
                return ERROR_INVALID_PARAM;
            }
            break;
        }
        case PARAM_TYPE_UINT16: {
            uint16_t val = *(uint16_t *)value;
            if (val < param->min_value || val > param->max_value) {
                return ERROR_INVALID_PARAM;
            }
            break;
        }
        case PARAM_TYPE_UINT32: {
            uint32_t val = *(uint32_t *)value;
            if (val < param->min_value || val > param->max_value) {
                return ERROR_INVALID_PARAM;
            }
            break;
        }
        case PARAM_TYPE_STRING: {
            const char *str = (const char *)value;
            if (strlen(str) >= MAX_PARAMETER_VALUE_LEN) {
                return ERROR_BUFFER_TOO_SMALL;
            }
            break;
        }
        /* Boolean and enum types are inherently valid */
        case PARAM_TYPE_BOOLEAN:
        case PARAM_TYPE_ENUM:
            break;
        default:
            return ERROR_INVALID_PARAM;
    }
    
    return SUCCESS;
}

static config_parameter_t *find_parameter(const char *name, uint8_t module_id) {
    for (int i = 0; i < g_config_manager.parameter_count; i++) {
        config_parameter_t *param = &g_config_manager.parameters[i];
        if (strcmp(param->name, name) == 0) {
            /* Check module ID match for module-specific parameters */
            if (param->scope == PARAM_SCOPE_MODULE && module_id != 0xFF) {
                if (param->module_id == module_id) {
                    return param;
                }
            } else if (module_id == 0xFF || param->scope != PARAM_SCOPE_MODULE) {
                return param;
            }
        }
    }
    return NULL;
}

static int save_configuration_to_file(const char *filename) {
    /* Configuration file saving implementation */
    FILE *f;
    int i;
    config_parameter_t *param;
    
    if (dos_busy()) {
        log_warning("Cannot save config while DOS is busy");
        return ERROR_DOS_BUSY;
    }
    
    f = fopen(filename, "wt");
    if (!f) {
        log_error("Failed to create config file: %s", filename);
        return ERROR_FILE_WRITE_FAILED;
    }
    
    /* Write header comment */
    fprintf(f, "; 3Com Packet Driver Configuration\n");
    fprintf(f, "; Generated automatically - edit with care\n\n");
    
    /* Write parameters in key=value format */
    for (i = 0; i < g_config_manager.parameter_count; i++) {
        param = &g_config_manager.parameters[i];
        
        switch (param->type) {
            case CONFIG_TYPE_INT:
                fprintf(f, "%s=%d\n", param->name, param->value.int_val);
                break;
            case CONFIG_TYPE_STRING:
                fprintf(f, "%s=%s\n", param->name, 
                       param->value.string_val ? param->value.string_val : "");
                break;
            case CONFIG_TYPE_BOOL:
                fprintf(f, "%s=%s\n", param->name, 
                       param->value.bool_val ? "yes" : "no");
                break;
        }
    }
    
    fclose(f);
    
    log_info("Configuration saved to %s (%d parameters)", 
             filename, g_config_manager.parameter_count);
    
    /* Reset change tracking */
    g_config_manager.changes_pending = 0;
    g_config_manager.last_save_time = get_system_time();
    
    /* Mark all parameters as saved */
    for (int i = 0; i < g_config_manager.parameter_count; i++) {
        g_config_manager.parameters[i].modified = false;
    }
    
    return SUCCESS;
}

/* Configuration loading helper */
static int config_load_handler(const char *key, const char *value, int line_number, void *user_data) {
    config_parameter_t *param;
    int *loaded_count = (int *)user_data;
    
    /* Find existing parameter by name */
    param = find_parameter_by_name(key);
    if (!param) {
        log_warning("Unknown configuration parameter: %s (line %d)", key, line_number);
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Update parameter value based on type */
    switch (param->type) {
        case CONFIG_TYPE_INT:
            if (sscanf(value, "%d", &param->value.int_val) != 1) {
                log_error("Invalid integer value for %s: %s (line %d)", key, value, line_number);
                return ERROR_INVALID_VALUE;
            }
            break;
            
        case CONFIG_TYPE_STRING:
            if (param->value.string_val) {
                free(param->value.string_val);
            }
            param->value.string_val = strdup(value);
            break;
            
        case CONFIG_TYPE_BOOL:
            param->value.bool_val = string_to_bool(value);
            break;
            
        default:
            log_error("Unsupported parameter type for %s (line %d)", key, line_number);
            return ERROR_UNSUPPORTED_TYPE;
    }
    
    param->modified = true;
    (*loaded_count)++;
    return SUCCESS;
}

static int load_configuration_from_file(const char *filename) {
    /* Configuration file loading implementation */
    int result;
    int loaded_count = 0;
    
    log_info("Loading configuration from %s", filename);
    
    result = load_dos_config_file(filename, config_load_handler, &loaded_count);
    if (result < 0) {
        switch (result) {
            case ERROR_DOS_BUSY:
                log_error("Cannot load config while DOS is busy");
                break;
            case ERROR_FILE_NOT_FOUND:
                log_warning("Configuration file not found: %s", filename);
                break;
            default:
                log_error("Failed to load configuration: error %d", result);
                break;
        }
        return result;
    }
    
    log_info("Successfully loaded %d configuration parameters from %s", loaded_count, filename);
    
    return SUCCESS;
}

static const char *parameter_type_to_string(parameter_type_t type) {
    switch (type) {
        case PARAM_TYPE_UINT8: return "uint8";
        case PARAM_TYPE_UINT16: return "uint16";
        case PARAM_TYPE_UINT32: return "uint32";
        case PARAM_TYPE_STRING: return "string";
        case PARAM_TYPE_BOOLEAN: return "boolean";
        case PARAM_TYPE_ENUM: return "enum";
        default: return "invalid";
    }
}

static const char *parameter_scope_to_string(parameter_scope_t scope) {
    switch (scope) {
        case PARAM_SCOPE_GLOBAL: return "global";
        case PARAM_SCOPE_MODULE: return "module";
        case PARAM_SCOPE_HANDLE: return "handle";
        case PARAM_SCOPE_INTERFACE: return "interface";
        default: return "invalid";
    }
}

/* External system time function */
extern uint32_t get_system_time(void);