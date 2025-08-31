/**
 * @file 3com_pci.h
 * @brief 3Com PCI/CardBus NIC generation definitions and structures
 *
 * This file defines the generation flags, capabilities, and structures for
 * supporting all 3Com PCI network controllers from Vortex through Tornado.
 * Based on Donald Becker's Linux 3c59x driver architecture.
 *
 * 3Com Packet Driver - Support for 3C590/3C595/3C900/3C905 families
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _3COM_PCI_H_
#define _3COM_PCI_H_

#include <stdint.h>
#include "common.h"
#include "nic_init.h"

/* Generation identification flags */
#define IS_VORTEX       0x01    /* 3c59x Vortex - PIO only */
#define IS_BOOMERANG    0x02    /* 3c900/3c905 Boomerang - Bus master DMA */
#define IS_CYCLONE      0x04    /* 3c905B Cyclone - Enhanced DMA */
#define IS_TORNADO      0x08    /* 3c905C Tornado - Advanced features */

/* Capability flags */
#define HAS_PWR_CTRL    0x0020  /* Power management control */
#define HAS_MII         0x0040  /* MII transceiver support */
#define HAS_NWAY        0x0080  /* Auto-negotiation support */
#define HAS_CB_FNS      0x0100  /* CardBus specific functions */
#define INVERT_MII_PWR  0x0200  /* Invert MII power bit */
#define INVERT_LED_PWR  0x0400  /* Invert LED power bit */
#define MAX_COLL_RESET  0x0800  /* Needs max collision reset */
#define EEPROM_OFFSET   0x1000  /* EEPROM address offset */
#define HAS_HWCKSM      0x2000  /* Hardware checksum capability */
#define WNO_XCVR_PWR    0x4000  /* Window 0 transceiver power */
#define EXTRA_PREAMBLE  0x8000  /* Needs extra MII preamble */
#define EEPROM_8BIT     0x0010  /* 8-bit EEPROM interface */
#define EEPROM_RESET    0x10000 /* Needs EEPROM reset */

/* I/O region sizes by generation */
#define VORTEX_TOTAL_SIZE     0x20  /* 32 bytes for Vortex */
#define BOOMERANG_TOTAL_SIZE  0x40  /* 64 bytes for Boomerang */
#define CYCLONE_TOTAL_SIZE    0x80  /* 128 bytes for Cyclone/Tornado */

/* Window selection */
#define EL3_CMD         0x0E        /* Command register offset */
#define SelectWindow    (1<<11)     /* Window select command */

/* Window definitions */
#define WN0_EEPROM_CMD  0x0A        /* Window 0: EEPROM command */
#define WN0_EEPROM_DATA 0x0C        /* Window 0: EEPROM data */
#define WN2_RESET_OPT   0x0C        /* Window 2: Reset options */
#define WN3_CONFIG      0x00        /* Window 3: Internal config */
#define WN3_MAC_CTRL    0x06        /* Window 3: MAC control */
#define WN3_OPTIONS     0x08        /* Window 3: Options */
#define WN4_MEDIA       0x0A        /* Window 4: Media status */
#define WN4_NET_DIAG    0x06        /* Window 4: Network diagnostics */
#define WN4_FIFO_DIAG   0x04        /* Window 4: FIFO diagnostics */
#define WN4_PHYS_MGMT   0x08        /* Window 4: Physical management */
#define WN6_STATS_BASE  0x00        /* Window 6: Statistics base */
#define WN7_VLAN_TYPE   0x04        /* Window 7: VLAN EtherType */
#define WN7_CONFIG      0x00        /* Window 7: Configuration */

/* Command definitions */
#define TotalReset      (0<<11)     /* Total adapter reset */
#define StartCoax       (2<<11)     /* Start coax transceiver */
#define RxDisable       (3<<11)     /* Disable receiver */
#define RxEnable        (4<<11)     /* Enable receiver */
#define RxReset         (5<<11)     /* Reset receiver */
#define TxDisable       (10<<11)    /* Disable transmitter */
#define TxEnable        (9<<11)     /* Enable transmitter */
#define TxReset         (11<<11)    /* Reset transmitter */
#define AckIntr         (13<<11)    /* Acknowledge interrupt */
#define SetIntrEnb      (14<<11)    /* Set interrupt enable mask */
#define SetRxFilter     (16<<11)    /* Set receive filter */

/* Register offsets common to all generations */
#define TX_FIFO         0x00        /* Transmit FIFO (Vortex) */
#define TX_STATUS       0x1B        /* Transmit status */
#define TX_FREE         0x1C        /* Transmit free bytes */
#define RX_FIFO         0x00        /* Receive FIFO */
#define RX_STATUS       0x18        /* Receive status */
#define DMA_CTRL        0x20        /* DMA control (Boomerang+) */
#define DOWN_LIST_PTR   0x24        /* Download list pointer */
#define UP_LIST_PTR     0x38        /* Upload list pointer */

