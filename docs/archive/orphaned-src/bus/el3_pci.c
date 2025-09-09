/**
 * @file el3_pci.c
 * @brief PCI Bus Prober for 3Com EtherLink III
 *
 * PCI-specific device detection for Vortex, Boomerang, Cyclone, and Tornado.
 * Uses INT 1Ah BIOS services for real-mode PCI configuration access.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "../core/el3_core.h"
#include "../../include/logging.h"
#include "../../include/pci_bios.h"

/* 3Com PCI vendor ID */
#define PCI_VENDOR_3COM     0x10B7

/* PCI device structure */
struct pci_device {
    uint16_t device_id;
    const char *name;
    enum el3_generation generation;
    uint16_t capabilities;
};

/* Comprehensive 3Com PCI device table (from BOOMTEX) */
static const struct pci_device pci_devices[] = {
    /* Vortex Family - First Generation PCI */
    {0x5900, "3C590 Vortex 10Mbps", EL3_GEN_VORTEX, 0},
    {0x5920, "3C592 EISA 10Mbps", EL3_GEN_VORTEX, 0},
    {0x5950, "3C595 Vortex 100baseTX", EL3_GEN_VORTEX, 0},
    {0x5951, "3C595 Vortex 100baseT4", EL3_GEN_VORTEX, 0},
    {0x5952, "3C595 Vortex 100base-MII", EL3_GEN_VORTEX, 0},
    {0x5970, "3C597 EISA Fast Demon/Vortex", EL3_GEN_VORTEX, 0},
    {0x5971, "3C597 EISA Fast Demon/Vortex", EL3_GEN_VORTEX, 0},
    
    /* Boomerang Family - Enhanced DMA */
    {0x9000, "3C900-TPO Boomerang", EL3_GEN_BOOMERANG, 0},
    {0x9001, "3C900-COMBO Boomerang", EL3_GEN_BOOMERANG, 0},
    {0x9004, "3C900B-TPO Etherlink XL", EL3_GEN_BOOMERANG, 0},
    {0x9005, "3C900B-COMBO Etherlink XL", EL3_GEN_BOOMERANG, 0},
    {0x9006, "3C900B-TPC Etherlink XL", EL3_GEN_BOOMERANG, 0},
    {0x900A, "3C900B-FL 10base-FL", EL3_GEN_BOOMERANG, 0},
    {0x9050, "3C905-TX Fast Etherlink XL", EL3_GEN_BOOMERANG, 0},
    {0x9051, "3C905-T4 Fast Etherlink XL", EL3_GEN_BOOMERANG, 0},
    {0x9055, "3C905B-TX Fast Etherlink XL", EL3_GEN_BOOMERANG, 0},
    {0x9056, "3C905B-T4 Fast Etherlink XL", EL3_GEN_BOOMERANG, 0},
    {0x9058, "3C905B-COMBO Deluxe", EL3_GEN_BOOMERANG, 0},
    {0x905A, "3C905B-FX Fast Etherlink XL", EL3_GEN_BOOMERANG, 0},
    
    /* Cyclone Family - Hardware Offload */
    {0x9200, "3C905C-TX Fast Etherlink", EL3_GEN_CYCLONE, 1},  /* HW checksum */
    {0x9201, "3C905C-T4 Fast Etherlink", EL3_GEN_CYCLONE, 1},
    {0x9202, "3C920B-EMB Integrated", EL3_GEN_CYCLONE, 1},
    {0x9210, "3C920B-EMB-WNM Integrated", EL3_GEN_CYCLONE, 1},
    
    /* Tornado Family - Advanced Features */
    {0x9300, "3C905CX-TX Fast Etherlink", EL3_GEN_TORNADO, 3},  /* HW csum + WoL */
    {0x9301, "3C905CX-FX Fast Etherlink", EL3_GEN_TORNADO, 3},
    {0x9800, "3C980-TX Fast Etherlink Server", EL3_GEN_TORNADO, 3},
    {0x9805, "3C980C-TXM Fast Etherlink Server", EL3_GEN_TORNADO, 3},
    
    /* CardBus variants */
    {0x5157, "3C575 Megahertz CardBus", EL3_GEN_BOOMERANG, 0},
    {0x5257, "3C575B Megahertz CardBus", EL3_GEN_CYCLONE, 1},
    {0x5057, "3C575CT Megahertz CardBus", EL3_GEN_CYCLONE, 1},
    {0x6560, "3C656 10/100 LAN CardBus", EL3_GEN_CYCLONE, 1},
    {0x6561, "3C656B 10/100 LAN CardBus", EL3_GEN_CYCLONE, 1},
    {0x6562, "3C656C 10/100 LAN CardBus", EL3_GEN_TORNADO, 3},
    {0x6563, "3C656-Modem CardBus", EL3_GEN_TORNADO, 3},
    {0x6564, "3C656B-Modem CardBus", EL3_GEN_TORNADO, 3},
    
    /* End of table */
    {0x0000, NULL, EL3_GEN_UNKNOWN, 0}
};

