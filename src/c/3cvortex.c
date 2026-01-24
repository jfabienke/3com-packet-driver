/**
 * @file 3com_vortex.c
 * @brief Vortex generation PIO transmission and reception
 *
 * Implements programmed I/O (PIO) based packet transmission and reception
 * for 3Com Vortex generation NICs (3c590/3c595). These NICs use a FIFO-based
 * approach similar to the 3C509B but with improved buffering.
 *
 * 3Com Packet Driver - Vortex PIO Implementation
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/hardware.h"
#include "../../include/packet.h"
#include "../../include/logging.h"
#include "../../include/common.h"
#include <dos.h>
#include <string.h>

/* Vortex-specific register offsets */
#define VORTEX_TX_PIO_DATA      0x00   /* TX FIFO data port */
#define VORTEX_TX_STATUS        0x1B   /* Transmit status */
#define VORTEX_TX_FREE          0x1C   /* Free bytes in TX FIFO */
#define VORTEX_RX_PIO_DATA      0x00   /* RX FIFO data port */
#define VORTEX_RX_STATUS        0x18   /* Receive status */
#define VORTEX_INT_STATUS       0x0E   /* Interrupt status */

/* Command register commands */
#define CMD_TX_ENABLE           (9<<11)
#define CMD_RX_ENABLE           (4<<11)
#define CMD_TX_RESET            (11<<11)
#define CMD_RX_RESET            (5<<11)
#define CMD_ACK_INTR            (13<<11)
#define CMD_SET_RX_FILTER       (16<<11)

/* TX/RX status bits */
#define TX_STATUS_COMPLETE      0x80
#define TX_STATUS_ERROR         0x10
#define RX_STATUS_COMPLETE      0x8000
#define RX_STATUS_ERROR         0x4000

/* Interrupt status bits */
#define INT_TX_COMPLETE         0x0004
#define INT_RX_COMPLETE         0x0001
#define INT_TX_ERROR            0x0008
#define INT_RX_ERROR            0x0002

/* FIFO thresholds */
#define TX_FIFO_THRESHOLD       1536    /* Minimum free space before TX */
#define RX_FIFO_THRESHOLD       1514    /* Maximum packet size */

/**
 * @brief Initialize Vortex generation NIC
 * 
 * Wrapper for vortex_init_pio to match the vtable interface.
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
/* Forward declaration for init function */
int vortex_init_pio(pci_3com_context_t *ctx);

int vortex_init(pci_3com_context_t *ctx)
{
    return vortex_init_pio(ctx);
}

/**
 * @brief Start packet transmission using Vortex PIO
 * 
 * Transmits a packet using programmed I/O through the TX FIFO.
 * This is similar to 3C509B but with larger FIFOs and better flow control.
 * 
 * @param ctx 3Com PCI context
 * @param pkt Packet to transmit
 * @return 0 on success, negative error code on failure
 */
