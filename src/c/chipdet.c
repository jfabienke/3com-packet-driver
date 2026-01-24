/**
 * @file chipset_detect.c
 * @brief Safe chipset detection for diagnostic purposes only
 *
 * 3Com Packet Driver - Safe Chipset Detection
 *
 * This module implements safe chipset detection using only standardized
 * methods (PCI configuration space). NO risky I/O port probing is performed
 * on pre-PCI systems. All chipset information is used for diagnostic
 * purposes only - runtime testing determines actual behavior.
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "chipdet.h"
#include "logging.h"
#include "hardware.h"
#include "common.h"
#include <string.h>
#include <stdint.h>

/* Known chipset database for identification */
static const chipset_info_t known_chipsets[] = {
    /* Intel Chipsets */
    {0x8086, 0x122D, "Intel 82437FX (Triton)", CHIPSET_ERA_PCI, true, false},
    {0x8086, 0x7030, "Intel 82437VX (Triton II)", CHIPSET_ERA_PCI, true, false},
    {0x8086, 0x7100, "Intel 82439TX (430TX)", CHIPSET_ERA_PCI, true, false},
    {0x8086, 0x84C4, "Intel 82450GX (Orion)", CHIPSET_ERA_PCI, true, true},
    {0x8086, 0x84C5, "Intel 82450KX (Orion)", CHIPSET_ERA_PCI, true, false},
    {0x8086, 0x1237, "Intel 82441FX (Natoma)", CHIPSET_ERA_PCI, true, false},
    {0x8086, 0x7000, "Intel 82371SB (PIIX3)", CHIPSET_ERA_PCI, false, false},
    {0x8086, 0x7110, "Intel 82371AB (PIIX4)", CHIPSET_ERA_PCI, false, false},
    
    /* VIA Chipsets */
    {0x1106, 0x0585, "VIA VT82C585VP (Apollo VP)", CHIPSET_ERA_PCI, true, false},
    {0x1106, 0x0595, "VIA VT82C595 (Apollo VP2)", CHIPSET_ERA_PCI, true, false},
    {0x1106, 0x0597, "VIA VT82C597 (Apollo VP3)", CHIPSET_ERA_PCI, true, false},
    {0x1106, 0x0598, "VIA VT82C598MVP (Apollo MVP3)", CHIPSET_ERA_PCI, true, false},
    
    /* SiS Chipsets */
    {0x1039, 0x0496, "SiS 85C496/497", CHIPSET_ERA_PCI, true, false},
    {0x1039, 0x5571, "SiS 5571 (Trinity)", CHIPSET_ERA_PCI, true, false},
    {0x1039, 0x5597, "SiS 5597/5598", CHIPSET_ERA_PCI, true, false},
    
    /* ALi Chipsets */
    {0x10B9, 0x1521, "ALi M1521 (Aladdin III)", CHIPSET_ERA_PCI, true, false},
    {0x10B9, 0x1531, "ALi M1531 (Aladdin IV)", CHIPSET_ERA_PCI, true, false},
    {0x10B9, 0x1541, "ALi M1541 (Aladdin V)", CHIPSET_ERA_PCI, true, false},
    
    /* OPTi Chipsets */
    {0x1045, 0xC557, "OPTi 82C557 (Viper-M)", CHIPSET_ERA_PCI, true, false},
    {0x1045, 0xC558, "OPTi 82C558 (Viper-M)", CHIPSET_ERA_PCI, true, false},
    
    /* AMD Chipsets */
    {0x1022, 0x7006, "AMD-751 (Irongate)", CHIPSET_ERA_PCI, true, false},
    {0x1022, 0x700C, "AMD-761 (IGD4)", CHIPSET_ERA_PCI, true, true},
    
    /* Sentinel */
    {0x0000, 0x0000, NULL, CHIPSET_ERA_UNKNOWN, false, false}
};

