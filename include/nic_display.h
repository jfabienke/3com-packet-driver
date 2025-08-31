/**
 * @file nic_display.h
 * @brief Network Interface Display Functions Header
 *
 * Professional Quarterdeck-style display functions for the 3Com packet driver.
 * Provides colored status displays, network monitoring, and diagnostic output
 * using the ANSI console system.
 */

#ifndef _NIC_DISPLAY_H_
#define _NIC_DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

// Forward declaration
typedef struct nic_info nic_info_t;

// === Driver Startup Display Functions ===

/**
 * Display the main driver loading banner with version
 */
void display_driver_banner(const char *version);

/**
 * Display hardware detection progress screen
 */
void display_detection_progress(void);

/**
 * Display detected NIC with status indicator
 */
void display_detected_nic(nic_info_t *nic, int nic_index, bool success);

/**
 * Display driver configuration progress
 */
void display_configuration_progress(void);

/**
 * Display network interface status summary
 */
void display_nic_status_summary(nic_info_t *nics, int nic_count);

// === Real-time Network Monitor ===

/**
 * Display full-screen network monitor with activity graphs
 */
void display_network_monitor(nic_info_t *nics, int nic_count);

// === Diagnostic and Status Messages ===

/**
 * Display timestamped diagnostic message with color coding
 * @param level Message level: "INFO", "WARNING", "ERROR", "SUCCESS"
 * @param message The diagnostic message text
 */
void display_diagnostic_message(const char *level, const char *message);

// === TSR and System Status ===

/**
 * Display TSR loaded confirmation with memory usage
 */
void display_tsr_loaded(uint16_t segment, uint8_t interrupt, uint16_t size_kb);

#endif /* _NIC_DISPLAY_H_ */