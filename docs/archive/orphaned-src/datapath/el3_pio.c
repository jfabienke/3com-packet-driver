/**
 * @file el3_pio.c
 * @brief PIO Datapath Implementation for 3Com EtherLink III
 *
 * High-performance programmed I/O datapath for 3C509B and Vortex cards.
 * This is HOT PATH code - no HAL, no abstraction, direct I/O only!
 *
 * This file is part of the 3Com Packet Driver project.
 */

#define EL3_DATAPATH_COMPILATION  /* Prevent HAL inclusion */

#include <stdint.h>
#include <string.h>
#include <dos.h>
#include "../core/el3_core.h"
#include "el3_datapath.h"
#include "../../include/logging.h"

/* PIO register offsets - direct access for performance */
#define TX_FIFO         0x00    /* TX FIFO write */
#define TX_STATUS       0x0B    /* TX status (window 1) */
#define TX_FREE         0x0C    /* Free bytes in TX FIFO (window 1) */
#define RX_FIFO         0x00    /* RX FIFO read */
#define RX_STATUS       0x08    /* RX status (window 1) */
#define FIFO_DIAG       0x04    /* FIFO diagnostics (window 4) */

/* Command register - offset 0x0E */
#define CMD_REG         0x0E

/* Status register - offset 0x0E (read) */
#define STATUS_REG      0x0E

/* Status bits */
#define S_INT_LATCH     0x0001
#define S_TX_COMPLETE   0x0004
#define S_TX_AVAIL      0x0008
#define S_RX_COMPLETE   0x0010
#define S_RX_EARLY      0x0020
#define S_UPDATE_STATS  0x0080
#define S_CMD_IN_PROG   0x1000

/* Commands */
#define CMD_TX_ENABLE   (9<<11)
#define CMD_RX_ENABLE   (4<<11)
#define CMD_TX_RESET    (11<<11)
#define CMD_RX_RESET    (5<<11)
#define CMD_ACK_INT     (13<<11)
#define CMD_RX_DISCARD  (8<<11)
#define CMD_TX_DONE     (7<<11)

/* TX status bits */
#define TX_S_COMPLETE   0x80
#define TX_S_INTRQ      0x40
#define TX_S_JABBER     0x20
#define TX_S_UNDERRUN   0x10
#define TX_S_MAX_COLL   0x08

/* RX status bits */
#define RX_S_INCOMPLETE 0x8000
#define RX_S_ERROR      0x4000

/* FIFO sizes by generation */
#define FIFO_SIZE_3C509B    2048
#define FIFO_SIZE_VORTEX    8192

/* TX threshold - start transmission when this many bytes are in FIFO */
#define TX_THRESHOLD_3C509B 256
#define TX_THRESHOLD_VORTEX 512

/* Per-device PIO state (hot data, cache-aligned would be nice) */
struct pio_state {
    uint16_t io_base;           /* Pre-cached for speed */
    uint16_t tx_threshold;      /* When to start TX */
    uint16_t fifo_size;         /* Total FIFO size */
    uint16_t tx_room;           /* Last known TX room */
    uint8_t has_permanent_win1; /* Avoid window switches */
};

static struct pio_state g_pio_state[MAX_EL3_DEVICES];

/* Forward declarations */
static void el3_pio_write_fifo(uint16_t io_base, const uint8_t *data, uint16_t len);
static void el3_pio_read_fifo(uint16_t io_base, uint8_t *data, uint16_t len);
static int el3_pio_wait_tx_complete(uint16_t io_base);

/**
 * @brief Initialize PIO datapath
 *
 * Sets up PIO-specific state for fast operation.
 */
int el3_pio_init(struct el3_dev *dev)
{
    struct pio_state *ps;
    int index = el3_get_device_count() - 1;  /* Current device */
    
    if (index < 0 || index >= MAX_EL3_DEVICES) {
        return -EINVAL;
    }
    
    ps = &g_pio_state[index];
    
    /* Pre-cache values for hot path */
    ps->io_base = dev->io_base;
    ps->fifo_size = dev->caps.fifo_size;
    ps->has_permanent_win1 = dev->caps.has_permanent_window1;
    
    /* Set TX threshold based on generation */
    if (dev->generation == EL3_GEN_3C509B) {
        ps->tx_threshold = TX_THRESHOLD_3C509B;
    } else {
        ps->tx_threshold = TX_THRESHOLD_VORTEX;
    }
    
    /* Ensure we're in window 1 for PIO operation */
    if (!ps->has_permanent_win1) {
        outportw(ps->io_base + CMD_REG, (1<<11) | 1);  /* SelectWindow 1 */
    }
    
    /* Enable TX and RX */
    outportw(ps->io_base + CMD_REG, CMD_TX_ENABLE);
    outportw(ps->io_base + CMD_REG, CMD_RX_ENABLE);
    
    LOG_INFO("EL3-PIO: Initialized for %s, FIFO: %d bytes, Threshold: %d",
             dev->name, ps->fifo_size, ps->tx_threshold);
    
    return 0;
}

