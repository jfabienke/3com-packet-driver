/**
 * @file hardware_init.c
 * @brief Hardware abstraction layer - Initialization functions (OVERLAY segment)
 *
 * This file contains only the initialization functions that are called once
 * and can be discarded after init:
 * - NIC detection and initialization
 * - Error handling system setup
 * - Buffer system registration
 * - VTable initialization
 *
 * Runtime functions are in hardware_rt.c (ROOT segment)
 *
 * Updated: 2026-01-28 05:35:00 UTC
 */

#include "hardware.h"
#include "hwhal.h"
#include "nicctx.h"
#include "halerr.h"
#include "regacc.h"
#include "nic_init.h"
#include "logging.h"
#include "memory.h"
#include "diag.h"
#include "3c509b.h"
#include "3c515.h"
#include "errhndl.h"
#include "bufaloc.h"
#include "nicbufp.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * External declarations for global state (defined in hardware_rt.c)
 * ============================================================================ */

extern nic_info_t g_nic_infos[MAX_NICS];
extern int g_num_nics;
extern bool g_hardware_initialized;

/* External function declarations */
extern void nic_irq_uninstall(void);

/* ============================================================================
 * NIC Operations VTables
 * ============================================================================ */

static nic_ops_t g_3c509b_ops;
static nic_ops_t g_3c515_ops;

/* PnP detection results storage */
static nic_detect_info_t g_pnp_detection_results[MAX_NICS];
static int g_pnp_detection_count = 0;

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

static void hardware_reset_stats(void);
static int hardware_register_nic_with_buffer_system(nic_info_t* nic, int nic_index);
static void hardware_unregister_nic_from_buffer_system(int nic_index);

/* ============================================================================
 * VTable Initialization
 * ============================================================================ */

/**
 * @brief Initialize 3C509B operations vtable
 */
void init_3c509b_ops(void) {
    /* Get operations from 3c509b driver */
    nic_ops_t *ops = get_3c509b_ops();
    if (ops) {
        memcpy(&g_3c509b_ops, ops, sizeof(nic_ops_t));
    }
}

/**
 * @brief Initialize 3C515 operations vtable
 */
void init_3c515_ops(void) {
    /* Get operations from 3c515 driver */
    nic_ops_t *ops = get_3c515_ops();
    if (ops) {
        memcpy(&g_3c515_ops, ops, sizeof(nic_ops_t));
    }
}

/**
 * @brief Get NIC operations vtable by type
 */
nic_ops_t* get_nic_ops(nic_type_t type) {
    static bool vtables_initialized = false;

    /* Initialize vtables on first call */
    if (!vtables_initialized) {
        init_3c509b_ops();
        init_3c515_ops();
        vtables_initialized = true;
    }

    switch (type) {
        case NIC_TYPE_3C509B:
            return &g_3c509b_ops;
        case NIC_TYPE_3C515_TX:
            return &g_3c515_ops;
        default:
            return NULL;
    }
}

int hardware_register_nic_ops(nic_type_t type, nic_ops_t *ops) {
    if (!ops) {
        return ERROR_INVALID_PARAM;
    }

    /* Operations are registered during hardware initialization */
    return SUCCESS;
}

/* ============================================================================
 * Hardware Initialization
 * ============================================================================ */

/**
 * @brief Initialize the hardware abstraction layer
 */
int hardware_init(void) {
    int result;
    int i;

    if (g_hardware_initialized) {
        return SUCCESS;
    }

    LOG_INFO("Initializing hardware abstraction layer");

    /* Initialize NIC array */
    memory_zero(g_nic_infos, sizeof(g_nic_infos));
    g_num_nics = 0;

    /* Initialize statistics */
    hardware_reset_stats();

    /* Initialize NIC detection and initialization system */
    result = nic_init_system();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize NIC system: %d", result);
        return result;
    }

    /* Initialize error handling system */
    result = hardware_init_error_handling();
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize error handling system: %d", result);
        return result;
    }

    /* Detect and initialize all NICs */
    g_num_nics = nic_init_all_detected();
    if (g_num_nics < 0) {
        LOG_WARNING("No NICs detected or initialized");
        g_num_nics = 0;
    }

    /* Create error handling contexts and register NICs with buffer system */
    for (i = 0; i < g_num_nics; i++) {
        result = hardware_create_error_context(&g_nic_infos[i]);
        if (result != SUCCESS) {
            LOG_WARNING("Failed to create error context for NIC %d: %d", i, result);
        }

        /* Register NIC with per-NIC buffer pool system */
        result = hardware_register_nic_with_buffer_system(&g_nic_infos[i], i);
        if (result != SUCCESS) {
            LOG_WARNING("Failed to register NIC %d with buffer system: %d", i, result);
        }
    }

    g_hardware_initialized = true;

    LOG_INFO("Hardware layer initialized with %d NICs and error handling", g_num_nics);

    return SUCCESS;
}

