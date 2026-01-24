/**
 * @file pciscan.c
 * @brief PCI device enumeration utility
 * 
 * Scans the PCI bus and displays all detected devices with their
 * configuration information. Useful for debugging and system inventory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <stdint.h>
#include <stdbool.h>
#include "pci_bios.h"
#include "pci_shim.h"

/* Display options */
static struct {
    bool verbose;       /* Show detailed information */
    bool show_bars;     /* Display BAR information */
    bool show_caps;     /* Show capabilities */
    bool use_shim;      /* Use enhanced shim */
    bool raw_dump;      /* Dump raw config space */
    uint8_t target_bus; /* Specific bus to scan (-1 for all) */
} options = {
    .verbose = false,
    .show_bars = false,
    .show_caps = false,
    .use_shim = false,
    .raw_dump = false,
    .target_bus = 0xFF
};

/* PCI class code names */
static const char* get_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Pre-PCI 2.0";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal Processing";
        default:   return "Unknown";
    }
}

/* Get subclass name for network devices */
static const char* get_network_subclass(uint8_t subclass) {
    switch (subclass) {
        case 0x00: return "Ethernet";
        case 0x01: return "Token Ring";
        case 0x02: return "FDDI";
        case 0x03: return "ATM";
        case 0x04: return "ISDN";
        case 0x05: return "WorldFip";
        case 0x06: return "PICMG";
        case 0x80: return "Other";
        default:   return "Unknown";
    }
}

/* Known vendor names */
typedef struct {
    uint16_t vendor_id;
    const char* name;
} vendor_info_t;

static const vendor_info_t known_vendors[] = {
    {0x8086, "Intel"},
    {0x1022, "AMD"},
    {0x10DE, "NVIDIA"},
    {0x1002, "ATI/AMD"},
    {0x10B7, "3Com"},
    {0x10EC, "Realtek"},
    {0x14E4, "Broadcom"},
    {0x1106, "VIA"},
    {0x1039, "SiS"},
    {0x10B9, "ALi"},
    {0x1000, "NCR/Symbios"},
    {0x9004, "Adaptec"},
    {0x9005, "Adaptec"},
    {0x1011, "DEC"},
    {0x102B, "Matrox"},
    {0x121A, "3Dfx"},
    {0x5333, "S3"},
    {0x1013, "Cirrus Logic"},
    {0x1023, "Trident"},
    {0x100B, "National Semi"},
    {0, NULL}
};

/* Get vendor name */
static const char* get_vendor_name(uint16_t vendor_id) {
    int i;
    for (i = 0; known_vendors[i].name != NULL; i++) {
        if (known_vendors[i].vendor_id == vendor_id) {
            return known_vendors[i].name;
        }
    }
    return "Unknown";
}

/* Known 3Com device names */
typedef struct {
    uint16_t device_id;
    const char* name;
} device_info_t;

static const device_info_t known_3com_devices[] = {
    {0x5900, "3C590 Vortex 10Mbps"},
    {0x5920, "3C592 EISA 10Mbps Demon"},
    {0x5950, "3C595 Vortex 100baseTx"},
    {0x5951, "3C595 Vortex 100baseT4"},
    {0x5952, "3C595 Vortex 100base-MII"},
    {0x9000, "3C900 Boomerang 10baseT"},
    {0x9001, "3C900 Boomerang 10Mbps Combo"},
    {0x9004, "3C900B-TPO Cyclone"},
    {0x9005, "3C900B-Combo Cyclone"},
    {0x9006, "3C900B-TPC Cyclone"},
    {0x900A, "3C900B-FL Cyclone"},
    {0x9050, "3C905 Boomerang 100baseTx"},
    {0x9051, "3C905 Boomerang 100baseT4"},
    {0x9055, "3C905B Cyclone 100baseTx"},
    {0x9056, "3C905B-T4 Cyclone"},
    {0x9058, "3C905B-Combo Cyclone"},
    {0x905A, "3C905B-FX Cyclone"},
    {0x9200, "3C905C Tornado"},
    {0x9201, "3C905C-TX Tornado"},
    {0x9202, "3C920B-EMB Tornado"},
    {0x9210, "3C920B-EMB-WNM Tornado"},
    {0x9800, "3C980 Cyclone Server"},
    {0x9805, "3C980C Tornado Server"},
    {0x7646, "3CSOHO100-TX Hurricane"},
    {0x5055, "3C555 Laptop Hurricane"},
    {0x6055, "3C556 Laptop Hurricane"},
    {0x6056, "3C556B Laptop Hurricane"},
    {0x5157, "3C575 Megahertz"},
    {0x5257, "3C575B/C CardBus"},
    {0x6560, "3CCFE656 CardBus"},
    {0x6562, "3CCFE656B CardBus"},
    {0x6564, "3CCFE656C CardBus"},
    {0x4500, "3C450 Cyclone HomePNA"},
    {0x9201, "3C920 Tornado"},
    {0x1201, "3C982 Server Tornado"},
    {0x9056, "3C905B-T4 Cyclone"},
    {0x9210, "3C920B Tornado"},
    {0, NULL}
};