/* Forward declarations */
static int el3_pci_scan_bus(void);
static int el3_pci_probe_device(uint8_t bus, uint8_t device, uint8_t function);
static const struct pci_device *el3_pci_lookup_device(uint16_t device_id);
static struct el3_dev *el3_pci_alloc_device(void);
static int el3_pci_configure_device(struct el3_dev *dev, uint8_t bus, 
                                    uint8_t device, uint8_t function,
                                    const struct pci_device *pci_dev);
static int el3_pci_enable_device(uint8_t bus, uint8_t device, uint8_t function);

/**
 * @brief Main PCI bus probe function
 *
 * Scans PCI bus for supported 3Com NICs using INT 1Ah BIOS services.
 *
 * @return Number of devices found
 */
int el3_pci_probe(void)
{
    int count;
    
    LOG_INFO("EL3-PCI: Starting PCI bus probe");
    
    /* Check for PCI BIOS presence */
    if (!pci_bios_present()) {
        LOG_INFO("EL3-PCI: No PCI BIOS found");
        return 0;
    }
    
    /* Scan PCI bus */
    count = el3_pci_scan_bus();
    
    LOG_INFO("EL3-PCI: Probe complete, found %d device(s)", count);
    return count;
}

/**
 * @brief Scan PCI bus for 3Com devices
 */
static int el3_pci_scan_bus(void)
{
    uint8_t bus, device, function;
    uint8_t last_bus;
    uint16_t vendor_id;
    uint8_t header_type;
    int count = 0;
    
    /* Get last PCI bus number */
    last_bus = pci_get_last_bus();
    if (last_bus > 4) {
        last_bus = 4;  /* Limit scan for DOS */
    }
    
    LOG_DEBUG("EL3-PCI: Scanning buses 0-%d", last_bus);
    
    /* Scan all buses */
    for (bus = 0; bus <= last_bus; bus++) {
        /* Scan all devices */
        for (device = 0; device < 32; device++) {
            /* Check function 0 first */
            vendor_id = pci_read_config_word(bus, device, 0, PCI_VENDOR_ID);
            
            /* Skip if no device */
            if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
                continue;
            }
            
            /* Check if it's a 3Com device */
            if (vendor_id == PCI_VENDOR_3COM) {
                count += el3_pci_probe_device(bus, device, 0);
            }
            
            /* Check for multi-function device */
            header_type = pci_read_config_byte(bus, device, 0, PCI_HEADER_TYPE);
            if (header_type & 0x80) {
                /* Multi-function device - check other functions */
                for (function = 1; function < 8; function++) {
                    vendor_id = pci_read_config_word(bus, device, function, PCI_VENDOR_ID);
                    
                    if (vendor_id == PCI_VENDOR_3COM) {
                        count += el3_pci_probe_device(bus, device, function);
                    }
                }
            }
        }
    }
    
    return count;
}

/**
 * @brief Probe a specific PCI device
 */
static int el3_pci_probe_device(uint8_t bus, uint8_t device, uint8_t function)
{
    uint16_t device_id;
    const struct pci_device *pci_dev;
    struct el3_dev *dev;
    
    /* Read device ID */
    device_id = pci_read_config_word(bus, device, function, PCI_DEVICE_ID);
    
    /* Look up device in table */
    pci_dev = el3_pci_lookup_device(device_id);
    if (!pci_dev) {
        LOG_DEBUG("EL3-PCI: Unknown 3Com device 0x%04X at %02X:%02X.%X",
                  device_id, bus, device, function);
        return 0;
    }
    
    LOG_INFO("EL3-PCI: Found %s at %02X:%02X.%X",
             pci_dev->name, bus, device, function);
    
    /* Allocate device structure */
    dev = el3_pci_alloc_device();
    if (!dev) {
        LOG_ERROR("EL3-PCI: Failed to allocate device structure");
        return 0;
    }
    
    /* Configure device */
    if (el3_pci_configure_device(dev, bus, device, function, pci_dev) < 0) {
        free(dev);
        return 0;
    }
    
    /* Initialize the device */
    if (el3_init(dev) < 0) {
        LOG_ERROR("EL3-PCI: Failed to initialize %s", pci_dev->name);
        free(dev);
        return 0;
    }
    
    return 1;
}

