/**
 * @file pci_power.h
 * @brief PCI Power Management interface
 * 
 * Provides functions for PCI power state management, capability list walking,
 * and device bring-up from low power states.
 */

#ifndef _PCI_POWER_H_
#define _PCI_POWER_H_

#include <stdint.h>
#include <stdbool.h>

/* Power states */
#define PCI_POWER_D0    0  /* Full power */
#define PCI_POWER_D1    1  /* Light sleep */
#define PCI_POWER_D2    2  /* Deep sleep */
#define PCI_POWER_D3HOT 3  /* Deep sleep, hot reset */

/* Function prototypes */

/**
 * @brief Find PCI capability in device's capability list
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param cap_id Capability ID to find
 * @return Offset of capability in config space, or 0 if not found
 */
uint8_t pci_find_capability(uint8_t bus, uint8_t device, uint8_t function, uint8_t cap_id);

/**
 * @brief Get current PCI power state
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return Current power state (0-3), or -1 on error
 */
int pci_get_power_state(uint8_t bus, uint8_t device, uint8_t function);

/**
 * @brief Set PCI power state
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param state Target power state (0-3)
 * @return true on success, false on error
 */
bool pci_set_power_state(uint8_t bus, uint8_t device, uint8_t function, uint8_t state);

/**
 * @brief Clear PME (Power Management Event) status
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return true on success, false on error
 */
bool pci_clear_pme_status(uint8_t bus, uint8_t device, uint8_t function);

/**
 * @brief Perform complete power-on sequence for PCI device
 * 
 * Brings device from any power state to D0 with all housekeeping.
 * Critical for warm reboot scenarios.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return true on success, false on error
 */
bool pci_power_on_device(uint8_t bus, uint8_t device, uint8_t function);

/**
 * @brief Check if device supports specific power state
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param state Power state to check (0-3)
 * @return true if supported, false otherwise
 */
bool pci_power_state_supported(uint8_t bus, uint8_t device, uint8_t function, uint8_t state);

#endif /* _PCI_POWER_H_ */
