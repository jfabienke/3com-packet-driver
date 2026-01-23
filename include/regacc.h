/**
 * @file register_access.h
 * @brief Inline register access functions for 3C509B and 3C515-TX NICs
 *
 * Groups 6A & 6B - C Interface Architecture
 * Provides optimized inline functions for hardware register access
 * with proper DOS compatibility and timing considerations.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _REGISTER_ACCESS_H_
#define _REGISTER_ACCESS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "3c509b.h"
#include "3c515.h"
#include "nicctx.h"
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct nic_context;

/* I/O timing and delay functions */
static inline void io_delay(void) {
    /* Standard I/O delay for slow ISA bus timing */
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0) : "memory");
}

static inline void io_delay_us(uint16_t microseconds) {
    /* Microsecond delay using PIT counter */
    uint16_t ticks = (uint16_t)((microseconds * 1193182UL) / 1000000UL);
    uint16_t start, current;
    
    /* Read current PIT counter */
    outb(0x43, 0x00);  /* Latch counter 0 */
    start = inb(0x40);
    start |= (inb(0x40) << 8);
    
    /* Wait for specified ticks */
    do {
        outb(0x43, 0x00);
        current = inb(0x40);
        current |= (inb(0x40) << 8);
    } while ((start - current) < ticks);
}

/**
 * @brief Generic register access functions
 */

/* Basic I/O port access with timing */
static inline uint8_t reg_read8(uint16_t port) {
    uint8_t value = inb(port);
    io_delay();
    return value;
}

static inline void reg_write8(uint16_t port, uint8_t value) {
    outb(port, value);
    io_delay();
}

static inline uint16_t reg_read16(uint16_t port) {
    uint16_t value = inw(port);
    io_delay();
    return value;
}

static inline void reg_write16(uint16_t port, uint16_t value) {
    outw(port, value);
    io_delay();
}

static inline uint32_t reg_read32(uint16_t port) {
    uint32_t low = inw(port);
    uint32_t high = inw(port + 2);
    io_delay();
    return (high << 16) | low;
}

static inline void reg_write32(uint16_t port, uint32_t value) {
    outw(port, (uint16_t)(value & 0xFFFF));
    outw(port + 2, (uint16_t)(value >> 16));
    io_delay();
}

/**
 * @brief 3C509B-specific register access functions
 */

/* Window selection for 3C509B */
static inline void c509b_select_window(struct nic_context *ctx, uint8_t window) {
    reg_write16(ctx->io_base + _3C509B_COMMAND_REG, 
                _3C509B_CMD_SELECT_WINDOW | (window & 0x07));
}

/* Command register access */
static inline void c509b_write_command(struct nic_context *ctx, uint16_t command) {
    reg_write16(ctx->io_base + _3C509B_COMMAND_REG, command);
}

static inline uint16_t c509b_read_status(struct nic_context *ctx) {
    return reg_read16(ctx->io_base + _3C509B_STATUS_REG);
}

/* Window 0 - Configuration and EEPROM */
static inline uint16_t c509b_read_eeprom(struct nic_context *ctx, uint8_t offset) {
    c509b_select_window(ctx, _3C509B_WINDOW_0);
    reg_write16(ctx->io_base + _3C509B_W0_EEPROM_COMMAND, 
                _3C509B_EEPROM_CMD_READ | (offset & 0x3F));
    
    /* Wait for EEPROM operation to complete */
    uint16_t timeout = 1000;
    while (timeout--) {
        if (!(reg_read16(ctx->io_base + _3C509B_W0_EEPROM_COMMAND) & _3C509B_EEPROM_BUSY))
            break;
        io_delay_us(10);
    }
    
    return reg_read16(ctx->io_base + _3C509B_W0_EEPROM_DATA);
}

static inline uint16_t c509b_read_product_id(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_0);
    return reg_read16(ctx->io_base + _3C509B_W0_PRODUCT_ID);
}

/* Window 1 - Operating registers */
static inline uint16_t c509b_read_tx_status(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_1);
    return reg_read16(ctx->io_base + _3C509B_W1_TX_STATUS);
}

static inline uint16_t c509b_read_rx_status(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_1);
    return reg_read16(ctx->io_base + _3C509B_W1_RX_STATUS);
}