/**
 * @brief Cleanup the hardware abstraction layer
 */
void hardware_cleanup(void) {
    int i;

    if (!g_hardware_initialized) {
        return;
    }

    LOG_INFO("Shutting down hardware layer");

    /* Restore original IRQ vector before tearing down NICs */
    nic_irq_uninstall();

    /* Cleanup all NICs */
    for (i = 0; i < g_num_nics; i++) {
        /* Unregister from buffer system first */
        hardware_unregister_nic_from_buffer_system(i);

        if (g_nic_infos[i].ops && g_nic_infos[i].ops->cleanup) {
            g_nic_infos[i].ops->cleanup(&g_nic_infos[i]);
        }
    }

    /* Cleanup NIC initialization system */
    nic_init_cleanup();

    /* Cleanup error handling system */
    hardware_cleanup_error_handling();

    g_num_nics = 0;
    g_hardware_initialized = false;
}

/* ============================================================================
 * NIC Registration
 * ============================================================================ */

/**
 * @brief Add a NIC to the hardware layer
 */
int hardware_add_nic(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (g_num_nics >= MAX_NICS) {
        LOG_ERROR("Cannot add NIC: maximum reached (%d)", MAX_NICS);
        return ERROR_NO_MEMORY;
    }

    /* Copy NIC info to global array */
    memcpy(&g_nic_infos[g_num_nics], nic, sizeof(nic_info_t));
    g_nic_infos[g_num_nics].index = g_num_nics;

    /* Assign operations vtable */
    g_nic_infos[g_num_nics].ops = get_nic_ops(nic->type);

    LOG_INFO("Added NIC %d: type=%d, io=0x%04X, irq=%d",
             g_num_nics, nic->type, nic->io_base, nic->irq);

    g_num_nics++;

    return SUCCESS;
}

/**
 * @brief Configure a NIC
 */
int hardware_configure_nic(nic_info_t *nic, const void *config) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (!nic->ops || !nic->ops->configure) {
        return ERROR_NOT_SUPPORTED;
    }

    return nic->ops->configure(nic, config);
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void hardware_reset_stats(void) {
    /* Stats are reset in hardware_rt.c */
}

static int hardware_register_nic_with_buffer_system(nic_info_t* nic, int nic_index) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Create per-NIC buffer pool using the nicbufp.h API */
    return nic_buffer_pool_create((nic_id_t)nic_index, nic->type, "NIC");
}

static void hardware_unregister_nic_from_buffer_system(int nic_index) {
    nic_buffer_pool_destroy((nic_id_t)nic_index);
}

/* ============================================================================
 * PnP Detection Integration
 * ============================================================================ */

/**
 * @brief Store PnP detection result
 */
int hardware_store_pnp_result(const nic_detect_info_t *info) {
    if (!info) {
        return ERROR_INVALID_PARAM;
    }

    if (g_pnp_detection_count >= MAX_NICS) {
        return ERROR_NO_MEMORY;
    }

    memcpy(&g_pnp_detection_results[g_pnp_detection_count], info, sizeof(nic_detect_info_t));
    g_pnp_detection_count++;

    return SUCCESS;
}

/**
 * @brief Get PnP detection count
 */
int hardware_get_pnp_count(void) {
    return g_pnp_detection_count;
}

/**
 * @brief Get PnP detection result
 */
const nic_detect_info_t* hardware_get_pnp_result(int index) {
    if (index < 0 || index >= g_pnp_detection_count) {
        return NULL;
    }

    return &g_pnp_detection_results[index];
}

/* ============================================================================
 * Error Handling Integration (Init-time setup)
 * ============================================================================ */

/**
 * @brief Initialize error handling system
 */
int hardware_init_error_handling(void) {
    /* Error handling initialization */
    return error_handling_init();
}

/**
 * @brief Cleanup error handling system
 */
void hardware_cleanup_error_handling(void) {
    error_handling_cleanup();
}

/**
 * @brief Create error context for a NIC
 */
int hardware_create_error_context(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Allocate and initialize error context */
    nic->error_context = error_context_create(nic->index);
    if (!nic->error_context) {
        return ERROR_NO_MEMORY;
    }

    return SUCCESS;
}
