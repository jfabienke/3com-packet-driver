/**
 * @file centralized_detection.h
 * @brief Centralized Hardware Detection Service Interface
 * 
 * This header defines the interface for the centralized detection service
 * that performs all hardware detection once at startup and shares results
 * with modules during initialization.
 */

#ifndef CENTRALIZED_DETECTION_H
#define CENTRALIZED_DETECTION_H

#include "../include/hardware.h"
#include "../include/cpu_detect.h"
#include "../modules/common/module_bridge.h"
#include "device_registry.h"

/* Forward declarations */
typedef struct system_environment system_environment_t;

/**
 * @brief Initialize centralized detection service
 * 
 * Performs comprehensive system hardware detection including:
 * - CPU type and features
 * - System memory configuration  
 * - Chipset identification
 * - Cache coherency analysis
 * - All network hardware discovery
 * 
 * This function should be called once during system startup
 * before loading any modules.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int centralized_detection_initialize(void);

/**
 * @brief Get module initialization context for specific hardware
 * 
 * Modules call this function during initialization to get their
 * hardware configuration. The context contains all detection
 * results relevant to the requesting module.
 * 
 * @param module_id Module ID requesting context
 * @param nic_type Requested NIC type (NIC_TYPE_3C509B, NIC_TYPE_3C515_TX, etc.)
 * @return Pointer to module init context or NULL if not available
 */
module_init_context_t* centralized_detection_get_context(uint16_t module_id, uint8_t nic_type);

/**
 * @brief Get system environment information
 * 
 * Returns complete system environment including CPU, memory,
 * chipset, and all detected hardware information.
 * 
 * @return Pointer to system environment structure or NULL if not ready
 */
const system_environment_t* centralized_detection_get_environment(void);

/**
 * @brief Detect PCI NICs and populate detection results
 * 
 * Uses PCI BIOS services to scan for all supported 3Com PCI devices.
 * Supports all 3Com PCI families: Vortex, Boomerang, Cyclone, Tornado, CardBus.
 * 
 * @param info_list Array to populate with detection results  
 * @param max_count Maximum number of devices to detect
 * @return Number of devices found, 0 if no PCI or negative error code
 */
int detect_pci_nics(nic_detect_info_t *info_list, int max_count);

/**
 * @brief Get detection performance metrics
 * 
 * Returns timing information for each phase of hardware detection.
 * Useful for optimization and diagnostics.
 * 
 * @param total_ms Total detection time in milliseconds
 * @param cpu_ms CPU detection time in milliseconds
 * @param chipset_ms Chipset detection time in milliseconds  
 * @param nic_ms NIC detection time in milliseconds
 * @return SUCCESS if detection completed, ERROR_NOT_READY otherwise
 */
int centralized_detection_get_performance(uint32_t *total_ms, uint32_t *cpu_ms,
                                         uint32_t *chipset_ms, uint32_t *nic_ms);

/**
 * @brief Check if detection has been completed
 * 
 * @return 1 if detection completed, 0 otherwise
 */
int centralized_detection_is_ready(void);

/* Device Registry Service Access - Centralized detection initializes this */

/**
 * @brief Get device registry statistics
 * 
 * Convenience wrapper for device registry statistics.
 * 
 * @param total_devices Total devices in registry
 * @param claimed_devices Number of claimed devices
 * @param verified_devices Number of verified devices
 * @return SUCCESS on success, negative error code on failure
 */
int centralized_detection_get_device_stats(int *total_devices, int *claimed_devices, int *verified_devices);

/**
 * @brief Find available device for module
 * 
 * Convenience function to find unclaimed device matching criteria.
 * 
 * @param nic_type Requested NIC type
 * @param vendor_id Required vendor ID (0 for any)
 * @param device_id Required device ID (0 for any)
 * @return Registry ID of available device, or negative error code
 */
int centralized_detection_find_available_device(uint8_t nic_type, uint16_t vendor_id, uint16_t device_id);

#endif /* CENTRALIZED_DETECTION_H */