/**
 * @brief Look up device in table
 */
static const struct pci_device *el3_pci_lookup_device(uint16_t device_id)
{
    int i;
    
    for (i = 0; pci_devices[i].device_id != 0; i++) {
        if (pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Allocate device structure
 */
static struct el3_dev *el3_pci_alloc_device(void)
{
    struct el3_dev *dev;
    
    dev = (struct el3_dev *)calloc(1, sizeof(struct el3_dev));
    if (!dev) {
        LOG_ERROR("EL3-PCI: Failed to allocate device structure");
        return NULL;
    }
    
    return dev;
}

/**
 * @brief Configure PCI device
 */
static int el3_pci_configure_device(struct el3_dev *dev, uint8_t bus,
                                    uint8_t device, uint8_t function,
                                    const struct pci_device *pci_dev)
{
    uint32_t bar0, bar1;
    uint16_t command;
    uint8_t irq;
    
    /* Copy device info */
    strcpy(dev->name, pci_dev->name);
    dev->vendor_id = PCI_VENDOR_3COM;
    dev->device_id = pci_dev->device_id;
    dev->generation = pci_dev->generation;
    
    /* Read BARs */
    bar0 = pci_read_config_dword(bus, device, function, PCI_BAR0);
    bar1 = pci_read_config_dword(bus, device, function, PCI_BAR1);
    
    /* Determine I/O or memory mapped */
    if (bar0 & 0x01) {
        /* I/O mapped */
        dev->io_mapped = true;
        dev->io_base = bar0 & 0xFFFC;
        LOG_DEBUG("EL3-PCI: I/O mapped at 0x%04X", dev->io_base);
    } else {
        /* Memory mapped */
        dev->io_mapped = false;
        dev->mem_base = bar0 & 0xFFFFFFF0;
        LOG_DEBUG("EL3-PCI: Memory mapped at 0x%08lX", dev->mem_base);
        
        /* For DOS, we prefer I/O mapping - check BAR1 */
        if (bar1 & 0x01) {
            dev->io_mapped = true;
            dev->io_base = bar1 & 0xFFFC;
            LOG_DEBUG("EL3-PCI: Using I/O at 0x%04X instead", dev->io_base);
        }
    }
    
    /* Read IRQ */
    irq = pci_read_config_byte(bus, device, function, PCI_INTERRUPT_LINE);
    dev->irq = irq & 0x0F;
    
    /* Enable the device */
    if (el3_pci_enable_device(bus, device, function) < 0) {
        LOG_ERROR("EL3-PCI: Failed to enable device");
        return -EIO;
    }
    
    /* Set capabilities based on generation */
    if (pci_dev->capabilities & 1) {
        /* Hardware checksum capable */
        /* Will be detected by el3_detect_capabilities */
    }
    if (pci_dev->capabilities & 2) {
        /* Wake-on-LAN capable */
        /* Will be detected by el3_detect_capabilities */
    }
    
    LOG_INFO("EL3-PCI: Configured %s at I/O 0x%04X IRQ %d",
             dev->name, dev->io_base, dev->irq);
    
    return 0;
}

/**
 * @brief Enable PCI device
 */
static int el3_pci_enable_device(uint8_t bus, uint8_t device, uint8_t function)
{
    uint16_t command;
    
    /* Read current command register */
    command = pci_read_config_word(bus, device, function, PCI_COMMAND);
    
    /* Enable I/O, memory, and bus mastering */
    command |= PCI_CMD_IO_ENABLE | PCI_CMD_MEM_ENABLE | PCI_CMD_BUS_MASTER;
    
    /* Write back command register */
    if (!pci_write_config_word(bus, device, function, PCI_COMMAND, command)) {
        LOG_ERROR("EL3-PCI: Failed to enable device");
        return -EIO;
    }
    
    /* Set latency timer */
    pci_write_config_byte(bus, device, function, PCI_LATENCY_TIMER, 64);
    
    return 0;
}