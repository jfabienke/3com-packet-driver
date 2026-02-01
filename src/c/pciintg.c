/**
 * @file pci_integration.c
 * @brief Integration layer for PCI support in packet driver framework
 *
 * Hooks PCI BIOS shim and 3Com PCI detection into the existing driver
 * initialization flow. Provides seamless detection and initialization
 * of PCI-based 3Com NICs alongside ISA variants.
 */

/* C89 compatible includes - portabl.h first for types */
#include "portabl.h"
#include "dos_io.h"
#include <string.h>
#include "pci_bios.h"
#include "pci_shim.h"
#include "3com_pci.h"
#include "nic_init.h"
#include "hardware.h"
#include "diag.h"
#include "config.h"
#include "cpudet.h"

/* External functions from 3com_pci_detect.c */
extern int scan_3com_pci_devices(nic_detect_info_t *detect_info, int max_devices);
extern int init_3com_pci(nic_detect_info_t *info);
extern const char* get_3com_generation_string(uint8_t generation);

/* NIC capability flags for PCI NICs */
#define NIC_CAP_MII         0x0001
#define NIC_CAP_AUTONEG     0x0002
#define NIC_CAP_HW_CSUM     0x0004
#define NIC_CAP_BUS_MASTER  0x0008

/* Bus type constant for PCI */
#define BUS_TYPE_PCI        NIC_BUS_PCI

/**
 * @brief Initialize PCI subsystem
 *
 * Sets up PCI BIOS access, installs shim if needed, and prepares
 * for PCI device detection.
 *
 * @return 0 on success, negative error code on failure
 */
int pci_subsystem_init(void) {
    uint8_t last_bus;

    LOG_INFO("Initializing PCI subsystem");

    /* Check if PCI BIOS is present */
    if (!pci_bios_present()) {
        LOG_INFO("PCI BIOS not detected - PCI support disabled");
        return -1;  /* Not an error, just no PCI */
    }

    /* Install PCI BIOS shim for broken BIOS workarounds */
    if (!pci_shim_install()) {
        LOG_WARNING("Failed to install PCI BIOS shim - using direct BIOS");
        /* Continue anyway - shim is optional */
    } else {
        LOG_INFO("PCI BIOS shim installed successfully");
    }

    /* Log PCI configuration */
    last_bus = pci_get_last_bus();
    LOG_INFO("PCI BIOS detected, last bus = %d", last_bus);

    return 0;
}

/**
 * @brief Cleanup PCI subsystem
 *
 * Removes PCI BIOS shim and releases any PCI resources.
 */
void pci_subsystem_cleanup(void) {
    uint32_t total_calls;
    uint32_t fallback_calls;

    /* Get and log shim statistics */
    pci_shim_get_stats(&total_calls, &fallback_calls);
    if (total_calls > 0) {
        LOG_INFO("PCI shim stats: %lu total calls, %lu fallbacks",
                 total_calls, fallback_calls);
    }

    /* Uninstall shim */
    pci_shim_uninstall();
    LOG_INFO("PCI subsystem cleaned up");
}

/**
 * @brief Detect and initialize 3Com PCI NICs
 *
 * Scans PCI bus for supported 3Com NICs and initializes them.
 * Integrates with the existing NIC detection framework.
 *
 * @param config Driver configuration
 * @param max_nics Maximum NICs to detect
 * @return Number of PCI NICs detected and initialized
 */
