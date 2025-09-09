/**
 * @file unified_api.h
 * @brief Unified Packet Driver API Header - Agent 12 Implementation
 *
 * 3Com Packet Driver - Unified API for Multi-Module Coordination
 * Header file for unified packet driver API with complete Packet Driver
 * Specification v1.11 compliance and multi-module dispatch system.
 * 
 * Agent 12: Driver API
 * Week 1 Deliverable - Complete unified testing ready
 */

#ifndef UNIFIED_API_H
#define UNIFIED_API_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Unified API Version */
#define UNIFIED_API_MAJOR_VERSION   1
#define UNIFIED_API_MINOR_VERSION   11
#define UNIFIED_API_VERSION_CODE    0x0111

/* Module Identifiers */
#define UNIFIED_MODULE_PTASK        0
#define UNIFIED_MODULE_CORKSCRW     1
#define UNIFIED_MODULE_BOOMTEX      2
#define UNIFIED_MODULE_COUNT        3

/* Unified API Function Codes */
#define UNIFIED_FUNC_GET_UNIFIED_STATS      0x20
#define UNIFIED_FUNC_SET_MODULE_PREFERENCE  0x21
#define UNIFIED_FUNC_GET_MODULE_STATUS      0x22
#define UNIFIED_FUNC_CONFIGURE_RUNTIME      0x23

/* Handle Flags */
#define UNIFIED_HANDLE_FLAG_ACTIVE          0x01
#define UNIFIED_HANDLE_FLAG_PRIORITY        0x02
#define UNIFIED_HANDLE_FLAG_MODULE_LOCKED   0x04
#define UNIFIED_HANDLE_FLAG_ERROR_HANDLER   0x08

/* Module Status Flags */
#define MODULE_STATUS_INACTIVE      0x00
#define MODULE_STATUS_ACTIVE        0x01
#define MODULE_STATUS_ERROR         0x02
#define MODULE_STATUS_DEGRADED      0x03

/* Configuration Parameters */
typedef struct {
    uint8_t interrupt_vector;       /* Packet driver interrupt vector */
    uint8_t default_module;         /* Default module for new handles */
    uint16_t max_handles;           /* Maximum concurrent handles */
    uint32_t performance_flags;     /* Performance monitoring flags */
    uint32_t debug_flags;           /* Debug and logging flags */
} unified_config_t;

/* Module Status Information */
typedef struct {
    char module_name[12];           /* Module name */
    uint8_t module_id;              /* Module identifier */
    uint8_t status;                 /* Module status */
    uint16_t active_handles;        /* Number of active handles */
    uint32_t packets_processed;     /* Total packets processed */
    uint32_t errors;                /* Error count */
    uint32_t last_activity_time;    /* Last activity timestamp */
    uint16_t memory_usage;          /* Memory usage in paragraphs */
    uint16_t cpu_usage;             /* CPU usage percentage */
} unified_module_status_t;

/* Unified Statistics Structure */
typedef struct {
    /* Global Statistics */
    uint32_t total_packets_in;      /* Total received packets */
    uint32_t total_packets_out;     /* Total transmitted packets */
    uint32_t total_bytes_in;        /* Total received bytes */
    uint32_t total_bytes_out;       /* Total transmitted bytes */
    uint32_t total_errors;          /* Total error count */
    uint32_t total_drops;           /* Total dropped packets */
    
    /* Per-Module Statistics */
    uint32_t module_packets_in[UNIFIED_MODULE_COUNT];
    uint32_t module_packets_out[UNIFIED_MODULE_COUNT];
    uint32_t module_errors[UNIFIED_MODULE_COUNT];
    
    /* API Performance Metrics */
    uint32_t api_call_count;        /* Total API calls */
    uint32_t api_total_time;        /* Total API execution time */
    uint32_t api_max_time;          /* Maximum API call time */
    uint32_t api_min_time;          /* Minimum API call time */
    
    /* Handle Management Statistics */
    uint16_t active_handles;        /* Currently active handles */
    uint16_t peak_handles;          /* Peak concurrent handles */
    uint32_t handle_allocations;    /* Total handle allocations */
    uint32_t handle_deallocations;  /* Total handle deallocations */
    
    /* Memory Statistics */
    uint16_t memory_allocated;      /* Allocated memory in paragraphs */
    uint16_t memory_peak;           /* Peak memory usage */
    uint16_t dma_buffers_active;    /* Active DMA buffers */
    
    /* Performance Statistics */
    uint32_t interrupt_count;       /* Hardware interrupt count */
    uint32_t context_switches;      /* Module context switches */
    uint32_t optimal_routes;        /* Optimal routing decisions */
    uint32_t suboptimal_routes;     /* Suboptimal routing decisions */
    
} unified_statistics_t;

/* Module Preference Structure */
typedef struct {
    uint8_t preferred_module;       /* Preferred module ID */
    uint8_t fallback_module;        /* Fallback module ID */
    uint16_t preference_flags;      /* Preference flags */
    uint32_t packet_type_mask;      /* Packet types for this preference */
} module_preference_t;

/* Runtime Configuration Structure */
typedef struct {
    uint8_t config_type;            /* Configuration type */
    uint8_t target_module;          /* Target module (0xFF = all) */
    uint16_t parameter_id;          /* Parameter identifier */
    uint32_t parameter_value;       /* Parameter value */
    char description[32];           /* Configuration description */
} runtime_config_t;

