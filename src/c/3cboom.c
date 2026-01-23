/**
 * @file 3com_boomerang.c
 * @brief Boomerang/Cyclone/Tornado DMA implementation
 *
 * Implements bus master DMA packet transmission and reception for 3Com
 * Boomerang and later generation NICs. Adapted from BoomTex module.
 *
 * 3Com Packet Driver - Boomerang DMA Implementation
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/packet.h"
#include "../../include/dma.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/cachecoh.h"
#include "../../include/hwchksm.h"
#include <dos.h>
#include <string.h>

/* Boomerang/Cyclone/Tornado Register Offsets (from BoomTex) */
#define BOOM_COMMAND          0x00        /* Command register */
#define BOOM_STATUS           0x02        /* Status register */
#define BOOM_INT_STATUS       0x04        /* Interrupt status */
#define BOOM_INT_ENABLE       0x06        /* Interrupt enable */
#define BOOM_FIFO_DIAG        0x08        /* FIFO diagnostics */
#define BOOM_TIMER            0x0A        /* General purpose timer */
#define BOOM_TX_STATUS        0x0C        /* TX status */
#define BOOM_DMA_CTRL         0x20        /* DMA control */
#define BOOM_DN_LIST_PTR      0x24        /* Downlist pointer (TX) */
#define BOOM_UP_LIST_PTR      0x38        /* Uplist pointer (RX) */

/* Command Values */
#define BOOM_CMD_GLOBAL_RESET 0x0000      /* Global reset */
#define BOOM_CMD_TX_ENABLE    0x4800      /* Enable transmitter */
#define BOOM_CMD_RX_ENABLE    0x2000      /* Enable receiver */
#define BOOM_CMD_TX_RESET     0x5800      /* Reset transmitter */
#define BOOM_CMD_RX_RESET     0x2800      /* Reset receiver */
#define BOOM_CMD_INT_ACK      0x6800      /* Acknowledge interrupt */
#define BOOM_CMD_DN_STALL     0x3002      /* Stall download (TX) */
#define BOOM_CMD_DN_UNSTALL   0x3003      /* Unstall download */
#define BOOM_CMD_UP_STALL     0x3000      /* Stall upload (RX) */
#define BOOM_CMD_UP_UNSTALL   0x3001      /* Unstall upload */

/* Status Bits */
#define BOOM_STAT_INT_LATCH   0x0001      /* Interrupt latch */
#define BOOM_STAT_HOST_ERROR  0x0002      /* Host error */
#define BOOM_STAT_TX_COMPLETE 0x0004      /* TX complete */
#define BOOM_STAT_RX_COMPLETE 0x0010      /* RX complete */
#define BOOM_STAT_CMD_IN_PROG 0x1000      /* Command in progress */

/* DMA Control Bits */
#define BOOM_DMA_DN_COMPLETE  0x00010000  /* Download complete */
#define BOOM_DMA_UP_COMPLETE  0x00020000  /* Upload complete */
#define BOOM_DMA_DN_STALLED   0x00040000  /* Download stalled */
#define BOOM_DMA_UP_STALLED   0x00080000  /* Upload stalled */

/* Descriptor control flags */
#define DESC_DN_COMPLETE      0x00010000  /* Download complete */
#define DESC_ERROR            0x00004000  /* Error occurred */
#define DESC_LAST             0x80000000  /* Last fragment */

/* Ring buffer parameters */
#define BOOM_TX_RING_SIZE     16
#define BOOM_RX_RING_SIZE     16

/**
 * @brief Initialize Boomerang/Cyclone/Tornado generation NIC
 * 
 * Wrapper for boomerang_init_dma to match the vtable interface.
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
/* Forward declaration for init function */
int boomerang_init_dma(pci_3com_context_t *ctx);

int boomerang_init(pci_3com_context_t *ctx)
{
    return boomerang_init_dma(ctx);
}

/**
 * @brief Initialize TX descriptor ring
 */