/* Get 3Com device name */
static const char* get_3com_device_name(uint16_t device_id) {
    int i;
    for (i = 0; known_3com_devices[i].name != NULL; i++) {
        if (known_3com_devices[i].device_id == device_id) {
            return known_3com_devices[i].name;
        }
    }
    return "Unknown 3Com Device";
}

/* Display BAR information */
static void display_bar(uint8_t bus, uint8_t dev, uint8_t func, int bar_num) {
    uint8_t offset = PCI_BASE_ADDRESS_0 + (bar_num * 4);
    uint32_t bar_value, size, original;
    
    /* Read current BAR value */
    bar_value = pci_read_config_dword(bus, dev, func, offset);
    
    if (bar_value == 0 || bar_value == 0xFFFFFFFF) {
        return;  /* BAR not implemented */
    }
    
    /* Save original value */
    original = bar_value;
    
    /* Write all 1s to determine size */
    pci_write_config_dword(bus, dev, func, offset, 0xFFFFFFFF);
    size = pci_read_config_dword(bus, dev, func, offset);
    
    /* Restore original value */
    pci_write_config_dword(bus, dev, func, offset, original);
    
    if (size == 0 || size == 0xFFFFFFFF) {
        return;
    }
    
    /* Determine type and size */
    if (bar_value & 0x01) {
        /* I/O space */
        uint16_t io_base = bar_value & 0xFFFC;
        size = (~(size & 0xFFFC) + 1) & 0xFFFF;
        printf("    BAR%d: I/O at 0x%04X [size=%u]\n", bar_num, io_base, size);
    } else {
        /* Memory space */
        uint32_t mem_base = bar_value & 0xFFFFFFF0;
        uint8_t mem_type = (bar_value >> 1) & 0x03;
        const char* type_str = "";
        
        switch (mem_type) {
            case 0: type_str = "32-bit"; break;
            case 1: type_str = "< 1MB"; break;
            case 2: type_str = "64-bit"; break;
            case 3: type_str = "Reserved"; break;
        }
        
        size = ~(size & 0xFFFFFFF0) + 1;
        
        if (size < 1024) {
            printf("    BAR%d: Memory at 0x%08lX [%s, size=%lu]\n",
                   bar_num, mem_base, type_str, size);
        } else if (size < 1048576) {
            printf("    BAR%d: Memory at 0x%08lX [%s, size=%luK]\n",
                   bar_num, mem_base, type_str, size / 1024);
        } else {
            printf("    BAR%d: Memory at 0x%08lX [%s, size=%luM]\n",
                   bar_num, mem_base, type_str, size / 1048576);
        }
        
        /* Skip next BAR if 64-bit */
        if (mem_type == 2) {
            bar_num++;
        }
    }
}

