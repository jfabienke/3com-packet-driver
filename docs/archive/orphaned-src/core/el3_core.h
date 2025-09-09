/**
 * @file el3_core.h
 * @brief Unified 3Com EtherLink III Core Driver Header
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _EL3_CORE_H_
#define _EL3_CORE_H_

#include <stdint.h>
#include <stdbool.h>

/* Maximum devices supported */
#define MAX_EL3_DEVICES     4

/* Error codes */
#define EINVAL      1
#define ENODEV      2
#define ENOMEM      3
#define ETIMEDOUT   4
#define EIO         5
#define ENOSPC      6

/* Device generations */
enum el3_generation {
    EL3_GEN_UNKNOWN = 0,
    EL3_GEN_3C509B,      /* ISA EtherLink III */
    EL3_GEN_3C515,       /* ISA Fast EtherLink */
    EL3_GEN_VORTEX,      /* PCI 3C59x */
    EL3_GEN_BOOMERANG,   /* PCI 3C90x */
    EL3_GEN_CYCLONE,     /* PCI 3C905B */
    EL3_GEN_TORNADO      /* PCI 3C905C */
};

/* RX mode flags */
#define RX_MODE_PROMISC     0x01
#define RX_MODE_BROADCAST   0x02
#define RX_MODE_MULTICAST   0x04
#define RX_MODE_ALL_MULTI   0x08

/* Common register offsets */
#define EL3_CMD             0x0E
#define EL3_STATUS          0x0E

/* Forward declarations */
struct el3_dev;
struct packet;

/* Generation-specific operations */
struct el3_ops {
    int (*reset)(struct el3_dev *dev);
    int (*init_phy)(struct el3_dev *dev);
    int (*get_link)(struct el3_dev *dev);
};

/* Device capabilities structure */
struct el3_caps {
    /* Basic capabilities */
    bool has_bus_master;
    bool has_permanent_window1;
    bool has_stats_window;
    bool has_flow_control;
    bool has_large_packets;
    bool has_nway;
    
    /* Advanced features */
    bool has_hw_checksum;
    bool has_vlan_support;
    bool has_wake_on_lan;
    
    /* Hardware parameters */
    uint16_t fifo_size;
    uint16_t rx_filter_mask;
    uint16_t interrupt_mask;
    uint16_t flags;
};

/* Network statistics */
struct el3_stats {
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t tx_dropped;
    uint32_t rx_dropped;
    uint32_t multicast;
    uint32_t collisions;
    
    /* Detailed errors */
    uint32_t rx_length_errors;
    uint32_t rx_over_errors;
    uint32_t rx_crc_errors;
    uint32_t rx_frame_errors;
    uint32_t rx_fifo_errors;
    uint32_t rx_missed_errors;
    
    uint32_t tx_aborted_errors;
    uint32_t tx_carrier_errors;
    uint32_t tx_fifo_errors;
    uint32_t tx_heartbeat_errors;
    uint32_t tx_window_errors;
};

/* Main device structure */
struct el3_dev {
    /* Device identification */
    char name[32];
    uint16_t vendor_id;
    uint16_t device_id;
    enum el3_generation generation;
    
    /* Hardware resources */
    uint16_t io_base;
    uint32_t mem_base;  /* For MMIO cards */
    uint8_t irq;
    bool io_mapped;     /* true = I/O, false = MMIO */
    
    /* Capabilities */
    struct el3_caps caps;
    
    /* MAC address */
    uint8_t mac_addr[6];
    
    /* Operating state */
    bool initialized;
    bool running;
    uint8_t current_window;
    uint16_t rx_mode;
    uint16_t interrupt_mask;
    
    /* Statistics */
    struct el3_stats stats;
    
    /* Generation-specific operations */
    const struct el3_ops *ops;
    
    /* Datapath operations (set during init) */
    int (*start_xmit)(struct el3_dev *dev, struct packet *pkt);
    int (*rx_poll)(struct el3_dev *dev);
    void (*isr)(struct el3_dev *dev);
    
    /* DMA structures (if bus master) */
    void *tx_ring;
    void *rx_ring;
    uint32_t tx_ring_phys;
    uint32_t rx_ring_phys;
    uint16_t cur_tx;
    uint16_t dirty_tx;
    uint16_t cur_rx;
    
    /* Private data for bus probers */
    void *priv;
};

/* Core driver functions */
int el3_init(struct el3_dev *dev);
int el3_start(struct el3_dev *dev);
int el3_stop(struct el3_dev *dev);
int el3_set_rx_mode(struct el3_dev *dev);
void el3_update_statistics(struct el3_dev *dev);
void el3_set_interrupt_mask(struct el3_dev *dev, uint16_t mask);

/* Device management */
struct el3_dev *el3_get_device(int index);
int el3_get_device_count(void);
const char *el3_generation_name(enum el3_generation gen);

/* From el3_caps.c */
int el3_detect_capabilities(struct el3_dev *dev);
int el3_read_mac_address(struct el3_dev *dev);

/* From el3_hal.c */
uint8_t el3_read8(struct el3_dev *dev, uint16_t offset);
uint16_t el3_read16(struct el3_dev *dev, uint16_t offset);
uint32_t el3_read32(struct el3_dev *dev, uint16_t offset);
void el3_write8(struct el3_dev *dev, uint16_t offset, uint8_t value);
void el3_write16(struct el3_dev *dev, uint16_t offset, uint16_t value);
void el3_write32(struct el3_dev *dev, uint16_t offset, uint32_t value);
void el3_select_window(struct el3_dev *dev, uint8_t window);
void el3_issue_command(struct el3_dev *dev, uint16_t cmd);

/* From datapath modules */
int el3_pio_init(struct el3_dev *dev);
int el3_pio_xmit(struct el3_dev *dev, struct packet *pkt);
int el3_pio_rx_poll(struct el3_dev *dev);
void el3_pio_isr(struct el3_dev *dev);

int el3_dma_init(struct el3_dev *dev);
int el3_dma_xmit(struct el3_dev *dev, struct packet *pkt);
int el3_dma_rx_poll(struct el3_dev *dev);
void el3_dma_isr(struct el3_dev *dev);

/* Utility functions */
void delay_us(unsigned int us);
void delay_ms(unsigned int ms);

/* SMC statistics */
struct el3_smc_stats {
    int patches_applied;
    int code_bytes_modified;
    int cycles_saved_per_packet;
};

/* SMC optimizer functions */
int el3_smc_init(struct el3_dev *dev);
int el3_smc_restore(void);
void el3_smc_get_stats(struct el3_smc_stats *stats);

/* Missing generation-specific ops selection */
void el3_select_generation_ops(struct el3_dev *dev);
const char *el3_get_generation_name(enum el3_generation gen);

/* Bus prober entry points */
int el3_isa_probe(void);
int el3_pci_probe(void);

#endif /* _EL3_CORE_H_ */