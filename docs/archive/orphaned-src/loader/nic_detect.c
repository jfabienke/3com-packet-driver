/**
 * @file nic_detect.c
 * @brief Centralized NIC detection for loader (cold section)
 *
 * Detects 3Com NICs (3C509B and 3C515) during initialization.
 * This entire module is discarded after TSR installation.
 *
 * Supports:
 * - 3C509B: 10 Mbps ISA with PnP
 * - 3C515: 100 Mbps ISA with bus mastering
 *
 * Constraints:
 * - DOS real mode only
 * - ISA bus I/O port scanning
 * - PnP BIOS calls when available
 * - Results used for SMC patching
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/production.h"

/* Mark entire file for cold section */
#pragma code_seg("COLD_TEXT", "CODE")

/* 3Com vendor ID */
#define VENDOR_3COM     0x10B7

/* Device IDs */
#define DEVICE_3C509B   0x5090
#define DEVICE_3C515    0x5157

/* I/O port ranges to scan */
#define IO_SCAN_START   0x200
#define IO_SCAN_END     0x3F0
#define IO_SCAN_STEP    0x10

/* 3Com ID port for PnP */
#define ID_PORT         0x110

/* EEPROM commands */
#define EEPROM_CMD_READ 0x80

/* Global NIC information */
nic_info_t g_detected_nics[MAX_NICS] = {0};
int g_nic_count = 0;

/**
 * @brief Read EEPROM register from 3Com NIC
 * @param io_base Base I/O address
 * @param reg EEPROM register to read
 * @return EEPROM value or 0xFFFF on error
 */
static uint16_t read_eeprom(uint16_t io_base, uint8_t reg) {
    uint16_t value;
    int timeout = 1000;
    
    /* Select window 0 */
    outp(io_base + 0x0E, 0x0800);
    
    /* Send EEPROM read command */
    outpw(io_base + 0x0A, EEPROM_CMD_READ | reg);
    
    /* Wait for EEPROM ready */
    while (timeout-- > 0) {
        value = inpw(io_base + 0x0A);
        if (!(value & 0x8000)) {  /* BUSY bit clear */
            break;
        }
    }
    
    if (timeout <= 0) {
        return 0xFFFF;  /* Timeout */
    }
    
    /* Read EEPROM data */
    value = inpw(io_base + 0x0C);
    
    return value;
}

/**
 * @brief Check if I/O port has a 3Com NIC
 * @param io_base I/O base address to check
 * @return NIC type or NIC_TYPE_NONE
 */
static nic_type_t check_3com_signature(uint16_t io_base) {
    uint16_t manufacturer_id;
    uint16_t product_id;
    
    /* Try to read manufacturer ID from EEPROM */
    manufacturer_id = read_eeprom(io_base, 0x0A);
    
    /* Check for 3Com manufacturer ID (0x6D50) */
    if (manufacturer_id != 0x6D50) {
        return NIC_TYPE_NONE;
    }
    
    /* Read product ID */
    product_id = read_eeprom(io_base, 0x03);
    
    /* Identify NIC model */
    switch (product_id & 0xFF00) {
    case 0x9000:  /* 3C509 family */
    case 0x9050:  /* 3C509B */
        LOG_INFO("Found 3C509B at I/O 0x%03X", io_base);
        return NIC_TYPE_3C509B;
        
    case 0x5100:  /* 3C515 family */
    case 0x5157:  /* 3C515-TX */
        LOG_INFO("Found 3C515 at I/O 0x%03X", io_base);
        return NIC_TYPE_3C515;
        
    default:
        LOG_DEBUG("Unknown 3Com NIC (ID: 0x%04X) at 0x%03X", 
                 product_id, io_base);
        return NIC_TYPE_NONE;
    }
}

/**
 * @brief Scan ISA I/O ports for 3Com NICs
 * @return Number of NICs found
 */
static int scan_isa_ports(void) {
    uint16_t io_base;
    nic_type_t nic_type;
    int found = 0;
    
    LOG_DEBUG("Scanning ISA I/O ports 0x%03X-0x%03X...", 
             IO_SCAN_START, IO_SCAN_END);
    
    /* Common 3Com I/O addresses to check first */
    static const uint16_t common_ports[] = {
        0x300, 0x310, 0x320, 0x330,
        0x280, 0x2A0, 0x2E0,
        0x200, 0x210, 0x220, 0x240,
        0
    };
    
    /* Check common ports first */
    for (int i = 0; common_ports[i] != 0; i++) {
        io_base = common_ports[i];
        
        nic_type = check_3com_signature(io_base);
        if (nic_type != NIC_TYPE_NONE) {
            /* Found a NIC */
            g_detected_nics[found].type = nic_type;
            g_detected_nics[found].io_base = io_base;
            found++;
            
            if (found >= MAX_NICS) {
                break;
            }
        }
    }
    
    /* If not found in common ports, do full scan */
    if (found == 0) {
        for (io_base = IO_SCAN_START; io_base <= IO_SCAN_END; 
             io_base += IO_SCAN_STEP) {
            
            /* Skip already checked common ports */
            int skip = 0;
            for (int i = 0; common_ports[i] != 0; i++) {
                if (io_base == common_ports[i]) {
                    skip = 1;
                    break;
                }
            }
            if (skip) continue;
            
            nic_type = check_3com_signature(io_base);
            if (nic_type != NIC_TYPE_NONE) {
                g_detected_nics[found].type = nic_type;
                g_detected_nics[found].io_base = io_base;
                found++;
                
                if (found >= MAX_NICS) {
                    break;
                }
            }
        }
    }
    
    return found;
}