/* PCI BIOS detection functions */
static bool detect_pci_bios(void);
static uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static const chipset_info_t* lookup_chipset_info(uint16_t vendor_id, uint16_t device_id);

/* PnP ISA detection - GPT-5 recommended for ISA slot verification */
static bool has_pnp_isa_bios(void);
static int count_pnp_isa_nodes(void);

/* Bus detection functions - extern from assembly */
extern int is_mca_system(void);
extern int is_eisa_system(void);
extern int nic_detect_mca_3c523(void);
extern int nic_detect_mca_3c529(void);
extern int nic_detect_eisa_3c592(void);
extern int nic_detect_eisa_3c597(void);
extern int nic_detect_vlb(void);
extern int get_ps2_model(void);

/**
 * Detect system chipset safely using only PCI methods
 */
chipset_detection_result_t detect_system_chipset(void) {
    chipset_detection_result_t result = {0};
    
    log_info("Performing safe chipset detection...");
    
    /* First check if PCI BIOS is available */
    if (!detect_pci_bios()) {
        log_info("PCI BIOS not detected - pre-PCI system");
        result.detection_method = CHIPSET_DETECT_NONE;
        result.confidence = CHIPSET_CONFIDENCE_UNKNOWN;
        strcpy(result.chipset.name, "Unknown (Pre-PCI System)");
        strcpy(result.diagnostic_info, "Pre-1993 ISA-only system - no safe detection method available");
        return result;
    }
    
    log_info("PCI BIOS detected - attempting host bridge identification");
    
    /* Try to identify host bridge (Bus 0, Device 0, Function 0) */
    uint16_t vendor_id = pci_read_config_word(0, 0, 0, 0x00);
    uint16_t device_id = pci_read_config_word(0, 0, 0, 0x02);
    
    if (vendor_id == 0xFFFF || vendor_id == 0x0000) {
        log_warning("No valid PCI host bridge found");
        result.detection_method = CHIPSET_DETECT_PCI_FAILED;
        result.confidence = CHIPSET_CONFIDENCE_LOW;
        strcpy(result.chipset.name, "Unknown (PCI Detection Failed)");
        strcpy(result.diagnostic_info, "PCI BIOS present but host bridge not accessible");
        return result;
    }
    
    /* Successful PCI detection */
    result.detection_method = CHIPSET_DETECT_PCI_SUCCESS;
    result.chipset.vendor_id = vendor_id;
    result.chipset.device_id = device_id;
    result.chipset.era = CHIPSET_ERA_PCI;
    result.chipset.found = true;
    
    /* Look up chipset information */
    const chipset_info_t* known_info = lookup_chipset_info(vendor_id, device_id);
    if (known_info && known_info->name) {
        strcpy(result.chipset.name, known_info->name);
        result.chipset.supports_bus_master = known_info->supports_bus_master;
        result.chipset.reliable_snooping = known_info->reliable_snooping;
        result.confidence = CHIPSET_CONFIDENCE_HIGH;
        
        sprintf(result.diagnostic_info, 
                "PCI Host Bridge: %04X:%04X, Bus Master: %s, Snooping: %s",
                vendor_id, device_id,
                known_info->supports_bus_master ? "Yes" : "No",
                known_info->reliable_snooping ? "Documented" : "Undocumented");
    } else {
        sprintf(result.chipset.name, "Unknown Chipset (%04X:%04X)", vendor_id, device_id);
        result.chipset.supports_bus_master = true;  /* Assume yes for PCI-era */
        result.chipset.reliable_snooping = false;   /* Assume no - test will determine */
        result.confidence = CHIPSET_CONFIDENCE_MEDIUM;
        
        sprintf(result.diagnostic_info,
                "Unknown PCI Host Bridge: %04X:%04X (not in database)",
                vendor_id, device_id);
    }
    
    /* Additional PCI device scanning for more context */
    result.additional_info = scan_additional_pci_devices();
    
    log_info("Chipset detection: %s (confidence: %d)", 
             result.chipset.name, result.confidence);
    
    return result;
}