/**
 * @brief Transmit packet using PIO
 *
 * HOT PATH - Optimized for speed, no abstractions!
 */
int el3_pio_xmit(struct el3_dev *dev, struct packet *pkt)
{
    struct pio_state *ps;
    uint16_t io_base;
    uint16_t len;
    uint16_t tx_free;
    uint8_t tx_status;
    int retries = 100;
    int index = 0;  /* TODO: Get device index properly */
    
    /* Get pre-cached state */
    ps = &g_pio_state[index];
    io_base = ps->io_base;
    len = pkt->length;
    
    /* Validate packet length */
    if (len < 60 || len > 1514) {
        dev->stats.tx_errors++;
        return -EINVAL;
    }
    
    /* Pad short packets to minimum Ethernet size */
    if (len < 60) {
        memset(pkt->data + len, 0, 60 - len);
        len = 60;
    }
    
    /* Check TX FIFO space - Window 1, offset 0x0C */
    tx_free = inportw(io_base + TX_FREE);
    
    /* Wait for space if needed */
    while (tx_free < (len + 4) && retries > 0) {
        /* Check if previous TX completed */
        tx_status = inportb(io_base + TX_STATUS);
        if (tx_status & TX_S_COMPLETE) {
            /* Acknowledge completion */
            outportb(io_base + TX_STATUS, 0x00);
            
            /* Check for errors */
            if (tx_status & (TX_S_JABBER | TX_S_UNDERRUN | TX_S_MAX_COLL)) {
                dev->stats.tx_errors++;
                
                /* Reset TX FIFO on error */
                outportw(io_base + CMD_REG, CMD_TX_RESET);
                outportw(io_base + CMD_REG, CMD_TX_ENABLE);
            }
        }
        
        /* Small delay and retry */
        __asm { nop; nop; nop; nop; }
        tx_free = inportw(io_base + TX_FREE);
        retries--;
    }
    
    if (retries == 0) {
        dev->stats.tx_dropped++;
        return -ETIMEDOUT;
    }
    
    /* Write packet length to TX FIFO */
    outportw(io_base + TX_FIFO, len);
    outportw(io_base + TX_FIFO, 0);  /* Second word must be 0 */
    
    /* Write packet data to TX FIFO - optimized for speed */
    el3_pio_write_fifo(io_base, pkt->data, len);
    
    /* Pad to doubleword if needed */
    if (len & 0x03) {
        uint16_t pad = 4 - (len & 0x03);
        uint32_t zero = 0;
        el3_pio_write_fifo(io_base, (uint8_t *)&zero, pad);
    }
    
    /* Start transmission if threshold reached */
    if (len >= ps->tx_threshold) {
        /* TX should auto-start, but ensure it */
        tx_status = inportb(io_base + TX_STATUS);
        if (!(tx_status & 0x80)) {
            /* Force TX start */
            outportw(io_base + CMD_REG, CMD_TX_DONE);
        }
    }
    
    /* Update statistics */
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += len;
    
    return 0;
}

/**
 * @brief Receive packets using PIO
 *
 * HOT PATH - Optimized for speed!
 */
int el3_pio_rx_poll(struct el3_dev *dev)
{
    struct pio_state *ps;
    uint16_t io_base;
    uint16_t rx_status;
    uint16_t pkt_len;
    uint8_t *rx_buffer;
    int packets = 0;
    int index = 0;  /* TODO: Get device index properly */
    
    /* Get pre-cached state */
    ps = &g_pio_state[index];
    io_base = ps->io_base;
    
    /* Allocate RX buffer on stack for speed */
    uint8_t buffer[1536];
    rx_buffer = buffer;
    
    /* Process all pending packets */
    while (1) {
        /* Read RX status - Window 1, offset 0x08 */
        rx_status = inportw(io_base + RX_STATUS);
        
        /* Check if packet is complete */
        if (rx_status & RX_S_INCOMPLETE) {
            break;  /* No more complete packets */
        }
        
        /* Extract packet length from status */
        pkt_len = rx_status & 0x07FF;
        
        /* Check for errors */
        if (rx_status & RX_S_ERROR) {
            dev->stats.rx_errors++;
            
            /* Discard bad packet */
            outportw(io_base + CMD_REG, CMD_RX_DISCARD);
            
            /* Wait for discard to complete */
            while (inportw(io_base + STATUS_REG) & S_CMD_IN_PROG) {
                __asm { nop; }
            }
            continue;
        }
        
        /* Validate packet length */
        if (pkt_len < 14 || pkt_len > 1514) {
            dev->stats.rx_length_errors++;
            outportw(io_base + CMD_REG, CMD_RX_DISCARD);
            continue;
        }
        
        /* Read packet from RX FIFO */
        el3_pio_read_fifo(io_base, rx_buffer, pkt_len);
        
        /* Discard packet from FIFO (we've read it) */
        outportw(io_base + CMD_REG, CMD_RX_DISCARD);
        
        /* Pass packet up to network stack */
        /* TODO: Call packet handler */
        /* packet_receive(rx_buffer, pkt_len); */
        
        /* Update statistics */
        dev->stats.rx_packets++;
        dev->stats.rx_bytes += pkt_len;
        packets++;
        
        /* Limit packets per poll to prevent starvation */
        if (packets >= 16) {
            break;
        }
    }
    
    return packets;
}