static int boomerang_init_tx_ring(pci_3com_context_t *ctx)
{
    uint16_t ioaddr = ctx->base.io_base;
    int i;
    
    /* Allocate TX descriptor ring if not already allocated */
    if (!ctx->tx_ring) {
        ctx->tx_ring = (boom_tx_desc_t *)mem_alloc_aligned(
            sizeof(boom_tx_desc_t) * TX_RING_SIZE, 16);
        if (!ctx->tx_ring) {
            LOG_ERROR("Boomerang: Failed to allocate TX ring");
            return ERROR_NO_MEMORY;
        }
    }
    
    /* Initialize descriptors */
    memset(ctx->tx_ring, 0, sizeof(boom_tx_desc_t) * TX_RING_SIZE);
    
    for (i = 0; i < TX_RING_SIZE; i++) {
        /* Link descriptors in a ring */
        ctx->tx_ring[i].next = dma_virt_to_phys(&ctx->tx_ring[(i + 1) % TX_RING_SIZE]);
        ctx->tx_ring[i].status = 0;
        ctx->tx_ring[i].addr = 0;
        ctx->tx_ring[i].length = 0;
    }
    
    /* Initialize indices */
    ctx->cur_tx = 0;
    ctx->dirty_tx = 0;
    
    /* Set TX ring pointer in hardware */
    ctx->tx_ring_phys = dma_virt_to_phys(ctx->tx_ring);
    outl(ioaddr + BOOM_DN_LIST_PTR, ctx->tx_ring_phys);
    
    LOG_DEBUG("Boomerang: TX ring initialized at 0x%08X", ctx->tx_ring_phys);
    
    return SUCCESS;
}

/**
 * @brief Initialize RX descriptor ring
 */
static int boomerang_init_rx_ring(pci_3com_context_t *ctx)
{
    uint16_t ioaddr = ctx->base.io_base;
    int i;
    
    /* Allocate RX descriptor ring if not already allocated */
    if (!ctx->rx_ring) {
        ctx->rx_ring = (boom_rx_desc_t *)mem_alloc_aligned(
            sizeof(boom_rx_desc_t) * RX_RING_SIZE, 16);
        if (!ctx->rx_ring) {
            LOG_ERROR("Boomerang: Failed to allocate RX ring");
            return ERROR_NO_MEMORY;
        }
    }
    
    /* Initialize descriptors */
    memset(ctx->rx_ring, 0, sizeof(boom_rx_desc_t) * RX_RING_SIZE);
    
    for (i = 0; i < RX_RING_SIZE; i++) {
        /* Allocate receive buffer */
        packet_t *pkt = packet_alloc(PKT_BUF_SIZE);
        if (!pkt) {
            LOG_ERROR("Boomerang: Failed to allocate RX buffer %d", i);
            return ERROR_NO_MEMORY;
        }
        
        /* Setup descriptor */
        ctx->rx_ring[i].next = dma_virt_to_phys(&ctx->rx_ring[(i + 1) % RX_RING_SIZE]);
        ctx->rx_ring[i].status = 0;
        ctx->rx_ring[i].addr = dma_virt_to_phys(pkt->data);
        ctx->rx_ring[i].length = PKT_BUF_SIZE | DESC_LAST;
        
        /* Store packet pointer for later use */
        /* Note: In production, maintain separate array for packet pointers */
    }
    
    /* Initialize index */
    ctx->cur_rx = 0;
    
    /* Set RX ring pointer in hardware */
    ctx->rx_ring_phys = dma_virt_to_phys(ctx->rx_ring);
    outl(ioaddr + BOOM_UP_LIST_PTR, ctx->rx_ring_phys);
    
    LOG_DEBUG("Boomerang: RX ring initialized at 0x%08X", ctx->rx_ring_phys);
    
    return SUCCESS;
}

/**
 * @brief Start packet transmission using Boomerang DMA
 * 
 * Supports scatter-gather DMA for Cyclone/Tornado and hardware checksum offload.
 * 
 * @param ctx 3Com PCI context
 * @param pkt Packet to transmit
 * @return 0 on success, negative error code on failure
 */