/* Configuration Types */
#define CONFIG_TYPE_GLOBAL          0x00
#define CONFIG_TYPE_MODULE_SPECIFIC 0x01
#define CONFIG_TYPE_HANDLE_SPECIFIC 0x02
#define CONFIG_TYPE_PERFORMANCE     0x03

/* Function Prototypes - Initialization and Cleanup */
int unified_api_init(const void *config);
int unified_api_cleanup(void);

/* Function Prototypes - Main API Entry Point */
int unified_packet_driver_api(uint8_t function, uint16_t handle, void far *params);

/* Function Prototypes - Standard Packet Driver Functions */
int unified_get_driver_info(void *params);
int unified_access_type(void *params);
int unified_release_handle(uint16_t handle);
int unified_send_packet(uint16_t handle, void *params);
int unified_terminate_driver(uint16_t handle);
int unified_get_address(uint16_t handle, void *params);
int unified_reset_interface(uint16_t handle);

/* Function Prototypes - Extended Packet Driver Functions */
int unified_async_send_packet(uint16_t handle, void *params);
int unified_set_rcv_mode(uint16_t handle, void *params);
int unified_get_rcv_mode(uint16_t handle, void *params);
int unified_set_multicast_list(uint16_t handle, void *params);
int unified_get_multicast_list(uint16_t handle, void *params);
int unified_get_statistics(uint16_t handle, void *params);
int unified_set_address(uint16_t handle, void *params);

/* Function Prototypes - Unified Extended Functions */
int unified_get_unified_statistics(void *params);
int unified_set_module_preference(uint16_t handle, void *params);
int unified_get_module_status(void *params);
int unified_configure_runtime(void *params);

/* Function Prototypes - Module Integration */
int unified_register_module(uint8_t module_id, const char *name, void *functions);
int unified_unregister_module(uint8_t module_id);
int unified_process_received_packet(const uint8_t *packet, uint16_t length, uint8_t module_id);

/* Function Prototypes - Handle Management */
uint16_t unified_allocate_handle(void);
int unified_validate_handle(uint16_t handle);
int unified_get_handle_info(uint16_t handle, void *info);

/* Function Prototypes - Performance Monitoring */
int unified_start_performance_monitoring(void);
int unified_stop_performance_monitoring(void);
int unified_get_performance_report(void *report);

/* Function Prototypes - Memory Management Integration */
int unified_allocate_dma_safe_buffer(uint16_t size, void **buffer);
int unified_free_dma_safe_buffer(void *buffer);
int unified_get_memory_statistics(void *stats);

/* Function Prototypes - Error Handling */
int unified_register_error_handler(uint16_t handle, void far *error_handler);
int unified_report_error(uint16_t handle, uint16_t error_code, const char *description);
int unified_get_last_error(uint16_t handle, void *error_info);

/* Function Prototypes - Configuration Management */
int unified_load_configuration(const char *filename);
int unified_save_configuration(const char *filename);
int unified_set_default_configuration(void);

/* Utility Macros */
#define UNIFIED_HANDLE_TO_MODULE(handle) \
    ((handle) >> 12) /* Extract module ID from handle */

#define UNIFIED_MAKE_HANDLE(module_id, local_handle) \
    (((module_id) << 12) | ((local_handle) & 0x0FFF))

#define UNIFIED_IS_VALID_MODULE(module_id) \
    ((module_id) < UNIFIED_MODULE_COUNT)

/* Debug and Logging Support */
#ifdef DEBUG
#define UNIFIED_DEBUG(fmt, ...) \
    log_debug("[UNIFIED API] " fmt, ##__VA_ARGS__)
#else
#define UNIFIED_DEBUG(fmt, ...) /* No-op */
#endif

/* Performance Monitoring Macros */
#define UNIFIED_PERF_START() \
    uint32_t _start_time = get_system_time()

#define UNIFIED_PERF_END(operation) \
    do { \
        uint32_t _end_time = get_system_time(); \
        unified_record_performance(operation, _end_time - _start_time); \
    } while(0)

/* Error Checking Macros */
#define UNIFIED_RETURN_IF_ERROR(expr) \
    do { \
        int _result = (expr); \
        if (_result != SUCCESS) { \
            UNIFIED_DEBUG("Error %d in %s at line %d", _result, __FILE__, __LINE__); \
            return _result; \
        } \
    } while(0)

#define UNIFIED_VALIDATE_HANDLE_OR_RETURN(handle) \
    do { \
        if (!unified_validate_handle(handle)) { \
            UNIFIED_DEBUG("Invalid handle %04X", handle); \
            return ERROR_PKTDRV_HANDLE; \
        } \
    } while(0)

/* Module Function Pointer Types */
typedef int (*module_init_func_t)(const void *config);
typedef int (*module_cleanup_func_t)(void);
typedef int (*module_send_packet_func_t)(uint16_t handle, const void *params);
typedef int (*module_handle_access_type_func_t)(const void *params);
typedef int (*module_release_handle_func_t)(uint16_t handle);
typedef int (*module_get_statistics_func_t)(uint16_t handle, void *stats);

/* Module Function Table */
typedef struct {
    module_init_func_t init_func;
    module_cleanup_func_t cleanup_func;
    module_send_packet_func_t send_packet;
    module_handle_access_type_func_t handle_access_type;
    module_release_handle_func_t release_handle;
    module_get_statistics_func_t get_statistics;
} module_function_table_t;

/* External Variables */
extern unified_statistics_t unified_global_stats;
extern uint8_t unified_api_initialized;
extern uint8_t unified_interrupt_vector;

#ifdef __cplusplus
}
#endif

#endif /* UNIFIED_API_H */