/* Display capabilities */
static void display_capabilities(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t status;
    uint8_t cap_ptr, cap_id, cap_next;
    
    /* Check if capabilities list exists */
    status = pci_read_config_word(bus, dev, func, PCI_STATUS);
    if (!(status & 0x10)) {
        return;  /* No capabilities */
    }
    
    /* Get capabilities pointer */
    cap_ptr = pci_read_config_byte(bus, dev, func, PCI_CAPABILITY_LIST);
    cap_ptr &= 0xFC;  /* Mask lower 2 bits */
    
    printf("    Capabilities:");
    
    while (cap_ptr != 0 && cap_ptr < 0xFF) {
        cap_id = pci_read_config_byte(bus, dev, func, cap_ptr);
        cap_next = pci_read_config_byte(bus, dev, func, cap_ptr + 1);
        
        switch (cap_id) {
            case 0x01: printf(" PM"); break;       /* Power Management */
            case 0x02: printf(" AGP"); break;      /* AGP */
            case 0x03: printf(" VPD"); break;      /* Vital Product Data */
            case 0x04: printf(" SlotID"); break;   /* Slot ID */
            case 0x05: printf(" MSI"); break;      /* Message Signaled Interrupts */
            case 0x06: printf(" HotSwap"); break;  /* CompactPCI Hot Swap */
            case 0x07: printf(" PCI-X"); break;    /* PCI-X */
            case 0x08: printf(" HT"); break;       /* HyperTransport */
            case 0x09: printf(" VendorSpec"); break;
            case 0x0A: printf(" Debug"); break;
            case 0x0B: printf(" CPCI"); break;     /* CompactPCI Central Resource */
            case 0x0C: printf(" HotPlug"); break;  /* PCI Hot-Plug */
            case 0x0D: printf(" BridgeSubID"); break;
            case 0x0E: printf(" AGP8x"); break;
            case 0x0F: printf(" SecDev"); break;   /* Secure Device */
            case 0x10: printf(" PCIe"); break;     /* PCI Express */
            case 0x11: printf(" MSI-X"); break;
            default:   printf(" [%02X]", cap_id); break;
        }
        
        cap_ptr = cap_next & 0xFC;
    }
    
    printf("\n");
}

/* Dump raw config space */
static void dump_config_space(uint8_t bus, uint8_t dev, uint8_t func) {
    int i, j;
    printf("    Config Space:\n");

    for (i = 0; i < 256; i += 16) {
        printf("      %02X:", i);

        for (j = 0; j < 16; j++) {
            uint8_t byte = pci_read_config_byte(bus, dev, func, i + j);
            printf(" %02X", byte);
        }

        printf("  ");
        for (j = 0; j < 16; j++) {
            uint8_t byte = pci_read_config_byte(bus, dev, func, i + j);
            if (byte >= 32 && byte < 127) {
                printf("%c", byte);
            } else {
                printf(".");
            }
        }

        printf("\n");
    }
}

