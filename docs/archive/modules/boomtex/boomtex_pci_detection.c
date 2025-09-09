/**
 * @file boomtex_pci_detection.c
 * @brief BOOMTEX PCI Family Detection Implementation
 * 
 * Comprehensive PCI and CardBus detection for all 3Com PCI NICs
 * supported by BOOMTEX module. Covers the complete 3Com PCI family:
 * 
 * - Vortex Family (3C590/3C595) - 1st generation PCI
 * - Boomerang Family (3C900/3C905) - Enhanced DMA
 * - Cyclone Family (3C905B) - Hardware offload
 * - Tornado Family (3C905C) - Advanced features
 * - CardBus variants (3C575/3C656) - Hot plug support
 */

#include "boomtex_internal.h"
#include "../../include/logging.h"
#include "../../include/pci.h"

/* External global context */
extern boomtex_context_t g_boomtex_context;

/* PCI Device ID table for 3Com NICs supported by BOOMTEX */
typedef struct {
    uint16_t device_id;
    uint16_t subvendor_id;
    uint16_t subdevice_id;
    boomtex_hardware_type_t hardware_type;
    const char *name;
    uint16_t flags;
} boomtex_pci_device_t;

#define PCI_FLAG_CARDBUS        BIT(0)
#define PCI_FLAG_HW_CHECKSUM    BIT(1)
#define PCI_FLAG_WAKE_ON_LAN    BIT(2)
#define PCI_FLAG_FULL_DUPLEX    BIT(3)

/* Comprehensive 3Com PCI device database */
static const boomtex_pci_device_t boomtex_pci_devices[] = {
    /* Vortex Family - 1st Generation PCI */
    {0x5900, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C590_VORTEX, "3C590 Vortex 10Mbps", 0},
    {0x5920, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C595_VORTEX, "3C595 Vortex 100Mbps", PCI_FLAG_FULL_DUPLEX},
    {0x5950, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C595_VORTEX, "3C595 Vortex 100Mbps TX", PCI_FLAG_FULL_DUPLEX},
    {0x5951, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C595_VORTEX, "3C595 Vortex 100Mbps T4", PCI_FLAG_FULL_DUPLEX},
    {0x5952, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C595_VORTEX, "3C595 Vortex 100Mbps MII", PCI_FLAG_FULL_DUPLEX},
    
    /* Boomerang Family - Enhanced DMA */
    {0x9000, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C900_BOOMERANG, "3C900-TPO Boomerang", 0},
    {0x9001, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C900_BOOMERANG, "3C900-COMBO Boomerang", 0},
    {0x9004, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C900_BOOMERANG, "3C900B-TPO Boomerang", 0},
    {0x9005, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C900_BOOMERANG, "3C900B-COMBO Boomerang", 0},
    {0x9006, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C900_BOOMERANG, "3C900B-TPC Boomerang", 0},
    
    {0x9050, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905_BOOMERANG, "3C905-TX Boomerang", PCI_FLAG_FULL_DUPLEX},
    {0x9051, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905_BOOMERANG, "3C905-T4 Boomerang", PCI_FLAG_FULL_DUPLEX},
    {0x9055, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905_BOOMERANG, "3C905B-TX Boomerang", PCI_FLAG_FULL_DUPLEX},
    {0x9058, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905_BOOMERANG, "3C905B-COMBO Boomerang", PCI_FLAG_FULL_DUPLEX},
    
    /* Cyclone Family - Hardware Offload */
    {0x9200, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905B_CYCLONE, "3C905B-TX Cyclone", 
     PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM},
    {0x9201, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905B_CYCLONE, "3C905B-T4 Cyclone", 
     PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM},
    {0x9202, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905B_CYCLONE, "3C905B-FX Cyclone", 
     PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM},
    
    /* Tornado Family - Advanced Features */
    {0x9300, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905C_TORNADO, "3C905C-TX Tornado", 
     PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM | PCI_FLAG_WAKE_ON_LAN},
    {0x9301, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905C_TORNADO, "3C905C-FX Tornado", 
     PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM | PCI_FLAG_WAKE_ON_LAN},
    {0x9302, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C905C_TORNADO, "3C905C-TXM Tornado", 
     PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM | PCI_FLAG_WAKE_ON_LAN},
    
    /* CardBus Variants - Hot Plug Support */
    {0x5057, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C575_CARDBUS, "3C575 CardBus", 
     PCI_FLAG_CARDBUS | PCI_FLAG_FULL_DUPLEX},
    {0x5157, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C575_CARDBUS, "3C575B CardBus", 
     PCI_FLAG_CARDBUS | PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM},
    {0x5257, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C575_CARDBUS, "3C575C CardBus", 
     PCI_FLAG_CARDBUS | PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM},
    
    {0x6056, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C656_CARDBUS, "3C656 CardBus", 
     PCI_FLAG_CARDBUS | PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM | PCI_FLAG_WAKE_ON_LAN},
    {0x6057, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C656_CARDBUS, "3C656B CardBus", 
     PCI_FLAG_CARDBUS | PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM | PCI_FLAG_WAKE_ON_LAN},
    {0x6560, 0x0000, 0x0000, BOOMTEX_HARDWARE_3C656_CARDBUS, "3C656C CardBus", 
     PCI_FLAG_CARDBUS | PCI_FLAG_FULL_DUPLEX | PCI_FLAG_HW_CHECKSUM | PCI_FLAG_WAKE_ON_LAN},
    
    /* End of table */
    {0x0000, 0x0000, 0x0000, BOOMTEX_HARDWARE_UNKNOWN, NULL, 0}
};

