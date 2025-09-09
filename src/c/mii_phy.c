/**
 * @file mii_phy.c
 * @brief MII/PHY management for link negotiation and media control
 * 
 * Implements Media Independent Interface (MII) management for PHY control,
 * including auto-negotiation, link status monitoring, and media selection.
 */

#include <dos.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "mii_phy.h"
#include "hardware.h"
#include "logging.h"
#include "common.h"

/* MII management registers (in Window 4) */
#define WINDOW_4                4
#define PHY_MGMT                0x08    /* PHY management register */
#define PHY_DATA_MASK           0xFFFF  /* Data mask */

/* PHY management register bits */
#define PHY_MGMT_CLK            0x01    /* MII clock */
#define PHY_MGMT_DATA           0x02    /* MII data */
#define PHY_MGMT_DIR            0x04    /* Direction (1=write) */

/* MII frame structure */
#define MII_PREAMBLE            0xFFFFFFFF  /* 32 ones */
#define MII_START               0x01        /* Start bits (01) */
#define MII_READ_OP             0x02        /* Read operation */
#define MII_WRITE_OP            0x01        /* Write operation */
#define MII_TURNAROUND          0x02        /* Turnaround time */

/* Timing parameters */
#define MII_CLOCK_PERIOD_US     1           /* 1us clock period (2.5MHz) */
#define MII_SETUP_TIME_US       1           /* Setup time before clock */
#define PHY_RESET_TIME_MS       500         /* PHY reset time */
#define AUTONEG_TIMEOUT_MS      5000        /* Auto-negotiation timeout */
#define LINK_POLL_INTERVAL_MS   1000        /* Link status poll interval */

/* PHY capabilities cache */
typedef struct {
    uint16_t phy_id1;
    uint16_t phy_id2;
    uint16_t basic_cap;
    uint16_t extended_cap;
    bool gigabit_capable;
    bool flow_control;
    uint8_t phy_addr;
} phy_info_t;

static phy_info_t phy_cache = {0};

/**
 * @brief Generate MII clock pulse
 * 
 * @param iobase NIC I/O base address
 * @param data_bit Data bit to send (0 or 1)
 */
static void mii_clock_pulse(uint16_t iobase, uint8_t data_bit) {
    uint16_t mgmt = 0;
    
    /* Set data bit */
    if (data_bit) {
        mgmt |= PHY_MGMT_DATA;
    }
    mgmt |= PHY_MGMT_DIR;  /* Write mode */
    
    /* Clock low with data */
    outw(iobase + PHY_MGMT, mgmt);
    delay_us(MII_SETUP_TIME_US);
    
    /* Clock high with data */
    mgmt |= PHY_MGMT_CLK;
    outw(iobase + PHY_MGMT, mgmt);
    delay_us(MII_CLOCK_PERIOD_US);
    
    /* Clock low */
    mgmt &= ~PHY_MGMT_CLK;
    outw(iobase + PHY_MGMT, mgmt);
    delay_us(MII_SETUP_TIME_US);
}

/**
 * @brief Send MII preamble
 * 
 * @param iobase NIC I/O base address
 */
static void mii_send_preamble(uint16_t iobase) {
    int i;
    
    /* Send 32 ones */
    for (i = 0; i < 32; i++) {
        mii_clock_pulse(iobase, 1);
    }
}

/**
 * @brief Send MII frame bits
 * 
 * @param iobase NIC I/O base address
 * @param data Data to send
 * @param bits Number of bits to send
 */
static void mii_send_bits(uint16_t iobase, uint32_t data, int bits) {
    int i;
    
    for (i = bits - 1; i >= 0; i--) {
        mii_clock_pulse(iobase, (data >> i) & 0x01);
    }
}

/**
 * @brief Receive MII frame bits
 * 
 * @param iobase NIC I/O base address
 * @param bits Number of bits to receive
 * @return Received data
 */
static uint16_t mii_receive_bits(uint16_t iobase, int bits) {
    uint16_t data = 0;
    uint16_t mgmt;
    int i;
    
    /* Switch to read mode */
    mgmt = 0;  /* DIR=0 for read */
    outw(iobase + PHY_MGMT, mgmt);
    
    for (i = 0; i < bits; i++) {
        /* Clock high */
        mgmt |= PHY_MGMT_CLK;
        outw(iobase + PHY_MGMT, mgmt);
        delay_us(MII_CLOCK_PERIOD_US);
        
        /* Read data bit */
        if (inw(iobase + PHY_MGMT) & PHY_MGMT_DATA) {
            data |= (1 << (bits - 1 - i));
        }
        
        /* Clock low */
        mgmt &= ~PHY_MGMT_CLK;
        outw(iobase + PHY_MGMT, mgmt);
        delay_us(MII_SETUP_TIME_US);
    }
    
    return data;
}

