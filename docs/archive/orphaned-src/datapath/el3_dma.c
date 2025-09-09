/**
 * @file el3_dma.c
 * @brief DMA Datapath Implementation for 3Com EtherLink III
 *
 * High-performance DMA ring buffer management for 3C515-TX ISA and
 * Boomerang+ PCI cards. This is HOT PATH code - direct I/O only!
 *
 * This file is part of the 3Com Packet Driver project.
 */

#define EL3_DATAPATH_COMPILATION  /* Prevent HAL inclusion */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include "../core/el3_core.h"
#include "el3_datapath.h"
#include "../../include/logging.h"

/* DMA register offsets - Boomerang+ */
#define DMA_CTRL        0x20    /* DMA control */
#define DN_LIST_PTR     0x24    /* Download (TX) list pointer */
#define DN_BURST_THRESH 0x2C    /* Download burst threshold */
#define DN_PRIORITY     0x2F    /* Download priority threshold */
#define UP_LIST_PTR     0x38    /* Upload (RX) list pointer */
#define UP_BURST_THRESH 0x3C    /* Upload burst threshold */
#define UP_PRIORITY     0x3D    /* Upload priority threshold */

/* DMA control register bits */
#define DMA_DN_IN_PROG  0x00000080  /* Download in progress */
#define DMA_UP_COMPLETE 0x00008000  /* Upload complete */
#define DMA_DN_COMPLETE 0x00010000  /* Download complete */
#define DMA_UP_RX_EARLY 0x00020000  /* Upload RX early */
#define DMA_ARM_COUNTDN 0x00040000  /* Arm countdown */
#define DMA_DN_STALLED  0x00080000  /* Download stalled */
#define DMA_UP_STALLED  0x00100000  /* Upload stalled */
#define DMA_DEFEAT_MWI  0x00200000  /* Defeat MWI (Memory Write Invalidate) */
#define DMA_DEFEAT_MRL  0x00400000  /* Defeat MRL (Memory Read Line) */
#define DMA_DEFEAT_MRM  0x00800000  /* Defeat MRM (Memory Read Multiple) */

/* Command register commands */
#define CMD_REG         0x0E
#define CMD_DN_STALL    (2<<11) | 0x0002
#define CMD_DN_UNSTALL  (2<<11) | 0x0003
#define CMD_UP_STALL    (2<<11) | 0x0000
#define CMD_UP_UNSTALL  (2<<11) | 0x0001
#define CMD_TX_ENABLE   (9<<11)
#define CMD_RX_ENABLE   (4<<11)
#define CMD_TX_RESET    (11<<11)
#define CMD_RX_RESET    (5<<11)
#define CMD_ACK_INT     (13<<11)
#define CMD_INT_ENABLE  (14<<11)

/* Status register */
#define STATUS_REG      0x0E
#define S_DN_COMPLETE   0x0200
#define S_UP_COMPLETE   0x0400

/* Descriptor status/length fields */
#define DESC_LEN_MASK   0x00001FFF  /* Length mask (13 bits) */
#define DESC_LAST_FRAG  0x80000000  /* Last fragment flag */
#define DESC_DN_COMPLETE 0x00010000 /* Download complete */
#define DESC_UP_COMPLETE 0x00008000 /* Upload complete */
#define DESC_UP_ERROR   0x00004000  /* Upload error */
#define DESC_DN_INDICATE 0x80000000 /* Download indicate (generate interrupt) */

/* Ring sizes - must be power of 2 */
#define TX_RING_SIZE    16
#define RX_RING_SIZE    32
#define TX_RING_MASK    (TX_RING_SIZE - 1)
#define RX_RING_MASK    (RX_RING_SIZE - 1)

/* Buffer sizes */
#define PKT_BUF_SIZE    1536
#define DMA_ALIGN       16      /* Descriptor alignment requirement */

/* ISA DMA channels for 3C515-TX */
#define ISA_DMA_TX_CHANNEL  5   /* Typical TX DMA channel */
#define ISA_DMA_RX_CHANNEL  6   /* Typical RX DMA channel */

/* Boomerang descriptor format */
struct boom_desc {
    uint32_t next;          /* Physical address of next descriptor */
    uint32_t status;        /* Status and packet length */
    uint32_t addr;          /* Physical address of buffer */
    uint32_t length;        /* Buffer length and flags */
};

/* Per-device DMA state */
struct dma_state {
    uint16_t io_base;           /* Pre-cached I/O base */
    
    /* TX ring */
    struct boom_desc *tx_ring;  /* Virtual address */
    uint32_t tx_ring_phys;      /* Physical address */
    uint8_t *tx_buffers[TX_RING_SIZE];
    uint16_t cur_tx;            /* Next descriptor to use */
    uint16_t dirty_tx;          /* First descriptor to clean */
    uint16_t tx_free;           /* Free descriptors */
    
