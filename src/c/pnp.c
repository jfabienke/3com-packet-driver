/**
 * @file pnp.c
 * @brief Tiny PnP manager for 3Com NICs
 *
 * Implements NIC detection and resource assignment for 3C515-TX and 3C509B NICs
 * using ISAPnP. Supports multi-NIC scenarios (ARS 3.6), manual configuration fallback
 * (ARS 2.4), and targets a small footprint (<6 KB TSR, ARS 4.1).
 */

#include "../include/hardware.h"  // NIC types and structures  
#include "../include/logging.h"   // For logging
#include "../include/3c509b.h"    // 3C509B definitions
#include "../include/3c515.h"     // 3C515-TX definitions
#include "../include/nic_init.h"  // NIC detection structures
#include "../include/common.h"    // Common definitions
#include <dos.h>                  // DOS I/O functions
#include <string.h>               // For memset

// --- ISA PnP Constants ---
#define ISAPNP_ADDRESS       0x279
#define ISAPNP_WRITE_DATA    0xA79
#define ISAPNP_READ_PORT     0x203
#define ISAPNP_LFSR_SEED     0x6A

// ISA PnP Registers
#define ISAPNP_SET_READ_DATA_PORT   0x00
#define ISAPNP_SERIAL_ISOLATION     0x01
#define ISAPNP_CONFIG_CONTROL       0x02
#define ISAPNP_WAKE                 0x03
#define ISAPNP_RESOURCEDATA         0x04
#define ISAPNP_STATUS               0x05
#define ISAPNP_CARDSELECT           0x06
#define ISAPNP_LOGICALDEVICE        0x07

// Configuration control values
#define ISAPNP_CONFIG_WAIT_FOR_KEY  0x02
#define ISAPNP_CONFIG_RESET_CSN     0x04
#define ISAPNP_CONFIG_RESET         0x01

// Resource configuration
#define ISAPNP_ACTIVATE             0x30
#define ISAPNP_IOBASE(n)           (0x60 + (n)*2)
#define ISAPNP_IRQNO(n)            (0x70 + (n)*2)

// 3Com Vendor ID
#define PNP_VENDOR_3COM             0x10B7

// ISA PnP Key sequence (32 bytes)
static const uint8_t isapnp_key[32] = {
    0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE,
    0xDF, 0x6F, 0x37, 0x1B, 0x0D, 0x86, 0xC3, 0x61,
    0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45, 0xA2, 0xD1,
    0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x39
};

// ISA PnP Identifier structure
struct isapnp_identifier {
    uint8_t vendor_id[2];      // Vendor ID
    uint8_t product_id[2];     // Product ID  
    uint8_t serial_number[4];  // Serial number
    uint8_t checksum;          // Checksum
};

// --- Resource Pools ---
uint16_t io_pool[] = {0x300, 0x320, 0x340, 0x360, 0x380, 0x3A0, 0x3C0, 0x3E0};
uint8_t  irq_pool[] = {5, 7, 9, 10, 11, 12};
int io_pool_idx = 0;
int irq_pool_idx = 0;

// --- Global Data ---
nic_info_t nic_infos[MAX_NICS]; // Array to store detected NICs
int nic_count = 0;              // Number of detected NICs

// --- Helper Functions ---

/**
 * @brief Send ISAPnP key sequence to enter Wait for Key state
 */
static void isapnp_send_key(void) {
    for (int i = 0; i < 32; i++) {
        outb(ISAPNP_ADDRESS, isapnp_key[i]);
    }
    
    /* Small delay after key sequence */
    delay_microseconds(100);
}

/**
 * @brief Write to ISA PnP address register
 */
static void isapnp_write_addr(uint8_t addr) {
    outb(ISAPNP_ADDRESS, addr);
    delay_microseconds(1);
}

/**
 * @brief Write to ISA PnP data register  
 */
static void isapnp_write_data(uint8_t data) {
    outb(ISAPNP_WRITE_DATA, data);
    delay_microseconds(1);
}

/**
 * @brief Read from ISA PnP data port
 */
static uint8_t isapnp_read_data(void) {
    delay_microseconds(1);
    return inb(ISAPNP_READ_PORT);
}

/**
 * @brief Initialize ISA PnP subsystem
 */
static int isapnp_init(void) {
    /* Set read data port */
    isapnp_write_addr(ISAPNP_SET_READ_DATA_PORT);
    isapnp_write_data((ISAPNP_READ_PORT >> 2));
    
    /* Send key sequence to enter configuration mode */
    isapnp_send_key();
    
    /* Reset all cards */
    isapnp_write_addr(ISAPNP_CONFIG_CONTROL);
    isapnp_write_data(ISAPNP_CONFIG_RESET);
    delay_milliseconds(10);
    
    /* Enter Wait for Key state */
    isapnp_write_addr(ISAPNP_CONFIG_CONTROL);
    isapnp_write_data(ISAPNP_CONFIG_WAIT_FOR_KEY);
    delay_milliseconds(2);
    
    return 0;
}