/**
 * @brief Try PnP detection for 3Com NICs
 * @return Number of NICs found via PnP
 */
static int detect_pnp_nics(void) {
    uint8_t pnp_csn = 0;
    int found = 0;
    
    LOG_DEBUG("Attempting PnP detection...");
    
    /* Initialize PnP */
    outp(0x279, 0x02);  /* Reset CSNs */
    outp(0x279, 0x03);  /* Wake[0] */
    
    /* Initiation key sequence */
    outp(0x279, 0x00);
    outp(0x279, 0x00);
    
    for (int i = 0; i < 32; i++) {
        outp(0x279, 0x6A ^ i);
    }
    
    /* Try to find PnP cards */
    for (pnp_csn = 1; pnp_csn <= 16; pnp_csn++) {
        uint32_t vendor_id;
        uint16_t io_base;
        
        /* Wake card with CSN */
        outp(0x279, 0x03);
        outp(0xA79, pnp_csn);
        
        /* Read vendor ID */
        outp(0x279, 0x09);  /* Select vendor ID registers */
        vendor_id = inp(0x203);
        vendor_id |= inp(0x203) << 8;
        vendor_id |= inp(0x203) << 16;
        vendor_id |= inp(0x203) << 24;
        
        /* Check for 3Com vendor */
        if ((vendor_id & 0xFFFFFF) == 0x506D50) {  /* TCM (3Com backwards) */
            /* Read I/O base configuration */
            outp(0x279, 0x60);  /* I/O base high */
            io_base = inp(0x203) << 8;
            outp(0x279, 0x61);  /* I/O base low */
            io_base |= inp(0x203);
            
            if (io_base != 0 && io_base != 0xFFFF) {
                /* Verify it's a supported NIC */
                nic_type_t nic_type = check_3com_signature(io_base);
                if (nic_type != NIC_TYPE_NONE) {
                    g_detected_nics[g_nic_count + found].type = nic_type;
                    g_detected_nics[g_nic_count + found].io_base = io_base;
                    g_detected_nics[g_nic_count + found].pnp_csn = pnp_csn;
                    found++;
                    
                    LOG_INFO("PnP: Found NIC at CSN %d, I/O 0x%03X", 
                            pnp_csn, io_base);
                }
            }
        }
    }
    
    /* Return to wait for key state */
    outp(0x279, 0x02);
    
    return found;
}

/**
 * @brief Get NIC capabilities and configuration
 * @param nic Pointer to NIC info structure
 */
static void get_nic_capabilities(nic_info_t* nic) {
    uint16_t io_base = nic->io_base;
    uint16_t config;
    
    /* Read MAC address from EEPROM */
    for (int i = 0; i < 3; i++) {
        uint16_t word = read_eeprom(io_base, i);
        nic->mac_addr[i*2] = word & 0xFF;
        nic->mac_addr[i*2 + 1] = word >> 8;
    }
    
    /* Read configuration */
    config = read_eeprom(io_base, 0x08);
    
    /* Determine IRQ from configuration */
    nic->irq = (config >> 12) & 0x0F;
    if (nic->irq == 0) nic->irq = 3;  /* Default */
    
    /* Set capabilities based on NIC type */
    switch (nic->type) {
    case NIC_TYPE_3C509B:
        nic->capabilities = NIC_CAP_10MBPS | NIC_CAP_PIO;
        nic->max_packet_size = 1514;
        strncpy(nic->name, "3C509B", sizeof(nic->name));
        break;
        
    case NIC_TYPE_3C515:
        nic->capabilities = NIC_CAP_100MBPS | NIC_CAP_BUSMASTER | 
                          NIC_CAP_DMA | NIC_CAP_RING_BUFFER;
        nic->max_packet_size = 1514;
        strncpy(nic->name, "3C515-TX", sizeof(nic->name));
        
        /* Check for bus master support */
        if (check_busmaster_capable()) {
            nic->capabilities |= NIC_CAP_DMA_VERIFIED;
            LOG_INFO("3C515: Bus mastering supported");
        } else {
            LOG_WARNING("3C515: Bus mastering not available, using PIO");
        }
        break;
        
    default:
        nic->capabilities = NIC_CAP_10MBPS | NIC_CAP_PIO;
        nic->max_packet_size = 1514;
        strncpy(nic->name, "Unknown 3Com", sizeof(nic->name));
        break;
    }
    
    LOG_INFO("%s: MAC %02X:%02X:%02X:%02X:%02X:%02X, IRQ %d", 
            nic->name,
            nic->mac_addr[0], nic->mac_addr[1], nic->mac_addr[2],
            nic->mac_addr[3], nic->mac_addr[4], nic->mac_addr[5],
            nic->irq);
}