int detect_and_init_pci_nics(const config_t *config, int max_nics) {
    nic_detect_info_t detect_info[MAX_NICS];
    int detected_count;
    int initialized_count;
    int i;
    int result;
    nic_info_t *nic;
    pci_generic_info_t *pci_info;
    const pci_3com_info_t *dev_info;
    bool supports_busmaster;

    /* Suppress unused parameter warning */
    (void)config;

    initialized_count = 0;

    LOG_INFO("Phase 3: Detecting 3Com PCI NICs");

    /* Check CPU capability for PCI support */
    if (g_cpu_info.cpu_type < CPU_DET_80386) {
        LOG_INFO("CPU does not support PCI (requires 386+), skipping PCI detection");
        return 0;
    }

    /* Initialize PCI subsystem if not already done */
    if (pci_subsystem_init() != 0) {
        return 0;  /* No PCI available */
    }

    /* Scan for 3Com PCI devices */
    detected_count = scan_3com_pci_devices(detect_info, max_nics);

    if (detected_count == 0) {
        LOG_INFO("No 3Com PCI NICs detected");
        return 0;
    }

    LOG_INFO("Found %d 3Com PCI NIC(s)", detected_count);

    /* Initialize each detected PCI NIC */
    for (i = 0; i < detected_count && i < max_nics; i++) {
        nic = hardware_get_nic(initialized_count);
        if (!nic) {
            LOG_ERROR("Failed to get NIC slot %d", initialized_count);
            continue;
        }

        /* Initialize PCI-specific fields using detect_info */
        nic->io_base = detect_info[i].io_base;
        nic->irq = detect_info[i].irq;

        /* Get generation info from detect_info */
        pci_info = &detect_info[i].pci_info;
        dev_info = NULL;
        supports_busmaster = false;

        /* Use generation from pci_info */
        if (pci_info->generation != 0) {
            /* Determine NIC type based on generation */
            if (pci_info->generation & IS_VORTEX) {
                nic->type = NIC_TYPE_3C590_VORTEX;  /* Vortex PIO */
            } else if (pci_info->generation & IS_BOOMERANG) {
                nic->type = NIC_TYPE_3C900_BOOMERANG;  /* Boomerang DMA */
                supports_busmaster = true;
            } else if (pci_info->generation & IS_CYCLONE) {
                nic->type = NIC_TYPE_3C905_CYCLONE; /* Cyclone enhanced DMA */
                supports_busmaster = true;
            } else if (pci_info->generation & IS_TORNADO) {
                nic->type = NIC_TYPE_3C905C_TORNADO; /* Tornado advanced */
                supports_busmaster = true;
            }

            LOG_INFO("Detected PCI NIC (%s generation) at %02X:%02X.%X",
                     get_3com_generation_string(pci_info->generation),
                     detect_info[i].pci_bus, detect_info[i].pci_device,
                     detect_info[i].pci_function);
        }

        if (supports_busmaster && config->busmaster == BUSMASTER_OFF) {
            LOG_WARNING("Bus mastering disabled by configuration");
            /* Still initialize but in PIO mode */
        }

        /* Initialize the PCI NIC */
        result = init_3com_pci(&detect_info[i]);
        if (result == 0) {
            initialized_count++;
            LOG_INFO("PCI NIC %d initialized at I/O=0x%04X, IRQ=%d",
                     initialized_count, nic->io_base, nic->irq);

            /* Set capabilities based on generation */
            if (pci_info->hw_capabilities & HAS_MII) {
                nic->capabilities |= NIC_CAP_MII;
            }
            if (pci_info->hw_capabilities & HAS_NWAY) {
                nic->capabilities |= NIC_CAP_AUTONEG;
            }
            if (pci_info->hw_capabilities & HAS_HWCKSM) {
                nic->capabilities |= NIC_CAP_HW_CSUM;
            }
            if (supports_busmaster) {
                nic->capabilities |= NIC_CAP_BUS_MASTER;
            }
        } else {
            LOG_ERROR("Failed to initialize PCI NIC: error %d", result);
        }
    }

    return initialized_count;
}

/**
 * @brief Check if PCI support is available
 *
 * Quick check to determine if PCI detection should be attempted.
 *
 * @return true if PCI is available, false otherwise
 */
bool is_pci_available(void) {
    /* Need at least 386 for PCI */
    if (g_cpu_info.cpu_type < CPU_DET_80386) {
        return false;
    }

    /* Check for PCI BIOS */
    return pci_bios_present();
}

/**
 * @brief Get PCI device information string
 *
 * Formats PCI device information for diagnostics and logging.
 *
 * @param nic NIC information structure
 * @param buffer Output buffer
 * @param buf_size Buffer size
 */
void get_pci_device_info(const nic_info_t *nic, char *buffer, size_t buf_size) {
    if (!nic || !buffer || buf_size == 0) {
        return;
    }

    /* Format basic NIC info - nic_info_t doesn't have bus_type field,
     * so we check capabilities for PCI indication */
    if (!(nic->capabilities & NIC_CAP_BUS_MASTER)) {
        snprintf(buffer, buf_size, "NIC at I/O=0x%04X IRQ=%d",
                 nic->io_base, nic->irq);
        return;
    }

    /* Format PCI NIC info */
    snprintf(buffer, buf_size, "PCI NIC Type:%d Caps:%04lX TX:%lu RX:%lu Err:%lu/%lu",
             (int)nic->type,
             (unsigned long)nic->capabilities,
             (unsigned long)nic->tx_packets,
             (unsigned long)nic->rx_packets,
             (unsigned long)nic->tx_errors,
             (unsigned long)nic->rx_errors);
}