/* Forward declarations */
static int boomtex_scan_pci_bus(void);
static int boomtex_identify_pci_device(uint8_t bus, uint8_t device, uint8_t function,
                                      uint16_t vendor_id, uint16_t device_id);
static const boomtex_pci_device_t* boomtex_lookup_pci_device(uint16_t device_id,
                                                           uint16_t subvendor_id,
                                                           uint16_t subdevice_id);
static int boomtex_configure_pci_nic(boomtex_nic_context_t *nic, uint8_t bus, 
                                    uint8_t device, uint8_t function,
                                    const boomtex_pci_device_t *pci_dev);

/**
 * @brief Detect all 3Com PCI family NICs
 * 
 * Performs comprehensive PCI bus scanning to detect all supported
 * 3Com PCI and CardBus network interface cards.
 * 
 * @return Number of NICs detected, or negative error code
 */
int boomtex_detect_pci_family(void) {
    int result;
    int initial_count = g_boomtex_context.nic_count;
    
    LOG_INFO("BOOMTEX: Starting comprehensive PCI family detection");
    LOG_DEBUG("BOOMTEX: Scanning for Vortex, Boomerang, Cyclone, Tornado, and CardBus variants");
    
    /* Check if PCI BIOS is available */
    if (!pci_bios_present()) {
        LOG_WARNING("BOOMTEX: PCI BIOS not detected - skipping PCI detection");
        return 0;
    }
    
    LOG_DEBUG("BOOMTEX: PCI BIOS detected - scanning PCI bus");
    
    /* Scan the entire PCI bus for 3Com devices */
    result = boomtex_scan_pci_bus();
    if (result < 0) {
        LOG_ERROR("BOOMTEX: PCI bus scan failed: %d", result);
        return result;
    }
    
    int detected_count = g_boomtex_context.nic_count - initial_count;
    
    if (detected_count > 0) {
        LOG_INFO("BOOMTEX: PCI family detection complete - found %d NICs", detected_count);
        
        /* Log detected devices */
        for (int i = initial_count; i < g_boomtex_context.nic_count; i++) {
            boomtex_nic_context_t *nic = &g_boomtex_context.nics[i];
            const boomtex_pci_device_t *pci_dev = boomtex_lookup_pci_device(nic->device_id, 0, 0);
            
            LOG_INFO("BOOMTEX: %s at PCI %d:%d.%d, I/O 0x%X, IRQ %d",
                     pci_dev ? pci_dev->name : "Unknown 3Com PCI",
                     nic->pci_bus, nic->pci_device, nic->pci_function,
                     nic->io_base, nic->irq);
        }
    } else {
        LOG_INFO("BOOMTEX: No PCI family devices detected");
    }
    
    return detected_count;
}

