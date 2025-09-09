/**
 * @file 3c900tpo.c
 * @brief 3C900-TPO PCI Driver Implementation
 * 
 * BOOMTEX.MOD - 3C900-TPO PCI Driver with Auto-Negotiation
 * Team C (Agents 09-10) - Week 1 Critical Deliverable
 * 
 * Implements 3C900-TPO 10Mbps Ethernet PCI support with Boomerang architecture,
 * IEEE 802.3u auto-negotiation, and full-duplex operation.
 */

#include "boomtex_internal.h"
#include "../../include/pci.h"

/* 3C900-TPO PCI Configuration */
#define C3C900_VENDOR_ID        0x10B7      /* 3Com PCI vendor ID */
#define C3C900_DEVICE_ID        0x9000      /* 3C900-TPO device ID */
#define C3C900_CLASS_CODE       0x020000    /* Network controller class */

/* 3C900 Memory-Mapped Register Offsets */
#define C3C900_COMMAND          0x00        /* Command register */
#define C3C900_STATUS           0x02        /* Status register */
#define C3C900_INT_STATUS       0x04        /* Interrupt status */
#define C3C900_INT_ENABLE       0x06        /* Interrupt enable */
#define C3C900_FIFO_DIAG        0x08        /* FIFO diagnostics */
#define C3C900_TIMER            0x0A        /* General purpose timer */
#define C3C900_TX_STATUS        0x0C        /* TX status */
#define C3C900_DMA_CTRL         0x20        /* DMA control */
#define C3C900_DN_LIST_PTR      0x24        /* Downlist pointer (TX) */
#define C3C900_UP_LIST_PTR      0x38        /* Uplist pointer (RX) */

/* Window Register Access (I/O space) */
#define C3C900_WINDOW_CMD       0x0E        /* Window select command */

/* Window 0: Configuration */
#define C3C900_W0_CONFIG_CTRL   0x04        /* Configuration control */
#define C3C900_W0_RESOURCE_CFG  0x06        /* Resource configuration */

/* Window 2: Station Address */
#define C3C900_W2_ADDR_LO       0x00        /* Station address 0-1 */
#define C3C900_W2_ADDR_MID      0x02        /* Station address 2-3 */
#define C3C900_W2_ADDR_HI       0x04        /* Station address 4-5 */

/* Window 3: FIFO Management */
#define C3C900_W3_TX_FREE       0x0C        /* TX free bytes */

/* Window 4: Diagnostics */
#define C3C900_W4_MEDIA_STATUS  0x08        /* Media status */
#define C3C900_W4_NET_DIAG      0x06        /* Network diagnostics */
#define C3C900_W4_PHY_MGMT      0x08        /* PHY management */

/* Command Values */
#define C3C900_CMD_GLOBAL_RESET 0x0000      /* Global reset */
#define C3C900_CMD_TX_ENABLE    0x4800      /* Enable transmitter */
#define C3C900_CMD_RX_ENABLE    0x2000      /* Enable receiver */
#define C3C900_CMD_TX_RESET     0x5800      /* Reset transmitter */
#define C3C900_CMD_RX_RESET     0x2800      /* Reset receiver */
#define C3C900_CMD_INT_ACK      0x6800      /* Acknowledge interrupt */
#define C3C900_CMD_SET_WIN      0x0800      /* Select window (+ window #) */

/* Status Bits */
#define C3C900_STAT_INT_LATCH   0x0001      /* Interrupt latch */
#define C3C900_STAT_HOST_ERROR  0x0002      /* Host error */
#define C3C900_STAT_TX_COMPLETE 0x0004      /* TX complete */
#define C3C900_STAT_RX_COMPLETE 0x0010      /* RX complete */
#define C3C900_STAT_RX_EARLY    0x0020      /* RX early */
#define C3C900_STAT_INT_REQ     0x0040      /* Interrupt requested */
#define C3C900_STAT_UPDATE_STAT 0x0080      /* Update statistics */
#define C3C900_STAT_LINK_EVENT  0x0100      /* Link event */
#define C3C900_STAT_CMD_IN_PROG 0x1000      /* Command in progress */