int boomerang_start_xmit(pci_3com_context_t *ctx, packet_t *pkt)
{
    uint16_t ioaddr;
    uint16_t entry;
    boom_tx_desc_t *desc;
    uint32_t phys_addr;
    uint32_t desc_flags = 0;
    
    if (!ctx || !pkt || !pkt->data) {
        LOG_ERROR("Boomerang: Invalid parameters for transmission");
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    /* Validate packet length */
    if (pkt->length < MIN_PACKET_SIZE || pkt->length > MAX_PACKET_SIZE) {
        LOG_ERROR("Boomerang: Invalid packet length %d", pkt->length);
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check if ring is full */
    if ((ctx->cur_tx - ctx->dirty_tx) >= TX_RING_SIZE) {
        LOG_ERROR("Boomerang: TX ring full");
        ctx->base.stats.tx_dropped++;
        return ERROR_BUFFER_FULL;
    }
    
    /* Get next descriptor */
    entry = ctx->cur_tx % TX_RING_SIZE;
    desc = &ctx->tx_ring[entry];
    
    /* Ensure descriptor is available */
    if (desc->status & DESC_DN_COMPLETE) {
        LOG_ERROR("Boomerang: TX descriptor not ready");
        return ERROR_BUSY;
    }
    
    /* Hardware checksum offload for Cyclone/Tornado */
    if (ctx->checksum_enabled && (ctx->generation & (IS_CYCLONE | IS_TORNADO))) {
        /* Request hardware checksum calculation */
        uint32_t checksum_protocols = (1 << CHECKSUM_PROTO_IP);
        
        /* Check if packet is TCP or UDP for L4 checksum */
        uint8_t *ip_header = pkt->data + ETH_HEADER_SIZE;
        if (pkt->length >= ETH_HEADER_SIZE + 20) {
            uint8_t protocol = ip_header[9];  /* IP protocol field */
            if (protocol == IP_PROTO_TCP) {
                checksum_protocols |= (1 << CHECKSUM_PROTO_TCP);
                desc_flags |= DESC_CALC_TCP_CSUM;
            } else if (protocol == IP_PROTO_UDP) {
                checksum_protocols |= (1 << CHECKSUM_PROTO_UDP);
                desc_flags |= DESC_CALC_UDP_CSUM;
            }
        }
        
        desc_flags |= DESC_CALC_IP_CSUM;
        
        /* If hardware checksum not available, do it in software */
        if (!(ctx->capabilities & HAS_HWCKSM)) {
            hw_checksum_tx_calculate((nic_context_t *)ctx, pkt->data, 
                                    pkt->length, checksum_protocols);
        }
    }
    
    /* Setup descriptor with scatter-gather support */
    if (ctx->capabilities & HAS_NWAY) {
        /* Cyclone/Tornado support scatter-gather */
        dma_sg_list_t sg_list;
        sg_list.num_fragments = 1;  /* Single fragment for now */
        sg_list.fragments[0].virt_addr = pkt->data;
        sg_list.fragments[0].phys_addr = dma_virt_to_phys(pkt->data);
        sg_list.fragments[0].length = pkt->length;
        
        /* Use DMA subsystem for address translation */
        phys_addr = sg_list.fragments[0].phys_addr;
    } else {
        /* Simple physical address for Vortex/Boomerang */
        phys_addr = dma_virt_to_phys(pkt->data);
    }
    
    /* Ensure cache coherency for DMA */
    if (ctx->base.cache_tier != CACHE_TIER_4_FALLBACK) {
        cache_flush_range(pkt->data, pkt->length);
    }
    
    desc->addr = phys_addr;
    desc->length = pkt->length | DESC_LAST | desc_flags;
    desc->status = pkt->length;  /* Length in lower bits */
    
    /* Advance TX pointer */
    ctx->cur_tx++;
    
    /* Kick DMA engine if needed */
    if (inl(ioaddr + BOOM_DN_LIST_PTR) == 0) {
        /* DMA idle, restart it */
        outl(ioaddr + BOOM_DN_LIST_PTR, 
             ctx->tx_ring_phys + (entry * sizeof(boom_tx_desc_t)));
    }
    
    /* Unstall download if stalled */
    if (inl(ioaddr + BOOM_DMA_CTRL) & BOOM_DMA_DN_STALLED) {
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_DN_UNSTALL);
    }
    
    /* Update statistics */
    ctx->tx_packets++;
    ctx->base.stats.tx_packets++;
    ctx->base.stats.tx_bytes += pkt->length;
    
    LOG_DEBUG("Boomerang: Transmitted %d byte packet via DMA", pkt->length);
    
    return SUCCESS;
}

/**
 * @brief Receive packets using Boomerang DMA
 * 
 * @param ctx 3Com PCI context
 * @return Number of packets received (>= 0) or negative error code
 */
int boomerang_rx(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    uint16_t entry;
    boom_rx_desc_t *desc;
    int packets_received = 0;
    packet_t *pkt;
    uint16_t pkt_len;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    /* Process all completed RX descriptors */
    while (packets_received < RX_RING_SIZE) {
        entry = ctx->cur_rx % RX_RING_SIZE;
        desc = &ctx->rx_ring[entry];
        
        /* Check if descriptor is ready */
        if (!(desc->status & UP_COMPLETE)) {
            break;  /* No more packets */
        }
        
        /* Extract packet length */
        pkt_len = desc->status & 0x1FFF;
        
        /* Check for errors */
        if (desc->status & UP_ERROR) {
            LOG_ERROR("Boomerang: RX error status 0x%08X", desc->status);
            ctx->rx_errors++;
            ctx->base.stats.rx_errors++;
        } else if (pkt_len >= MIN_PACKET_SIZE && pkt_len <= MAX_PACKET_SIZE) {
            /* Allocate new packet for received data */
            pkt = packet_alloc(pkt_len);
            if (pkt) {
                /* Ensure cache coherency for DMA read */
                if (ctx->base.cache_tier != CACHE_TIER_4_FALLBACK) {
                    cache_invalidate_range((void *)desc->addr, pkt_len);
                }
                
                /* Copy data (in production, use zero-copy) */
                memcpy(pkt->data, (void *)desc->addr, pkt_len);
                pkt->length = pkt_len;
                
                /* Update statistics */
                ctx->rx_packets++;
                ctx->base.stats.rx_packets++;
                ctx->base.stats.rx_bytes += pkt_len;
                
                /* Pass packet to upper layer */
                if (ctx->base.receive_callback) {
                    ctx->base.receive_callback(&ctx->base, pkt);
                } else {
                    packet_free(pkt);
                }
                
                packets_received++;
            } else {
                ctx->base.stats.rx_dropped++;
            }
        }
        
        /* Reset descriptor for reuse */
        desc->status = 0;
        
        /* Advance RX pointer */
        ctx->cur_rx++;
    }
    
    /* Restart RX if stalled */
    if (packets_received > 0 && (inl(ioaddr + BOOM_DMA_CTRL) & BOOM_DMA_UP_STALLED)) {
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_UP_UNSTALL);
    }
    
    LOG_DEBUG("Boomerang: Received %d packets via DMA", packets_received);
    
    return packets_received;
}