    /* RX ring */
    struct boom_desc *rx_ring;  /* Virtual address */
    uint32_t rx_ring_phys;      /* Physical address */
    uint8_t *rx_buffers[RX_RING_SIZE];
    uint16_t cur_rx;            /* Next descriptor to check */
    
    /* DMA mode */
    uint8_t is_isa_dma;         /* ISA DMA vs PCI bus master */
    uint8_t tx_dma_channel;
    uint8_t rx_dma_channel;
};

static struct dma_state g_dma_state[MAX_EL3_DEVICES];

/* Forward declarations */
static int el3_dma_alloc_rings(struct el3_dev *dev, struct dma_state *ds);
static void el3_dma_free_rings(struct dma_state *ds);
static uint32_t virt_to_phys(void *ptr);
static void el3_dma_setup_isa(struct dma_state *ds);
static void el3_dma_kick_tx(uint16_t io_base);
static void el3_dma_kick_rx(uint16_t io_base);

/**
 * @brief Initialize DMA datapath
 */
int el3_dma_init(struct el3_dev *dev)
{
    struct dma_state *ds;
    int index = el3_get_device_count() - 1;
    int i;
    
    if (index < 0 || index >= MAX_EL3_DEVICES) {
        return -EINVAL;
    }
    
    ds = &g_dma_state[index];
    memset(ds, 0, sizeof(struct dma_state));
    
    /* Pre-cache I/O base */
    ds->io_base = dev->io_base;
    
    /* Determine DMA mode */
    if (dev->generation == EL3_GEN_3C515) {
        ds->is_isa_dma = 1;
        ds->tx_dma_channel = ISA_DMA_TX_CHANNEL;
        ds->rx_dma_channel = ISA_DMA_RX_CHANNEL;
    } else {
        ds->is_isa_dma = 0;  /* PCI bus master */
    }
    
    /* Allocate descriptor rings and buffers */
    if (el3_dma_alloc_rings(dev, ds) < 0) {
        LOG_ERROR("EL3-DMA: Failed to allocate rings");
        return -ENOMEM;
    }
    
    /* Initialize TX ring */
    for (i = 0; i < TX_RING_SIZE; i++) {
        ds->tx_ring[i].next = virt_to_phys(&ds->tx_ring[(i + 1) & TX_RING_MASK]);
        ds->tx_ring[i].status = 0;
        ds->tx_ring[i].addr = 0;
        ds->tx_ring[i].length = 0;
    }
    ds->cur_tx = 0;
    ds->dirty_tx = 0;
    ds->tx_free = TX_RING_SIZE;
    
    /* Initialize RX ring */
    for (i = 0; i < RX_RING_SIZE; i++) {
        ds->rx_ring[i].next = virt_to_phys(&ds->rx_ring[(i + 1) & RX_RING_MASK]);
        ds->rx_ring[i].status = 0;
        ds->rx_ring[i].addr = virt_to_phys(ds->rx_buffers[i]);
        ds->rx_ring[i].length = PKT_BUF_SIZE | DESC_LAST_FRAG;
    }
    ds->cur_rx = 0;
    
    /* Set up ISA DMA if needed */
    if (ds->is_isa_dma) {
        el3_dma_setup_isa(ds);
    }
    
    /* Program ring pointers */
    outportl(ds->io_base + DN_LIST_PTR, ds->tx_ring_phys);
    outportl(ds->io_base + UP_LIST_PTR, ds->rx_ring_phys);
    
    /* Set burst thresholds */
    outportb(ds->io_base + DN_BURST_THRESH, 0x40);  /* 256 bytes */
    outportb(ds->io_base + UP_BURST_THRESH, 0x40);  /* 256 bytes */
    
    /* Enable DMA */
    outportw(ds->io_base + CMD_REG, CMD_TX_ENABLE);
    outportw(ds->io_base + CMD_REG, CMD_RX_ENABLE);
    
    /* Start RX DMA */
    el3_dma_kick_rx(ds->io_base);
    
    LOG_INFO("EL3-DMA: Initialized %s mode, TX: %d, RX: %d descriptors",
             ds->is_isa_dma ? "ISA" : "PCI", TX_RING_SIZE, RX_RING_SIZE);
    
    /* Store ring pointers in device structure */
    dev->tx_ring = ds->tx_ring;
    dev->rx_ring = ds->rx_ring;
    dev->tx_ring_phys = ds->tx_ring_phys;
    dev->rx_ring_phys = ds->rx_ring_phys;
    
    return 0;
}

/**
 * @brief Transmit packet using DMA
 *
 * HOT PATH - Optimized for speed!
 */