/**
 * Detect if PnP ISA BIOS is present (GPT-5 corrected)
 * Used to verify actual ISA slots vs LPC-only systems
 */
static bool has_pnp_isa_bios(void) {
    uint16_t status;
    uint16_t segment, offset;
    uint8_t version_major, version_minor;
    uint8_t carry_flag;
    
    /* PnP ISA BIOS Installation Check - INT 1Ah, AX=5F00h */
    __asm__ __volatile__ (
        "movw $0x5F00, %%ax\n\t"
        "int $0x1A\n\t"
        "pushf\n\t"
        "pop %%cx\n\t"
        "movw %%ax, %0\n\t"
        "movw %%es, %1\n\t"
        "movw %%di, %2\n\t"
        "movb %%bh, %3\n\t"
        "movb %%bl, %4\n\t"
        "movb %%cl, %5"
        : "=m" (status), "=m" (segment), "=m" (offset),
          "=m" (version_major), "=m" (version_minor), "=m" (carry_flag)
        :
        : "ax", "bx", "cx", "dx", "es", "di"
    );
    
    /* Check carry flag for success */
    if (carry_flag & 0x01) {
        return false;  /* PnP BIOS not present */
    }
    
    /* ES:DI points to PnP Installation Check Structure */
    uint8_t __far* signature = MK_FP(segment, offset);
    
    /* Verify $PnP signature */
    if (signature[0] == '$' && signature[1] == 'P' && 
        signature[2] == 'n' && signature[3] == 'P') {
        
        /* Verify structure checksum */
        uint8_t length = signature[5];  /* Structure length at offset 5 */
        uint8_t checksum = 0;
        
        {
            uint8_t i;
            for (i = 0; i < length; i++) {
                checksum += signature[i];
            }
        }
        
        if (checksum == 0) {  /* Valid checksum sums to 0 */
            log_debug("PnP ISA BIOS v%d.%d detected", version_major, version_minor);
            return true;
        }
    }
    
    return false;
}

/**
 * Count PnP ISA nodes to estimate ISA slot availability
 */
static int count_pnp_isa_nodes(void) {
    uint16_t status;
    uint16_t node_count = 0;
    uint8_t carry_flag;
    
    if (!has_pnp_isa_bios()) {
        return 0;
    }
    
    /* Get Number of System Device Nodes - INT 1Ah, AX=5F00h */
    __asm__ __volatile__ (
        "movw $0x5F00, %%ax\n\t"
        "int $0x1A\n\t"
        "pushf\n\t"
        "pop %%dx\n\t"
        "movw %%ax, %0\n\t"
        "movw %%cx, %1\n\t"
        "movb %%dl, %2"
        : "=m" (status), "=m" (node_count), "=m" (carry_flag)
        :
        : "ax", "bx", "cx", "dx"
    );
    
    if (!(carry_flag & 0x01)) {  /* Success if carry clear */
        log_debug("PnP ISA: %u device nodes found", node_count);
        return node_count;
    }
    
    return 0;
}

/**
 * Detect PCI BIOS presence
 */
static bool detect_pci_bios(void) {
    uint16_t status;
    uint8_t major_version, minor_version;
    
    /* PCI BIOS Installation Check - INT 1Ah, AX=B101h */
    __asm__ __volatile__ (
        "movw $0xB101, %%ax\n\t"
        "int $0x1A\n\t"
        "movw %%ax, %0\n\t"
        "movb %%bh, %1\n\t"
        "movb %%bl, %2"
        : "=m" (status), "=m" (major_version), "=m" (minor_version)
        :
        : "ax", "bx", "cx", "dx"
    );
    
    /* Check if PCI BIOS is present */
    if (status != 0x0001) {
        log_debug("PCI BIOS installation check failed: AX=%04X", status);
        return false;
    }
    
    log_debug("PCI BIOS v%d.%d detected", major_version, minor_version);
    return true;
}

