/**
 * @file 3com_windows.c
 * @brief Window-based register abstraction for 3Com PCI NICs
 *
 * Implements the window switching and register access functions for all
 * 3Com PCI network controllers. The window architecture allows accessing
 * different register sets through a single I/O space.
 *
 * 3Com Packet Driver - Window Management Implementation
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/media_control.h"
#include <dos.h>

/* MII PHY Registers */
#define MII_BMCR            0x00    /* Basic Mode Control Register */
#define MII_BMSR            0x01    /* Basic Mode Status Register */
#define MII_PHYSID1         0x02    /* PHY Identifier 1 */
#define MII_PHYSID2         0x03    /* PHY Identifier 2 */
#define MII_ADVERTISE       0x04    /* Auto-negotiation Advertisement */
#define MII_ANLPAR          0x05    /* Auto-negotiation Link Partner */

/* MII BMCR bits */
#define BMCR_RESET          0x8000  /* Reset PHY */
#define BMCR_LOOPBACK       0x4000  /* Enable loopback */
#define BMCR_SPEED100       0x2000  /* Select 100Mbps */
#define BMCR_ANENABLE       0x1000  /* Enable auto-negotiation */
#define BMCR_POWERDOWN      0x0800  /* Power down PHY */
#define BMCR_ISOLATE        0x0400  /* Electrically isolate PHY */
#define BMCR_ANRESTART      0x0200  /* Restart auto-negotiation */
#define BMCR_FULLDPLX       0x0100  /* Full duplex */

/* MII BMSR bits */
#define BMSR_100FULL        0x4000  /* Can do 100mbps full-duplex */
#define BMSR_100HALF        0x2000  /* Can do 100mbps half-duplex */
#define BMSR_10FULL         0x1000  /* Can do 10mbps full-duplex */
#define BMSR_10HALF         0x0800  /* Can do 10mbps half-duplex */
#define BMSR_ANEGCOMPLETE   0x0020  /* Auto-negotiation complete */
#define BMSR_LSTATUS        0x0004  /* Link status */

/* MII ANLPAR bits */
#define ANLPAR_100FULL      0x0100  /* Partner can do 100mbps full */
#define ANLPAR_100HALF      0x0080  /* Partner can do 100mbps half */
#define ANLPAR_10FULL       0x0040  /* Partner can do 10mbps full */
#define ANLPAR_10HALF       0x0020  /* Partner can do 10mbps half */

/* Track current window to minimize switching */
static uint8_t current_window[MAX_NICS] = {
    0xFF, 0xFF, 0xFF, 0xFF  /* Invalid window = force first switch */
};

/**
 * @brief Select a register window
 * 
 * Switches to the specified window if not already selected.
 * Window switching is expensive, so we track the current window.
 * 
 * @param ioaddr Base I/O address of the NIC
 * @param window Window number (0-7)
 */
void select_window(uint16_t ioaddr, uint8_t window)
{
    uint8_t nic_index = 0;
    
    /* Simple NIC index calculation based on I/O address */
    /* Assumes NICs are at standard offsets */
    if (ioaddr >= 0x300) {
        nic_index = (ioaddr - 0x300) / 0x20;
        if (nic_index >= MAX_NICS) {
            nic_index = 0;
        }
    }
    
    /* Only switch if necessary */
    if (current_window[nic_index] != window) {
        outpw(ioaddr + EL3_CMD, SelectWindow + window);
        current_window[nic_index] = window;
        
        LOG_DEBUG("3Com: Switched to window %d at I/O 0x%04X", window, ioaddr);
    }
}

/**
 * @brief Read 8-bit value from windowed register
 * 
 * @param ioaddr Base I/O address
 * @param window Window number
 * @param reg Register offset within window
 * @return 8-bit register value
 */
uint8_t window_read8(uint16_t ioaddr, uint8_t window, uint8_t reg)
{
    select_window(ioaddr, window);
    return inp(ioaddr + reg);
}

/**
 * @brief Read 16-bit value from windowed register
 * 
 * @param ioaddr Base I/O address
 * @param window Window number
 * @param reg Register offset within window
 * @return 16-bit register value
 */
uint16_t window_read16(uint16_t ioaddr, uint8_t window, uint8_t reg)
{
    select_window(ioaddr, window);
    return inpw(ioaddr + reg);
}

/**
 * @brief Read 32-bit value from windowed register
 * 
 * @param ioaddr Base I/O address
 * @param window Window number
 * @param reg Register offset within window
 * @return 32-bit register value
 */