/* DMA Control Bits */
#define C3C900_DMA_DN_COMPLETE  0x00010000  /* Download complete */
#define C3C900_DMA_UP_COMPLETE  0x00020000  /* Upload complete */
#define C3C900_DMA_DN_STALLED   0x00040000  /* Download stalled */
#define C3C900_DMA_UP_STALLED   0x00080000  /* Upload stalled */

/* Media Status Bits (Window 4) */
#define C3C900_MEDIA_SQE_ENABLE     0x0008  /* SQE enable */
#define C3C900_MEDIA_COLLISION_DET  0x0010  /* Collision detect */
#define C3C900_MEDIA_CARRIER_SENSE  0x0020  /* Carrier sense */
#define C3C900_MEDIA_JABBER_GUARD   0x0040  /* Jabber guard enable */
#define C3C900_MEDIA_LINK_BEAT      0x0080  /* Link beat enable */
#define C3C900_MEDIA_JABBER_DET     0x0200  /* Jabber detect */
#define C3C900_MEDIA_POLARITY_OK    0x1000  /* Polarity OK */
#define C3C900_MEDIA_LINK_DET       0x0800  /* Link detect */
#define C3C900_MEDIA_DC_CONV        0x4000  /* DC converter enabled */
#define C3C900_MEDIA_AUI_DIS        0x8000  /* AUI disable */

/* MII Management Interface */
#define C3C900_MII_DATA         0x08        /* MII data register (window 4) */
#define C3C900_MII_CMD          0x0A        /* MII command register */

/* MII Command Bits */
#define C3C900_MII_DIR          0x0001      /* Direction (1=write, 0=read) */
#define C3C900_MII_READ         0x0002      /* Start read operation */
#define C3C900_MII_WRITE        0x0004      /* Start write operation */

/* Standard MII Registers */
#define MII_BMCR                0x00        /* Basic Mode Control Register */
#define MII_BMSR                0x01        /* Basic Mode Status Register */
#define MII_ANAR                0x04        /* Auto-Negotiation Advertisement */
#define MII_ANLPAR              0x05        /* Auto-Negotiation Link Partner Ability */

/* Basic Mode Control Register */
#define BMCR_RESET              0x8000      /* PHY reset */
#define BMCR_LOOPBACK           0x4000      /* Loopback enable */
#define BMCR_SPEED_SELECT       0x2000      /* Speed select (1=100Mbps, 0=10Mbps) */
#define BMCR_ANENABLE           0x1000      /* Auto-negotiation enable */
#define BMCR_POWERDOWN          0x0800      /* Power down */
#define BMCR_ISOLATE            0x0400      /* Isolate */
#define BMCR_ANRESTART          0x0200      /* Restart auto-negotiation */
#define BMCR_DUPLEX             0x0100      /* Duplex mode (1=full, 0=half) */
#define BMCR_COLLISION_TEST     0x0080      /* Collision test */

/* Basic Mode Status Register */
#define BMSR_100BT4             0x8000      /* 100BASE-T4 capable */
#define BMSR_100BTXFULL         0x4000      /* 100BASE-TX full duplex capable */
#define BMSR_100BTXHALF         0x2000      /* 100BASE-TX half duplex capable */
#define BMSR_10BTFULL           0x1000      /* 10BASE-T full duplex capable */
#define BMSR_10BTHALF           0x0800      /* 10BASE-T half duplex capable */
#define BMSR_ANEGCAPABLE        0x0008      /* Auto-negotiation capable */
#define BMSR_ANEGCOMPLETE       0x0020      /* Auto-negotiation complete */
#define BMSR_LSTATUS            0x0004      /* Link status */

/* Auto-Negotiation Advertisement Register */
#define ANAR_NP                 0x8000      /* Next page bit */
#define ANAR_ACK                0x4000      /* Acknowledge bit */
#define ANAR_RF                 0x2000      /* Remote fault */
#define ANAR_PAUSE              0x0400      /* Pause operation */
#define ANAR_100BT4             0x0200      /* 100BASE-T4 */
#define ANAR_100BTXFULL         0x0100      /* 100BASE-TX full duplex */
#define ANAR_100BTXHALF         0x0080      /* 100BASE-TX half duplex */
#define ANAR_10BTFULL           0x0040      /* 10BASE-T full duplex */
#define ANAR_10BTHALF           0x0020      /* 10BASE-T half duplex */
#define ANAR_CSMA               0x0001      /* CSMA */