/**
 * Read PCI configuration byte
 */
static uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint8_t result = 0xFF;
    uint16_t status;
    uint8_t dev_func = (device << 3) | function;
    
    /* PCI BIOS Read Configuration Byte - INT 1Ah, AX=B108h */
    __asm__ __volatile__ (
        "movw $0xB108, %%ax\n\t"
        "movb %2, %%bh\n\t"          /* Bus number */
        "movb %3, %%bl\n\t"          /* Device/Function */
        "movw %4, %%di\n\t"          /* Register offset */
        "int $0x1A\n\t"
        "movw %%ax, %0\n\t"          /* Status */
        "movb %%cl, %1"              /* Data */
        : "=m" (status), "=m" (result)
        : "m" (bus), "m" (dev_func), "m" (offset)
        : "ax", "bx", "cx", "dx", "di"
    );
    
    if ((status & 0xFF00) != 0x0000) {
        return 0xFF;
    }
    
    return result;
}

/**
 * Read PCI configuration word
 */
static uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint16_t result = 0xFFFF;
    uint16_t status;
    uint8_t dev_func = (device << 3) | function;
    
    /* PCI BIOS Read Configuration Word - INT 1Ah, AX=B109h */
    __asm__ __volatile__ (
        "movw $0xB109, %%ax\n\t"
        "movb %2, %%bh\n\t"          /* Bus number */
        "movb %3, %%bl\n\t"          /* Device/Function */
        "movw %4, %%di\n\t"          /* Register offset */
        "int $0x1A\n\t"
        "movw %%ax, %0\n\t"          /* Status */
        "movw %%cx, %1"              /* Data */
        : "=m" (status), "=m" (result)
        : "m" (bus), "m" (dev_func), "m" (offset)
        : "ax", "bx", "cx", "dx", "di"
    );
    
    if ((status & 0xFF00) != 0x0000) {
        return 0xFFFF;
    }
    
    return result;
}

/**
 * Read PCI configuration dword
 */
static uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t result = 0xFFFFFFFF;
    uint16_t status;
    uint8_t dev_func = (device << 3) | function;
    
    /* PCI BIOS Read Configuration Dword - INT 1Ah, AX=B10Ah */
    __asm__ __volatile__ (
        "movw $0xB10A, %%ax\n\t"
        "movb %2, %%bh\n\t"          /* Bus number */
        "movb %3, %%bl\n\t"          /* Device/Function */
        "movw %4, %%di\n\t"          /* Register offset */
        "int $0x1A\n\t"
        "movw %%ax, %0\n\t"          /* Status */
        "movl %%ecx, %1"             /* Data */
        : "=m" (status), "=m" (result)
        : "m" (bus), "m" (dev_func), "m" (offset)
        : "ax", "bx", "ecx", "dx", "di"
    );
    
    if ((status & 0xFF00) != 0x0000) {
        return 0xFFFFFFFF;
    }
    
    return result;
}

/**
 * Look up chipset information in known database
 */
static const chipset_info_t* lookup_chipset_info(uint16_t vendor_id, uint16_t device_id) {
    {
        int i;
        for (i = 0; known_chipsets[i].name != NULL; i++) {
            if (known_chipsets[i].vendor_id == vendor_id &&
                known_chipsets[i].device_id == device_id) {
                return &known_chipsets[i];
            }
        }
    }
    return NULL;
}

/* LPC-only bridges that don't have physical ISA slots */
static const uint16_t lpc_only_bridges[][2] = {
    /* Intel ICH6+ are typically LPC-only without ISA slots */
    {0x8086, 0x2640},  /* ICH6 */
    {0x8086, 0x27B8},  /* ICH7 */
    {0x8086, 0x2810},  /* ICH8 */
    {0x8086, 0x2914},  /* ICH9 */
    {0x8086, 0x3A18},  /* ICH10 */
    /* Add more known LPC-only bridges as needed */
};

