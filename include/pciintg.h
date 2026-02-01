/**
 * @file pci_integration.h
 * @brief PCI integration interface for packet driver framework
 * 
 * Provides functions to integrate PCI support into the existing
 * DOS packet driver architecture.
 */

#ifndef _PCI_INTEGRATION_H_
#define _PCI_INTEGRATION_H_

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "nic_init.h"

/**
 * @brief Initialize PCI subsystem
 * 
 * Sets up PCI BIOS access and installs shim if needed.
 * Must be called before any PCI operations.
 * 
 * @return 0 on success, -1 if PCI not available
 */
int pci_subsystem_init(void);

/**
 * @brief Cleanup PCI subsystem
 * 
 * Removes PCI BIOS shim and releases resources.
 * Should be called during driver cleanup.
 */
void pci_subsystem_cleanup(void);

/**
 * @brief Detect and initialize 3Com PCI NICs
 * 
 * Scans PCI bus for supported 3Com NICs and initializes them.
 * 
 * @param config Driver configuration
 * @param max_nics Maximum number of NICs to detect
 * @return Number of PCI NICs initialized
 */
int detect_and_init_pci_nics(const config_t *config, int max_nics);

/**
 * @brief Check if PCI support is available
 * 
 * Quick check for PCI availability without full initialization.
 * 
 * @return true if PCI is available, false otherwise
 */
bool is_pci_available(void);

/**
 * @brief Get PCI device information string
 * 
 * Formats PCI device info for diagnostics.
 * 
 * @param nic NIC information structure
 * @param buffer Output buffer
 * @param size Buffer size
 */
void get_pci_device_info(const nic_info_t *nic, char *buffer, size_t size);

/* NIC type definitions for PCI devices */
#define NIC_TYPE_3C59X    0x10  /* Vortex PIO */
#define NIC_TYPE_3C90X    0x11  /* Boomerang DMA */
#define NIC_TYPE_3C905B   0x12  /* Cyclone enhanced */
#define NIC_TYPE_3C905C   0x13  /* Tornado advanced */

/* NIC capability flags for PCI features */
#define NIC_CAP_MII       0x0001  /* MII transceiver */
#define NIC_CAP_AUTONEG   0x0002  /* Auto-negotiation */
#define NIC_CAP_HW_CSUM   0x0004  /* Hardware checksums */
#define NIC_CAP_BUS_MASTER 0x0008  /* Bus master DMA */

#endif /* _PCI_INTEGRATION_H_ */