/* Forward declarations */
static int c3c900_pci_scan(boomtex_nic_context_t *nic);
static int c3c900_read_mac_address(boomtex_nic_context_t *nic);
static int c3c900_setup_media(boomtex_nic_context_t *nic);
static uint16_t c3c900_mii_read(boomtex_nic_context_t *nic, uint8_t phy_addr, uint8_t reg_addr);
static void c3c900_mii_write(boomtex_nic_context_t *nic, uint8_t phy_addr, uint8_t reg_addr, uint16_t data);
static int c3c900_autonegotiate(boomtex_nic_context_t *nic);

/**
 * @brief Detect 3C900-TPO PCI NICs
 * 
 * Scans PCI bus for 3C900-TPO network controllers.
 * 
 * @return Positive hardware type on success, negative error code on failure
 */
int boomtex_detect_3c900tpo(void) {
    pci_device_t pci_dev;
    int cards_found = 0;
    
    LOG_DEBUG("BOOMTEX: Scanning PCI bus for 3C900-TPO cards");
    
    /* Scan all PCI buses */
    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                /* Read PCI configuration header */
                if (pci_read_config_header(bus, device, function, &pci_dev) != SUCCESS) {
                    continue;
                }
                
                /* Check for 3Com vendor ID */
                if (pci_dev.vendor_id != C3C900_VENDOR_ID) {
                    continue;
                }
                
                /* Check for 3C900-TPO device ID */
                if (pci_dev.device_id == C3C900_DEVICE_ID) {
                    LOG_INFO("BOOMTEX: Found 3C900-TPO at PCI %02X:%02X.%d", 
                             bus, device, function);
                    cards_found++;
                }
                
                /* Break if we found maximum cards */
                if (cards_found >= BOOMTEX_MAX_NICS) {
                    goto scan_done;
                }
            }
        }
    }
    
scan_done:
    if (cards_found > 0) {
        LOG_INFO("BOOMTEX: Detected %d 3C900-TPO PCI cards", cards_found);
        return BOOMTEX_HARDWARE_3C900TPO;
    }
    
    LOG_DEBUG("BOOMTEX: No 3C900-TPO cards found");
    return ERROR_HARDWARE_NOT_FOUND;
}

/**
 * @brief Initialize 3C900-TPO NIC
 * 
 * Configures the 3C900-TPO for operation with auto-negotiation.
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_init_3c900tpo(boomtex_nic_context_t *nic) {
    int result;
    uint16_t command;
    
    LOG_DEBUG("BOOMTEX: Initializing 3C900-TPO NIC");
    
    /* Verify we have a 3C900-TPO */
    if (nic->hardware_type != BOOMTEX_HARDWARE_3C900TPO) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Scan and configure PCI device */
    result = c3c900_pci_scan(nic);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: 3C900-TPO PCI scan failed: %d", result);
        return result;
    }
    
    /* Global reset */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_GLOBAL_RESET);
    delay_ms(10);   /* Wait for reset to complete */
    
    /* Wait for command to complete */
    for (int timeout = 1000; timeout > 0; timeout--) {
        uint16_t status = inw(nic->io_base + C3C900_STATUS);
        if (!(status & C3C900_STAT_CMD_IN_PROG)) {
            break;
        }
        delay_us(10);
    }
    
    /* Read MAC address */
    result = c3c900_read_mac_address(nic);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: Failed to read 3C900-TPO MAC address: %d", result);
        return result;
    }
    
    /* Setup media and auto-negotiation */
    result = c3c900_setup_media(nic);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: Failed to setup 3C900-TPO media: %d", result);
        return result;
    }
    
    /* Enable bus mastering in PCI configuration */
    pci_read_config_word(nic->pci_bus, nic->pci_device, nic->pci_function, 0x04, &command);
    command |= 0x0004;  /* Bus master enable */
    pci_write_config_word(nic->pci_bus, nic->pci_device, nic->pci_function, 0x04, command);
    
    /* Enable transmit and receive */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_TX_ENABLE);
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_RX_ENABLE);
    
    LOG_INFO("BOOMTEX: 3C900-TPO initialized at I/O 0x%X, IRQ %d, MAC %02X:%02X:%02X:%02X:%02X:%02X",
             nic->io_base, nic->irq,
             nic->mac_address[0], nic->mac_address[1], nic->mac_address[2],
             nic->mac_address[3], nic->mac_address[4], nic->mac_address[5]);
    
    return SUCCESS;
}