int el3_dma_xmit(struct el3_dev *dev, struct packet *pkt)
{
    struct dma_state *ds;
    struct boom_desc *desc;
    uint16_t io_base;
    uint16_t len;
    int index = 0;  /* TODO: Get device index */
    
    ds = &g_dma_state[index];
    io_base = ds->io_base;
    len = pkt->length;
    
    /* Check for free descriptors */
    if (ds->tx_free == 0) {
        /* Try to clean completed descriptors */
        el3_dma_tx_clean(dev);
        if (ds->tx_free == 0) {
            dev->stats.tx_dropped++;
            return -ENOSPC;
        }
    }
    
    /* Get current descriptor */
    desc = &ds->tx_ring[ds->cur_tx];
    
    /* Copy packet to DMA buffer */
    memcpy(ds->tx_buffers[ds->cur_tx], pkt->data, len);
    
    /* Pad short packets */
    if (len < 60) {
        memset(ds->tx_buffers[ds->cur_tx] + len, 0, 60 - len);
        len = 60;
    }
    
    /* Set up descriptor */
    desc->addr = virt_to_phys(ds->tx_buffers[ds->cur_tx]);
    desc->length = len | DESC_LAST_FRAG;
    desc->status = len;  /* Length in lower 13 bits */
    
    /* Memory barrier - ensure descriptor is written */
    __asm { }
    
    /* Give ownership to NIC */
    desc->status |= DESC_DN_INDICATE;
    
    /* Advance to next descriptor */
    ds->cur_tx = (ds->cur_tx + 1) & TX_RING_MASK;
    ds->tx_free--;
    
    /* Kick DMA engine */
    el3_dma_kick_tx(io_base);
    
    /* Update statistics */
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += len;
    
    return 0;
}

/**
 * @brief Receive packets using DMA
 *
 * HOT PATH - Process completed RX descriptors
 */
int el3_dma_rx_poll(struct el3_dev *dev)
{
    struct dma_state *ds;
    struct boom_desc *desc;
    uint16_t pkt_len;
    int packets = 0;
    int index = 0;  /* TODO: Get device index */
    
    ds = &g_dma_state[index];
    
    while (packets < 16) {  /* Limit per poll */
        desc = &ds->rx_ring[ds->cur_rx];
        
        /* Check if descriptor is complete */
        if (!(desc->status & DESC_UP_COMPLETE)) {
            break;
        }
        
        /* Extract packet length */
        pkt_len = desc->status & DESC_LEN_MASK;
        
        /* Check for errors */
        if (desc->status & DESC_UP_ERROR) {
            dev->stats.rx_errors++;
        } else if (pkt_len >= 14 && pkt_len <= 1514) {
            /* Valid packet - pass to network stack */
            /* TODO: Call packet handler */
            /* packet_receive(ds->rx_buffers[ds->cur_rx], pkt_len); */
            
            dev->stats.rx_packets++;
            dev->stats.rx_bytes += pkt_len;
        } else {
            dev->stats.rx_length_errors++;
        }
        
        /* Return descriptor to NIC */
        desc->status = 0;
        desc->length = PKT_BUF_SIZE | DESC_LAST_FRAG;
        
        /* Move to next descriptor */
        ds->cur_rx = (ds->cur_rx + 1) & RX_RING_MASK;
        packets++;
    }
    
    /* Restart RX DMA if stalled */
    if (packets > 0) {
        el3_dma_kick_rx(ds->io_base);
    }
    
    return packets;
}

/**
 * @brief DMA interrupt service routine
 *
 * HOT PATH - Handle DMA interrupts
 */
void el3_dma_isr(struct el3_dev *dev)
{
    struct dma_state *ds;
    uint16_t io_base;
    uint16_t status;
    uint32_t dma_ctrl;
    int index = 0;  /* TODO: Get device index */
    
    ds = &g_dma_state[index];
    io_base = ds->io_base;
    
    /* Read interrupt status */
    status = inportw(io_base + STATUS_REG);
    dma_ctrl = inportl(io_base + DMA_CTRL);
    
    /* Handle download (TX) complete */
    if (dma_ctrl & DMA_DN_COMPLETE) {
        el3_dma_tx_clean(dev);
    }
    
    /* Handle upload (RX) complete */
    if (dma_ctrl & DMA_UP_COMPLETE) {
        el3_dma_rx_poll(dev);
    }
    
    /* Check for DMA stalls */
    if (dma_ctrl & DMA_DN_STALLED) {
        /* TX stalled - try to restart */
        el3_dma_kick_tx(io_base);
    }
    
    if (dma_ctrl & DMA_UP_STALLED) {
        /* RX stalled - try to restart */
        el3_dma_kick_rx(io_base);
    }
    
    /* Acknowledge interrupts */
    outportw(io_base + CMD_REG, CMD_ACK_INT | 0xFF);
}