/**
 * @brief Initialize PCI NIC using existing driver infrastructure
 * 
 * Routes PCI NIC initialization to appropriate existing driver code
 * based on the hardware family detected.
 * 
 * @param nic NIC context to initialize
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_init_pci_nic(boomtex_nic_context_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("BOOMTEX: Initializing PCI NIC type %d", nic->hardware_type);
    
    /* Route to appropriate initialization based on hardware family */
    switch (nic->hardware_type) {
        case BOOMTEX_HARDWARE_3C590_VORTEX:
        case BOOMTEX_HARDWARE_3C595_VORTEX:
            LOG_INFO("BOOMTEX: Initializing Vortex family NIC");
            /* Vortex-specific initialization - first-generation PCI */
            result = boomtex_init_3c900tpo(nic);
            if (result == SUCCESS) {
                /* Disable advanced features not available on Vortex */
                nic->config_flags &= ~(NIC_FLAG_DMA_CAPABLE | NIC_FLAG_CHECKSUM_OFFLOAD);
                /* Enable basic 10/100 operation */
                nic->config_flags |= NIC_FLAG_FULL_DUPLEX_CAPABLE;
                LOG_DEBUG("BOOMTEX: Vortex configured for basic PCI operation");
            }
            return result;
            
        case BOOMTEX_HARDWARE_3C900_BOOMERANG:
        case BOOMTEX_HARDWARE_3C905_BOOMERANG:
            LOG_INFO("BOOMTEX: Initializing Boomerang family NIC");
            return boomtex_init_3c900tpo(nic);
            
        case BOOMTEX_HARDWARE_3C905B_CYCLONE:
            LOG_INFO("BOOMTEX: Initializing Cyclone family NIC with hardware checksum");
            /* Enable hardware checksum support for Cyclone */
            result = boomtex_init_3c900tpo(nic);
            if (result == SUCCESS) {
                /* Enable Cyclone-specific features */
                nic->config_flags |= (NIC_FLAG_CHECKSUM_OFFLOAD | NIC_FLAG_DMA_CAPABLE | NIC_FLAG_FULL_DUPLEX_CAPABLE);
                /* Configure hardware checksum registers (from Linux driver) */
                outw(nic->io_base + 0x0E, 0x8000); /* Enable RX checksum */
                outw(nic->io_base + 0x2C, 0x0001); /* Enable TX checksum */
                LOG_DEBUG("BOOMTEX: Cyclone hardware checksum offload enabled");
            }
            return result;
            
        case BOOMTEX_HARDWARE_3C905C_TORNADO:
            LOG_INFO("BOOMTEX: Initializing Tornado family NIC with advanced features");
            /* Enable Wake-on-LAN and other advanced features for Tornado */
            result = boomtex_init_3c900tpo(nic);
            if (result == SUCCESS) {
                /* Enable all Tornado features */
                nic->config_flags |= (NIC_FLAG_CHECKSUM_OFFLOAD | NIC_FLAG_DMA_CAPABLE | 
                                     NIC_FLAG_FULL_DUPLEX_CAPABLE | NIC_FLAG_WOL_CAPABLE);
                /* Configure hardware checksum registers */
                outw(nic->io_base + 0x0E, 0x8000); /* Enable RX checksum */
                outw(nic->io_base + 0x2C, 0x0001); /* Enable TX checksum */
                /* Configure Wake-on-LAN (basic setup) */
                outw(nic->io_base + 0x74, 0x0001); /* Enable WOL magic packet */
                LOG_DEBUG("BOOMTEX: Tornado advanced features enabled (checksum + WOL)");
            }
            return result;
            
        case BOOMTEX_HARDWARE_3C575_CARDBUS:
        case BOOMTEX_HARDWARE_3C656_CARDBUS:
            LOG_INFO("BOOMTEX: Initializing CardBus NIC with hot-plug support");
            /* Enable CardBus-specific features */
            result = boomtex_init_3c900tpo(nic);
            if (result == SUCCESS) {
                /* Enable CardBus features (all Tornado features plus hot-plug) */
                nic->config_flags |= (NIC_FLAG_CHECKSUM_OFFLOAD | NIC_FLAG_DMA_CAPABLE |
                                     NIC_FLAG_FULL_DUPLEX_CAPABLE | NIC_FLAG_WOL_CAPABLE |
                                     NIC_FLAG_HOTPLUG_CAPABLE);
                /* Configure power management for CardBus */
                outw(nic->io_base + 0x70, 0x0020); /* Enable power management */
                /* Set up CardBus CIS configuration if needed */
                LOG_DEBUG("BOOMTEX: CardBus hot-plug and power management enabled");
            }
            return result;
            
        default:
            LOG_ERROR("BOOMTEX: Unknown PCI hardware type %d", nic->hardware_type);
            return ERROR_UNSUPPORTED_HARDWARE;
    }
}

