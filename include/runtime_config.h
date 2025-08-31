/**
 * @file runtime_config.h
 * @brief Runtime Configuration API Header
 * 
 * Phase 5 Enhancement: Dynamic reconfiguration without restart
 * Allows real-time adjustment of driver parameters
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

/* Configuration Categories */
#define CONFIG_CAT_GENERAL      0x00
#define CONFIG_CAT_MEMORY       0x01
#define CONFIG_CAT_NETWORK      0x02
#define CONFIG_CAT_PERFORMANCE  0x03
#define CONFIG_CAT_ROUTING      0x04
#define CONFIG_CAT_LOGGING      0x05
#define CONFIG_CAT_DIAGNOSTICS  0x06
#define CONFIG_CAT_COUNT        7

/* Configuration Parameter IDs */
#define CONFIG_PARAM_LOG_LEVEL          0x0100
#define CONFIG_PARAM_LOG_DESTINATION    0x0101
#define CONFIG_PARAM_BUFFER_SIZE        0x0200
#define CONFIG_PARAM_BUFFER_COUNT       0x0201
#define CONFIG_PARAM_XMS_ENABLE         0x0202
#define CONFIG_PARAM_XMS_THRESHOLD      0x0203
#define CONFIG_PARAM_PROMISCUOUS        0x0300
#define CONFIG_PARAM_MULTICAST          0x0301
#define CONFIG_PARAM_MTU                0x0302
#define CONFIG_PARAM_IRQ_COALESCE       0x0400
#define CONFIG_PARAM_TX_QUEUE_SIZE      0x0401
#define CONFIG_PARAM_RX_QUEUE_SIZE      0x0402
#define CONFIG_PARAM_ROUTING_MODE       0x0500
#define CONFIG_PARAM_DEFAULT_ROUTE      0x0501
#define CONFIG_PARAM_STATS_INTERVAL     0x0600
#define CONFIG_PARAM_DIAG_MODE          0x0601

/* Configuration Types */
#define CONFIG_TYPE_BOOL        0x01
#define CONFIG_TYPE_UINT8       0x02
#define CONFIG_TYPE_UINT16      0x03
#define CONFIG_TYPE_UINT32      0x04
#define CONFIG_TYPE_STRING      0x05

/* Configuration Flags */
#define CONFIG_FLAG_DYNAMIC         0x01    /* Can be changed at runtime */
#define CONFIG_FLAG_REQUIRES_RESET  0x02    /* Requires driver reset */
#define CONFIG_FLAG_PER_NIC         0x04    /* Per-NIC configuration */
#define CONFIG_FLAG_READONLY        0x08    /* Read-only parameter */
#define CONFIG_FLAG_ADVANCED        0x10    /* Advanced parameter */
#define CONFIG_FLAG_INITIALIZED     0x20    /* System initialized */

/* Export/Import Magic and Version */
#define CONFIG_EXPORT_MAGIC     0x43464758  /* 'CFGX' */
#define CONFIG_EXPORT_VERSION   1

/**
 * @brief Configuration parameter definition
 */
typedef struct {
    uint16_t param_id;          /* Parameter identifier */
    uint8_t type;               /* Parameter type */
    uint8_t category;           /* Parameter category */
    const char *name;           /* Parameter name */
    const char *description;    /* Parameter description */
    uint32_t min_value;         /* Minimum value */
    uint32_t max_value;         /* Maximum value */
    uint32_t default_value;     /* Default value */
    uint8_t flags;              /* Parameter flags */
} config_param_def_t;

/**
 * @brief Configuration parameter value
 */
typedef struct {
    uint16_t param_id;          /* Parameter identifier */
    uint32_t current_value;     /* Current value */
    uint32_t pending_value;     /* Pending value (requires reset) */
    int has_pending;            /* Has pending change (0=false, 1=true) */
    uint8_t nic_index;          /* NIC index (0xFF = global) */
} config_param_value_t;

/**
 * @brief Configuration change callback
 */