static inline void c509b_write_tx_data(struct nic_context *ctx, uint16_t data) {
    c509b_select_window(ctx, _3C509B_WINDOW_1);
    reg_write16(ctx->io_base + _3C509B_W1_TX_DATA, data);
}

static inline uint16_t c509b_read_rx_data(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_1);
    return reg_read16(ctx->io_base + _3C509B_W1_RX_DATA);
}

/* Window 2 - Station address */
static inline void c509b_write_station_address(struct nic_context *ctx, const uint8_t *mac) {
    c509b_select_window(ctx, _3C509B_WINDOW_2);
    for (int i = 0; i < 6; i += 2) {
        uint16_t addr_word = mac[i] | (mac[i + 1] << 8);
        reg_write16(ctx->io_base + _3C509B_W2_STATION_ADDR + i, addr_word);
    }
}

static inline void c509b_read_station_address(struct nic_context *ctx, uint8_t *mac) {
    c509b_select_window(ctx, _3C509B_WINDOW_2);
    for (int i = 0; i < 6; i += 2) {
        uint16_t addr_word = reg_read16(ctx->io_base + _3C509B_W2_STATION_ADDR + i);
        mac[i] = (uint8_t)(addr_word & 0xFF);
        mac[i + 1] = (uint8_t)(addr_word >> 8);
    }
}

/* Window 3 - FIFO management */
static inline uint16_t c509b_read_free_tx_bytes(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_3);
    return reg_read16(ctx->io_base + _3C509B_W3_FREE_TX_BYTES);
}

static inline uint16_t c509b_read_rx_bytes(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_3);
    return reg_read16(ctx->io_base + _3C509B_W3_RX_BYTES);
}

/* Window 4 - Media/diagnostics */
static inline uint16_t c509b_read_media_status(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_4);
    return reg_read16(ctx->io_base + _3C509B_W4_MEDIA_STATUS);
}

static inline void c509b_write_media_control(struct nic_context *ctx, uint16_t control) {
    c509b_select_window(ctx, _3C509B_WINDOW_4);
    reg_write16(ctx->io_base + _3C509B_W4_MEDIA_CONTROL, control);
}

/* Window 6 - Statistics */
static inline uint8_t c509b_read_stat_tx_bytes_ok(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_6);
    return reg_read8(ctx->io_base + _3C509B_W6_TX_BYTES_OK);
}

static inline uint8_t c509b_read_stat_rx_bytes_ok(struct nic_context *ctx) {
    c509b_select_window(ctx, _3C509B_WINDOW_6);
    return reg_read8(ctx->io_base + _3C509B_W6_RX_BYTES_OK);
}

/**
 * @brief 3C515-TX-specific register access functions
 */

/* Window selection for 3C515-TX */
static inline void c515_select_window(struct nic_context *ctx, uint8_t window) {
    reg_write16(ctx->io_base + _3C515_TX_COMMAND_REG, 
                _3C515_TX_CMD_SELECT_WINDOW | (window & 0x07));
}

/* Command and status register access */
static inline void c515_write_command(struct nic_context *ctx, uint16_t command) {
    reg_write16(ctx->io_base + _3C515_TX_COMMAND_REG, command);
}

static inline uint16_t c515_read_status(struct nic_context *ctx) {
    return reg_read16(ctx->io_base + _3C515_TX_STATUS_REG);
}

/* Window 0 - EEPROM access */
static inline uint16_t c515_read_eeprom(struct nic_context *ctx, uint8_t offset) {
    c515_select_window(ctx, _3C515_TX_WINDOW_0);
    reg_write16(ctx->io_base + _3C515_TX_W0_EEPROM_COMMAND,
                _3C515_TX_EEPROM_CMD_READ | (offset & 0x3F));
    
    /* Wait for EEPROM operation */
    uint16_t timeout = 1000;
    while (timeout--) {
        if (!(reg_read16(ctx->io_base + _3C515_TX_W0_EEPROM_COMMAND) & _3C515_TX_EEPROM_BUSY))
            break;
        io_delay_us(10);
    }
    
    return reg_read16(ctx->io_base + _3C515_TX_W0_EEPROM_DATA);
}