/**
 * @brief Read MII PHY register
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @return Register value, or 0xFFFF on error
 */
uint16_t mii_read_phy(uint16_t iobase, uint8_t phy_addr, uint8_t reg_addr) {
    uint16_t value;
    uint16_t old_window;
    
    if (phy_addr > 31 || reg_addr > 31) {
        LOG_ERROR("Invalid PHY/register address: %d/%d", phy_addr, reg_addr);
        return 0xFFFF;
    }
    
    /* Save and select Window 4 */
    old_window = inw(iobase + 0x0E) >> 13;
    outw(iobase + 0x0E, 0x0800 | WINDOW_4);
    
    /* Send preamble */
    mii_send_preamble(iobase);
    
    /* Send start bits (01) */
    mii_send_bits(iobase, MII_START, 2);
    
    /* Send read opcode (10) */
    mii_send_bits(iobase, MII_READ_OP, 2);
    
    /* Send PHY address */
    mii_send_bits(iobase, phy_addr, 5);
    
    /* Send register address */
    mii_send_bits(iobase, reg_addr, 5);
    
    /* Turnaround (Z0) - tristate for one bit, then zero */
    mii_receive_bits(iobase, 2);
    
    /* Read 16-bit data */
    value = mii_receive_bits(iobase, 16);
    
    /* Restore original window */
    outw(iobase + 0x0E, 0x0800 | old_window);
    
    LOG_DEBUG("MII read: PHY %d reg %d = 0x%04X", phy_addr, reg_addr, value);
    
    return value;
}

/**
 * @brief Write MII PHY register
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @param value Value to write
 * @return true on success, false on error
 */
bool mii_write_phy(uint16_t iobase, uint8_t phy_addr, uint8_t reg_addr, uint16_t value) {
    uint16_t old_window;
    
    if (phy_addr > 31 || reg_addr > 31) {
        LOG_ERROR("Invalid PHY/register address: %d/%d", phy_addr, reg_addr);
        return false;
    }
    
    LOG_DEBUG("MII write: PHY %d reg %d = 0x%04X", phy_addr, reg_addr, value);
    
    /* Save and select Window 4 */
    old_window = inw(iobase + 0x0E) >> 13;
    outw(iobase + 0x0E, 0x0800 | WINDOW_4);
    
    /* Send preamble */
    mii_send_preamble(iobase);
    
    /* Send start bits (01) */
    mii_send_bits(iobase, MII_START, 2);
    
    /* Send write opcode (01) */
    mii_send_bits(iobase, MII_WRITE_OP, 2);
    
    /* Send PHY address */
    mii_send_bits(iobase, phy_addr, 5);
    
    /* Send register address */
    mii_send_bits(iobase, reg_addr, 5);
    
    /* Turnaround (10) */
    mii_send_bits(iobase, MII_TURNAROUND, 2);
    
    /* Write 16-bit data */
    mii_send_bits(iobase, value, 16);
    
    /* Restore original window */
    outw(iobase + 0x0E, 0x0800 | old_window);
    
    return true;
}

/**
 * @brief Find PHY address by scanning
 * 
 * @param iobase NIC I/O base address
 * @return PHY address (0-31), or 0xFF if not found
 */
