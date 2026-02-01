/**
 * @file mii.h
 * @brief MII (Media Independent Interface) PHY register definitions
 *
 * IEEE 802.3 Clause 22 MII Management Interface constants
 */

#ifndef _MII_H_
#define _MII_H_

/* MII Management opcodes */
#define MII_OP_READ         0x02    /* Read operation */
#define MII_OP_WRITE        0x01    /* Write operation */

/* MII PHY Registers (IEEE 802.3 Clause 22) */
#define MII_BMCR            0x00    /* Basic Mode Control Register */
#define MII_BMSR            0x01    /* Basic Mode Status Register */
#define MII_PHYSID1         0x02    /* PHY Identifier Register 1 */
#define MII_PHYSID2         0x03    /* PHY Identifier Register 2 */
#define MII_ANAR            0x04    /* Auto-Negotiation Advertisement Register */
#define MII_ANLPAR          0x05    /* Auto-Negotiation Link Partner Ability Register */
#define MII_ANER            0x06    /* Auto-Negotiation Expansion Register */

/* Basic Mode Control Register (BMCR) bits */
#define BMCR_RESET          0x8000  /* Software reset */
#define BMCR_LOOPBACK       0x4000  /* Enable loopback mode */
#define BMCR_SPEED100       0x2000  /* Select 100Mbps */
#define BMCR_ANENABLE       0x1000  /* Enable auto-negotiation */
#define BMCR_POWERDOWN      0x0800  /* Power down PHY */
#define BMCR_ISOLATE        0x0400  /* Electrically isolate PHY */
#define BMCR_ANRESTART      0x0200  /* Restart auto-negotiation */
#define BMCR_FULLDPLX       0x0100  /* Full duplex */
#define BMCR_CTST           0x0080  /* Collision test */

/* Basic Mode Status Register (BMSR) bits */
#define BMSR_100FULL        0x4000  /* 100Base-TX full duplex capable */
#define BMSR_100HALF        0x2000  /* 100Base-TX half duplex capable */
#define BMSR_10FULL         0x1000  /* 10Base-T full duplex capable */
#define BMSR_10HALF         0x0800  /* 10Base-T half duplex capable */
#define BMSR_ERCAP          0x0001  /* Extended register capability */
#define BMSR_ANEGCOMPLETE   0x0020  /* Auto-negotiation complete */
#define BMSR_RFAULT         0x0010  /* Remote fault detected */
#define BMSR_ANEGCAPABLE    0x0008  /* Auto-negotiation capable */
#define BMSR_LSTATUS        0x0004  /* Link status (latched) */
#define BMSR_JABBERDETECT   0x0002  /* Jabber detected */

/* Auto-Negotiation Advertisement Register (ANAR) bits */
#define ANAR_NP             0x8000  /* Next page */
#define ANAR_RF             0x2000  /* Remote fault */
#define ANAR_PAUSE_ASYM     0x0800  /* Asymmetric pause */
#define ANAR_PAUSE          0x0400  /* Symmetric pause */
#define ANAR_100FULL        0x0100  /* 100Base-TX full duplex */
#define ANAR_100HALF        0x0080  /* 100Base-TX half duplex */
#define ANAR_10FULL         0x0040  /* 10Base-T full duplex */
#define ANAR_10HALF         0x0020  /* 10Base-T half duplex */
#define ANAR_CSMA           0x0001  /* Supports IEEE 802.3 CSMA/CD */

/* Auto-Negotiation Link Partner Ability Register (ANLPAR) bits */
#define ANLPAR_NP           0x8000  /* Next page */
#define ANLPAR_ACK          0x4000  /* Acknowledge */
#define ANLPAR_RF           0x2000  /* Remote fault */
#define ANLPAR_PAUSE_ASYM   0x0800  /* Asymmetric pause */
#define ANLPAR_PAUSE        0x0400  /* Symmetric pause */
#define ANLPAR_100FULL      0x0100  /* 100Base-TX full duplex */
#define ANLPAR_100HALF      0x0080  /* 100Base-TX half duplex */
#define ANLPAR_10FULL       0x0040  /* 10Base-T full duplex */
#define ANLPAR_10HALF       0x0020  /* 10Base-T half duplex */
#define ANLPAR_CSMA         0x0001  /* Supports IEEE 802.3 CSMA/CD */

/* 3C515-TX specific MII management registers (Window 4) */
#define _3C515_MII_CMD      0x0A    /* MII command register */
#define _3C515_MII_DATA     0x0C    /* MII data register */

/* MII Command Register format for 3C515 */
#define MII_CMD_READ        0x2000  /* Read command bit */
#define MII_CMD_WRITE       0x1000  /* Write command bit */
#define MII_CMD_BUSY        0x0800  /* MII busy bit */
#define MII_CMD_PHY_SHIFT   11      /* PHY address bit position */
#define MII_CMD_REG_SHIFT   5       /* Register address bit position */

/* MII timing parameters */
#define MII_POLL_TIMEOUT_US 10000   /* Maximum time to wait for MII ready (10ms) */
#define MII_POLL_DELAY_US   10      /* Delay between polls */
#define MII_RESET_TIMEOUT   500     /* Reset timeout in milliseconds */

/* PHY address constants */
#define PHY_ADDR_MIN        0       /* Minimum PHY address */
#define PHY_ADDR_MAX        31      /* Maximum PHY address */
#define PHY_ADDR_INVALID    0xFF    /* Invalid PHY address marker */

/* PHY ID validation */
#define PHY_ID_VALID(id)    ((id) != 0x0000 && (id) != 0xFFFF)

/* Common PHY vendor IDs (OUI) */
#define PHY_ID_3COM_OUI     0x00A0  /* 3Com OUI (upper 16 bits) */
#define PHY_ID_NATIONAL_OUI 0x0080  /* National Semiconductor OUI */
#define PHY_ID_BROADCOM_OUI 0x0018  /* Broadcom OUI */

#endif /* _MII_H_ */
