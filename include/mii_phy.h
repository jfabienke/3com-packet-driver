/**
 * @file mii_phy.h
 * @brief MII/PHY management interface
 */

#ifndef _MII_PHY_H_
#define _MII_PHY_H_

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* MII standard registers */
#define MII_CONTROL             0x00    /* Control register */
#define MII_STATUS              0x01    /* Status register */
#define MII_PHY_ID1             0x02    /* PHY ID 1 */
#define MII_PHY_ID2             0x03    /* PHY ID 2 */
#define MII_ADVERTISE           0x04    /* Advertisement register */
#define MII_LPA                 0x05    /* Link partner ability */
#define MII_EXPANSION           0x06    /* Expansion register */
#define MII_NEXT_PAGE           0x07    /* Next page */
#define MII_LPA_NEXT            0x08    /* Link partner next page */
#define MII_EXT_STATUS          0x0F    /* Extended status */

/* MII Control register bits */
#define MII_CTRL_RESET          0x8000  /* Reset PHY */
#define MII_CTRL_LOOPBACK       0x4000  /* Enable loopback */
#define MII_CTRL_SPEED_100      0x2000  /* Select 100 Mbps */
#define MII_CTRL_AUTONEG_ENABLE 0x1000  /* Enable auto-negotiation */
#define MII_CTRL_POWER_DOWN     0x0800  /* Power down */
#define MII_CTRL_ISOLATE        0x0400  /* Electrically isolate PHY */
#define MII_CTRL_RESTART_AUTONEG 0x0200 /* Restart auto-negotiation */
#define MII_CTRL_FULL_DUPLEX    0x0100  /* Select full duplex */
#define MII_CTRL_COLLISION_TEST 0x0080  /* Enable collision test */

/* MII Status register bits */
#define MII_STAT_100BASE_T4     0x8000  /* 100BASE-T4 capable */
#define MII_STAT_100BASE_TX_FD  0x4000  /* 100BASE-TX full duplex capable */
#define MII_STAT_100BASE_TX_HD  0x2000  /* 100BASE-TX half duplex capable */
#define MII_STAT_10BASE_T_FD    0x1000  /* 10BASE-T full duplex capable */
#define MII_STAT_10BASE_T_HD    0x0800  /* 10BASE-T half duplex capable */
#define MII_STAT_MF_PREAMBLE    0x0040  /* Preamble suppression */
#define MII_STAT_AUTONEG_COMPLETE 0x0020 /* Auto-negotiation complete */
#define MII_STAT_REMOTE_FAULT   0x0010  /* Remote fault detected */
#define MII_STAT_AUTONEG_CAPABLE 0x0008 /* Auto-negotiation capable */
#define MII_STAT_LINK_UP        0x0004  /* Link is up */
#define MII_STAT_JABBER         0x0002  /* Jabber detected */
#define MII_STAT_EXTENDED       0x0001  /* Extended capability */

/* MII Advertisement/LPA bits */
#define MII_ADV_NEXT_PAGE       0x8000  /* Next page */
#define MII_ADV_REMOTE_FAULT    0x2000  /* Remote fault */
#define MII_ADV_ASYM_PAUSE      0x0800  /* Asymmetric pause */
#define MII_ADV_PAUSE           0x0400  /* Pause capable */
#define MII_ADV_100BASE_T4      0x0200  /* 100BASE-T4 */
#define MII_ADV_100BASE_TX_FD   0x0100  /* 100BASE-TX full duplex */
#define MII_ADV_100BASE_TX_HD   0x0080  /* 100BASE-TX half duplex */
#define MII_ADV_10BASE_T_FD     0x0040  /* 10BASE-T full duplex */
#define MII_ADV_10BASE_T_HD     0x0020  /* 10BASE-T half duplex */
#define MII_ADV_CSMA            0x0001  /* CSMA capable */

/* Link status structure */
typedef struct {
    bool link_up;           /* Link is up */
    uint16_t speed;         /* Speed in Mbps (10/100/1000) */
    bool full_duplex;       /* Full duplex mode */
    bool autoneg_enabled;   /* Auto-negotiation enabled */
    bool autoneg_complete;  /* Auto-negotiation complete */
    bool flow_control;      /* Flow control enabled */
    bool mdi_x;            /* MDI-X (crossover) active */
} link_status_t;

/* PHY statistics */
typedef struct {
    uint32_t phy_id;        /* PHY identifier */
    uint8_t phy_addr;       /* PHY address */
    bool gigabit_capable;   /* Gigabit capable */
    uint32_t link_failures; /* Link down events */
    uint32_t rx_errors;     /* Receive errors */
} phy_stats_t;

/* Function prototypes */

/**
 * @brief Read MII PHY register
 */
uint16_t mii_read_phy(uint16_t iobase, uint8_t phy_addr, uint8_t reg_addr);

/**
 * @brief Write MII PHY register
 */
bool mii_write_phy(uint16_t iobase, uint8_t phy_addr, uint8_t reg_addr, uint16_t value);

/**
 * @brief Find PHY address by scanning
 */
uint8_t mii_find_phy(uint16_t iobase);

/**
 * @brief Reset PHY
 */
bool mii_reset_phy(uint16_t iobase, uint8_t phy_addr);

/**
 * @brief Configure and start auto-negotiation
 */
bool mii_auto_negotiate(uint16_t iobase, uint8_t phy_addr, uint16_t advertise);

/**
 * @brief Force specific speed and duplex
 */
bool mii_force_mode(uint16_t iobase, uint8_t phy_addr, uint16_t speed, bool full_duplex);

/**
 * @brief Get current link status
 */
bool mii_get_link_status(uint16_t iobase, uint8_t phy_addr, link_status_t *status);

/**
 * @brief Initialize PHY with optimal settings
 */
uint8_t mii_init_phy(uint16_t iobase, const config_t *config);

/**
 * @brief Enable PHY loopback for testing
 */
bool mii_set_loopback(uint16_t iobase, uint8_t phy_addr, bool enable);

/**
 * @brief Get PHY statistics
 */
bool mii_get_phy_stats(uint16_t iobase, uint8_t phy_addr, phy_stats_t *stats);

#endif /* _MII_PHY_H_ */