uint8_t mii_find_phy(uint16_t iobase) {
    uint8_t phy_addr;
    uint16_t phy_id1, phy_id2;
    
    LOG_INFO("Scanning for PHY...");
    
    /* Try common addresses first */
    const uint8_t common_addrs[] = {0, 1, 24, 31};
    int i;
    
    for (i = 0; i < sizeof(common_addrs); i++) {
        phy_addr = common_addrs[i];
        phy_id1 = mii_read_phy(iobase, phy_addr, MII_PHY_ID1);
        phy_id2 = mii_read_phy(iobase, phy_addr, MII_PHY_ID2);
        
        if (phy_id1 != 0x0000 && phy_id1 != 0xFFFF &&
            phy_id2 != 0x0000 && phy_id2 != 0xFFFF) {
            LOG_INFO("Found PHY at address %d: ID=%04X:%04X", 
                    phy_addr, phy_id1, phy_id2);
            
            /* Cache PHY info */
            phy_cache.phy_addr = phy_addr;
            phy_cache.phy_id1 = phy_id1;
            phy_cache.phy_id2 = phy_id2;
            
            return phy_addr;
        }
    }
    
    /* Scan all addresses */
    for (phy_addr = 0; phy_addr < 32; phy_addr++) {
        /* Skip already checked */
        for (i = 0; i < sizeof(common_addrs); i++) {
            if (phy_addr == common_addrs[i]) break;
        }
        if (i < sizeof(common_addrs)) continue;
        
        phy_id1 = mii_read_phy(iobase, phy_addr, MII_PHY_ID1);
        if (phy_id1 != 0x0000 && phy_id1 != 0xFFFF) {
            phy_id2 = mii_read_phy(iobase, phy_addr, MII_PHY_ID2);
            if (phy_id2 != 0x0000 && phy_id2 != 0xFFFF) {
                LOG_INFO("Found PHY at address %d: ID=%04X:%04X",
                        phy_addr, phy_id1, phy_id2);
                
                /* Cache PHY info */
                phy_cache.phy_addr = phy_addr;
                phy_cache.phy_id1 = phy_id1;
                phy_cache.phy_id2 = phy_id2;
                
                return phy_addr;
            }
        }
    }
    
    LOG_ERROR("No PHY found");
    return 0xFF;
}

/**
 * @brief Reset PHY
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address
 * @return true on success, false on error
 */
bool mii_reset_phy(uint16_t iobase, uint8_t phy_addr) {
    uint16_t control;
    int timeout = PHY_RESET_TIME_MS;
    
    LOG_INFO("Resetting PHY %d", phy_addr);
    
    /* Set reset bit */
    if (!mii_write_phy(iobase, phy_addr, MII_CONTROL, MII_CTRL_RESET)) {
        return false;
    }
    
    /* Wait for reset to complete */
    while (timeout > 0) {
        delay_ms(10);
        control = mii_read_phy(iobase, phy_addr, MII_CONTROL);
        
        if (!(control & MII_CTRL_RESET)) {
            LOG_INFO("PHY reset complete");
            return true;
        }
        
        timeout -= 10;
    }
    
    LOG_ERROR("PHY reset timeout");
    return false;
}

/**
 * @brief Configure and start auto-negotiation
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address
 * @param advertise Capabilities to advertise
 * @return true on success, false on error
 */
bool mii_auto_negotiate(uint16_t iobase, uint8_t phy_addr, uint16_t advertise) {
    uint16_t control, status;
    int timeout;
    
    LOG_INFO("Starting auto-negotiation on PHY %d with advertise=0x%04X", 
            phy_addr, advertise);
    
    /* Set advertisement register */
    if (!mii_write_phy(iobase, phy_addr, MII_ADVERTISE, advertise)) {
        return false;
    }
    
    /* Enable and restart auto-negotiation */
    control = MII_CTRL_AUTONEG_ENABLE | MII_CTRL_RESTART_AUTONEG;
    if (!mii_write_phy(iobase, phy_addr, MII_CONTROL, control)) {
        return false;
    }
    
    /* Wait for auto-negotiation to complete */
    timeout = AUTONEG_TIMEOUT_MS;
    while (timeout > 0) {
        delay_ms(100);
        
        status = mii_read_phy(iobase, phy_addr, MII_STATUS);
        if (status & MII_STAT_AUTONEG_COMPLETE) {
            LOG_INFO("Auto-negotiation complete");
            
            /* Read link partner ability */
            uint16_t lpa = mii_read_phy(iobase, phy_addr, MII_LPA);
            LOG_INFO("Link partner abilities: 0x%04X", lpa);
            
            /* Determine negotiated speed/duplex */
            uint16_t common = advertise & lpa;
            if (common & MII_ADV_100BASE_T4) {
                LOG_INFO("Negotiated: 100BASE-T4");
            } else if (common & MII_ADV_100BASE_TX_FD) {
                LOG_INFO("Negotiated: 100BASE-TX Full Duplex");
            } else if (common & MII_ADV_100BASE_TX_HD) {
                LOG_INFO("Negotiated: 100BASE-TX Half Duplex");
            } else if (common & MII_ADV_10BASE_T_FD) {
                LOG_INFO("Negotiated: 10BASE-T Full Duplex");
            } else if (common & MII_ADV_10BASE_T_HD) {
                LOG_INFO("Negotiated: 10BASE-T Half Duplex");
            }
            
            return true;
        }
        
        timeout -= 100;
    }
    
    LOG_ERROR("Auto-negotiation timeout");
    return false;
}