uint32_t window_read32(uint16_t ioaddr, uint8_t window, uint8_t reg)
{
    uint32_t value;
    
    select_window(ioaddr, window);
    
    /* Read as two 16-bit values for compatibility */
    value = inpw(ioaddr + reg);
    value |= ((uint32_t)inpw(ioaddr + reg + 2)) << 16;
    
    return value;
}

/**
 * @brief Write 8-bit value to windowed register
 * 
 * @param ioaddr Base I/O address
 * @param window Window number
 * @param reg Register offset within window
 * @param value Value to write
 */
void window_write8(uint16_t ioaddr, uint8_t window, uint8_t reg, uint8_t value)
{
    select_window(ioaddr, window);
    outp(ioaddr + reg, value);
}

/**
 * @brief Write 16-bit value to windowed register
 * 
 * @param ioaddr Base I/O address
 * @param window Window number
 * @param reg Register offset within window
 * @param value Value to write
 */
void window_write16(uint16_t ioaddr, uint8_t window, uint8_t reg, uint16_t value)
{
    select_window(ioaddr, window);
    outpw(ioaddr + reg, value);
}

/**
 * @brief Write 32-bit value to windowed register
 * 
 * @param ioaddr Base I/O address
 * @param window Window number
 * @param reg Register offset within window
 * @param value Value to write
 */
void window_write32(uint16_t ioaddr, uint8_t window, uint8_t reg, uint32_t value)
{
    select_window(ioaddr, window);
    
    /* Write as two 16-bit values for compatibility */
    outpw(ioaddr + reg, (uint16_t)(value & 0xFFFF));
    outpw(ioaddr + reg + 2, (uint16_t)(value >> 16));
}

/**
 * @brief Read EEPROM value
 * 
 * Reads a 16-bit value from the EEPROM at the specified offset.
 * Uses Window 0 for EEPROM access.
 * 
 * @param ioaddr Base I/O address
 * @param offset EEPROM offset (0-63)
 * @return 16-bit EEPROM value
 */
uint16_t read_eeprom(uint16_t ioaddr, uint8_t offset)
{
    uint16_t value;
    int timeout;
    
    /* Select window 0 for EEPROM access */
    select_window(ioaddr, 0);
    
    /* Issue read command */
    outpw(ioaddr + WN0_EEPROM_CMD, 0x80 | offset);
    
    /* Wait for completion (162us minimum) */
    for (timeout = 1000; timeout > 0; timeout--) {
        if ((inpw(ioaddr + WN0_EEPROM_CMD) & 0x8000) == 0) {
            break;
        }
        delay_us(1);
    }
    
    if (timeout == 0) {
        LOG_ERROR("3Com: EEPROM read timeout at offset %d", offset);
        return 0xFFFF;
    }
    
    /* Read the data */
    value = inpw(ioaddr + WN0_EEPROM_DATA);
    
    LOG_DEBUG("3Com: EEPROM[%02X] = 0x%04X", offset, value);
    
    return value;
}

/**
 * @brief Write EEPROM value
 * 
 * Writes a 16-bit value to the EEPROM at the specified offset.
 * WARNING: EEPROM has limited write cycles, use sparingly.
 * 
 * @param ioaddr Base I/O address
 * @param offset EEPROM offset (0-63)
 * @param value Value to write
 * @return 0 on success, -1 on error
 */
int write_eeprom(uint16_t ioaddr, uint8_t offset, uint16_t value)
{
    int timeout;
    
    /* Select window 0 for EEPROM access */
    select_window(ioaddr, 0);
    
    /* Enable EEPROM write */
    outpw(ioaddr + WN0_EEPROM_CMD, 0x30);  /* EWEN - Enable write */
    delay_us(162);
    
    /* Write the data */
    outpw(ioaddr + WN0_EEPROM_DATA, value);
    
    /* Issue write command */
    outpw(ioaddr + WN0_EEPROM_CMD, 0x40 | offset);
    
    /* Wait for completion (much longer for writes) */
    for (timeout = 10000; timeout > 0; timeout--) {
        if ((inpw(ioaddr + WN0_EEPROM_CMD) & 0x8000) == 0) {
            break;
        }
        delay_us(10);
    }
    
    /* Disable EEPROM write */
    outpw(ioaddr + WN0_EEPROM_CMD, 0x00);  /* EWDS - Disable write */
    
    if (timeout == 0) {
        LOG_ERROR("3Com: EEPROM write timeout at offset %d", offset);
        return -1;
    }
    
    LOG_INFO("3Com: EEPROM[%02X] written with 0x%04X", offset, value);
    
    return 0;
}

