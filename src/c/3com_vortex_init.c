/**
 * @file 3com_vortex_init.c
 * @brief Vortex generation initialization
 *
 * Initializes Vortex generation NICs for PIO operation.
 *
 * 3Com Packet Driver - Vortex Initialization
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include <dos.h>

/* Vortex-specific initialization commands */
#define CMD_TX_ENABLE       (9<<11)
#define CMD_RX_ENABLE       (4<<11)
#define CMD_SET_TX_RECLAIM  (18<<11)
#define CMD_SET_RX_EARLY    (17<<11)

/* TX/RX thresholds for Vortex */
#define VORTEX_TX_THRESHOLD     256    /* Start TX after 256 bytes */
#define VORTEX_RX_THRESHOLD     64     /* RX early threshold */

/**
 * @brief Initialize Vortex generation for PIO operation
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
    
    /* Set TX start threshold */
    outw(ioaddr + EL3_CMD, CMD_SET_TX_RECLAIM | (VORTEX_TX_THRESHOLD >> 2));
    
    /* Set RX early threshold */
    outw(ioaddr + EL3_CMD, CMD_SET_RX_EARLY | (VORTEX_RX_THRESHOLD >> 2));
    
    /* Select Window 3 for internal configuration */
    select_window(ioaddr, 3);
    
    /* Enable TX and RX FIFOs */
    uint32_t config = window_read32(ioaddr, 3, WN3_CONFIG);
    config |= 0x00000001;  /* Enable FIFOs */
    window_write32(ioaddr, 3, WN3_CONFIG, config);
    
    /* Select Window 4 for diagnostics */
    select_window(ioaddr, 4);
    
    /* Check FIFO status */
    uint16_t fifo_diag = window_read16(ioaddr, 4, WN4_FIFO_DIAG);
    if (fifo_diag & 0x0400) {
        LOG_WARNING("Vortex: TX FIFO underrun detected");
    }
    if (fifo_diag & 0x2000) {
        LOG_WARNING("Vortex: RX FIFO overrun detected");
    }
    
    /* Enable transmitter and receiver */
    outw(ioaddr + EL3_CMD, CMD_TX_ENABLE);
    outw(ioaddr + EL3_CMD, CMD_RX_ENABLE);
    
    /* Set operation mode flags */
    ctx->tx_mode = TX_MODE_PIO;
    ctx->rx_mode = RX_MODE_PIO;
    
    /* Vortex uses PIO, no DMA setup needed */
    ctx->tx_ring = NULL;
    ctx->rx_ring = NULL;
    
    /* Setup function pointers for PIO operations */
    ctx->base.tx_handler = (void *)vortex_start_xmit;
    ctx->base.rx_handler = (void *)vortex_rx;
    
    LOG_INFO("Vortex: PIO initialization complete");
    
    return SUCCESS;
}