/**
 * @brief Scan PCI bus and configure 3C900-TPO device
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
static int c3c900_pci_scan(boomtex_nic_context_t *nic) {
    pci_device_t pci_dev;
    uint32_t bar0, bar1;
    
    /* Find the first 3C900-TPO device */
    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                if (pci_read_config_header(bus, device, function, &pci_dev) != SUCCESS) {
                    continue;
                }
                
                if (pci_dev.vendor_id == C3C900_VENDOR_ID && 
                    pci_dev.device_id == C3C900_DEVICE_ID) {
                    
                    /* Store PCI location */
                    nic->pci_bus = bus;
                    nic->pci_device = device;
                    nic->pci_function = function;
                    nic->vendor_id = pci_dev.vendor_id;
                    nic->device_id = pci_dev.device_id;
                    nic->revision = pci_dev.revision;
                    
                    /* Read Base Address Registers */
                    pci_read_config_dword(bus, device, function, 0x10, &bar0);
                    pci_read_config_dword(bus, device, function, 0x14, &bar1);
                    
                    /* Configure I/O and memory bases */
                    if (bar0 & 0x01) {
                        /* I/O space */
                        nic->io_base = bar0 & ~0x03;
                    } else {
                        /* Memory space */
                        nic->mem_base = bar0 & ~0x0F;
                        nic->io_base = bar1 & ~0x03;  /* Secondary I/O */
                    }
                    
                    /* Read IRQ line */
                    uint8_t irq_line;
                    pci_read_config_byte(bus, device, function, 0x3C, &irq_line);
                    nic->irq = irq_line;
                    
                    LOG_DEBUG("BOOMTEX: 3C900-TPO configured - PCI %02X:%02X.%d, I/O 0x%X, IRQ %d",
                              bus, device, function, nic->io_base, nic->irq);
                    
                    return SUCCESS;
                }
            }
        }
    }
    
    return ERROR_HARDWARE_NOT_FOUND;
}

/**
 * @brief Read MAC address from 3C900-TPO
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
static int c3c900_read_mac_address(boomtex_nic_context_t *nic) {
    uint16_t mac_words[3];
    
    /* Select window 2 (Station Address) */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_SET_WIN | 2);
    
    /* Read MAC address from station address registers */
    mac_words[0] = inw(nic->io_base + C3C900_W2_ADDR_LO);
    mac_words[1] = inw(nic->io_base + C3C900_W2_ADDR_MID);
    mac_words[2] = inw(nic->io_base + C3C900_W2_ADDR_HI);
    
    /* Convert to byte array */
    nic->mac_address[0] = mac_words[0] & 0xFF;
    nic->mac_address[1] = (mac_words[0] >> 8) & 0xFF;
    nic->mac_address[2] = mac_words[1] & 0xFF;
    nic->mac_address[3] = (mac_words[1] >> 8) & 0xFF;
    nic->mac_address[4] = mac_words[2] & 0xFF;
    nic->mac_address[5] = (mac_words[2] >> 8) & 0xFF;
    
    /* Verify MAC address is valid */
    uint8_t zero_count = 0, ff_count = 0;
    for (int i = 0; i < 6; i++) {
        if (nic->mac_address[i] == 0x00) zero_count++;
        if (nic->mac_address[i] == 0xFF) ff_count++;
    }
    
    if (zero_count == 6 || ff_count == 6) {
        LOG_ERROR("BOOMTEX: Invalid 3C900-TPO MAC address");
        return ERROR_HARDWARE_EEPROM;
    }
    
    LOG_DEBUG("BOOMTEX: 3C900-TPO MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
              nic->mac_address[0], nic->mac_address[1], nic->mac_address[2],
              nic->mac_address[3], nic->mac_address[4], nic->mac_address[5]);
    
    return SUCCESS;
}