/**
 * Verify if ISA slots are likely present (GPT-5 recommended)
 * Distinguishes real ISA slots from LPC-only systems
 */
bool verify_isa_slots_present(const chipset_additional_info_t* info) {
    /* 1. Check for ISA bridge presence */
    if (!info || !info->has_isa_bridge) {
        return false;
    }
    
    /* 2. Check against known LPC-only bridges */
    {
        int i;
        for (i = 0; i < info->pci_device_count; i++) {
            uint16_t vendor = info->pci_devices[i].vendor_id;
            uint16_t device = info->pci_devices[i].device_id;
            size_t j;

            /* Check if this is a known LPC-only bridge */
            for (j = 0; j < sizeof(lpc_only_bridges)/sizeof(lpc_only_bridges[0]); j++) {
                if (vendor == lpc_only_bridges[j][0] &&
                    device >= lpc_only_bridges[j][1]) {
                    /* This is an LPC-only bridge, no physical ISA slots */
                    log_info("LPC-only bridge detected, no physical ISA slots");
                    return false;
                }
            }
        }
    }
    
    /* 3. PnP ISA BIOS as corroborating signal only */
    /* Note: PnP BIOS can exist on LPC-only systems, so it's not definitive */
    bool has_pnp = has_pnp_isa_bios();
    if (has_pnp) {
        log_debug("PnP ISA BIOS detected (corroborating signal)");
    }
    
    /* 4. Conservative approach: assume ISA slots present unless proven otherwise */
    /* For pre-2000 chipsets, ISA slots are almost always present */
    /* For 2000+ chipsets without being in LPC-only list, assume ISA present */
    log_info("ISA slots likely present");
    return true;
}

/* Extended ISA bridge detection table */
static const struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char* name;
} isa_bridges[] = {
    /* Intel chipset device IDs - ISA bridges only */
    {0x8086, 0x122E, "Intel 82371FB PIIX ISA"},
    {0x8086, 0x7000, "Intel 82371SB PIIX3 ISA"},
    {0x8086, 0x7110, "Intel 82371AB PIIX4 ISA"},
    {0x8086, 0x7198, "Intel 82443MX ISA"},
    
    /* Intel ICH series ISA/LPC bridges */
    {0x8086, 0x2410, "Intel 82801AA ICH LPC"},
    {0x8086, 0x2420, "Intel 82801AB ICH0 LPC"},
    {0x8086, 0x2440, "Intel 82801BA ICH2 LPC"},
    {0x8086, 0x2480, "Intel 82801CA ICH3 LPC"},
    {0x8086, 0x24C0, "Intel 82801DB ICH4 LPC"},
    {0x8086, 0x24D0, "Intel 82801EB ICH5 LPC"},
    
    /* VIA bridges */
    {0x1106, 0x0586, "VIA VT82C586 ISA"},
    {0x1106, 0x0596, "VIA VT82C596 ISA"},
    {0x1106, 0x0686, "VIA VT82C686 ISA"},
    {0x1106, 0x8231, "VIA VT8231 ISA"},
    
    /* SiS bridges */
    {0x1039, 0x0008, "SiS 85C503 ISA"},
    {0x1039, 0x0018, "SiS 85C503 ISA"},
    
    /* ALi bridges */
    {0x10B9, 0x1533, "ALi M1533 ISA"},
    
    /* AMD bridges */
    {0x1022, 0x7400, "AMD-766 ISA"},
    {0x1022, 0x7408, "AMD-768 ISA"}
};

/**
 * Scan additional PCI devices for diagnostic information
 */
