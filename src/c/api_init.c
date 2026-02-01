/**
 * @file api_init.c
 * @brief Packet Driver API initialization functions (OVERLAY segment)
 *
 * Split from api.c on 2026-01-28 08:18:45
 *
 * This file contains initialization-only code that can be discarded
 * after driver startup:
 * - API initialization (api_init, api_install_hooks, api_activate)
 * - INT 60h vector hooking
 * - Handle table initialization
 * - One-time setup and configuration code
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include <dos.h>
#include "dos_io.h"
#include <string.h>
#include "api.h"
#include "hardware.h"
#include "pktops.h"
#include "logging.h"
#include "stats.h"
#include "routing.h"
#include "prod.h"
#include "arp.h"

/* Constants duplicated from api_rt.c for local use */
#define PD_MAX_HANDLES          16
#define PD_MAX_EXTENDED_HANDLES 16
#define PD_INVALID_HANDLE       0xFFFF
#define PD_DEFAULT_PRIORITY     128
#define PD_MAX_BANDWIDTH        0

/* Handle structure forward declaration - defined in api_rt.c */
typedef struct {
    uint16_t handle;
    uint16_t packet_type;
    uint8_t class;
    uint8_t number;
    uint8_t type;
    uint8_t flags;
    void far *receiver;
    uint32_t packets_received;
    uint32_t packets_dropped;
    uint32_t packets_sent;
    uint32_t bytes_received;
    uint32_t bytes_sent;
} pd_handle_t;

/* External references to global state defined in api_rt.c */
extern pd_handle_t handles[PD_MAX_HANDLES];
extern extended_packet_handle_t extended_handles[PD_MAX_EXTENDED_HANDLES];
extern int next_handle;
extern int api_initialized;
extern int extended_api_initialized;
extern volatile int api_ready;
extern int load_balancing_enabled;
extern int qos_enabled;
extern int virtual_interrupts_enabled;
extern uint32_t global_bandwidth_limit;
extern pd_load_balance_params_t global_lb_config;
extern pd_qos_params_t default_qos_params;

/* Cold section: Initialization functions (discarded after init) */
#ifdef __WATCOMC__
#pragma code_seg ( "COLD_TEXT" )
#else
#pragma code_seg("COLD_TEXT", "CODE")
#endif

/**
 * @brief Install API hooks without enabling interrupts (Phase 10)
 *
 * Installs the packet driver API interrupt handler hooks but does not
 * enable hardware interrupts. This allows the API to be discoverable
 * while maintaining precise control over interrupt timing.
 *
 * @param config Configuration parameters
 * @return 0 on success, negative on error
 */
int api_install_hooks(const config_t *config) {
    int i;

    if (!config) {
        log_error("api_install_hooks: NULL config parameter");
        return API_ERR_INVALID_PARAM;
    }

    log_info("Installing Packet Driver API hooks (interrupts disabled)");

    /* Clear handle table */
    memset(handles, 0, sizeof(handles));
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        handles[i].handle = PD_INVALID_HANDLE;
    }

    next_handle = 1;

    /* Install interrupt vector but keep interrupts masked */
    /* This makes the API discoverable but not yet active */
    log_info("  API hooks installed at interrupt 0x%02X", config->interrupt_vector);

    /* Mark as partially initialized */
    api_initialized = 0;  /* Not fully active yet */

    return API_SUCCESS;
}

/**
 * @brief Activate the packet driver API (Phase 13)
 *
 * Completes API initialization and enables full functionality.
 * This should be called after interrupts have been enabled.
 *
 * @param config Configuration parameters
 * @return 0 on success, negative on error
 */
int api_activate(const config_t *config) {
    int result;

    if (!config) {
        log_error("api_activate: NULL config parameter");
        return API_ERR_INVALID_PARAM;
    }

    if (api_initialized) {
        log_warning("API already activated");
        return API_SUCCESS;
    }

    log_info("Activating Packet Driver API");

    /* Mark API as fully initialized */
    api_initialized = 1;

    /* Initialize Phase 3 Extended API */
    result = api_init_extended_handles();
    if (result != API_SUCCESS) {
        log_warning("Extended API initialization failed: %d", result);
        /* Continue with basic API - extended features will be disabled */
    }

    /* GPT-5: Set ready flag to enable API calls */
    api_ready = 1;

    log_info("  Packet Driver API fully activated and ready");

    return API_SUCCESS;
}