/**
 * @brief Clean completed TX descriptors
 */
void el3_dma_tx_clean(struct el3_dev *dev)
{
    struct dma_state *ds;
    struct boom_desc *desc;
    int index = 0;  /* TODO: Get device index */
    
    ds = &g_dma_state[index];
    
    while (ds->dirty_tx != ds->cur_tx) {
        desc = &ds->tx_ring[ds->dirty_tx];
        
        /* Check if descriptor is complete */
        if (!(desc->status & DESC_DN_COMPLETE)) {
            break;
        }
        
        /* Clear descriptor */
        desc->status = 0;
        desc->addr = 0;
        desc->length = 0;
        
        /* Move to next descriptor */
        ds->dirty_tx = (ds->dirty_tx + 1) & TX_RING_MASK;
        ds->tx_free++;
    }
}

/**
 * @brief Allocate DMA rings and buffers
 */
static int el3_dma_alloc_rings(struct el3_dev *dev, struct dma_state *ds)
{
    int i;
    
    /* Allocate TX ring - must be aligned */
    ds->tx_ring = (struct boom_desc *)calloc(TX_RING_SIZE, sizeof(struct boom_desc));
    if (!ds->tx_ring) {
        return -ENOMEM;
    }
    ds->tx_ring_phys = virt_to_phys(ds->tx_ring);
    
    /* Allocate RX ring */
    ds->rx_ring = (struct boom_desc *)calloc(RX_RING_SIZE, sizeof(struct boom_desc));
    if (!ds->rx_ring) {
        free(ds->tx_ring);
        return -ENOMEM;
    }
    ds->rx_ring_phys = virt_to_phys(ds->rx_ring);
    
    /* Allocate TX buffers */
    for (i = 0; i < TX_RING_SIZE; i++) {
        ds->tx_buffers[i] = (uint8_t *)malloc(PKT_BUF_SIZE);
        if (!ds->tx_buffers[i]) {
            el3_dma_free_rings(ds);
            return -ENOMEM;
        }
    }
    
    /* Allocate RX buffers */
    for (i = 0; i < RX_RING_SIZE; i++) {
        ds->rx_buffers[i] = (uint8_t *)malloc(PKT_BUF_SIZE);
        if (!ds->rx_buffers[i]) {
            el3_dma_free_rings(ds);
            return -ENOMEM;
        }
    }
    
    return 0;
}

/**
 * @brief Free DMA rings and buffers
 */
static void el3_dma_free_rings(struct dma_state *ds)
{
    int i;
    
    for (i = 0; i < TX_RING_SIZE; i++) {
        if (ds->tx_buffers[i]) {
            free(ds->tx_buffers[i]);
        }
    }
    
    for (i = 0; i < RX_RING_SIZE; i++) {
        if (ds->rx_buffers[i]) {
            free(ds->rx_buffers[i]);
        }
    }
    
    if (ds->tx_ring) {
        free(ds->tx_ring);
    }
    
    if (ds->rx_ring) {
        free(ds->rx_ring);
    }
}

/**
 * @brief Convert virtual to physical address
 */
static uint32_t virt_to_phys(void *ptr)
{
    uint32_t seg = FP_SEG(ptr);
    uint32_t off = FP_OFF(ptr);
    return (seg << 4) + off;
}

/**
 * @brief Set up ISA DMA for 3C515-TX
 */
static void el3_dma_setup_isa(struct dma_state *ds)
{
    /* ISA DMA setup would go here */
    /* This involves programming the 8237 DMA controller */
    /* For now, this is a placeholder */
    LOG_DEBUG("EL3-DMA: ISA DMA setup for channels %d/%d",
              ds->tx_dma_channel, ds->rx_dma_channel);
}

/**
 * @brief Kick TX DMA engine
 */
static void el3_dma_kick_tx(uint16_t io_base)
{
    /* Unstall download engine */
    outportw(io_base + CMD_REG, CMD_DN_UNSTALL);
}

/**
 * @brief Kick RX DMA engine
 */
static void el3_dma_kick_rx(uint16_t io_base)
{
    /* Unstall upload engine */
    outportw(io_base + CMD_REG, CMD_UP_UNSTALL);
}

/**
 * @brief Helper to write 32-bit value (for DOS)
 */
static void outportl(uint16_t port, uint32_t value)
{
    outportw(port, value & 0xFFFF);
    outportw(port + 2, (value >> 16) & 0xFFFF);
}

/**
 * @brief Helper to read 32-bit value (for DOS)
 */
static uint32_t inportl(uint16_t port)
{
    uint32_t low = inportw(port);
    uint32_t high = inportw(port + 2);
    return (high << 16) | low;
}