/**
 * @brief Exit ISA PnP configuration mode
 */
static void isapnp_exit(void) {
    /* Reset configuration control */
    isapnp_write_addr(ISAPNP_CONFIG_CONTROL);
    isapnp_write_data(ISAPNP_CONFIG_WAIT_FOR_KEY);
}

/**
 * @brief Perform serial isolation and assign Card Select Number (CSN)
 * @return 0 on success, -1 if no more cards
 */
static int isapnp_isolate(uint8_t *csn) {
    uint8_t lfsr = ISAPNP_LFSR_SEED;
    uint8_t bit, seen_55aa = 0;
    int i;
    
    /* Start serial isolation */
    isapnp_write_addr(ISAPNP_SERIAL_ISOLATION);
    delay_microseconds(100);
    
    /* Check for isolation pattern 0x55AA */
    for (i = 0; i < 64; i++) {
        /* Read bit pair */
        uint8_t data1 = isapnp_read_data();
        uint8_t data2 = isapnp_read_data();
        
        /* Compare with expected pattern */
        bit = ((lfsr & 1) == 1) ? 0x55 : 0xAA;
        
        if (data1 == 0x55 && data2 == bit) {
            seen_55aa = 1;
        } else if (data1 != 0x55 || data2 != 0xAA) {
            break; /* No more cards */
        }
        
        /* Advance LFSR */
        if (lfsr & 1) {
            lfsr = (lfsr >> 1) ^ 0x8C;
        } else {
            lfsr >>= 1;
        }
    }
    
    if (!seen_55aa) {
        return -1; /* No card found */
    }
    
    /* Assign Card Select Number */
    isapnp_write_addr(ISAPNP_CARDSELECT);
    isapnp_write_data(*csn);
    delay_microseconds(100);
    
    return 0;
}

/**
 * @brief Read serial identifier for a given CSN
 * @return 0 on success, -1 on failure
 */
static int isapnp_read_serial_id(uint8_t csn, struct isapnp_identifier *id) {
    if (!id) return -1;
    
    /* Wake up card with CSN */
    isapnp_write_addr(ISAPNP_WAKE);
    isapnp_write_data(csn);
    delay_microseconds(100);
    
    /* Read 9-byte serial identifier from resource data */
    isapnp_write_addr(ISAPNP_RESOURCEDATA);
    
    /* Read vendor ID (2 bytes) */
    id->vendor_id[0] = isapnp_read_data();
    id->vendor_id[1] = isapnp_read_data();
    
    /* Read product ID (2 bytes) */
    id->product_id[0] = isapnp_read_data();
    id->product_id[1] = isapnp_read_data();
    
    /* Read serial number (4 bytes) */
    for (int i = 0; i < 4; i++) {
        id->serial_number[i] = isapnp_read_data();
    }
    
    /* Read checksum */
    id->checksum = isapnp_read_data();
    
    return 0;
}

/**
 * @brief Assign I/O base and IRQ to a NIC
 * @return 0 on success, -1 on failure
 */
static int isapnp_assign_resources(uint8_t csn, uint16_t io_base, uint8_t irq) {
    /* Wake up card with CSN */
    isapnp_write_addr(ISAPNP_WAKE);
    isapnp_write_data(csn);
    delay_microseconds(100);
    
    /* Select logical device 0 */
    isapnp_write_addr(ISAPNP_LOGICALDEVICE);
    isapnp_write_data(0);
    
    /* Set I/O base address (16-bit) */
    isapnp_write_addr(ISAPNP_IOBASE(0));
    isapnp_write_data((io_base >> 8) & 0xFF);
    isapnp_write_addr(ISAPNP_IOBASE(0) + 1);
    isapnp_write_data(io_base & 0xFF);
    
    /* Set IRQ number */
    isapnp_write_addr(ISAPNP_IRQNO(0));
    isapnp_write_data(irq);
    isapnp_write_addr(ISAPNP_IRQNO(0) + 1);
    isapnp_write_data(0x02); /* IRQ type: high true, edge sensitive */
    
    /* Activate the logical device */
    isapnp_write_addr(ISAPNP_ACTIVATE);
    isapnp_write_data(1);
    delay_microseconds(100);
    
    return 0;
}

/**
 * @brief Add delay functions for PnP operations
 */
static void delay_microseconds(uint32_t us) {
    /* Simple busy-wait delay for DOS */
    volatile uint32_t i;
    for (i = 0; i < us * 10; i++) {
        /* Empty loop */
    }
}