/**
 * @brief Handle Boomerang interrupts
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int boomerang_interrupt(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    uint16_t status;
    uint32_t dma_status;
    int handled = 0;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    /* Read interrupt status */
    status = inw(ioaddr + BOOM_STATUS);
    
    /* Handle TX completion */
    if (status & BOOM_STAT_TX_COMPLETE) {
        /* Process completed TX descriptors */
        while (ctx->dirty_tx != ctx->cur_tx) {
            uint16_t entry = ctx->dirty_tx % TX_RING_SIZE;
            boom_tx_desc_t *desc = &ctx->tx_ring[entry];
            
            if (!(desc->status & DESC_DN_COMPLETE)) {
                break;  /* Not completed yet */
            }
            
            /* Check for errors */
            if (desc->status & DESC_ERROR) {
                ctx->tx_errors++;
                ctx->base.stats.tx_errors++;
            }
            
            /* Clear descriptor */
            desc->status = 0;
            ctx->dirty_tx++;
        }
        
        /* Acknowledge interrupt */
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_INT_ACK | BOOM_STAT_TX_COMPLETE);
        handled = 1;
    }
    
    /* Handle RX packets */
    if (status & BOOM_STAT_RX_COMPLETE) {
        boomerang_rx(ctx);
        
        /* Acknowledge interrupt */
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_INT_ACK | BOOM_STAT_RX_COMPLETE);
        handled = 1;
    }
    
    /* Handle host errors */
    if (status & BOOM_STAT_HOST_ERROR) {
        LOG_ERROR("Boomerang: Host error detected");
        
        /* Reset and reinitialize */
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_TX_RESET);
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_RX_RESET);
        delay_ms(1);
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_TX_ENABLE);
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_RX_ENABLE);
        
        /* Acknowledge interrupt */
        outw(ioaddr + BOOM_COMMAND, BOOM_CMD_INT_ACK | BOOM_STAT_HOST_ERROR);
        handled = 1;
    }
    
    return handled ? SUCCESS : ERROR_NOT_FOUND;
}

/**
 * @brief Initialize Boomerang DMA mode
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int boomerang_init_dma(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    int result;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("Boomerang: Initializing DMA mode at I/O 0x%04X", ioaddr);
    
    /* Reset TX and RX engines */
    outw(ioaddr + BOOM_COMMAND, BOOM_CMD_TX_RESET);
    delay_ms(1);
    outw(ioaddr + BOOM_COMMAND, BOOM_CMD_RX_RESET);
    delay_ms(1);
    
    /* Initialize descriptor rings */
    result = boomerang_init_tx_ring(ctx);
    if (result != SUCCESS) {
        return result;
    }
    
    result = boomerang_init_rx_ring(ctx);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Enable TX and RX DMA */
    outw(ioaddr + BOOM_COMMAND, BOOM_CMD_TX_ENABLE);
    outw(ioaddr + BOOM_COMMAND, BOOM_CMD_RX_ENABLE);
    
    /* Clear any pending interrupts */
    outw(ioaddr + BOOM_COMMAND, BOOM_CMD_INT_ACK | 0xFF);
    
    /* Set function pointers for DMA mode */
    ctx->base.transmit = (transmit_func_t)boomerang_start_xmit;
    ctx->base.receive = (receive_func_t)boomerang_rx;
    ctx->base.interrupt_handler = (interrupt_func_t)boomerang_interrupt;
    
    LOG_INFO("Boomerang: DMA mode initialized successfully");
    
    return SUCCESS;
}