typedef void (*config_callback_func_t)(uint16_t param_id, 
                                       uint32_t old_value,
                                       uint32_t new_value,
                                       uint8_t nic_index,
                                       void *context);

/**
 * @brief Configuration callback registration
 */
typedef struct config_callback_s {
    config_callback_func_t callback;
    uint16_t param_id;          /* 0 = all parameters */
    void *context;
    struct config_callback_s *next;
} config_callback_t;

/**
 * @brief Configuration export format
 */
typedef struct {
    uint32_t magic;             /* Export magic number */
    uint16_t version;           /* Export version */
    uint16_t param_count;       /* Number of parameters */
    uint16_t checksum;          /* Data checksum */
    uint16_t reserved;          /* Reserved for alignment */
} config_export_t;

/**
 * @brief Exported parameter entry
 */
typedef struct {
    uint16_t param_id;
    uint32_t value;
    uint8_t nic_index;
    uint8_t reserved;
} config_param_export_t;

/**
 * @brief Configuration statistics
 */
typedef struct {
    uint32_t total_changes;     /* Total configuration changes */
    uint32_t immediate_changes; /* Changes applied immediately */
    uint32_t reset_applied_changes; /* Changes applied on reset */
    uint32_t failed_changes;    /* Failed configuration attempts */
    uint32_t exports;           /* Configuration exports */
    uint32_t imports;           /* Configuration imports */
} config_stats_t;

/**
 * @brief Runtime configuration manager
 */
typedef struct {
    config_param_value_t *param_values;    /* Parameter values */
    uint16_t param_count;                  /* Number of parameters */
    uint16_t pending_changes;              /* Pending change count */
    config_callback_t *callbacks;          /* Change callbacks */
    config_stats_t stats;                  /* Configuration statistics */
    uint8_t flags;                         /* Manager flags */
} runtime_config_manager_t;

/* Function Prototypes - Initialization */
int runtime_config_init(void);
int runtime_config_cleanup(void);

/* Function Prototypes - Parameter Management */
int runtime_config_set_param(uint16_t param_id, uint32_t value, uint8_t nic_index);
int runtime_config_get_param(uint16_t param_id, uint32_t *value, uint8_t nic_index);
int runtime_config_reset_param(uint16_t param_id, uint8_t nic_index);
int runtime_config_apply_pending(void);

/* Function Prototypes - Callbacks */
int runtime_config_register_callback(config_callback_func_t callback, 
                                    uint16_t param_id,
                                    void *context);
int runtime_config_unregister_callback(config_callback_func_t callback);

/* Function Prototypes - Export/Import */
int runtime_config_export(void *buffer, uint16_t *size);
int runtime_config_import(const void *buffer, uint16_t size);
int runtime_config_save_to_file(const char *filename);
int runtime_config_load_from_file(const char *filename);

/* Function Prototypes - Utilities */
void runtime_config_set_defaults(void);
void runtime_config_dump(void);
const config_param_def_t* runtime_config_get_param_info(uint16_t param_id);
int runtime_config_validate_value(uint16_t param_id, uint32_t value);

/* Internal helper functions */
static const config_param_def_t* runtime_config_get_definition(uint16_t param_id);
static config_param_value_t* runtime_config_find_param(uint16_t param_id, uint8_t nic_index);
static int runtime_config_apply_param(uint16_t param_id, uint32_t value, uint8_t nic_index);
static void runtime_config_notify_callbacks(uint16_t param_id, uint32_t old_value, 
                                           uint32_t new_value, uint8_t nic_index);
static uint16_t runtime_config_calculate_checksum(const void *data, uint16_t size);

/* Inline helper functions */
static inline bool runtime_config_is_dynamic(uint16_t param_id) {
    const config_param_def_t *def = runtime_config_get_param_info(param_id);
    return def && (def->flags & CONFIG_FLAG_DYNAMIC);
}

static inline int runtime_config_has_pending(void) {
    extern runtime_config_manager_t g_config_manager;
    return g_config_manager.pending_changes > 0;
}

#endif /* RUNTIME_CONFIG_H */