/**
 * @brief Check if system supports bus mastering
 * @return 1 if bus mastering capable, 0 otherwise
 */
int check_busmaster_capable(void) {
    /* Simple check - would need chipset detection for real implementation */
    /* For now, assume 386+ systems might support it */
    extern cpu_info_t g_cpu_info;
    
    if (g_cpu_info.cpu_type >= CPU_TYPE_80386) {
        /* Could do chipset detection here */
        return 1;  /* Optimistic */
    }
    
    return 0;
}

/**
 * @brief Main NIC detection function
 * @return SUCCESS or error code
 *
 * Called once during initialization to detect all NICs.
 * Results are used for SMC patching.
 */
int nic_detect_init(void) {
    int pnp_found;
    int isa_found;
    
    LOG_INFO("Starting NIC detection...");
    
    /* Clear detection results */
    memset(g_detected_nics, 0, sizeof(g_detected_nics));
    g_nic_count = 0;
    
    /* Try PnP detection first */
    pnp_found = detect_pnp_nics();
    g_nic_count += pnp_found;
    
    /* Then scan ISA ports */
    if (g_nic_count < MAX_NICS) {
        isa_found = scan_isa_ports();
        g_nic_count += isa_found;
    }
    
    /* Check if we found any NICs */
    if (g_nic_count == 0) {
        LOG_ERROR("No supported NICs found");
        #ifdef PRODUCTION
        printf("Error: No 3Com NICs detected\n");
        #endif
        return ERROR_NO_NIC_FOUND;
    }
    
    /* Get capabilities for each NIC */
    for (int i = 0; i < g_nic_count; i++) {
        get_nic_capabilities(&g_detected_nics[i]);
    }
    
    /* Display summary */
    #ifdef PRODUCTION
    printf("Found %d NIC%s:\n", g_nic_count, g_nic_count > 1 ? "s" : "");
    for (int i = 0; i < g_nic_count; i++) {
        printf("  %s at I/O 0x%03X, IRQ %d\n",
               g_detected_nics[i].name,
               g_detected_nics[i].io_base,
               g_detected_nics[i].irq);
    }
    #endif
    
    LOG_INFO("NIC detection complete: %d NICs found", g_nic_count);
    
    return SUCCESS;
}

/**
 * @brief Get detected NIC information
 * @param index NIC index (0-based)
 * @return Pointer to NIC info or NULL
 */
const nic_info_t* nic_get_info(int index) {
    if (index >= 0 && index < g_nic_count) {
        return &g_detected_nics[index];
    }
    return NULL;
}

/**
 * @brief Get number of detected NICs
 * @return Number of NICs found
 */
int nic_get_count(void) {
    return g_nic_count;
}

/**
 * @brief Configure NIC for operation
 * @param nic Pointer to NIC info
 * @return SUCCESS or error code
 *
 * Performs basic NIC configuration before TSR installation.
 */
int nic_configure(nic_info_t* nic) {
    uint16_t io_base = nic->io_base;
    
    LOG_INFO("Configuring %s at 0x%03X", nic->name, io_base);
    
    /* Reset NIC */
    outpw(io_base + 0x0E, 0x0000);  /* Global reset */
    
    /* Wait for reset complete */
    for (int i = 0; i < 1000; i++) {
        if (inpw(io_base + 0x0E) & 0x1000) {
            break;
        }
    }
    
    /* Enable NIC */
    outpw(io_base + 0x0E, 0x0001);  /* Enable */
    
    /* Configure IRQ */
    outpw(io_base + 0x0E, 0x0800);  /* Window 0 */
    outpw(io_base + 0x08, nic->irq << 12);  /* Set IRQ */
    
    /* Enable interrupts */
    outpw(io_base + 0x0E, 0x7800);  /* Window 7 */
    outpw(io_base + 0x0E, 0x00FF);  /* Enable all interrupts */
    
    return SUCCESS;
}

/* Restore default code segment */
#pragma code_seg()