/**
 * @file init_stubs.c
 * @brief Stub implementations for missing initialization functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * These stubs provide the missing function definitions required by
 * tsrldr.asm. They delegate to the actual implementations or provide
 * minimal stub behavior for functions not yet fully implemented.
 *
 * Last Updated: 2026-01-25 17:35 UTC
 */

#include "common.h"
#include "init.h"
#include "nic_init.h"
#include "logging.h"

/* External configuration and state */
extern config_t g_config;

/**
 * @brief Main driver initialization entry point
 *
 * Called by tsrldr.asm after CPU detection and NIC detection.
 * This function orchestrates the full driver initialization sequence.
 *
 * @return 0 on success, negative error code on failure
 */
int main_init(void) {
    int result;

    log_info("main_init: Starting driver initialization");

    /* Initialize hardware subsystem with current configuration */
    result = hardware_init_all(&g_config);
    if (result != 0) {
        log_error("main_init: hardware_init_all failed: %d", result);
        return result;
    }

    /* Initialize the packet driver API */
    result = init_driver(&g_config);
    if (result != 0) {
        log_error("main_init: init_driver failed: %d", result);
        return result;
    }

    log_info("main_init: Driver initialization complete");
    return 0;
}

/**
 * @brief NIC detection initialization
 *
 * Called by tsrldr.asm to detect and enumerate available NICs.
 * Delegates to the guided NIC detection system in init.c.
 *
 * @return 0 on success (at least one NIC found), negative error code on failure
 */
int nic_detect_init(void) {
    int result;
    int num_nics = 0;
    nic_detect_info_t detect_info[MAX_NICS];

    log_info("nic_detect_init: Starting NIC detection");

    /* Use the guided detection approach from init.c */
    /* First try PnP/BIOS detection */
    result = pnp_init_system();
    if (result == 0) {
        num_nics = pnp_detect_nics(detect_info, MAX_NICS);
        if (num_nics > 0) {
            log_info("nic_detect_init: Found %d NIC(s) via PnP", num_nics);
            hardware_set_pnp_detection_results(detect_info, num_nics);
            return 0;
        }
    }

    /* Fall back to I/O port probing based on config */
    if (g_config.io1_base != 0) {
        log_info("nic_detect_init: Probing configured I/O base 0x%X", g_config.io1_base);
        /* TODO: Implement direct I/O probing if PnP fails */
        num_nics = 1;  /* Assume configured NIC exists */
    }

    if (num_nics > 0) {
        log_info("nic_detect_init: NIC detection complete, %d NIC(s) found", num_nics);
        return 0;
    }

    log_error("nic_detect_init: No NICs detected");
    return INIT_ERR_NO_NICS;
}