/* DMA descriptor structures for Boomerang+ */
typedef struct boom_rx_desc {
    uint32_t next;              /* Next descriptor physical address */
    uint32_t status;            /* Status and packet length */
    uint32_t addr;              /* Buffer physical address */
    uint32_t length;            /* Buffer length with flags */
} boom_rx_desc_t;

typedef struct boom_tx_desc {
    uint32_t next;              /* Next descriptor physical address */
    uint32_t status;            /* Status and packet length */
    uint32_t addr;              /* Buffer physical address */
    uint32_t length;            /* Buffer length with flags */
} boom_tx_desc_t;

/* Descriptor flags */
#define LAST_FRAG       0x80000000  /* Last fragment in packet */
#define DN_COMPLETE     0x00010000  /* Download complete */
#define UP_COMPLETE     0x00008000  /* Upload complete */

/* Ring sizes */
#define TX_RING_SIZE    16          /* Must be power of 2 */
#define RX_RING_SIZE    32          /* Must be power of 2 */
#define PKT_BUF_SIZE    1536        /* Size of each RX buffer */

/* Generation info structure */
typedef struct pci_3com_info {
    uint16_t device_id;         /* PCI device ID */
    const char *name;           /* Human-readable name */
    uint8_t generation;         /* IS_VORTEX, IS_BOOMERANG, etc */
    uint16_t capabilities;      /* HAS_MII, HAS_HWCKSM, etc */
    uint8_t io_size;           /* I/O region size (32, 64, or 128) */
} pci_3com_info_t;

/* Extended NIC context for 3Com PCI devices */
typedef struct pci_3com_context {
    /* Base context */
    nic_context_t base;
    
    /* Generation info */
    uint8_t generation;
    uint16_t capabilities;
    uint8_t current_window;
    
    /* DMA structures for Boomerang+ */
    boom_tx_desc_t *tx_ring;
    boom_rx_desc_t *rx_ring;
    uint32_t tx_ring_phys;
    uint32_t rx_ring_phys;
    uint16_t cur_tx;
    uint16_t dirty_tx;
    uint16_t cur_rx;
    
    /* Media and link state */
    uint16_t available_media;
    uint16_t media_status;
    uint8_t full_duplex;
    uint8_t auto_negotiation;
    
    /* Performance counters */
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_errors;
    uint32_t rx_errors;
} pci_3com_context_t;

/* Function prototypes */

/* Window management */
void select_window(uint16_t ioaddr, uint8_t window);
uint8_t window_read8(uint16_t ioaddr, uint8_t window, uint8_t reg);
uint16_t window_read16(uint16_t ioaddr, uint8_t window, uint8_t reg);
uint32_t window_read32(uint16_t ioaddr, uint8_t window, uint8_t reg);
void window_write8(uint16_t ioaddr, uint8_t window, uint8_t reg, uint8_t value);
void window_write16(uint16_t ioaddr, uint8_t window, uint8_t reg, uint16_t value);
void window_write32(uint16_t ioaddr, uint8_t window, uint8_t reg, uint32_t value);

/* Generation detection and initialization */
int detect_3com_generation(uint16_t device_id, pci_generic_info_t *info);
int init_3com_pci(nic_detect_info_t *info);

/* Transmission functions */
int vortex_start_xmit(pci_3com_context_t *ctx, packet_t *pkt);
int boomerang_start_xmit(pci_3com_context_t *ctx, packet_t *pkt);

/* Reception functions */
int vortex_rx(pci_3com_context_t *ctx);
int boomerang_rx(pci_3com_context_t *ctx);

/* Interrupt handling */
int vortex_interrupt(pci_3com_context_t *ctx);
int boomerang_interrupt(pci_3com_context_t *ctx);

/* Media management */
int set_media_type(pci_3com_context_t *ctx, uint8_t media);
int check_link_status(pci_3com_context_t *ctx);

/* EEPROM access */
uint16_t read_eeprom(uint16_t ioaddr, uint8_t offset);
int write_eeprom(uint16_t ioaddr, uint8_t offset, uint16_t value);

/* MII PHY access */
uint16_t mdio_read(uint16_t ioaddr, uint8_t phy_id, uint8_t reg);
void mdio_write(uint16_t ioaddr, uint8_t phy_id, uint8_t reg, uint16_t value);

/* Statistics */
void update_stats(pci_3com_context_t *ctx);
void reset_stats(pci_3com_context_t *ctx);

#endif /* _3COM_PCI_H_ */