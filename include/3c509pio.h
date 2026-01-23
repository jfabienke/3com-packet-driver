/**
 * @file 3c509b_pio.h
 * @brief 3C509B EL3 PIO Fast Path - GPT-5 Implementation
 *
 * This header provides register definitions and helper functions for the
 * 3C509B EtherLink III PIO fast path that completely bypasses the DMA
 * mapping layer for performance.
 *
 * Key Features:
 * - Direct I/O port access to EL3 windowed registers
 * - TX FIFO management with space checking
 * - No cache operations or DMA mapping overhead
 * - Safe timeout handling for TX space availability
 */

#ifndef _3C509B_PIO_H_
#define _3C509B_PIO_H_

#include <stdint.h>
#include <stdbool.h>
#include <dos.h>

#ifdef __cplusplus
extern "C" {
#endif

/* EL3 (EtherLink III) windowed register interface */
#define EL3_CMD             0x0E    /* Command/Status register */
#define EL3_STATUS          0x0E    /* Status (read) */
#define EL3_WINDOW          0x0E    /* Window select (write) */
#define EL3_DATA_PORT       0x00    /* Data port (shared RX/TX FIFO) */

/* Window 1 (Operating) register offsets */
#define WN1_TX_FREE         0x0C    /* TX FIFO free bytes (word) */
#define WN1_TX_STATUS       0x0B    /* TX status (byte) */
#define WN1_RX_STATUS       0x08    /* RX status (word) */

/* EL3 Command codes (upper 11 bits are command, lower 5 bits are parameter) */
#define CMD_GLOBAL_RESET    0x0000  /* Global reset */
#define CMD_SELECT_WINDOW   0x0800  /* Select register window (0-7) */
#define CMD_TX_ENABLE       0x4800  /* Enable transmitter (9 << 11) */
#define CMD_TX_DISABLE      0x5000  /* Disable transmitter (10 << 11) */
#define CMD_TX_RESET        0x5800  /* Reset transmitter (11 << 11) */
#define CMD_ENABLE_RX       0x0001  /* Enable receiver */
#define CMD_DISABLE_RX      0x0002  /* Disable receiver */
#define CMD_ACK_INTR        0xA000  /* Acknowledge interrupt */
#define CMD_SET_INTR_MASK   0x8000  /* Set interrupt mask */
#define CMD_SET_RX_FILTER   0x6000  /* Set RX filter */
#define CMD_RX_DISCARD      0x2000  /* Discard current RX packet */
#define CMD_SET_TX_THRESHOLD 0x9800  /* Set TX start threshold (19 << 11) */

/* Status register bits */
#define STAT_TX_COMPLETE    BIT(2)  /* TX complete */
#define STAT_TX_AVAILABLE   BIT(1)  /* TX space available */
#define STAT_RX_COMPLETE    BIT(3)  /* RX complete */
#define STAT_CMD_IN_PROG    BIT(12) /* Command in progress */
#define STAT_WINDOW_MASK    0xE000  /* Current window (bits 13-15) */

/* Interrupt bits for CMD_ACK_INTR */
#define INTR_LATCH          BIT(0)  /* Interrupt latch */
#define INTR_TX_AVAIL       BIT(1)  /* TX space available */
#define INTR_TX_COMPLETE    BIT(2)  /* TX complete */
#define INTR_RX_COMPLETE    BIT(3)  /* RX complete */
#define INTR_RX_EARLY       BIT(4)  /* RX early warning */
#define INTR_STATS_FULL     BIT(7)  /* Statistics counter full */

/* GPT-5 A: Frame size constants - sizes EXCLUDE 4-byte FCS */
#define ETH_MIN_FRAME       60      /* Minimum Ethernet frame (without FCS) */
#define ETH_MAX_FRAME       1514    /* Maximum Ethernet frame (without FCS) */
#define ETH_MAX_FRAME_VLAN  1518    /* Maximum with VLAN tag (without FCS) */
#define ETH_HEADER_LEN      14      /* Ethernet header length */
#define ETH_FCS_LEN         4       /* Frame Check Sequence (added by hardware) */

/* TX configuration - Based on 3Com Manual */
#define EL3_TX_TIMEOUT_MS   25      /* TX space wait timeout */
#define EL3_TX_PREAMBLE_SIZE 4      /* Manual: 4-byte preamble (2 words) */
#define EL3_TX_THRESHOLD_SAFE 1792  /* High threshold to prevent underruns */
#define EL3_TX_THRESHOLD_MAX  2047  /* Maximum threshold value (11 bits) */
#define EL3_TX_THRESHOLD_MIN  512   /* Minimum useful threshold */
#define EL3_TX_THRESHOLD_INCREMENT 256  /* Increase amount on underrun */

/* TX Status Register bits (Window 1, Port 0x0B) - Manual Table 6-4 */
#define TX_STATUS_CM        0x80    /* Complete (not error!) */
#define TX_STATUS_IS        0x40    /* Interrupt Status */
#define TX_STATUS_JB        0x20    /* Jabber Error */
#define TX_STATUS_UN        0x10    /* Underrun Error */  
#define TX_STATUS_MC        0x08    /* Maximum Collisions Error */
#define TX_STATUS_ERROR_MASK (TX_STATUS_JB | TX_STATUS_UN | TX_STATUS_MC)

/* I/O port access macros - assume these are defined elsewhere or implement */
#ifndef inb
extern uint8_t inb(uint16_t port);
extern void outb(uint8_t value, uint16_t port);
extern uint16_t inw(uint16_t port);
extern void outw(uint16_t value, uint16_t port);
extern void outsw(uint16_t port, const void *buffer, uint16_t count);
#endif

/*==============================================================================
 * EL3 Helper Functions
 *==============================================================================*/

/**
 * @brief Select EL3 register window with caching (Manual: fast command)
 * @param nic NIC information structure (for window caching)
 * @param window Window number (0-7)
 * @note Caches window state to reduce unnecessary I/O operations
 */
void el3_select_window_cached(struct nic_info *nic, uint8_t window);

/**
 * @brief Select EL3 register window (Manual: fast command, uncached)
 * @param io_base NIC I/O base address
 * @param window Window number (0-7)
 */
static inline void el3_select_window(uint16_t io_base, uint8_t window) {
    el3_execute_fast_command(io_base, CMD_SELECT_WINDOW | (window & 0x07));
}

/**
 * @brief Get current EL3 register window
 * @param io_base NIC I/O base address
 * @return Current window number (0-7)
 */
static inline uint8_t el3_get_current_window(uint16_t io_base) {
    return (uint8_t)((inw(io_base + EL3_STATUS) & STAT_WINDOW_MASK) >> 13);
}

/**
 * @brief Check if command is still in progress
 * @param io_base NIC I/O base address
 * @return true if command in progress
 */
static inline bool el3_command_in_progress(uint16_t io_base) {
    return (inw(io_base + EL3_STATUS) & STAT_CMD_IN_PROG) != 0;
}

/**
 * @brief Wait for command to complete
 * @param io_base NIC I/O base address
 * @param timeout_ms Maximum wait time in milliseconds
 * @return true if command completed, false on timeout
 */
bool el3_wait_command_complete(uint16_t io_base, uint32_t timeout_ms);

/**
 * @brief Execute fast EL3 command (completes in one cycle)
 * @param io_base NIC I/O base address
 * @param command Command to execute
 * @note Manual: Fast commands (select window, ack interrupt) execute immediately
 */
static inline void el3_execute_fast_command(uint16_t io_base, uint16_t command) {
    outw(command, io_base + EL3_CMD);
    /* No wait needed - fast commands complete in one cycle */
}

/**
 * @brief Execute slow EL3 command (requires polling for completion)
 * @param io_base NIC I/O base address
 * @param command Command to execute
 * @param timeout_ms Maximum wait time in milliseconds
 * @return true if command completed, false on timeout
 * @note Manual: Slow commands (reset, enable/disable) need polling
 */
bool el3_execute_slow_command(uint16_t io_base, uint16_t command, uint32_t timeout_ms);

/**
 * @brief Get available TX FIFO space in bytes
 * @param io_base NIC I/O base address
 * @return Available space in bytes
 * @note Automatically switches to window 1 if needed
 */
uint16_t el3_get_tx_free_space(uint16_t io_base);

/**
 * @brief Wait for sufficient TX FIFO space
 * @param io_base NIC I/O base address
 * @param needed_bytes Required space in bytes
 * @param timeout_ms Maximum wait time in milliseconds
 * @return true if space available, false on timeout
 */
bool el3_wait_tx_space(uint16_t io_base, uint16_t needed_bytes, uint32_t timeout_ms);

/**
 * @brief Enable EL3 transmitter (Manual: slow command, required before transmission)
 * @param io_base NIC I/O base address
 * @return true if command completed, false on timeout
 */
static inline bool el3_enable_tx(uint16_t io_base) {
    return el3_execute_slow_command(io_base, CMD_TX_ENABLE, 100);
}

/**
 * @brief Disable EL3 transmitter (Manual: slow command)
 * @param io_base NIC I/O base address
 * @return true if command completed, false on timeout
 */
static inline bool el3_disable_tx(uint16_t io_base) {
    return el3_execute_slow_command(io_base, CMD_TX_DISABLE, 100);
}

/**
 * @brief Reset EL3 transmitter (Manual: slow command, required after errors)
 * @param io_base NIC I/O base address
 * @return true if command completed, false on timeout
 */
static inline bool el3_reset_tx(uint16_t io_base) {
    return el3_execute_slow_command(io_base, CMD_TX_RESET, 100);
}

/**
 * @brief Set TX start threshold (Manual: fast command, controls when TX begins)
 * @param io_base NIC I/O base address
 * @param threshold Byte count threshold (0-2047)
 */
static inline void el3_set_tx_threshold(uint16_t io_base, uint16_t threshold) {
    el3_execute_fast_command(io_base, CMD_SET_TX_THRESHOLD | (threshold & 0x7FF));
}

/**
 * @brief Enable EL3 receiver (Manual: slow command)
 * @param io_base NIC I/O base address
 * @return true if command completed, false on timeout
 */
static inline bool el3_enable_rx(uint16_t io_base) {
    return el3_execute_slow_command(io_base, CMD_ENABLE_RX, 100);
}

/**
 * @brief Disable EL3 receiver (Manual: slow command)
 * @param io_base NIC I/O base address
 * @return true if command completed, false on timeout
 */
static inline bool el3_disable_rx(uint16_t io_base) {
    return el3_execute_slow_command(io_base, CMD_DISABLE_RX, 100);
}

/**
 * @brief Write TX preamble to FIFO (Manual: 4-byte header before data)
 * @param io_base NIC I/O base address  
 * @param frame_length Total frame length in bytes
 * @param disable_crc Set true if driver provides CRC (normally false)
 * @param request_int Set true to request completion interrupt
 */
static inline void el3_write_tx_preamble(uint16_t io_base, uint16_t frame_length, 
                                       bool disable_crc, bool request_int) {
    uint16_t word1 = (frame_length & 0x7FF);  /* Bits 0-10: length */
    if (disable_crc) word1 |= 0x2000;         /* Bit 13: DCG */
    if (request_int) word1 |= 0x8000;         /* Bit 15: Int */
    
    uint16_t word2 = 0x0000;                  /* Reserved, must be zero */
    
    /* Write preamble as two words */
    outw(word1, io_base + EL3_DATA_PORT);
    outw(word2, io_base + EL3_DATA_PORT);
}

/**
 * @brief Write data to EL3 data port (TX FIFO)
 * @param io_base NIC I/O base address
 * @param buffer Data buffer to write
 * @param length Length in bytes
 * @note Handles both word writes and odd bytes correctly
 */
void el3_write_fifo_data(uint16_t io_base, const void *buffer, uint16_t length);

/**
 * @brief Drain TX status stack up to manual-specified limit
 * @param io_base NIC I/O base address
 * @param max_entries Maximum entries to drain (31 per manual)
 * @return Number of entries drained
 */
int el3_drain_tx_status_stack(uint16_t io_base, uint8_t max_entries);

/**
 * @brief Get current TX threshold setting
 * @param nic NIC information structure  
 * @return Current TX threshold value
 */
uint16_t el3_get_tx_threshold(struct nic_info *nic);

/**
 * @brief Adjust TX threshold adaptively on underrun (Manual: increase, never decrease)
 * @param nic NIC information structure
 * @param had_underrun True if underrun error occurred
 * @return New threshold value, or 0 if no change
 */
uint16_t el3_adjust_tx_threshold(struct nic_info *nic, bool had_underrun);

/**
 * @brief Acknowledge EL3 interrupts (Manual: fast command, INTR_LATCH bit mandatory)
 * @param io_base NIC I/O base address
 * @param intr_mask Interrupt bits to acknowledge
 * @note Manual: INTR_LATCH (bit 0) must always be set for proper acknowledgment
 */
static inline void el3_ack_interrupt(uint16_t io_base, uint16_t intr_mask) {
    /* Manual: Fast command, always include INTR_LATCH bit for proper acknowledgment */
    el3_execute_fast_command(io_base, CMD_ACK_INTR | ((intr_mask | INTR_LATCH) & 0xFF));
}

/*==============================================================================
 * Forward Declarations for 3C509B PIO Implementation
 *==============================================================================*/

struct nic_info;

/* PIO transmit function that bypasses all DMA mapping */
int el3_3c509b_pio_transmit(struct nic_info *nic, const uint8_t *buffer, uint16_t length);

/* PIO initialization for 3C509B */
int el3_3c509b_pio_init(struct nic_info *nic);

/* PIO cleanup for 3C509B */
void el3_3c509b_pio_cleanup(struct nic_info *nic);

#ifdef __cplusplus
}
#endif

#endif /* _3C509B_PIO_H_ */