chipset_additional_info_t scan_additional_pci_devices(void) {
    chipset_additional_info_t info = {0};
    
    log_debug("Scanning additional PCI devices...");

    /* Scan all PCI buses and functions */
    {
        uint8_t bus;
        for (bus = 0; bus < 4; bus++) {  /* Scan up to 4 buses */
            uint8_t dev;
            for (dev = 0; dev < 32; dev++) {
                uint16_t vendor = pci_read_config_word(bus, dev, 0, 0x00);
                uint8_t header_type;
                uint8_t max_func;
                uint8_t func;

                if (vendor == 0xFFFF) continue;

                /* Check if multi-function device */
                header_type = pci_read_config_byte(bus, dev, 0, 0x0E);
                max_func = (header_type & 0x80) ? 8 : 1;

                for (func = 0; func < max_func; func++) {
                    uint16_t device;
                    uint32_t class_rev;
                    uint8_t base_class;
                    uint8_t sub_class;

                    if (func > 0) {
                        vendor = pci_read_config_word(bus, dev, func, 0x00);
                        if (vendor == 0xFFFF) continue;
                    }

                    device = pci_read_config_word(bus, dev, func, 0x02);

                    /* Check PCI class code for ISA bridge */
                    class_rev = pci_read_config_dword(bus, dev, func, 0x08);
                    base_class = (class_rev >> 24) & 0xFF;
                    sub_class = (class_rev >> 16) & 0xFF;

                    if (base_class == 0x06 && sub_class == 0x01) {
                        /* This is an ISA/LPC bridge by class code */
                        bool found_name = false;
                        size_t j;

                        info.has_isa_bridge = true;

                        /* Try to identify specific bridge */
                        for (j = 0; j < sizeof(isa_bridges)/sizeof(isa_bridges[0]); j++) {
                            if (vendor == isa_bridges[j].vendor_id &&
                                device == isa_bridges[j].device_id) {
                                strncpy(info.isa_bridge_name, isa_bridges[j].name,
                                        sizeof(info.isa_bridge_name) - 1);
                                info.isa_bridge_name[sizeof(info.isa_bridge_name) - 1] = '\0';
                                found_name = true;
                                break;
                            }
                        }

                        if (!found_name) {
                            /* Generic name for unknown ISA bridge */
                            sprintf(info.isa_bridge_name, "ISA Bridge (%04X:%04X)", vendor, device);
                        }

                        log_debug("Found ISA bridge: %s", info.isa_bridge_name);
                    }

                    /* Store device info */
                    if (info.pci_device_count < MAX_ADDITIONAL_PCI_DEVICES) {
                        info.pci_devices[info.pci_device_count].vendor_id = vendor;
                        info.pci_devices[info.pci_device_count].device_id = device;
                        info.pci_device_count++;
                    }
                    info.total_pci_devices_found++;
                }
            }
        }
    }
    
    return info;
}

/**
 * Detect system bus type and report unsupported NICs
 */