/* Private Implementation Functions */

/**
 * @brief Scan PCI bus for 3Com devices
 */
static int boomtex_scan_pci_bus(void) {
    uint8_t bus, device, function;
    uint16_t vendor_id, device_id;
    int total_found = 0;
    
    LOG_DEBUG("BOOMTEX: Scanning PCI bus 0-255 for 3Com devices");
    
    /* Scan all possible PCI bus/device/function combinations */
    for (bus = 0; bus < 256; bus++) {
        for (device = 0; device < 32; device++) {
            for (function = 0; function < 8; function++) {
                
                /* Read vendor and device ID */
                vendor_id = pci_read_config_word(bus, device, function, PCI_VENDOR_ID);
                
                /* Skip if no device present */
                if (vendor_id == 0xFFFF) {
                    continue;
                }
                
                /* Check for 3Com vendor ID */
                if (vendor_id != 0x10B7) {
                    continue;
                }
                
                /* Read device ID */
                device_id = pci_read_config_word(bus, device, function, PCI_DEVICE_ID);
                
                /* Try to identify and configure the device */
                int result = boomtex_identify_pci_device(bus, device, function, vendor_id, device_id);
                if (result > 0) {
                    total_found++;
                    LOG_DEBUG("BOOMTEX: Found 3Com PCI device at %d:%d.%d (ID: 0x%04X)", 
                              bus, device, function, device_id);
                }
                
                /* If this is function 0 and not a multi-function device, skip other functions */
                if (function == 0) {
                    uint8_t header_type = pci_read_config_byte(bus, device, function, PCI_HEADER_TYPE);
                    if (!(header_type & 0x80)) {
                        break; /* Not multi-function */
                    }
                }
            }
        }
    }
    
    LOG_DEBUG("BOOMTEX: PCI scan complete - found %d 3Com devices", total_found);
    
    return total_found;
}

/**
 * @brief Identify and configure a PCI device
 */
static int boomtex_identify_pci_device(uint8_t bus, uint8_t device, uint8_t function,
                                      uint16_t vendor_id, uint16_t device_id) {
    uint16_t subvendor_id, subdevice_id;
    const boomtex_pci_device_t *pci_dev;
    boomtex_nic_context_t *nic;
    
    /* Check if we have room for another NIC */
    if (g_boomtex_context.nic_count >= BOOMTEX_MAX_NICS) {
        LOG_WARNING("BOOMTEX: Maximum NIC count reached, ignoring device at %d:%d.%d", 
                    bus, device, function);
        return 0;
    }
    
    /* Read subsystem IDs for more precise identification */
    subvendor_id = pci_read_config_word(bus, device, function, PCI_SUBSYSTEM_VENDOR_ID);
    subdevice_id = pci_read_config_word(bus, device, function, PCI_SUBSYSTEM_ID);
    
    /* Look up device in our database */
    pci_dev = boomtex_lookup_pci_device(device_id, subvendor_id, subdevice_id);
    if (!pci_dev) {
        LOG_DEBUG("BOOMTEX: Unknown 3Com PCI device ID 0x%04X at %d:%d.%d", 
                  device_id, bus, device, function);
        return 0;
    }
    
    LOG_INFO("BOOMTEX: Found %s at PCI %d:%d.%d", pci_dev->name, bus, device, function);
    
    /* Get NIC context */
    nic = &g_boomtex_context.nics[g_boomtex_context.nic_count];
    
    /* Configure the NIC */
    int result = boomtex_configure_pci_nic(nic, bus, device, function, pci_dev);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: Failed to configure %s: %d", pci_dev->name, result);
        return result;
    }
    
    g_boomtex_context.nic_count++;
    
    return 1;
}