/**
 * @brief Force specific speed and duplex
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address
 * @param speed Speed in Mbps (10 or 100)
 * @param full_duplex true for full duplex, false for half
 * @return true on success, false on error
 */
bool mii_force_mode(uint16_t iobase, uint8_t phy_addr, uint16_t speed, bool full_duplex) {
    uint16_t control = 0;
    
    LOG_INFO("Forcing PHY %d to %d Mbps %s duplex", 
            phy_addr, speed, full_duplex ? "full" : "half");
    
    /* Disable auto-negotiation */
    control = 0;
    
    /* Set speed */
    if (speed == 100) {
        control |= MII_CTRL_SPEED_100;
    } else if (speed != 10) {
        LOG_ERROR("Invalid speed %d (must be 10 or 100)", speed);
        return false;
    }
    
    /* Set duplex */
    if (full_duplex) {
        control |= MII_CTRL_FULL_DUPLEX;
    }
    
    /* Write control register */
    if (!mii_write_phy(iobase, phy_addr, MII_CONTROL, control)) {
        return false;
    }
    
    /* Wait for link */
    delay_ms(500);
    
    /* Check link status */
    uint16_t status = mii_read_phy(iobase, phy_addr, MII_STATUS);
    if (status & MII_STAT_LINK_UP) {
        LOG_INFO("Link up in forced mode");
        return true;
    } else {
        LOG_WARNING("Link down after forcing mode");
        return false;
    }
}

/**
 * @brief Get current link status
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address
 * @param status Output: Link status structure
 * @return true if link is up, false if down
 */
bool mii_get_link_status(uint16_t iobase, uint8_t phy_addr, link_status_t *status) {
    uint16_t mii_status, control, lpa;
    
    if (!status) {
        return false;
    }
    
    memset(status, 0, sizeof(link_status_t));
    
    /* Read status register twice (latched low) */
    mii_status = mii_read_phy(iobase, phy_addr, MII_STATUS);
    mii_status = mii_read_phy(iobase, phy_addr, MII_STATUS);
    
    /* Check link status */
    status->link_up = (mii_status & MII_STAT_LINK_UP) ? true : false;
    
    if (!status->link_up) {
        LOG_DEBUG("Link is down");
        return false;
    }
    
    /* Get control settings */
    control = mii_read_phy(iobase, phy_addr, MII_CONTROL);
    
    if (control & MII_CTRL_AUTONEG_ENABLE) {
        /* Auto-negotiation enabled */
        status->autoneg_enabled = true;
        status->autoneg_complete = (mii_status & MII_STAT_AUTONEG_COMPLETE) ? true : false;
        
        if (status->autoneg_complete) {
            /* Get negotiated capabilities */
            uint16_t advertise = mii_read_phy(iobase, phy_addr, MII_ADVERTISE);
            lpa = mii_read_phy(iobase, phy_addr, MII_LPA);
            uint16_t common = advertise & lpa;
            
            /* Determine speed/duplex from highest common capability */
            if (common & MII_ADV_100BASE_TX_FD) {
                status->speed = 100;
                status->full_duplex = true;
            } else if (common & MII_ADV_100BASE_TX_HD) {
                status->speed = 100;
                status->full_duplex = false;
            } else if (common & MII_ADV_10BASE_T_FD) {
                status->speed = 10;
                status->full_duplex = true;
            } else {
                status->speed = 10;
                status->full_duplex = false;
            }
            
            /* Check flow control */
            if ((common & MII_ADV_PAUSE) || (common & MII_ADV_ASYM_PAUSE)) {
                status->flow_control = true;
            }
        }
    } else {
        /* Forced mode */
        status->autoneg_enabled = false;
        status->speed = (control & MII_CTRL_SPEED_100) ? 100 : 10;
        status->full_duplex = (control & MII_CTRL_FULL_DUPLEX) ? true : false;
    }
    
    LOG_DEBUG("Link up: %d Mbps %s duplex%s", 
             status->speed, 
             status->full_duplex ? "full" : "half",
             status->flow_control ? " with flow control" : "");
    
    return true;
}