/**
 * @brief Read MII PHY register
 * 
 * Reads a register from the MII PHY using the management interface.
 * Uses Window 4 for PHY access.
 * 
 * @param ioaddr Base I/O address
 * @param phy_id PHY identifier (usually 24)
 * @param reg Register number (0-31)
 * @return 16-bit PHY register value
 */
uint16_t mdio_read(uint16_t ioaddr, uint8_t phy_id, uint8_t reg)
{
    uint16_t value = 0;
    int i;
    
    /* Select window 4 for PHY management */
    select_window(ioaddr, 4);
    
    /* Send preamble (32 ones) */
    for (i = 0; i < 32; i++) {
        outpw(ioaddr + WN4_PHYS_MGMT, 0x0001);  /* MDIO_DATA_WRITE1 */
        outpw(ioaddr + WN4_PHYS_MGMT, 0x0003);  /* MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK */
    }
    
    /* Send start (01), read (10), PHY address, and register */
    uint32_t cmd = (0x06 << 10) | (phy_id << 5) | reg;
    
    for (i = 13; i >= 0; i--) {
        uint16_t dataval = (cmd & (1 << i)) ? 0x0001 : 0x0000;
        outpw(ioaddr + WN4_PHYS_MGMT, dataval);
        outpw(ioaddr + WN4_PHYS_MGMT, dataval | 0x0002);  /* Add clock */
    }
    
    /* Turnaround - switch to input */
    outpw(ioaddr + WN4_PHYS_MGMT, 0x0000);  /* MDIO_ENB_IN */
    outpw(ioaddr + WN4_PHYS_MGMT, 0x0002);  /* MDIO_ENB_IN | MDIO_SHIFT_CLK */
    
    /* Read 16 bits of data */
    for (i = 0; i < 16; i++) {
        outpw(ioaddr + WN4_PHYS_MGMT, 0x0000);
        value = (value << 1) | ((inpw(ioaddr + WN4_PHYS_MGMT) & 0x0001) ? 1 : 0);
        outpw(ioaddr + WN4_PHYS_MGMT, 0x0002);
    }
    
    /* Idle - return bus to idle state */
    outpw(ioaddr + WN4_PHYS_MGMT, 0x0000);
    
    LOG_DEBUG("3Com: PHY[%d].reg[%d] = 0x%04X", phy_id, reg, value);
    
    return value;
}

/**
 * @brief Write MII PHY register
 * 
 * Writes a register in the MII PHY using the management interface.
 * 
 * @param ioaddr Base I/O address
 * @param phy_id PHY identifier (usually 24)
 * @param reg Register number (0-31)
 * @param value Value to write
 */
void mdio_write(uint16_t ioaddr, uint8_t phy_id, uint8_t reg, uint16_t value)
{
    int i;
    
    /* Select window 4 for PHY management */
    select_window(ioaddr, 4);
    
    /* Send preamble (32 ones) */
    for (i = 0; i < 32; i++) {
        outpw(ioaddr + WN4_PHYS_MGMT, 0x0001);
        outpw(ioaddr + WN4_PHYS_MGMT, 0x0003);
    }
    
    /* Send start (01), write (01), PHY address, register, and turnaround (10) */
    uint32_t cmd = (0x05 << 28) | (phy_id << 23) | (reg << 18) | (0x02 << 16) | value;
    
    for (i = 31; i >= 0; i--) {
        uint16_t dataval = (cmd & (1UL << i)) ? 0x0001 : 0x0000;
        outpw(ioaddr + WN4_PHYS_MGMT, dataval);
        outpw(ioaddr + WN4_PHYS_MGMT, dataval | 0x0002);
    }
    
    /* Idle */
    outpw(ioaddr + WN4_PHYS_MGMT, 0x0000);
    
    LOG_DEBUG("3Com: PHY[%d].reg[%d] written with 0x%04X", phy_id, reg, value);
}

/**
 * @brief Reset window tracking
 * 
 * Forces the next window selection to actually switch windows.
 * Useful after a chip reset or initialization.
 * 
 * @param ioaddr Base I/O address (0 for all)
 */