int vortex_start_xmit(pci_3com_context_t *ctx, packet_t *pkt)
{
    uint16_t ioaddr;
    uint16_t len;
    uint16_t free_space;
    int timeout;
    uint16_t status;
    
    if (!ctx || !pkt || !pkt->data) {
        LOG_ERROR("Vortex: Invalid parameters for transmission");
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    len = pkt->length;
    
    /* Validate packet length */
    if (len < MIN_PACKET_SIZE || len > MAX_PACKET_SIZE) {
        LOG_ERROR("Vortex: Invalid packet length %d", len);
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check TX FIFO space */
    free_space = inpw(ioaddr + VORTEX_TX_FREE);
    
    /* Wait for sufficient TX FIFO space */
    timeout = 1000;
    while (free_space < (len + 4) && timeout > 0) {
        delay_us(10);
        free_space = inpw(ioaddr + VORTEX_TX_FREE);
        timeout--;
    }
    
    if (timeout == 0) {
        LOG_ERROR("Vortex: TX FIFO timeout - no space available");
        ctx->base.stats.tx_errors++;
        return ERROR_TIMEOUT;
    }
    
    /* Disable interrupts during transmission */
    disable();
    
    /* Send packet length (as dword for compatibility) */
    outpw(ioaddr + VORTEX_TX_PIO_DATA, len);
    outpw(ioaddr + VORTEX_TX_PIO_DATA, 0);  /* Upper 16 bits of length */
    
    /* Send packet data */
    /* Use word transfers for speed */
    uint16_t *data16 = (uint16_t *)pkt->data;
    uint16_t words = (len + 1) >> 1;  /* Round up to word boundary */
    uint16_t i;

    for (i = 0; i < words; i++) {
        outpw(ioaddr + VORTEX_TX_PIO_DATA, data16[i]);
    }
    
    /* Pad if necessary (hardware should handle this) */
    if (len & 1) {
        outpw(ioaddr + VORTEX_TX_PIO_DATA, 0);
    }
    
    /* Issue transmit command */
    outpw(ioaddr + EL3_CMD, CMD_TX_ENABLE);
    
    /* Re-enable interrupts */
    enable();
    
    /* Update statistics */
    ctx->tx_packets++;
    ctx->base.stats.tx_packets++;
    ctx->base.stats.tx_bytes += len;
    
    LOG_DEBUG("Vortex: Transmitted %d byte packet", len);
    
    return SUCCESS;
}

/**
 * @brief Receive packets using Vortex PIO
 * 
 * Receives packets from the RX FIFO using programmed I/O.
 * Processes all available packets in the FIFO.
 * 
 * @param ctx 3Com PCI context
 * @return Number of packets received (>= 0) or negative error code
 */
int vortex_rx(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    uint16_t rx_status;
    uint16_t packet_len;
    int packets_received = 0;
    packet_t *pkt;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    /* Process all packets in RX FIFO */
    while (1) {
        /* Check RX status */
        rx_status = inpw(ioaddr + VORTEX_RX_STATUS);
        
        /* No more packets? */
        if (!(rx_status & RX_STATUS_COMPLETE)) {
            break;
        }
        
        /* Extract packet length from status */
        packet_len = rx_status & 0x1FFF;  /* Lower 13 bits */
        
        /* Check for errors */
        if (rx_status & RX_STATUS_ERROR) {
            LOG_ERROR("Vortex: RX error status 0x%04X", rx_status);
            ctx->rx_errors++;
            ctx->base.stats.rx_errors++;
            
            /* Discard error packet */
            outpw(ioaddr + EL3_CMD, CMD_RX_RESET);
            continue;
        }
        
        /* Validate packet length */
        if (packet_len < MIN_PACKET_SIZE || packet_len > MAX_PACKET_SIZE) {
            LOG_ERROR("Vortex: Invalid RX packet length %d", packet_len);
            ctx->rx_errors++;
            ctx->base.stats.rx_errors++;
            
            /* Discard invalid packet */
            outpw(ioaddr + EL3_CMD, CMD_RX_RESET);
            continue;
        }
        
        /* Allocate packet buffer */
        pkt = packet_alloc(packet_len);
        if (!pkt) {
            LOG_ERROR("Vortex: Failed to allocate packet buffer");
            ctx->base.stats.rx_dropped++;
            
            /* Discard packet due to memory shortage */
            outpw(ioaddr + EL3_CMD, CMD_RX_RESET);
            continue;
        }
        
        /* Read packet data from FIFO */
        uint16_t *data16 = (uint16_t *)pkt->data;
        uint16_t words = (packet_len + 1) >> 1;
        uint16_t i;

        for (i = 0; i < words; i++) {
            data16[i] = inpw(ioaddr + VORTEX_RX_PIO_DATA);
        }
        
        /* Set actual packet length */
        pkt->length = packet_len;
        
        /* Update statistics */
        ctx->rx_packets++;
        ctx->base.stats.rx_packets++;
        ctx->base.stats.rx_bytes += packet_len;
        
        /* Pass packet to upper layer */
        if (ctx->base.receive_callback) {
            ctx->base.receive_callback(&ctx->base, pkt);
        } else {
            /* No handler - free packet */
            packet_free(pkt);
        }
        
        packets_received++;
        
        /* Acknowledge packet reception */
        outpw(ioaddr + EL3_CMD, CMD_ACK_INTR | INT_RX_COMPLETE);
    }
    
    LOG_DEBUG("Vortex: Received %d packets", packets_received);
    
    return packets_received;
}

/**
 * @brief Handle Vortex interrupts
 * 
 * Processes interrupts for Vortex generation NICs.
 * Handles both TX completion and RX packet arrival.
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int vortex_interrupt(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    uint16_t int_status;
    int handled = 0;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    /* Read interrupt status */
    int_status = inpw(ioaddr + INT_STATUS);
    
    /* Handle TX completion */
    if (int_status & INT_TX_COMPLETE) {
        uint16_t tx_status = inp(ioaddr + VORTEX_TX_STATUS);
        
        if (tx_status & TX_STATUS_ERROR) {
            LOG_ERROR("Vortex: TX error status 0x%02X", tx_status);
            ctx->tx_errors++;
            ctx->base.stats.tx_errors++;
            
            /* Reset transmitter */
            outpw(ioaddr + EL3_CMD, CMD_TX_RESET);
            outpw(ioaddr + EL3_CMD, CMD_TX_ENABLE);
        }
        
        /* Acknowledge TX interrupt */
        outpw(ioaddr + EL3_CMD, CMD_ACK_INTR | INT_TX_COMPLETE);
        handled = 1;
    }
    
    /* Handle RX packets */
    if (int_status & INT_RX_COMPLETE) {
        vortex_rx(ctx);
        handled = 1;
    }
    
    /* Handle errors */
    if (int_status & (INT_TX_ERROR | INT_RX_ERROR)) {
        LOG_ERROR("Vortex: Error interrupt 0x%04X", int_status);
        
        if (int_status & INT_TX_ERROR) {
            ctx->tx_errors++;
            outpw(ioaddr + EL3_CMD, CMD_TX_RESET);
            outpw(ioaddr + EL3_CMD, CMD_TX_ENABLE);
        }
        
        if (int_status & INT_RX_ERROR) {
            ctx->rx_errors++;
            outpw(ioaddr + EL3_CMD, CMD_RX_RESET);
            outpw(ioaddr + EL3_CMD, CMD_RX_ENABLE);
        }
        
        /* Acknowledge error interrupts */
        outpw(ioaddr + EL3_CMD, CMD_ACK_INTR | (int_status & 0x00FF));
        handled = 1;
    }
    
    return handled ? SUCCESS : ERROR_NOT_FOUND;
}

/**
 * @brief Initialize Vortex PIO mode
 * 
 * Sets up the Vortex NIC for programmed I/O operation.
 * Configures FIFOs, thresholds, and enables TX/RX.
 * 
 * @param ctx 3Com PCI context
 * @return 0 on success, negative error code on failure
 */
int vortex_init_pio(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("Vortex: Initializing PIO mode at I/O 0x%04X", ioaddr);
    
    /* Reset TX and RX */
    outpw(ioaddr + EL3_CMD, CMD_TX_RESET);
    delay_ms(1);
    outpw(ioaddr + EL3_CMD, CMD_RX_RESET);
    delay_ms(1);
    
    /* Set RX filter (accept broadcast and our MAC) */
    outpw(ioaddr + EL3_CMD, CMD_SET_RX_FILTER | 0x01);  /* Station address */
    
    /* Enable TX and RX */
    outpw(ioaddr + EL3_CMD, CMD_TX_ENABLE);
    outpw(ioaddr + EL3_CMD, CMD_RX_ENABLE);
    
    /* Clear any pending interrupts */
    outpw(ioaddr + EL3_CMD, CMD_ACK_INTR | 0xFF);
    
    /* Set function pointers for PIO mode */
    ctx->base.transmit = (transmit_func_t)vortex_start_xmit;
    ctx->base.receive = (receive_func_t)vortex_rx;
    ctx->base.interrupt_handler = (interrupt_func_t)vortex_interrupt;
    
    LOG_INFO("Vortex: PIO mode initialized successfully");
    
    return SUCCESS;
}