/**
 * @brief Initialize PHY with optimal settings
 * 
 * @param iobase NIC I/O base address
 * @param config Driver configuration
 * @return PHY address, or 0xFF on error
 */
uint8_t mii_init_phy(uint16_t iobase, const config_t *config) {
    uint8_t phy_addr;
    uint16_t advertise;
    link_status_t link;
    
    LOG_INFO("Initializing PHY/MII management");
    
    /* Find PHY */
    phy_addr = mii_find_phy(iobase);
    if (phy_addr == 0xFF) {
        LOG_ERROR("No PHY found - may be internal/embedded");
        return 0xFF;
    }
    
    /* Reset PHY */
    if (!mii_reset_phy(iobase, phy_addr)) {
        LOG_ERROR("PHY reset failed");
        return 0xFF;
    }
    
    /* Check if speed/duplex forced in config */
    if (config && config->force_speed) {
        bool full_duplex = (config->force_duplex == 2);
        if (!mii_force_mode(iobase, phy_addr, config->force_speed, full_duplex)) {
            LOG_WARNING("Failed to force mode, trying auto-negotiation");
        } else {
            return phy_addr;
        }
    }
    
    /* Setup advertisement based on NIC capabilities */
    advertise = MII_ADV_CSMA;  /* Always advertise CSMA */
    
    if (!config || config->speed == 0 || config->speed == 100) {
        /* Advertise 100 Mbps if capable */
        advertise |= MII_ADV_100BASE_TX_FD | MII_ADV_100BASE_TX_HD;
    }
    
    if (!config || config->speed == 0 || config->speed == 10) {
        /* Advertise 10 Mbps */
        advertise |= MII_ADV_10BASE_T_FD | MII_ADV_10BASE_T_HD;
    }
    
    /* Advertise flow control if supported */
    advertise |= MII_ADV_PAUSE | MII_ADV_ASYM_PAUSE;
    
    /* Start auto-negotiation */
    if (!mii_auto_negotiate(iobase, phy_addr, advertise)) {
        LOG_WARNING("Auto-negotiation failed, checking link status");
    }
    
    /* Get final link status */
    if (mii_get_link_status(iobase, phy_addr, &link)) {
        LOG_INFO("PHY initialized: Link up at %d Mbps %s duplex",
                link.speed, link.full_duplex ? "full" : "half");
    } else {
        LOG_WARNING("PHY initialized but link is down");
    }
    
    return phy_addr;
}

/**
 * @brief Enable PHY loopback for testing
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address
 * @param enable true to enable, false to disable
 * @return true on success, false on error
 */
bool mii_set_loopback(uint16_t iobase, uint8_t phy_addr, bool enable) {
    uint16_t control;
    
    LOG_INFO("%s PHY loopback on PHY %d", enable ? "Enabling" : "Disabling", phy_addr);
    
    control = mii_read_phy(iobase, phy_addr, MII_CONTROL);
    
    if (enable) {
        control |= MII_CTRL_LOOPBACK;
    } else {
        control &= ~MII_CTRL_LOOPBACK;
    }
    
    return mii_write_phy(iobase, phy_addr, MII_CONTROL, control);
}

/**
 * @brief Get PHY statistics
 * 
 * @param iobase NIC I/O base address
 * @param phy_addr PHY address
 * @param stats Output: PHY statistics
 * @return true on success, false on error
 */
bool mii_get_phy_stats(uint16_t iobase, uint8_t phy_addr, phy_stats_t *stats) {
    if (!stats) {
        return false;
    }
    
    memset(stats, 0, sizeof(phy_stats_t));
    
    /* Read extended status if available */
    uint16_t ext_status = mii_read_phy(iobase, phy_addr, MII_EXT_STATUS);
    if (ext_status != 0xFFFF && ext_status != 0x0000) {
        stats->gigabit_capable = (ext_status & 0x3000) ? true : false;
    }
    
    /* Check for vendor-specific error counters */
    /* This would be PHY-specific */
    
    /* Store cached PHY info */
    stats->phy_id = (phy_cache.phy_id1 << 16) | phy_cache.phy_id2;
    stats->phy_addr = phy_addr;
    
    return true;
}