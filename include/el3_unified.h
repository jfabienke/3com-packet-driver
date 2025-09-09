#ifndef EL3_UNIFIED_H
#define EL3_UNIFIED_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_EL3_DEVICES 4

enum el3_generation {
    EL3_GEN_VORTEX = 0,
    EL3_GEN_BOOMERANG,
    EL3_GEN_CYCLONE,
    EL3_GEN_TORNADO
};

enum el3_capabilities {
    EL3_CAP_10BASE  = 0x0001,
    EL3_CAP_100BASE = 0x0002,
    EL3_CAP_DMA     = 0x0004,
    EL3_CAP_PM      = 0x0008,
    EL3_CAP_WOL     = 0x0010,
    EL3_CAP_MSI     = 0x0020,
    EL3_CAP_PCIE    = 0x0040
};

struct el3_device {
    uint16_t vendor;
    uint16_t device;
    uint8_t bus;
    uint8_t devfn;
    uint16_t iobase;
    uint8_t irq;
    uint8_t generation;
    uint16_t caps_static;
    uint16_t caps_runtime;
    char name[32];
    
    void *dma_tx_ring;
    void *dma_rx_ring;
    uint16_t dma_tx_phys;
    uint16_t dma_rx_phys;
    uint8_t tx_head;
    uint8_t tx_tail;
    uint8_t rx_head;
    
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_errors;
    uint32_t rx_errors;
};

struct el3_device_info {
    uint16_t vendor;
    uint16_t device;
    const char *name;
    uint8_t generation;
    uint16_t capabilities;
};

int el3_unified_init(void);
struct el3_device* el3_get_device(uint8_t index);
uint8_t el3_get_device_count(void);

int el3_init_dma(struct el3_device *dev);
int el3_transmit_pio(struct el3_device *dev, const void *data, uint16_t len);
int el3_transmit_dma(struct el3_device *dev, const void *data, uint16_t len);
int el3_receive_pio(struct el3_device *dev, void *buffer, uint16_t *len);
int el3_receive_dma(struct el3_device *dev, void *buffer, uint16_t *len);

void el3_install_smc_hooks(struct el3_device *dev);

#endif