void reset_window_tracking(uint16_t ioaddr)
{
    if (ioaddr == 0) {
        /* Reset all */
        for (int i = 0; i < MAX_NICS; i++) {
            current_window[i] = 0xFF;
        }
    } else {
        /* Reset specific NIC */
        uint8_t nic_index = 0;
        if (ioaddr >= 0x300) {
            nic_index = (ioaddr - 0x300) / 0x20;
            if (nic_index < MAX_NICS) {
                current_window[nic_index] = 0xFF;
            }
        }
    }
}

/**
 * @brief Start auto-negotiation on MII PHY
 * 
 * @param ioaddr Base I/O address
 * @param phy_id PHY identifier
 * @return 0 on success, negative on error
 */
int mii_start_autoneg(uint16_t ioaddr, uint8_t phy_id)
{
    uint16_t bmcr;
    
    /* Read Basic Mode Control Register */
    bmcr = mdio_read(ioaddr, phy_id, MII_BMCR);
    
    /* Enable and restart auto-negotiation */
    bmcr |= BMCR_ANENABLE | BMCR_ANRESTART;
    mdio_write(ioaddr, phy_id, MII_BMCR, bmcr);
    
    LOG_INFO("MII: Started auto-negotiation on PHY %d", phy_id);
    
    return SUCCESS;
}

/**
 * @brief Check auto-negotiation completion and get results
 * 
 * @param ioaddr Base I/O address
 * @param phy_id PHY identifier
 * @param speed Pointer to store negotiated speed (10/100)
 * @param duplex Pointer to store duplex mode (0=half, 1=full)
 * @return 0 on success, negative on error
 */
int mii_check_autoneg_complete(uint16_t ioaddr, uint8_t phy_id,
                               uint16_t *speed, uint8_t *duplex)
{
    uint16_t bmsr, anlpar;
    int timeout = 50;  /* 5 second timeout */
    
    /* Wait for auto-negotiation to complete */
    while (timeout > 0) {
        bmsr = mdio_read(ioaddr, phy_id, MII_BMSR);
        
        if (bmsr & BMSR_ANEGCOMPLETE) {
            /* Auto-negotiation complete - read partner abilities */
            anlpar = mdio_read(ioaddr, phy_id, MII_ANLPAR);
            
            /* Determine highest common speed and duplex */
            if (anlpar & ANLPAR_100FULL) {
                *speed = 100;
                *duplex = 1;
            } else if (anlpar & ANLPAR_100HALF) {
                *speed = 100;
                *duplex = 0;
            } else if (anlpar & ANLPAR_10FULL) {
                *speed = 10;
                *duplex = 1;
            } else {
                *speed = 10;
                *duplex = 0;
            }
            
            LOG_INFO("MII: Auto-negotiation complete - %d Mbps %s-duplex",
                     *speed, *duplex ? "full" : "half");
            
            return SUCCESS;
        }
        
        delay_ms(100);
        timeout--;
    }
    
    LOG_WARNING("MII: Auto-negotiation timeout");
    return ERROR_TIMEOUT;
}

/**
 * @brief Get MII PHY link status
 * 
 * @param ioaddr Base I/O address
 * @param phy_id PHY identifier
 * @return 1 if link up, 0 if link down
 */
int mii_get_link_status(uint16_t ioaddr, uint8_t phy_id)
{
    uint16_t bmsr;
    
    /* Read status register twice (latched low) */
    bmsr = mdio_read(ioaddr, phy_id, MII_BMSR);
    bmsr = mdio_read(ioaddr, phy_id, MII_BMSR);
    
    return (bmsr & BMSR_LSTATUS) ? 1 : 0;
}

/**
 * @brief Reset MII PHY
 * 
 * @param ioaddr Base I/O address
 * @param phy_id PHY identifier
 * @return 0 on success, negative on error
 */
int mii_reset_phy(uint16_t ioaddr, uint8_t phy_id)
{
    uint16_t bmcr;
    int timeout = 50;  /* 500ms timeout */
    
    /* Issue reset command */
    mdio_write(ioaddr, phy_id, MII_BMCR, BMCR_RESET);
    
    /* Wait for reset to complete */
    while (timeout > 0) {
        bmcr = mdio_read(ioaddr, phy_id, MII_BMCR);
        if (!(bmcr & BMCR_RESET)) {
            LOG_INFO("MII: PHY %d reset complete", phy_id);
            return SUCCESS;
        }
        delay_ms(10);
        timeout--;
    }
    
    LOG_ERROR("MII: PHY %d reset timeout", phy_id);
    return ERROR_TIMEOUT;
}