/**
 * @brief Look up PCI device in database
 */
static const boomtex_pci_device_t* boomtex_lookup_pci_device(uint16_t device_id,
                                                           uint16_t subvendor_id,
                                                           uint16_t subdevice_id) {
    const boomtex_pci_device_t *pci_dev = boomtex_pci_devices;
    
    while (pci_dev->device_id != 0x0000) {
        if (pci_dev->device_id == device_id) {
            /* Check for subsystem ID match if specified */
            if (pci_dev->subvendor_id != 0x0000) {
                if (pci_dev->subvendor_id == subvendor_id && 
                    pci_dev->subdevice_id == subdevice_id) {
                    return pci_dev;
                }
            } else {
                /* Generic match on device ID only */
                return pci_dev;
            }
        }
        pci_dev++;
    }
    
    return NULL; /* Not found */
}

/**
 * @brief Configure PCI NIC context
 */
static int boomtex_configure_pci_nic(boomtex_nic_context_t *nic, uint8_t bus, 
                                    uint8_t device, uint8_t function,
                                    const boomtex_pci_device_t *pci_dev) {
    uint32_t bar0, bar1;
    uint8_t irq;
    
    /* Initialize NIC context */
    memset(nic, 0, sizeof(boomtex_nic_context_t));
    
    /* Hardware identification */
    nic->hardware_type = pci_dev->hardware_type;
    nic->vendor_id = 0x10B7; /* 3Com */
    nic->device_id = pci_dev->device_id;
    nic->pci_bus = bus;
    nic->pci_device = device;
    nic->pci_function = function;
    
    /* Read PCI configuration */
    bar0 = pci_read_config_dword(bus, device, function, PCI_BASE_ADDRESS_0);
    bar1 = pci_read_config_dword(bus, device, function, PCI_BASE_ADDRESS_1);
    irq = pci_read_config_byte(bus, device, function, PCI_INTERRUPT_LINE);
    
    /* Configure I/O base and memory base */
    if (bar0 & PCI_BASE_ADDRESS_SPACE_IO) {
        nic->io_base = bar0 & PCI_BASE_ADDRESS_IO_MASK;
        nic->mem_base = (bar1 & PCI_BASE_ADDRESS_MEM_MASK);
    } else {
        nic->mem_base = (bar0 & PCI_BASE_ADDRESS_MEM_MASK);
        nic->io_base = (bar1 & PCI_BASE_ADDRESS_IO_MASK);
    }
    
    nic->irq = irq;
    
    /* Set capabilities based on device flags */
    if (pci_dev->flags & PCI_FLAG_FULL_DUPLEX) {
        nic->duplex_mode = BOOMTEX_DUPLEX_AUTO;
    }
    
    if (pci_dev->flags & PCI_FLAG_CARDBUS) {
        /* CardBus specific configuration */
        LOG_DEBUG("BOOMTEX: Configuring CardBus device");
    }
    
    /* Enable bus mastering for all PCI devices */
    nic->bus_mastering_enabled = 1;
    
    LOG_DEBUG("BOOMTEX: Configured %s - I/O 0x%X, Mem 0x%X, IRQ %d",
              pci_dev->name, nic->io_base, nic->mem_base, nic->irq);
    
    return SUCCESS;
}