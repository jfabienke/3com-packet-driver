#include <stdint.h>
#include <string.h>
#include <dos.h>
#include "el3_unified.h"
#include "hardware.h"

#define TX_FIFO_THRESH 256
#define RX_FIFO_THRESH 4

#define CMD_RX_RESET    0x2800
#define CMD_TX_RESET    0x5800
#define CMD_RX_ENABLE   0x2000
#define CMD_TX_ENABLE   0x4800
#define CMD_ACK_INTR    0x6800
#define CMD_SET_INTR    0x7800
#define CMD_SELECT_WIN  0x0800

#define WIN_0   0
#define WIN_1   1
#define WIN_3   3
#define WIN_4   4
#define WIN_6   6

#define PORT_CMD        0x0E
#define PORT_STATUS     0x0E
#define PORT_TX_STATUS  0x1B
#define PORT_TX_FREE    0x1C
#define PORT_RX_STATUS  0x18
#define PORT_TX_DATA    0x00
#define PORT_RX_DATA    0x00

static inline void select_window(uint16_t iobase, uint8_t window)
{
    outw(CMD_SELECT_WIN | window, iobase + PORT_CMD);
}

static inline uint16_t read_tx_free(uint16_t iobase)
{
    return inw(iobase + PORT_TX_FREE);
}

static inline uint8_t read_tx_status(uint16_t iobase)
{
    return inb(iobase + PORT_TX_STATUS);
}

static inline uint16_t read_rx_status(uint16_t iobase)
{
    return inw(iobase + PORT_RX_STATUS);
}

int el3_transmit_pio(struct el3_device *dev, const void *data, uint16_t len)
{
    uint16_t iobase = dev->iobase;
    uint16_t free_bytes;
    const uint16_t *word_ptr;
    uint16_t words;
    int timeout;
    
    if (len > 1514)
        return -1;
    
    select_window(iobase, WIN_1);
    
    timeout = 1000;
    while (timeout-- > 0) {
        free_bytes = read_tx_free(iobase);
        if (free_bytes >= len + 4)
            break;
        
        if (read_tx_status(iobase) & 0x84) {
            outw(CMD_TX_RESET, iobase + PORT_CMD);
            outw(CMD_TX_ENABLE, iobase + PORT_CMD);
            dev->tx_errors++;
            return -1;
        }
    }
    
    if (timeout <= 0) {
        dev->tx_errors++;
        return -1;
    }
    
    outw(len, iobase + PORT_TX_DATA);
    outw(0, iobase + PORT_TX_DATA);
    
    word_ptr = (const uint16_t *)data;
    words = (len + 1) >> 1;
    
    while (words > 0) {
        uint16_t burst = (words > 16) ? 16 : words;
        uint16_t i;
        
        for (i = 0; i < burst; i++) {
            outw(*word_ptr++, iobase + PORT_TX_DATA);
        }
        words -= burst;
    }
    
    if (len & 1) {
        outb(0, iobase + PORT_TX_DATA);
    }
    
    while ((read_tx_free(iobase) & 0x8000) == 0) {
        ;
    }
    
    dev->tx_packets++;
    
    return 0;
}

int el3_receive_pio(struct el3_device *dev, void *buffer, uint16_t *len)
{
    uint16_t iobase = dev->iobase;
    uint16_t rx_status;
    uint16_t pkt_len;
    uint16_t *word_ptr;
    uint16_t words;
    
    select_window(iobase, WIN_1);
    
    rx_status = read_rx_status(iobase);
    if ((rx_status & 0x8000) == 0) {
        return -1;
    }
    
    if (rx_status & 0x4000) {
        outw(CMD_RX_RESET, iobase + PORT_CMD);
        outw(CMD_RX_ENABLE, iobase + PORT_CMD);
        dev->rx_errors++;
        return -1;
    }
    
    pkt_len = rx_status & 0x1FFF;
    if (pkt_len > 1514) {
        outw(CMD_RX_RESET, iobase + PORT_CMD);
        outw(CMD_RX_ENABLE, iobase + PORT_CMD);
        dev->rx_errors++;
        return -1;
    }
    
    word_ptr = (uint16_t *)buffer;
    words = (pkt_len + 1) >> 1;
    
    while (words > 0) {
        uint16_t burst = (words > 16) ? 16 : words;
        uint16_t i;
        
        for (i = 0; i < burst; i++) {
            *word_ptr++ = inw(iobase + PORT_RX_DATA);
        }
        words -= burst;
    }
    
    if (pkt_len & 1) {
        inb(iobase + PORT_RX_DATA);
    }
    
    while (inw(iobase + PORT_STATUS) & 0x1000) {
        inb(iobase + PORT_RX_DATA);
    }
    
    outw(0x4000, iobase + PORT_STATUS);
    
    *len = pkt_len;
    dev->rx_packets++;
    
    return 0;
}