bus_type_t detect_system_bus(void) {
    bus_type_t primary_bus = BUS_TYPE_ISA;  /* Default to ISA */
    bool has_mca = false;
    bool has_eisa = false;
    bool has_pci = false;
    bool has_vlb = false;
    
    log_info("Detecting system bus architecture...");
    
    /* Check for MCA (MicroChannel) first - it's exclusive */
    has_mca = is_mca_system();
    if (has_mca) {
        primary_bus = BUS_TYPE_MCA;
        int ps2_model = get_ps2_model();
        
        if (ps2_model) {
            log_info("IBM PS/2 Model %s detected (MicroChannel Architecture)", 
                     get_ps2_model_name(ps2_model));
        } else {
            log_info("IBM MicroChannel Architecture detected (unknown model)");
        }
        
        /* Check for MCA NICs and warn if found */
        int has_mca_nics = 0;
        if (nic_detect_mca_3c523()) {
            log_warning("MCA: 3C523 EtherLink/MC detected but not supported");
            has_mca_nics = 1;
        }
        if (nic_detect_mca_3c529()) {
            log_warning("MCA: 3C529 EtherLink III/MC detected but not supported");
            has_mca_nics = 1;
        }
        
        /* MCA systems don't have ISA slots - skip ISA scanning entirely */
        if (has_mca_nics) {
            log_warning("MicroChannel NICs detected but not supported");
        }
        log_error("ERROR: No compatible network adapters available on this system.");
        log_error("This driver only supports ISA-based 3Com NICs (3C509B, 3C515-TX).");
        log_error("MicroChannel systems require MCA-specific network drivers.");
        /* Note: The driver should exit after this, handled by caller */
        return primary_bus;
    }
    
    /* Check for other bus types - these can coexist with ISA */
    has_eisa = is_eisa_system();
    has_pci = detect_pci_bios();
    has_vlb = nic_detect_vlb();
    
    /* Determine primary bus type for reporting */
    if (has_pci) {
        primary_bus = BUS_TYPE_PCI;
        log_info("PCI bus detected");
    } else if (has_eisa) {
        primary_bus = BUS_TYPE_EISA;
        log_info("EISA bus detected (ISA compatible)");
        
        /* Check for EISA NICs and warn if found */
        if (nic_detect_eisa_3c592()) {
            log_warning("EISA: 3C592 detected but not supported");
        }
        if (nic_detect_eisa_3c597()) {
            log_warning("EISA: 3C597 detected but not supported");
        }
        log_info("Will scan for ISA NICs (3C509B, 3C515-TX)");
    } else if (has_vlb) {
        primary_bus = BUS_TYPE_VLB;
        log_info("VESA Local Bus detected");
        log_warning("VLB NICs not supported - will scan for ISA NICs");
    } else {
        log_info("ISA bus system (default)");
    }
    
    /* For non-MCA systems, always scan for ISA NICs since:
     * - EISA systems have ISA slots
     * - PCI systems typically have ISA slots
     * - VLB systems have ISA slots
     * - Pure ISA systems obviously have ISA slots
     */
    
    return primary_bus;
}

/**
 * Get PS/2 model name string
 */
const char* get_ps2_model_name(int model) {
    switch (model) {
        case 0x50: return "50";
        case 0x60: return "60";
        case 0x70: return "70";
        case 0x80: return "80";
        case 0x90: return "90";
        case 0x95: return "95 (Server)";
        case 0x56: return "56";
        case 0x57: return "57";
        case 0x76: return "76";
        case 0x77: return "77";
        case 0x85: return "85";
        default:   return "Unknown";
    }
}

/**
 * Get bus type name string
 */
const char* get_bus_type_name(bus_type_t bus_type) {
    switch (bus_type) {
        case BUS_TYPE_ISA:      return "ISA";
        case BUS_TYPE_EISA:     return "EISA";
        case BUS_TYPE_MCA:      return "MicroChannel";
        case BUS_TYPE_VLB:      return "VESA Local Bus";
        case BUS_TYPE_PCI:      return "PCI";
        case BUS_TYPE_PCMCIA:   return "PCMCIA";
        case BUS_TYPE_CARDBUS:  return "CardBus";
        default:                return "Unknown";
    }
}

/**
 * Get chipset detection confidence description
 */
const char* get_chipset_confidence_description(chipset_confidence_t confidence) {
    switch (confidence) {
        case CHIPSET_CONFIDENCE_HIGH:
            return "High (Known chipset in database)";
        case CHIPSET_CONFIDENCE_MEDIUM:
            return "Medium (PCI detected, unknown chipset)";
        case CHIPSET_CONFIDENCE_LOW:
            return "Low (PCI BIOS present, detection failed)";
        case CHIPSET_CONFIDENCE_UNKNOWN:
            return "Unknown (Pre-PCI system)";
        default:
            return "Invalid";
    }
}

/**
 * Get chipset detection method description
 */