static void delay_milliseconds(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        delay_microseconds(1000);
    }
}

/**
 * @brief Detect and configure 3Com NICs via ISA PnP
 * @param info_list Array to store detection results
 * @param max_nics Maximum number of NICs to detect
 * @return Number of NICs detected, or negative on error
 */
int pnp_detect_nics(nic_detect_info_t *info_list, int max_nics) {
    if (!info_list || max_nics <= 0) {
        return -1;
    }
    
    int detected_count = 0;
    uint8_t csn = 1;
    
    LOG_INFO("Starting ISA PnP detection for 3Com NICs");
    
    /* Initialize PnP subsystem */
    if (isapnp_init() != 0) {
        LOG_ERROR("Failed to initialize ISA PnP subsystem");
        return -1;
    }
    
    /* Detect NICs using serial isolation */
    while (detected_count < max_nics && csn < 32) {
        /* Try to isolate next card */
        if (isapnp_isolate(&csn) != 0) {
            break; /* No more cards */
        }
        
        /* Read serial identifier */
        struct isapnp_identifier id;
        if (isapnp_read_serial_id(csn, &id) != 0) {
            csn++;
            continue;
        }
        
        /* Check vendor ID (3Com = 0x10B7) */
        uint16_t vendor_id = (id.vendor_id[0] | (id.vendor_id[1] << 8));
        if (vendor_id != PNP_VENDOR_3COM) {
            LOG_DEBUG("Skipping non-3Com device (vendor ID: 0x%X)", vendor_id);
            csn++;
            continue;
        }
        
        /* Get product ID */
        uint16_t product_id = (id.product_id[0] | (id.product_id[1] << 8));
        
        /* Determine NIC type */
        nic_type_t nic_type = NIC_TYPE_UNKNOWN;
        if ((product_id & _3C509B_PRODUCT_ID_MASK) == _3C509B_PRODUCT_ID_509B) {
            nic_type = NIC_TYPE_3C509B;
        } else if ((product_id & _3C515_TX_PRODUCT_ID_MASK) == _3C515_TX_PRODUCT_ID) {
            nic_type = NIC_TYPE_3C515_TX;
        } else {
            LOG_DEBUG("Unknown 3Com product ID: 0x%X", product_id);
            csn++;
            continue;
        }
        
        /* Assign resources from pools */
        if (io_pool_idx >= (int)(sizeof(io_pool)/sizeof(io_pool[0])) ||
            irq_pool_idx >= (int)(sizeof(irq_pool)/sizeof(irq_pool[0]))) {
            LOG_WARNING("Resource pool exhausted");
            break;
        }
        
        uint16_t io_base = io_pool[io_pool_idx++];
        uint8_t irq = irq_pool[irq_pool_idx++];
        
        /* Configure card resources */
        if (isapnp_assign_resources(csn, io_base, irq) != 0) {
            LOG_WARNING("Failed to assign resources to CSN %d", csn);
            /* Reset pool indices for retry */
            io_pool_idx--;
            irq_pool_idx--;
            csn++;
            continue;
        }
        
        /* Fill in detection info */
        nic_detect_info_t *info = &info_list[detected_count];
        memset(info, 0, sizeof(nic_detect_info_t));
        
        info->type = nic_type;
        info->vendor_id = vendor_id;
        info->device_id = product_id;
        info->io_base = io_base;
        info->irq = irq;
        info->pnp_capable = true;
        info->detected = true;
        
        /* Set capabilities based on NIC type */
        if (nic_type == NIC_TYPE_3C509B) {
            info->capabilities = HW_CAP_MULTICAST | HW_CAP_PROMISCUOUS;
        } else if (nic_type == NIC_TYPE_3C515_TX) {
            info->capabilities = HW_CAP_DMA | HW_CAP_BUS_MASTER | 
                                HW_CAP_MULTICAST | HW_CAP_PROMISCUOUS |
                                HW_CAP_FULL_DUPLEX | HW_CAP_AUTO_SPEED;
        }
        
        LOG_INFO("Detected %s NIC via PnP at I/O 0x%X, IRQ %d (CSN %d)",
                (nic_type == NIC_TYPE_3C509B) ? "3C509B" : "3C515-TX",
                io_base, irq, csn);
        
        detected_count++;
        csn++;
    }
    
    /* Exit PnP configuration mode */
    isapnp_exit();
    
    if (detected_count == 0) {
        LOG_INFO("No 3Com NICs detected via ISA PnP");
    } else {
        LOG_INFO("ISA PnP detection completed: %d 3Com NICs found", detected_count);
    }
    
    return detected_count;
}