/* Window 1 - Operating registers */
static inline void c515_write_tx_data(struct nic_context *ctx, uint32_t data) {
    c515_select_window(ctx, _3C515_TX_WINDOW_1);
    reg_write32(ctx->io_base + _3C515_TX_W1_TX_DATA, data);
}

static inline uint32_t c515_read_rx_data(struct nic_context *ctx) {
    c515_select_window(ctx, _3C515_TX_WINDOW_1);
    return reg_read32(ctx->io_base + _3C515_TX_W1_RX_DATA);
}

static inline uint16_t c515_read_tx_status(struct nic_context *ctx) {
    c515_select_window(ctx, _3C515_TX_WINDOW_1);
    return reg_read16(ctx->io_base + _3C515_TX_W1_TX_STATUS);
}

static inline uint16_t c515_read_rx_status(struct nic_context *ctx) {
    c515_select_window(ctx, _3C515_TX_WINDOW_1);
    return reg_read16(ctx->io_base + _3C515_TX_W1_RX_STATUS);
}

/* Window 2 - Station address */
static inline void c515_write_station_address(struct nic_context *ctx, const uint8_t *mac) {
    c515_select_window(ctx, _3C515_TX_WINDOW_2);
    for (int i = 0; i < 6; i += 2) {
        uint16_t addr_word = mac[i] | (mac[i + 1] << 8);
        reg_write16(ctx->io_base + _3C515_TX_W2_STATION_ADDR + i, addr_word);
    }
}

static inline void c515_read_station_address(struct nic_context *ctx, uint8_t *mac) {
    c515_select_window(ctx, _3C515_TX_WINDOW_2);
    for (int i = 0; i < 6; i += 2) {
        uint16_t addr_word = reg_read16(ctx->io_base + _3C515_TX_W2_STATION_ADDR + i);
        mac[i] = (uint8_t)(addr_word & 0xFF);
        mac[i + 1] = (uint8_t)(addr_word >> 8);
    }
}

/* Window 4 - Media control */
static inline uint16_t c515_read_media_status(struct nic_context *ctx) {
    c515_select_window(ctx, _3C515_TX_WINDOW_4);
    return reg_read16(ctx->io_base + _3C515_TX_W4_MEDIA_STATUS);
}

static inline void c515_write_media_control(struct nic_context *ctx, uint16_t control) {
    c515_select_window(ctx, _3C515_TX_WINDOW_4);
    reg_write16(ctx->io_base + _3C515_TX_W4_MEDIA_CONTROL, control);
}

/* Window 7 - Bus master control */
static inline uint32_t c515_read_master_address(struct nic_context *ctx) {
    c515_select_window(ctx, _3C515_TX_WINDOW_7);
    return reg_read32(ctx->io_base + _3C515_TX_W7_MASTER_ADDRESS);
}

static inline void c515_write_master_address(struct nic_context *ctx, uint32_t address) {
    c515_select_window(ctx, _3C515_TX_WINDOW_7);
    reg_write32(ctx->io_base + _3C515_TX_W7_MASTER_ADDRESS, address);
}

static inline uint32_t c515_read_master_length(struct nic_context *ctx) {
    c515_select_window(ctx, _3C515_TX_WINDOW_7);
    return reg_read32(ctx->io_base + _3C515_TX_W7_MASTER_LENGTH);
}

static inline void c515_write_master_length(struct nic_context *ctx, uint32_t length) {
    c515_select_window(ctx, _3C515_TX_WINDOW_7);
    reg_write32(ctx->io_base + _3C515_TX_W7_MASTER_LENGTH, length);
}

static inline uint16_t c515_read_master_status(struct nic_context *ctx) {
    c515_select_window(ctx, _3C515_TX_WINDOW_7);
    return reg_read16(ctx->io_base + _3C515_TX_W7_MASTER_STATUS);
}

static inline void c515_write_master_control(struct nic_context *ctx, uint16_t control) {
    c515_select_window(ctx, _3C515_TX_WINDOW_7);
    reg_write16(ctx->io_base + _3C515_TX_W7_MASTER_CONTROL, control);
}

/**
 * @brief High-level register access functions
 */

