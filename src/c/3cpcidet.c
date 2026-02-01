/**
 * @file 3com_pci_detect.c
 * @brief 3Com PCI NIC detection and generation dispatch
 * 
 * Consolidates device detection for all 3Com PCI/CardBus NICs from Vortex
 * through Tornado. Based on Donald Becker's unified architecture supporting
 * 47+ chip variants through generation flags and capability detection.
 * 
 * Leverages logic from orphaned BOOMTEX and CORKSCREW modules.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "3com_pci.h"
#include "pci_bios.h"
#include "pci_shim.h"
#include "nic_init.h"
#include "nicctx.h"
#include "logging.h"
#include "common.h"
#include "ovl_data.h"

/*
 * Init-only PCI device database - placed in INIT_DETECT overlay segment.
 * With OVL_DETECT_CONST, this data is loaded/unloaded with the overlay,
 * NOT kept in resident DGROUP. String literals go to code segment via -zc.
 *
 * IMPORTANT: Pointers to this data must not escape the init phase!
 */

/* Complete 3Com PCI device database (47+ variants) */
static OVL_DETECT_CONST pci_3com_info_t pci_3com_devices[] = {
    /* Vortex series - PIO only */
    {0x5900, "3C590 Vortex 10Mbps", IS_VORTEX, 0, VORTEX_TOTAL_SIZE},
    {0x5920, "3C592 EISA 10Mbps", IS_VORTEX, 0, VORTEX_TOTAL_SIZE},
    {0x5950, "3C595 Vortex 100baseTx", IS_VORTEX, HAS_MII, VORTEX_TOTAL_SIZE},
    {0x5951, "3C595 Vortex 100baseT4", IS_VORTEX, HAS_MII, VORTEX_TOTAL_SIZE},
    {0x5952, "3C595 Vortex 100base-MII", IS_VORTEX, HAS_MII, VORTEX_TOTAL_SIZE},
    {0x5970, "3C597 EISA Fast Vortex", IS_VORTEX, HAS_MII, VORTEX_TOTAL_SIZE},
    
    /* Boomerang series - Bus master DMA */
    {0x9000, "3C900 Boomerang 10baseT", IS_BOOMERANG, HAS_MII, BOOMERANG_TOTAL_SIZE},
    {0x9001, "3C900 Boomerang 10Mbps Combo", IS_BOOMERANG, 0, BOOMERANG_TOTAL_SIZE},
    {0x9004, "3C900B-TPO Etherlink XL", IS_BOOMERANG, HAS_MII, BOOMERANG_TOTAL_SIZE},
    {0x9005, "3C900B-Combo Etherlink XL", IS_BOOMERANG, 0, BOOMERANG_TOTAL_SIZE},
    {0x9006, "3C900B-TPC Etherlink XL", IS_BOOMERANG, HAS_MII, BOOMERANG_TOTAL_SIZE},
    
    {0x9050, "3C905 Boomerang 100baseTx", IS_BOOMERANG, HAS_MII, BOOMERANG_TOTAL_SIZE},
    {0x9051, "3C905 Boomerang 100baseT4", IS_BOOMERANG, HAS_MII, BOOMERANG_TOTAL_SIZE},
    {0x9055, "3C905B Cyclone 100baseTx", IS_CYCLONE, HAS_MII | HAS_NWAY, CYCLONE_TOTAL_SIZE},
    {0x9056, "3C905B-T4 Cyclone", IS_CYCLONE, HAS_MII | HAS_NWAY, CYCLONE_TOTAL_SIZE},
    {0x9058, "3C905B Cyclone 10/100/BNC", IS_CYCLONE, HAS_MII | HAS_NWAY, CYCLONE_TOTAL_SIZE},
    {0x905A, "3C905B-FX Cyclone 100baseFx", IS_CYCLONE, HAS_MII, CYCLONE_TOTAL_SIZE},
    
    /* Cyclone series - Enhanced DMA */
    {0x9200, "3C905C Tornado", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x9201, "3C920 Tornado", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x9202, "3C920B-EMB Tornado", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x9210, "3C920B-EMB-WNM Tornado", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    
    /* CardBus variants */
    {0x5257, "3CCFE575BT CardBus", IS_CYCLONE, HAS_MII | HAS_NWAY | HAS_CB_FNS, CYCLONE_TOTAL_SIZE},
    {0x5157, "3CCFE575CT CardBus", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_CB_FNS, CYCLONE_TOTAL_SIZE},
    {0x6560, "3CCFE656 CardBus", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_CB_FNS | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x6562, "3CCFEM656B CardBus", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_CB_FNS | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x6564, "3CXFEM656C CardBus", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_CB_FNS | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    
    /* Special OEM variants */
    {0x4500, "3C450 HomePNA", IS_CYCLONE, HAS_MII, CYCLONE_TOTAL_SIZE},
    {0x7646, "3CSOHO100-TX Hurricane", IS_CYCLONE, HAS_MII | HAS_NWAY, CYCLONE_TOTAL_SIZE},
    {0x9800, "3C980 Cyclone Server", IS_CYCLONE, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x9805, "3C980C Python-T", IS_CYCLONE, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x7940, "3C982 Dual Cyclone", IS_CYCLONE, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    
    /* Mini-PCI variants */
    {0x1700, "3C556 Mini-PCI", IS_TORNADO, HAS_MII | HAS_NWAY | INVERT_MII_PWR | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    {0x1201, "3C556B Mini-PCI", IS_TORNADO, HAS_MII | HAS_NWAY | INVERT_LED_PWR | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    
    /* Newer Tornado variants */
    {0x9210, "3C920B-EMB-WNM", IS_TORNADO, HAS_MII | HAS_NWAY | HAS_HWCKSM, CYCLONE_TOTAL_SIZE},
    
    {0x0000, NULL, 0, 0, 0}  /* Terminator */
};

/* Generation-specific vtable */
typedef struct {
    int (*init)(pci_3com_context_t *ctx);
    int (*start_xmit)(pci_3com_context_t *ctx, packet_t *pkt);
    int (*rx_poll)(pci_3com_context_t *ctx);
    int (*set_rx_mode)(pci_3com_context_t *ctx, uint8_t mode);
    int (*get_stats)(pci_3com_context_t *ctx);
} pci_3com_vtable_t;

/* Forward declarations for generation handlers */
extern int vortex_init(pci_3com_context_t *ctx);
extern int boomerang_init(pci_3com_context_t *ctx);
extern int vortex_start_xmit(pci_3com_context_t *ctx, packet_t *pkt);
extern int boomerang_start_xmit(pci_3com_context_t *ctx, packet_t *pkt);
extern int vortex_rx(pci_3com_context_t *ctx);
extern int boomerang_rx(pci_3com_context_t *ctx);

/* Generation dispatch tables - overlay-local, discarded after init */
static OVL_DETECT_CONST pci_3com_vtable_t vortex_vtable = {
    vortex_init,          /* init */
    vortex_start_xmit,    /* start_xmit */
    vortex_rx,            /* rx_poll */
    NULL,                 /* set_rx_mode - Common implementation */
    NULL                  /* get_stats - Common implementation */
};

static OVL_DETECT_CONST pci_3com_vtable_t boomerang_vtable = {
    boomerang_init,       /* init */
    boomerang_start_xmit, /* start_xmit */
    boomerang_rx,         /* rx_poll */
    NULL,                 /* set_rx_mode - Common implementation */
    NULL                  /* get_stats - Common implementation */
};

/**
 * @brief Find device info by PCI device ID
 */
static const pci_3com_info_t* find_device_info(uint16_t device_id) {
    int i;
    
    for (i = 0; pci_3com_devices[i].device_id != 0; i++) {
        if (pci_3com_devices[i].device_id == device_id) {
            return &pci_3com_devices[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Detect 3Com generation from device ID
 */
int detect_3com_generation(uint16_t device_id, pci_generic_info_t *info) {
    const pci_3com_info_t *dev_info;

    if (!info) {
        return -1;
    }

    dev_info = find_device_info(device_id);
    if (!dev_info) {
        log_debug("Unknown 3Com device ID 0x%04X", device_id);
        return -1;
    }

    log_info("Detected %s (ID 0x%04X)", dev_info->name, device_id);

    /* Fill in generic PCI info - use actual struct fields */
    info->generation = dev_info->generation;
    info->hw_capabilities = dev_info->capabilities;
    info->io_size = dev_info->io_size;

    return 0;
}

/**
 * @brief Scan PCI bus for all 3Com devices
 */
int scan_3com_pci_devices(nic_detect_info_t *detect_info, int max_devices) {
    uint8_t bus, device, function;
    uint16_t vendor_id, dev_id;
    uint32_t class_code;
    uint32_t bar0_val;
    int count = 0;
    int i;
    int j;
    bool already_found;

    if (!detect_info || max_devices <= 0) {
        return 0;
    }

    log_info("Scanning PCI bus for 3Com network controllers...");

    /* Method 1: Try finding by vendor ID */
    for (i = 0; i < max_devices; i++) {
        if (!pci_find_device(PCI_VENDOR_3COM, 0xFFFF, (uint16_t)i, &bus, &device, &function)) {
            break;  /* No more 3Com devices */
        }

        /* Read device ID */
        dev_id = pci_read_config_word(bus, device, function, PCI_DEVICE_ID);

        /* Check if it's a known NIC */
        if (find_device_info(dev_id) != NULL) {
            detect_info[count].bus_type = BUS_TYPE_PCI;
            detect_info[count].pci_bus = bus;
            detect_info[count].pci_device = device;
            detect_info[count].pci_function = function;
            detect_info[count].vendor_id = PCI_VENDOR_3COM;
            detect_info[count].device_id = dev_id;

            /* Read I/O BAR */
            bar0_val = pci_read_config_dword(bus, device, function, PCI_BAR0);
            detect_info[count].io_base = (uint16_t)(bar0_val & 0xFFFC);  /* Mask flags */

            /* Read IRQ */
            detect_info[count].irq = pci_read_config_byte(bus, device, function, PCI_INTERRUPT_LINE);

            log_info("Found 3Com PCI NIC at %02X:%02X.%X, I/O=0x%04X, IRQ=%d",
                     bus, device, function, detect_info[count].io_base, detect_info[count].irq);

            count++;
            if (count >= max_devices) {
                break;
            }
        }
    }

    /* Method 2: Also scan by class code for any we missed */
    for (i = 0; i < 32; i++) {  /* Reasonable limit */
        class_code = ((uint32_t)PCI_CLASS_NETWORK << 8) | (uint32_t)PCI_SUBCLASS_ETHERNET;

        if (!pci_find_class(class_code, (uint16_t)i, &bus, &device, &function)) {
            break;  /* No more network devices */
        }

        /* Check if it's a 3Com device */
        vendor_id = pci_read_config_word(bus, device, function, PCI_VENDOR_ID);
        if (vendor_id != PCI_VENDOR_3COM) {
            continue;
        }

        /* Check if we already found this one */
        dev_id = pci_read_config_word(bus, device, function, PCI_DEVICE_ID);
        already_found = false;

        for (j = 0; j < count; j++) {
            if (detect_info[j].pci_bus == bus &&
                detect_info[j].pci_device == device &&
                detect_info[j].pci_function == function) {
                already_found = true;
                break;
            }
        }

        if (!already_found && find_device_info(dev_id) != NULL) {
            detect_info[count].bus_type = BUS_TYPE_PCI;
            detect_info[count].pci_bus = bus;
            detect_info[count].pci_device = device;
            detect_info[count].pci_function = function;
            detect_info[count].vendor_id = PCI_VENDOR_3COM;
            detect_info[count].device_id = dev_id;

            /* Read I/O BAR */
            bar0_val = pci_read_config_dword(bus, device, function, PCI_BAR0);
            detect_info[count].io_base = (uint16_t)(bar0_val & 0xFFFC);

            /* Read IRQ */
            detect_info[count].irq = pci_read_config_byte(bus, device, function, PCI_INTERRUPT_LINE);

            log_info("Found additional 3Com NIC via class scan at %02X:%02X.%X",
                     bus, device, function);

            count++;
            if (count >= max_devices) {
                break;
            }
        }
    }

    log_info("PCI scan complete: found %d 3Com NIC(s)", count);
    return count;
}

/**
 * @brief Initialize detected 3Com PCI device
 */
int init_3com_pci(nic_detect_info_t *info) {
    pci_3com_context_t *ctx;
    const pci_3com_info_t *dev_info;
    const pci_3com_vtable_t *vtable;
    uint16_t command;
    int ret;

    if (!info || info->bus_type != BUS_TYPE_PCI) {
        return -1;
    }

    /* Get device info */
    dev_info = find_device_info(info->device_id);
    if (!dev_info) {
        log_error("Unknown 3Com device ID 0x%04X", info->device_id);
        return -1;
    }

    log_info("Initializing %s", dev_info->name);

    /* Allocate context (would normally come from driver framework) */
    ctx = (pci_3com_context_t*)calloc(1, sizeof(pci_3com_context_t));
    if (!ctx) {
        log_error("Failed to allocate device context");
        return -1;
    }

    /* Fill in context */
    ctx->base.io_base = info->io_base;
    ctx->base.irq_line = info->irq;
    ctx->generation = dev_info->generation;
    ctx->capabilities = dev_info->capabilities;

    /* Enable PCI device */
    command = pci_read_config_word(info->pci_bus, info->pci_device,
                                   info->pci_function, PCI_COMMAND);
    command |= PCI_CMD_IO_ENABLE;  /* Enable I/O space */

    if (dev_info->generation != IS_VORTEX) {
        command |= PCI_CMD_BUS_MASTER;  /* Enable bus mastering for DMA */
    }

    pci_write_config_word(info->pci_bus, info->pci_device,
                         info->pci_function, PCI_COMMAND, command);

    /* Set latency timer for better performance */
    if (dev_info->generation != IS_VORTEX) {
        pci_write_config_byte(info->pci_bus, info->pci_device,
                             info->pci_function, PCI_LATENCY_TIMER, 64);
    }

    /* Select appropriate vtable based on generation */
    if (dev_info->generation & IS_VORTEX) {
        vtable = &vortex_vtable;
        log_info("Using Vortex PIO mode");
    } else {
        vtable = &boomerang_vtable;
        log_info("Using Boomerang+ DMA mode");
    }

    /* Call generation-specific init */
    if (vtable->init) {
        ret = vtable->init(ctx);
        if (ret != 0) {
            log_error("Generation-specific init failed: %d", ret);
            free(ctx);
            return ret;
        }
    }

    /* Store context in pci_info for later use */
    info->pci_info.generation = dev_info->generation;
    info->pci_info.hw_capabilities = dev_info->capabilities;
    info->pci_info.io_size = dev_info->io_size;

    log_info("%s initialized successfully", dev_info->name);
    return 0;
}

/**
 * @brief Get generation string for diagnostics
 */
const char* get_3com_generation_string(uint8_t generation) {
    if (generation & IS_TORNADO) return "Tornado";
    if (generation & IS_CYCLONE) return "Cyclone";
    if (generation & IS_BOOMERANG) return "Boomerang";
    if (generation & IS_VORTEX) return "Vortex";
    return "Unknown";
}

/**
 * @brief Get capability string for diagnostics
 */
void get_3com_capability_string(uint16_t caps, char *buf, size_t size) {
    if (!buf || size == 0) return;
    
    buf[0] = '\0';
    
    if (caps & HAS_MII) strcat(buf, "MII ");
    if (caps & HAS_NWAY) strcat(buf, "NWAY ");
    if (caps & HAS_PWR_CTRL) strcat(buf, "PWR ");
    if (caps & HAS_HWCKSM) strcat(buf, "CSUM ");
    if (caps & HAS_CB_FNS) strcat(buf, "CardBus ");

    if (buf[0] == '\0') {
        strcat(buf, "None");
    }
}