/**
 * @brief Setup media and perform auto-negotiation for 3C900-TPO
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
static int c3c900_setup_media(boomtex_nic_context_t *nic) {
    int result;
    
    /* Set default media configuration */
    nic->media_type = BOOMTEX_MEDIA_AUTO;
    nic->duplex_mode = BOOMTEX_DUPLEX_AUTO;
    nic->link_speed = 10;  /* 3C900-TPO is 10Mbps */
    
    /* Perform auto-negotiation */
    result = c3c900_autonegotiate(nic);
    if (result < 0) {
        LOG_WARNING("BOOMTEX: 3C900-TPO auto-negotiation failed, using 10BT half-duplex");
        nic->media_type = BOOMTEX_MEDIA_10BT;
        nic->duplex_mode = BOOMTEX_DUPLEX_HALF;
    }
    
    /* Check final link status */
    result = boomtex_get_link_status(nic);
    nic->link_status = (result > 0) ? 1 : 0;
    
    LOG_INFO("BOOMTEX: 3C900-TPO media configured - %dMbps %s-duplex, Link %s",
             nic->link_speed,
             (nic->duplex_mode == BOOMTEX_DUPLEX_FULL) ? "full" : "half",
             nic->link_status ? "UP" : "DOWN");
    
    return SUCCESS;
}

/**
 * @brief Perform IEEE 802.3u auto-negotiation on 3C900-TPO
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
static int c3c900_autonegotiation(boomtex_nic_context_t *nic) {
    uint16_t bmcr, bmsr, anar, anlpar;
    uint8_t phy_addr = 0;  /* Internal PHY address */
    int timeout;
    
    /* Select window 4 for MII access */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_SET_WIN | 4);
    
    /* Read PHY capabilities */
    bmsr = c3c900_mii_read(nic, phy_addr, MII_BMSR);
    if (!(bmsr & BMSR_ANEGCAPABLE)) {
        LOG_INFO("BOOMTEX: 3C900-TPO PHY does not support auto-negotiation");
        return ERROR_NOT_IMPLEMENTED;
    }
    
    /* Configure advertisement register */
    anar = ANAR_CSMA;  /* CSMA/CD capability */
    if (bmsr & BMSR_10BTHALF) anar |= ANAR_10BTHALF;
    if (bmsr & BMSR_10BTFULL) anar |= ANAR_10BTFULL;
    
    c3c900_mii_write(nic, phy_addr, MII_ANAR, anar);
    
    /* Enable and restart auto-negotiation */
    bmcr = c3c900_mii_read(nic, phy_addr, MII_BMCR);
    bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
    c3c900_mii_write(nic, phy_addr, MII_BMCR, bmcr);
    
    LOG_INFO("BOOMTEX: Starting 3C900-TPO auto-negotiation (advertising 0x%04X)", anar);
    
    /* Wait for auto-negotiation completion (up to 3 seconds) */
    timeout = 3000;
    do {
        delay_ms(10);
        bmsr = c3c900_mii_read(nic, phy_addr, MII_BMSR);
        timeout -= 10;
    } while (!(bmsr & BMSR_ANEGCOMPLETE) && timeout > 0);
    
    if (!(bmsr & BMSR_ANEGCOMPLETE)) {
        LOG_ERROR("BOOMTEX: 3C900-TPO auto-negotiation timeout");
        return ERROR_TIMEOUT;
    }
    
    /* Read negotiation results */
    anlpar = c3c900_mii_read(nic, phy_addr, MII_ANLPAR);
    
    /* Determine best common mode */
    uint16_t common = anar & anlpar;
    
    if (common & ANAR_10BTFULL) {
        nic->media_type = BOOMTEX_MEDIA_10BT;
        nic->duplex_mode = BOOMTEX_DUPLEX_FULL;
    } else if (common & ANAR_10BTHALF) {
        nic->media_type = BOOMTEX_MEDIA_10BT;
        nic->duplex_mode = BOOMTEX_DUPLEX_HALF;
    } else {
        LOG_ERROR("BOOMTEX: No common media found in auto-negotiation");
        return ERROR_HARDWARE_INIT_FAILED;
    }
    
    LOG_INFO("BOOMTEX: 3C900-TPO auto-negotiation complete: %s %s-duplex",
             "10BASE-T",
             (nic->duplex_mode == BOOMTEX_DUPLEX_FULL) ? "full" : "half");
    
    return SUCCESS;
}