/* Scan a single device */
static void scan_device(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor_id, device_id;
    uint32_t class_code;
    uint8_t header_type;
    uint8_t irq, int_pin;
    
    /* Read vendor ID */
    vendor_id = pci_read_config_word(bus, dev, func, PCI_VENDOR_ID);
    
    /* Check if device exists */
    if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
        return;
    }
    
    /* Read device information */
    device_id = pci_read_config_word(bus, dev, func, PCI_DEVICE_ID);
    class_code = pci_read_config_dword(bus, dev, func, PCI_CLASS_REVISION) >> 8;
    header_type = pci_read_config_byte(bus, dev, func, PCI_HEADER_TYPE);
    
    /* Display basic information */
    printf("%02X:%02X.%X ", bus, dev, func);
    
    /* Class and subclass */
    uint8_t class = (class_code >> 16) & 0xFF;
    uint8_t subclass = (class_code >> 8) & 0xFF;
    uint8_t prog_if = class_code & 0xFF;
    
    printf("%s", get_class_name(class));
    
    if (class == 0x02) {  /* Network */
        printf("/%s", get_network_subclass(subclass));
    } else if (subclass != 0) {
        printf("/%02X", subclass);
    }
    
    printf(": ");
    
    /* Vendor and device */
    if (vendor_id == 0x10B7) {
        /* 3Com device */
        printf("3Com %s", get_3com_device_name(device_id));
        printf(" [%04X:%04X]", vendor_id, device_id);
    } else {
        printf("%s Device %04X", get_vendor_name(vendor_id), device_id);
        printf(" [%04X:%04X]", vendor_id, device_id);
    }
    
    /* Revision */
    uint8_t revision = pci_read_config_byte(bus, dev, func, PCI_CLASS_REVISION);
    if (revision != 0) {
        printf(" (rev %02X)", revision);
    }
    
    printf("\n");
    
    /* Verbose information */
    if (options.verbose) {
        /* Subsystem IDs */
        uint16_t subsys_vendor = pci_read_config_word(bus, dev, func, PCI_SUBSYSTEM_VENDOR_ID);
        uint16_t subsys_device = pci_read_config_word(bus, dev, func, PCI_SUBSYSTEM_ID);
        
        if (subsys_vendor != 0 && subsys_vendor != 0xFFFF) {
            printf("    Subsystem: %04X:%04X\n", subsys_vendor, subsys_device);
        }
        
        /* Command and status */
        uint16_t command = pci_read_config_word(bus, dev, func, PCI_COMMAND);
        uint16_t status = pci_read_config_word(bus, dev, func, PCI_STATUS);
        
        printf("    Control: I/O%c Mem%c BusMaster%c",
               (command & 0x01) ? '+' : '-',
               (command & 0x02) ? '+' : '-',
               (command & 0x04) ? '+' : '-');
        
        if (command & 0x08) printf(" SpecCycle");
        if (command & 0x10) printf(" MemWINV");
        if (command & 0x20) printf(" VGASnoop");
        if (command & 0x40) printf(" ParErr");
        if (command & 0x100) printf(" SERR");
        if (command & 0x200) printf(" FastB2B");
        printf("\n");
        
        printf("    Status: Cap%c 66MHz%c UDF%c FastB2B%c",
               (status & 0x10) ? '+' : '-',
               (status & 0x20) ? '+' : '-',
               (status & 0x40) ? '+' : '-',
               (status & 0x80) ? '+' : '-');
        
        if (status & 0x100) printf(" ParErr");
        if (status & 0x800) printf(" SERR");
        if (status & 0x1000) printf(" MasterAbort");
        if (status & 0x2000) printf(" TargetAbort");
        if (status & 0x4000) printf(" ReceivedTA");
        if (status & 0x8000) printf(" DetectedPE");
        printf("\n");
        
        /* IRQ */
        irq = pci_read_config_byte(bus, dev, func, PCI_INTERRUPT_LINE);
        int_pin = pci_read_config_byte(bus, dev, func, PCI_INTERRUPT_PIN);
        
        if (int_pin != 0) {
            printf("    Interrupt: pin %c routed to IRQ %d\n",
                   'A' + int_pin - 1, irq);
        }
        
        /* Latency and cache line */
        uint8_t latency = pci_read_config_byte(bus, dev, func, PCI_LATENCY_TIMER);
        uint8_t cache_line = pci_read_config_byte(bus, dev, func, PCI_CACHE_LINE_SIZE);
        
        if (latency != 0 || cache_line != 0) {
            printf("    Latency: %d", latency);
            if (cache_line != 0) {
                printf(", Cache Line Size: %d bytes", cache_line * 4);
            }
            printf("\n");
        }
    }
    
    /* BARs */
    if (options.show_bars) {
        int i;
        for (i = 0; i < 6; i++) {
            display_bar(bus, dev, func, i);
        }
    }
    
    /* Capabilities */
    if (options.show_caps) {
        display_capabilities(bus, dev, func);
    }
    
    /* Raw dump */
    if (options.raw_dump) {
        dump_config_space(bus, dev, func);
    }
}