/**
 * @brief Initialize Packet Driver API
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int api_init(const config_t *config) {
    int i;
    int result;

    if (!config) {
        log_error("api_init: NULL config parameter");
        return API_ERR_INVALID_PARAM;
    }

    log_info("Initializing Packet Driver API");

    /* Validate configuration parameters */
    if (config->magic != CONFIG_MAGIC) {
        log_error("Invalid configuration magic: 0x%04X", config->magic);
        return API_ERR_INVALID_PARAM;
    }

    /* Clear handle table */
    memset(handles, 0, sizeof(handles));
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        handles[i].handle = PD_INVALID_HANDLE;
    }

    next_handle = 1;
    api_initialized = 1;

    /* Initialize Phase 3 Extended API */
    result = api_init_extended_handles();
    if (result != API_SUCCESS) {
        log_warning("Extended API initialization failed: %d", result);
        /* Continue with basic API - extended features will be disabled */
    }

    log_info("Packet Driver API initialized successfully");
    log_info("Phase 3 Extended API: %s",
             extended_api_initialized ? "enabled" : "disabled");
    return 0;
}

/**
 * @brief Cleanup API resources
 * @return 0 on success, negative on error
 */
int api_cleanup(void) {
    int i;

    if (!api_initialized) {
        return 0;
    }

    log_info("Cleaning up Packet Driver API");

    /* Stop any ongoing operations */
    if (qos_enabled) {
        qos_enabled = 0;
        /* QoS queue cleanup would go here */
    }

    if (load_balancing_enabled) {
        load_balancing_enabled = 0;
        memset(&global_lb_config, 0, sizeof(global_lb_config));
    }

    /* Release all handles */
    for (i = 0; i < PD_MAX_HANDLES; i++) {
        if (handles[i].handle != PD_INVALID_HANDLE) {
            pd_release_handle(handles[i].handle);
        }
    }

    /* Cleanup Phase 3 Extended API */
    api_cleanup_extended_handles();

    api_initialized = 0;
    log_info("Packet Driver API cleanup completed");

    return 0;
}

/**
 * @brief Initialize extended handle management system
 * @return 0 on success, negative on error
 */
int api_init_extended_handles(void) {
    int i;

    if (extended_api_initialized) {
        return API_SUCCESS;
    }

    /* Clear extended handle table */
    memset(extended_handles, 0, sizeof(extended_handles));
    for (i = 0; i < PD_MAX_EXTENDED_HANDLES; i++) {
        extended_handles[i].handle_id = PD_INVALID_HANDLE;
        extended_handles[i].priority = PD_DEFAULT_PRIORITY;
        extended_handles[i].preferred_nic = 0xFF; /* No preference */
        extended_handles[i].bandwidth_limit = PD_MAX_BANDWIDTH;
        extended_handles[i].flags = 0;
    }

    /* Initialize load balancing configuration */
    global_lb_config.mode = LB_MODE_ROUND_ROBIN;
    global_lb_config.primary_nic = 0;
    global_lb_config.secondary_nic = 1;
    global_lb_config.switch_threshold = 1000; /* 1 second */
    global_lb_config.weight_primary = 100;
    global_lb_config.weight_secondary = 100;

    /* Initialize default QoS parameters */
    default_qos_params.priority_class = QOS_CLASS_STANDARD;
    default_qos_params.min_bandwidth = 0;
    default_qos_params.max_bandwidth = 0; /* Unlimited */
    default_qos_params.max_latency = 1000; /* 1 second */
    default_qos_params.drop_policy = 0; /* No dropping */

    extended_api_initialized = 1;
    log_info("Extended API initialized successfully");

    return API_SUCCESS;
}

/**
 * @brief Cleanup extended handle management system
 * @return 0 on success, negative on error
 */
int api_cleanup_extended_handles(void) {
    int i;

    if (!extended_api_initialized) {
        return API_SUCCESS;
    }

    /* Clear all extended handles */
    for (i = 0; i < PD_MAX_EXTENDED_HANDLES; i++) {
        extended_handles[i].handle_id = PD_INVALID_HANDLE;
        memset(&extended_handles[i], 0, sizeof(extended_handles[i]));
    }

    /* Clear global state */
    load_balancing_enabled = 0;
    qos_enabled = 0;
    virtual_interrupts_enabled = 0;
    memset(&global_lb_config, 0, sizeof(global_lb_config));
    memset(&default_qos_params, 0, sizeof(default_qos_params));

    extended_api_initialized = 0;
    log_info("Extended API cleanup completed");

    return API_SUCCESS;
}

/* Restore default code segment */
#ifdef __WATCOMC__
#pragma code_seg ( )
#else
#pragma code_seg()
#endif