/**
 * @brief Read MII register from 3C900-TPO PHY
 * 
 * @param nic NIC context structure
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @return Register value
 */
static uint16_t c3c900_mii_read(boomtex_nic_context_t *nic, uint8_t phy_addr, uint8_t reg_addr) {
    uint16_t cmd = (phy_addr << 5) | reg_addr;
    
    /* Select window 4 */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_SET_WIN | 4);
    
    /* Write PHY and register address */
    outw(nic->io_base + C3C900_MII_DATA, cmd);
    
    /* Start read operation */
    outw(nic->io_base + C3C900_MII_CMD, C3C900_MII_READ);
    
    /* Wait for operation to complete */
    for (int timeout = 1000; timeout > 0; timeout--) {
        if (!(inw(nic->io_base + C3C900_MII_CMD) & C3C900_MII_READ)) {
            break;
        }
        delay_us(10);
    }
    
    /* Read result */
    return inw(nic->io_base + C3C900_MII_DATA);
}

/**
 * @brief Write MII register to 3C900-TPO PHY
 * 
 * @param nic NIC context structure
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @param data Data to write
 */
static void c3c900_mii_write(boomtex_nic_context_t *nic, uint8_t phy_addr, uint8_t reg_addr, uint16_t data) {
    uint16_t cmd = (phy_addr << 5) | reg_addr;
    
    /* Select window 4 */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_SET_WIN | 4);
    
    /* Write data */
    outw(nic->io_base + C3C900_MII_DATA, data);
    
    /* Write PHY and register address */
    outw(nic->io_base + C3C900_MII_DATA + 2, cmd);
    
    /* Start write operation */
    outw(nic->io_base + C3C900_MII_CMD, C3C900_MII_WRITE);
    
    /* Wait for operation to complete */
    for (int timeout = 1000; timeout > 0; timeout--) {
        if (!(inw(nic->io_base + C3C900_MII_CMD) & C3C900_MII_WRITE)) {
            break;
        }
        delay_us(10);
    }
}

/**
 * @brief Get link status for 3C900-TPO
 * 
 * @param nic NIC context structure
 * @return 1 if link up, 0 if link down, negative error code on failure
 */
int boomtex_3c900tpo_get_link_status(boomtex_nic_context_t *nic) {
    uint16_t media_status;
    
    /* Select window 4 (Diagnostics) */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_SET_WIN | 4);
    
    /* Read media status register */
    media_status = inw(nic->io_base + C3C900_W4_MEDIA_STATUS);
    
    /* Check link detect bit */
    return (media_status & C3C900_MEDIA_LINK_DET) ? 1 : 0;
}

/**
 * @brief Handle 3C900-TPO interrupt
 * 
 * @param nic NIC context structure
 */
void boomtex_3c900tpo_interrupt(boomtex_nic_context_t *nic) {
    uint16_t status;
    
    /* Read interrupt status */
    status = inw(nic->io_base + C3C900_STATUS);
    
    /* Handle TX completion */
    if (status & C3C900_STAT_TX_COMPLETE) {
        boomtex_cleanup_tx_ring(nic);
    }
    
    /* Handle RX completion */
    if (status & C3C900_STAT_RX_COMPLETE) {
        boomtex_process_rx_ring(nic);
    }
    
    /* Handle link events */
    if (status & C3C900_STAT_LINK_EVENT) {
        int link_status = boomtex_3c900tpo_get_link_status(nic);
        if (link_status != nic->link_status) {
            nic->link_status = link_status;
            LOG_INFO("BOOMTEX: 3C900-TPO link %s", link_status ? "UP" : "DOWN");
        }
    }
    
    /* Acknowledge interrupt */
    outw(nic->io_base + C3C900_COMMAND, C3C900_CMD_INT_ACK | (status & 0x7F));
    
    nic->interrupts_handled++;
}