/* Scan entire PCI bus */
static void scan_pci_bus(void) {
    union REGS regs;
    struct SREGS sregs;
    uint8_t max_bus = 0;
    uint8_t header_type;
    int devices_found = 0;
    
    /* Get maximum bus number */
    memset(&regs, 0, sizeof(regs));
    memset(&sregs, 0, sizeof(sregs));
    
    regs.x.ax = PCI_FUNCTION_ID | PCI_BIOS_PRESENT;
    int86x(0x1A, &regs, &regs, &sregs);
    
    if (regs.x.cflag == 0) {
        max_bus = regs.h.cl;
        printf("PCI BIOS v%d.%d present, last bus=%d\n\n",
               regs.h.bh, regs.h.bl, max_bus);
    } else {
        printf("PCI BIOS not present!\n");
        return;
    }
    
    /* Install enhanced shim if requested */
    if (options.use_shim) {
        if (pci_shim_enhanced_install()) {
            printf("Enhanced PCI shim installed\n");
            
            /* Show shim stats */
            pci_shim_stats_t stats;
            pci_shim_get_extended_stats(&stats);
            
            printf("  V86 mode: %s\n", stats.in_v86_mode ? "Yes" : "No");
            printf("  Cache: %s\n", stats.cache_enabled ? "Enabled" : "Disabled");
            printf("  Mechanism: #%d\n\n", stats.mechanism);
        }
    }
    
    /* Determine bus range to scan */
    uint8_t start_bus = 0;
    uint8_t end_bus = max_bus;
    
    if (options.target_bus != 0xFF) {
        start_bus = options.target_bus;
        end_bus = options.target_bus;
    }

    /* Scan buses */
    {
    uint8_t bus, dev;
    for (bus = start_bus; bus <= end_bus; bus++) {
        for (dev = 0; dev < 32; dev++) {
            /* Check function 0 first */
            uint16_t vendor_id = pci_read_config_word(bus, dev, 0, PCI_VENDOR_ID);

            if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
                continue;  /* No device */
            }

            /* Device exists, scan function 0 */
            scan_device(bus, dev, 0);
            devices_found++;

            /* Check if multi-function */
            header_type = pci_read_config_byte(bus, dev, 0, PCI_HEADER_TYPE);

            if (header_type & 0x80) {
                /* Multi-function device, scan other functions */
                uint8_t func;
                for (func = 1; func < 8; func++) {
                    vendor_id = pci_read_config_word(bus, dev, func, PCI_VENDOR_ID);

                    if (vendor_id != 0xFFFF && vendor_id != 0x0000) {
                        scan_device(bus, dev, func);
                        devices_found++;
                    }
                }
            }
        }
    }
    }
    
    printf("\nTotal devices found: %d\n", devices_found);
    
    /* Show shim statistics if used */
    if (options.use_shim) {
        pci_shim_stats_t stats;
        pci_shim_get_extended_stats(&stats);
        
        printf("\nShim Statistics:\n");
        printf("  Total calls: %lu\n", stats.total_calls);
        printf("  Cache hits: %lu (%.1f%%)\n",
               stats.cache_hits,
               stats.cache_hits * 100.0 / (stats.cache_hits + stats.cache_misses + 1));
        
        pci_shim_enhanced_uninstall();
    }
}

/* Display help */
static void show_help(const char* prog_name) {
    printf("PCI Bus Scanner v1.0\n");
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -v, --verbose    Show detailed device information\n");
    printf("  -b, --bars       Display BAR (Base Address Register) info\n");
    printf("  -c, --caps       Show device capabilities\n");
    printf("  -s, --shim       Use enhanced PCI shim with caching\n");
    printf("  -r, --raw        Dump raw config space\n");
    printf("  -B <bus>         Scan specific bus only\n");
    printf("  -h, --help       Show this help\n");
    printf("\nExamples:\n");
    printf("  %s               Basic device listing\n", prog_name);
    printf("  %s -v -b         Verbose with BARs\n", prog_name);
    printf("  %s -s -v         Use shim with verbose output\n", prog_name);
    printf("  %s -B 0 -r       Dump config space for bus 0\n", prog_name);
}

/* Parse command line */
static void parse_args(int argc, char* argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            options.verbose = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bars") == 0) {
            options.show_bars = true;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--caps") == 0) {
            options.show_caps = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shim") == 0) {
            options.use_shim = true;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--raw") == 0) {
            options.raw_dump = true;
        } else if (strcmp(argv[i], "-B") == 0 && i + 1 < argc) {
            options.target_bus = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            exit(0);
        } else {
            printf("Unknown option: %s\n", argv[i]);
            show_help(argv[0]);
            exit(1);
        }
    }
}

/* Main function */
int main(int argc, char* argv[]) {
    /* Parse command line */
    parse_args(argc, argv);
    
    /* Scan PCI bus */
    scan_pci_bus();
    
    return 0;
}