/* Generic functions that work with both NICs */
static inline void nic_select_window(struct nic_context *ctx, uint8_t window) {
    if (ctx->nic_type == NIC_TYPE_3C509B) {
        c509b_select_window(ctx, window);
    } else if (ctx->nic_type == NIC_TYPE_3C515TX) {
        c515_select_window(ctx, window);
    }
}

static inline void nic_write_command(struct nic_context *ctx, uint16_t command) {
    if (ctx->nic_type == NIC_TYPE_3C509B) {
        c509b_write_command(ctx, command);
    } else if (ctx->nic_type == NIC_TYPE_3C515TX) {
        c515_write_command(ctx, command);
    }
}

static inline uint16_t nic_read_status(struct nic_context *ctx) {
    if (ctx->nic_type == NIC_TYPE_3C509B) {
        return c509b_read_status(ctx);
    } else if (ctx->nic_type == NIC_TYPE_3C515TX) {
        return c515_read_status(ctx);
    }
    return 0;
}

static inline void nic_write_station_address(struct nic_context *ctx, const uint8_t *mac) {
    if (ctx->nic_type == NIC_TYPE_3C509B) {
        c509b_write_station_address(ctx, mac);
    } else if (ctx->nic_type == NIC_TYPE_3C515TX) {
        c515_write_station_address(ctx, mac);
    }
}

static inline void nic_read_station_address(struct nic_context *ctx, uint8_t *mac) {
    if (ctx->nic_type == NIC_TYPE_3C509B) {
        c509b_read_station_address(ctx, mac);
    } else if (ctx->nic_type == NIC_TYPE_3C515TX) {
        c515_read_station_address(ctx, mac);
    }
}

/* Register validation and safety functions */
static inline bool nic_validate_io_range(struct nic_context *ctx, uint16_t offset) {
    return (offset < ctx->io_range);
}

static inline bool nic_is_register_accessible(struct nic_context *ctx, uint16_t offset) {
    return (ctx->state != NIC_STATE_UNINITIALIZED && 
            nic_validate_io_range(ctx, offset));
}

/* Atomic register operations */
static inline uint16_t nic_read_modify_write16(struct nic_context *ctx, uint16_t offset, 
                                              uint16_t mask, uint16_t value) {
    uint16_t reg_val = reg_read16(ctx->io_base + offset);
    reg_val = (reg_val & ~mask) | (value & mask);
    reg_write16(ctx->io_base + offset, reg_val);
    return reg_val;
}

static inline void nic_set_bits16(struct nic_context *ctx, uint16_t offset, uint16_t bits) {
    nic_read_modify_write16(ctx, offset, bits, bits);
}

static inline void nic_clear_bits16(struct nic_context *ctx, uint16_t offset, uint16_t bits) {
    nic_read_modify_write16(ctx, offset, bits, 0);
}

/* Register access macros for better performance */
#define NIC_REG_READ8(ctx, offset)  reg_read8((ctx)->io_base + (offset))
#define NIC_REG_WRITE8(ctx, offset, val) reg_write8((ctx)->io_base + (offset), (val))
#define NIC_REG_READ16(ctx, offset) reg_read16((ctx)->io_base + (offset))
#define NIC_REG_WRITE16(ctx, offset, val) reg_write16((ctx)->io_base + (offset), (val))
#define NIC_REG_READ32(ctx, offset) reg_read32((ctx)->io_base + (offset))
#define NIC_REG_WRITE32(ctx, offset, val) reg_write32((ctx)->io_base + (offset), (val))

/* Register access validation macros */
#define NIC_VALIDATE_REG_ACCESS(ctx, offset) \
    do { \
        if (!nic_is_register_accessible(ctx, offset)) \
            return HAL_ERROR_INVALID_STATE; \
    } while(0)

#define NIC_SAFE_REG_READ16(ctx, offset, result) \
    do { \
        NIC_VALIDATE_REG_ACCESS(ctx, offset); \
        *(result) = NIC_REG_READ16(ctx, offset); \
    } while(0)

#define NIC_SAFE_REG_WRITE16(ctx, offset, val) \
    do { \
        NIC_VALIDATE_REG_ACCESS(ctx, offset); \
        NIC_REG_WRITE16(ctx, offset, val); \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* _REGISTER_ACCESS_H_ */