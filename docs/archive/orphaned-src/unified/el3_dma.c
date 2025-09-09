#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "el3_unified.h"
#include "hardware.h"

#define DMA_RING_SIZE 16
#define DMA_BUFFER_SIZE 1536
#define DMA_ALIGN 16

struct dma_descriptor {
    uint32_t next;
    uint32_t status;
    uint32_t addr;
    uint32_t length;
};

static uint8_t* dma_alloc_64k_safe(uint16_t size)
{
    uint8_t *buffer;
    uint32_t phys_addr;
    uint16_t seg, off;
    
    size = (size + 15) & ~15;
    
    buffer = (uint8_t *)malloc(size + 65536);
    if (!buffer)
        return NULL;
    
    seg = FP_SEG(buffer);
    off = FP_OFF(buffer);
    phys_addr = ((uint32_t)seg << 4) + off;
    
    if ((phys_addr & 0xFFFF) + size > 0x10000) {
        uint32_t next_64k = (phys_addr + 0xFFFF) & ~0xFFFF;
        uint32_t adjustment = next_64k - phys_addr;
        
        buffer = (uint8_t *)MK_FP(seg + (adjustment >> 4), 0);
        phys_addr = ((uint32_t)FP_SEG(buffer) << 4) + FP_OFF(buffer);
    }
    
    if ((phys_addr & 0xFFFF) + size > 0x10000) {
        free(buffer);
        return NULL;
    }
    
    return buffer;
}

static uint32_t virt_to_phys(void *ptr)
{
    return ((uint32_t)FP_SEG(ptr) << 4) + FP_OFF(ptr);
}

int el3_init_dma(struct el3_device *dev)
{
    struct dma_descriptor *tx_ring, *rx_ring;
    uint8_t *tx_buffers, *rx_buffers;
    uint32_t tx_phys, rx_phys;
    int i;
    
    if (!(dev->caps_runtime & EL3_CAP_DMA))
        return -1;
    
    tx_ring = (struct dma_descriptor *)dma_alloc_64k_safe(
        sizeof(struct dma_descriptor) * DMA_RING_SIZE);
    if (!tx_ring)
        return -1;
    
    rx_ring = (struct dma_descriptor *)dma_alloc_64k_safe(
        sizeof(struct dma_descriptor) * DMA_RING_SIZE);
    if (!rx_ring) {
        free(tx_ring);
        return -1;
    }
    
    tx_buffers = dma_alloc_64k_safe(DMA_BUFFER_SIZE * DMA_RING_SIZE);
    if (!tx_buffers) {
        free(tx_ring);
        free(rx_ring);
        return -1;
    }
    
    rx_buffers = dma_alloc_64k_safe(DMA_BUFFER_SIZE * DMA_RING_SIZE);
    if (!rx_buffers) {
        free(tx_ring);
        free(rx_ring);
        free(tx_buffers);
        return -1;
    }
    
    memset(tx_ring, 0, sizeof(struct dma_descriptor) * DMA_RING_SIZE);
    memset(rx_ring, 0, sizeof(struct dma_descriptor) * DMA_RING_SIZE);
    
    tx_phys = virt_to_phys(tx_ring);
    rx_phys = virt_to_phys(rx_ring);
    
    for (i = 0; i < DMA_RING_SIZE; i++) {
        tx_ring[i].next = tx_phys + ((i + 1) % DMA_RING_SIZE) * sizeof(struct dma_descriptor);
        tx_ring[i].addr = virt_to_phys(tx_buffers + i * DMA_BUFFER_SIZE);
        tx_ring[i].length = 0;
        tx_ring[i].status = 0;
        
        rx_ring[i].next = rx_phys + ((i + 1) % DMA_RING_SIZE) * sizeof(struct dma_descriptor);
        rx_ring[i].addr = virt_to_phys(rx_buffers + i * DMA_BUFFER_SIZE);
        rx_ring[i].length = DMA_BUFFER_SIZE;
        rx_ring[i].status = 0x80000000;
    }
    
    dev->dma_tx_ring = tx_ring;
    dev->dma_rx_ring = rx_ring;
    dev->dma_tx_phys = tx_phys & 0xFFFF;
    dev->dma_rx_phys = rx_phys & 0xFFFF;
    dev->tx_head = 0;
    dev->tx_tail = 0;
    dev->rx_head = 0;
    
    outl(tx_phys, dev->iobase + 0x24);
    outl(rx_phys, dev->iobase + 0x20);
    
    outw(0x0082, dev->iobase + 0x0E);
    outw(0x00C3, dev->iobase + 0x0E);
    
    return 0;
}

int el3_transmit_dma(struct el3_device *dev, const void *data, uint16_t len)
{
    struct dma_descriptor *tx_ring = (struct dma_descriptor *)dev->dma_tx_ring;
    struct dma_descriptor *desc;
    uint8_t next_head;
    
    if (!(dev->caps_runtime & EL3_CAP_DMA))
        return -1;
    
    if (len > DMA_BUFFER_SIZE)
        return -1;
    
    next_head = (dev->tx_head + 1) % DMA_RING_SIZE;
    if (next_head == dev->tx_tail)
        return -1;
    
    desc = &tx_ring[dev->tx_head];
    
    memcpy((void *)(desc->addr), data, len);
    
    desc->length = 0x80000000 | len;
    desc->status = 0;
    
    dev->tx_head = next_head;
    
    outw(0x00CA, dev->iobase + 0x0E);
    
    dev->tx_packets++;
    
    return 0;
}

int el3_receive_dma(struct el3_device *dev, void *buffer, uint16_t *len)
{
    struct dma_descriptor *rx_ring = (struct dma_descriptor *)dev->dma_rx_ring;
    struct dma_descriptor *desc;
    uint16_t pkt_len;
    
    if (!(dev->caps_runtime & EL3_CAP_DMA))
        return -1;
    
    desc = &rx_ring[dev->rx_head];
    
    if (desc->status & 0x80000000)
        return -1;
    
    if (!(desc->status & 0x00008000)) {
        dev->rx_errors++;
        desc->status = 0x80000000;
        dev->rx_head = (dev->rx_head + 1) % DMA_RING_SIZE;
        return -1;
    }
    
    pkt_len = (desc->status >> 16) & 0x1FFF;
    if (pkt_len > DMA_BUFFER_SIZE) {
        dev->rx_errors++;
        desc->status = 0x80000000;
        dev->rx_head = (dev->rx_head + 1) % DMA_RING_SIZE;
        return -1;
    }
    
    memcpy(buffer, (void *)(desc->addr), pkt_len);
    *len = pkt_len;
    
    desc->status = 0x80000000;
    dev->rx_head = (dev->rx_head + 1) % DMA_RING_SIZE;
    
    dev->rx_packets++;
    
    return 0;
}