/**
 * @brief PIO interrupt service routine
 *
 * HOT PATH - Handle interrupts quickly!
 */
void el3_pio_isr(struct el3_dev *dev)
{
    struct pio_state *ps;
    uint16_t io_base;
    uint16_t status;
    uint8_t tx_status;
    int index = 0;  /* TODO: Get device index properly */
    
    /* Get pre-cached state */
    ps = &g_pio_state[index];
    io_base = ps->io_base;
    
    /* Read interrupt status */
    status = inportw(io_base + STATUS_REG);
    
    /* Handle TX complete */
    if (status & S_TX_COMPLETE) {
        tx_status = inportb(io_base + TX_STATUS);
        
        if (tx_status & TX_S_COMPLETE) {
            /* Clear TX status */
            outportb(io_base + TX_STATUS, 0x00);
            
            /* Check for TX errors */
            if (tx_status & (TX_S_JABBER | TX_S_UNDERRUN | TX_S_MAX_COLL)) {
                dev->stats.tx_errors++;
                
                /* Reset TX on error */
                outportw(io_base + CMD_REG, CMD_TX_RESET);
                outportw(io_base + CMD_REG, CMD_TX_ENABLE);
            }
        }
    }
    
    /* Handle RX complete */
    if (status & S_RX_COMPLETE) {
        /* Process received packets */
        el3_pio_rx_poll(dev);
    }
    
    /* Handle statistics update */
    if (status & S_UPDATE_STATS) {
        /* Update stats in window 6 */
        /* For PIO mode, we do this later to avoid window switches */
        /* el3_update_statistics(dev); */
    }
    
    /* Acknowledge all interrupts */
    outportw(io_base + CMD_REG, CMD_ACK_INT | 0xFF);
}

/**
 * @brief Optimized FIFO write
 *
 * HOT PATH - Assembly would be even better!
 */
static void el3_pio_write_fifo(uint16_t io_base, const uint8_t *data, uint16_t len)
{
    uint16_t words = len >> 1;
    uint16_t *wp = (uint16_t *)data;
    
    /* Write words first for speed */
    while (words--) {
        outportw(io_base + TX_FIFO, *wp++);
    }
    
    /* Write remaining byte if any */
    if (len & 1) {
        outportb(io_base + TX_FIFO, data[len - 1]);
    }
}

/**
 * @brief Optimized FIFO read
 *
 * HOT PATH - Assembly would be even better!
 */
static void el3_pio_read_fifo(uint16_t io_base, uint8_t *data, uint16_t len)
{
    uint16_t words = len >> 1;
    uint16_t *wp = (uint16_t *)data;
    
    /* Read words first for speed */
    while (words--) {
        *wp++ = inportw(io_base + RX_FIFO);
    }
    
    /* Read remaining byte if any */
    if (len & 1) {
        data[len - 1] = inportb(io_base + RX_FIFO);
    }
}

/**
 * @brief Wait for TX completion
 */
static int el3_pio_wait_tx_complete(uint16_t io_base)
{
    uint8_t tx_status;
    int timeout = 1000;
    
    while (timeout > 0) {
        tx_status = inportb(io_base + TX_STATUS);
        if (tx_status & TX_S_COMPLETE) {
            /* Clear status */
            outportb(io_base + TX_STATUS, 0x00);
            
            /* Check for errors */
            if (tx_status & (TX_S_JABBER | TX_S_UNDERRUN | TX_S_MAX_COLL)) {
                return -EIO;
            }
            
            return 0;
        }
        
        /* Brief delay */
        __asm { nop; nop; nop; nop; }
        timeout--;
    }
    
    return -ETIMEDOUT;
}