const char* get_chipset_detection_method_description(chipset_detection_method_t method) {
    switch (method) {
        case CHIPSET_DETECT_PCI_SUCCESS:
            return "PCI Configuration Space (Safe)";
        case CHIPSET_DETECT_PCI_FAILED:
            return "PCI BIOS Available (Detection Failed)";
        case CHIPSET_DETECT_NONE:
            return "No Safe Detection Method (Pre-PCI)";
        default:
            return "Unknown Method";
    }
}

/**
 * Check if chipset is known to support reliable snooping
 */
bool chipset_supports_reliable_snooping(const chipset_info_t* chipset) {
    if (!chipset || !chipset->found) {
        return false;
    }
    
    /* Only trust documented reliable snooping implementations */
    return chipset->reliable_snooping;
}

/**
 * Check if chipset era supports bus mastering
 */
bool chipset_era_supports_bus_master(chipset_era_t era) {
    switch (era) {
        case CHIPSET_ERA_PCI:
            return true;    /* PCI era chipsets generally support bus mastering */
        case CHIPSET_ERA_VLB:
            return true;    /* VESA Local Bus supports bus mastering */
        case CHIPSET_ERA_EISA:
            return true;    /* EISA supports bus mastering */
        case CHIPSET_ERA_ISA:
            return false;   /* Pure ISA chipsets typically don't support bus mastering */
        case CHIPSET_ERA_UNKNOWN:
        default:
            return false;   /* Conservative assumption */
    }
}

/**
 * Print detailed chipset detection results
 */
void print_chipset_detection_results(const chipset_detection_result_t* result) {
    if (!result) {
        return;
    }
    
    printf("\n=== Chipset Detection Results ===\n");
    printf("Detection Method: %s\n", 
           get_chipset_detection_method_description(result->detection_method));
    printf("Confidence Level: %s\n", 
           get_chipset_confidence_description(result->confidence));
    
    if (result->chipset.found) {
        printf("Chipset: %s\n", result->chipset.name);
        printf("Vendor/Device ID: %04X:%04X\n", 
               result->chipset.vendor_id, result->chipset.device_id);
        printf("Bus Master Support: %s\n", 
               result->chipset.supports_bus_master ? "Yes" : "No");
        printf("Documented Snooping: %s\n", 
               result->chipset.reliable_snooping ? "Yes" : "No");
    } else {
        printf("Chipset: Not detected\n");
    }
    
    printf("Diagnostic Info: %s\n", result->diagnostic_info);
    
    if (result->additional_info.pci_device_count > 0) {
        printf("Additional PCI Devices: %d found\n", 
               result->additional_info.pci_device_count);
        if (result->additional_info.has_isa_bridge) {
            printf("ISA Bridge: %s\n", result->additional_info.isa_bridge_name);
        }
    }
    
    printf("================================\n");
}

/**
 * Generate chipset recommendation based on detection
 */
chipset_recommendation_t generate_chipset_recommendation(const chipset_detection_result_t* detection) {
    chipset_recommendation_t recommendation = {0};
    
    if (!detection) {
        recommendation.use_runtime_testing = true;
        strcpy(recommendation.reasoning, "Invalid detection result - use runtime testing");
        return recommendation;
    }
    
    /* Always recommend runtime testing as primary method */
    recommendation.use_runtime_testing = true;
    
    if (detection->confidence == CHIPSET_CONFIDENCE_UNKNOWN) {
        recommendation.expect_cache_management = true;
        recommendation.expect_no_snooping = true;
        strcpy(recommendation.reasoning, 
               "Pre-PCI system - expect no hardware snooping, software cache management required");
    } else if (detection->chipset.found && detection->chipset.reliable_snooping) {
        recommendation.expect_cache_management = false;
        recommendation.expect_no_snooping = false;
        strcpy(recommendation.reasoning,
               "Chipset documented to support reliable snooping - but runtime test will verify");
    } else {
        recommendation.expect_cache_management = true;
        recommendation.expect_no_snooping = true;
        strcpy(recommendation.reasoning,
               "No documented reliable snooping - expect software cache management required");
    }
    
    return recommendation;
}