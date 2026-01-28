/**
 * @file 3c509b_rt.c
 * @brief 3Com 3C509B NIC driver - Runtime functions (ROOT segment)
 *
 * This file contains only the runtime functions needed after initialization:
 * - Packet send/receive
 * - Interrupt handling
 * - Link status
 * - Register access helpers
 *
 * Init-only functions are in 3c509b_init.c (OVERLAY segment)
 *
 * Updated: 2026-01-28 05:00:00 UTC
 */

#include "3c509b.h"
#include "hardware.h"
#include "logging.h"
#include "memory.h"
#include "common.h"
#include "bufaloc.h"
#include "pktops.h"
#include "medictl.h"
#include "nic_defs.h"
#include "irqmit.h"
#include "hwchksm.h"
#include "dirpioe.h"
#include "cachecoh.h"
#include "cachemgt.h"
#include <string.h>

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

/* Public runtime functions */
int _3c509b_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length);
int _3c509b_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length);
int _3c509b_send_packet_direct_pio(nic_info_t *nic, const uint8_t *packet, size_t length);
int _3c509b_check_interrupt(nic_info_t *nic);
void _3c509b_handle_interrupt(nic_info_t *nic);
int _3c509b_enable_interrupts(nic_info_t *nic);
int _3c509b_disable_interrupts(nic_info_t *nic);
int _3c509b_get_link_status(nic_info_t *nic);
int _3c509b_get_link_speed(nic_info_t *nic);
int _3c509b_set_promiscuous(nic_info_t *nic, bool enable);
int _3c509b_set_multicast(nic_info_t *nic, const uint8_t *mc_list, int count);

/* Internal helper functions (also needed by init) */
void _3c509b_select_window(nic_info_t *nic, uint8_t window);
int _3c509b_wait_for_cmd_busy(nic_info_t *nic, uint32_t timeout_ms);
void _3c509b_write_command(nic_info_t *nic, uint16_t command);
uint16_t _3c509b_read_reg(nic_info_t *nic, uint16_t reg);
void _3c509b_write_reg(nic_info_t *nic, uint16_t reg, uint16_t value);

/* PIO cache coherency helpers */
static void _3c509b_pio_prepare_rx_buffer(nic_info_t *nic, void *buffer, size_t length);
static void _3c509b_pio_complete_rx_buffer(nic_info_t *nic, void *buffer, size_t length);
static void _3c509b_pio_prepare_tx_buffer(nic_info_t *nic, const void *buffer, size_t length);

/* ============================================================================
 * Register Access Functions (Shared between runtime and init)
 * ============================================================================ */

uint16_t _3c509b_read_reg(nic_info_t *nic, uint16_t reg) {
    return inw(nic->io_base + reg);
}

void _3c509b_write_reg(nic_info_t *nic, uint16_t reg, uint16_t value) {
    outw(nic->io_base + reg, value);
}

/**
 * Select register window with proper CMD_BUSY checking
 */
void _3c509b_select_window(nic_info_t *nic, uint8_t window) {
    /* Wait for any pending command to complete */
    _3c509b_wait_for_cmd_busy(nic, 100);

    /* Select the window */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SELECT_WINDOW | window);
}

/**
 * Wait for CMD_BUSY to clear
 */
int _3c509b_wait_for_cmd_busy(nic_info_t *nic, uint32_t timeout_ms) {
    uint16_t status;

    while (timeout_ms > 0) {
        status = inw(nic->io_base + _3C509B_STATUS_REG);
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            return SUCCESS;
        }
        udelay(1000);
        timeout_ms--;
    }
    return ERROR_TIMEOUT;
}

/**
 * Write command with proper CMD_BUSY checking
 */
void _3c509b_write_command(nic_info_t *nic, uint16_t command) {
    /* Wait for any pending command to complete */
    _3c509b_wait_for_cmd_busy(nic, 100);

    /* Write the command */
    outw(nic->io_base + _3C509B_COMMAND_REG, command);
}

static int _3c509b_wait_for_command(nic_info_t *nic, uint32_t timeout_ms) {
    uint16_t status;

    while (timeout_ms > 0) {
        status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);

        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            return SUCCESS; /* Command completed */
        }

        udelay(1000); /* 1ms delay */
        timeout_ms--;
    }

    LOG_ERROR("3C509B command timeout");
    return ERROR_TIMEOUT;
}

/* ============================================================================
 * Packet Operations - Runtime Core
 * ============================================================================ */

int _3c509b_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length) {
    /* C89: All declarations at start of function */
    uint16_t status;
    uint16_t tx_free;
    uint16_t tx_fifo;
    size_t words;
    const uint16_t *packet_words;
    size_t i;

    if (!nic || !packet || length == 0) {
        return ERROR_INVALID_PARAM;
    }

    if (length > nic->mtu) {
        LOG_ERROR("Packet too large: %zu > %d", length, nic->mtu);
        return ERROR_INVALID_PARAM;
    }

    /* Ensure we're in Window 1 for TX operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Check if TX is ready */
    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        LOG_DEBUG("TX not available, status=0x%X", status);
        return ERROR_BUSY;
    }

    /* Check TX free space */
    tx_free = _3c509b_read_reg(nic, _3C509B_TX_FREE);
    if (tx_free < (uint16_t)length) {
        LOG_DEBUG("Insufficient TX FIFO space: need %zu, have %d", length, tx_free);
        return ERROR_BUSY;
    }

    /* Write packet length to TX FIFO - this starts transmission */
    tx_fifo = nic->io_base + _3C509B_TX_FIFO;
    outw(tx_fifo, (uint16_t)length);

    /* Write packet data to TX FIFO using PIO */
    words = length / 2;
    packet_words = (const uint16_t*)packet;

    for (i = 0; i < words; i++) {
        outw(tx_fifo, packet_words[i]);
    }

    /* Write remaining byte if length is odd */
    if (length & 1) {
        outb(tx_fifo, packet[length - 1]);
    }

    /* Update statistics */
    nic->tx_packets++;
    nic->tx_bytes += length;

    LOG_TRACE("Sent packet of %zu bytes", length);

    return SUCCESS;
}

int _3c509b_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length) {
    uint16_t status;
    uint16_t rx_status;
    uint16_t packet_length;
    uint16_t rx_fifo;
    size_t words;
    uint16_t *buffer_words;
    size_t i;

    if (!nic || !buffer || !length) {
        return ERROR_INVALID_PARAM;
    }

    /* Ensure we're in Window 1 for RX operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Check if RX packet is available */
    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_RX_COMPLETE)) {
        *length = 0;
        return ERROR_NO_DATA; /* No packet available */
    }

    /* Read RX status and length */
    rx_status = _3c509b_read_reg(nic, _3C509B_RX_STATUS);
    packet_length = rx_status & _3C509B_RXSTAT_LEN_MASK;

    /* Check for errors */
    if (rx_status & (_3C509B_RXSTAT_ERROR | _3C509B_RXSTAT_INCOMPLETE)) {
        LOG_DEBUG("RX error: status=0x%X", rx_status);

        /* Discard the packet */
        _3c509b_write_command(nic, _3C509B_CMD_RX_DISCARD);

        nic->rx_errors++;
        *length = 0;
        return ERROR_IO;
    }

    /* Check if packet fits in buffer */
    if (packet_length > *length) {
        LOG_WARNING("RX buffer too small: need %d, have %zu", packet_length, *length);

        /* Discard the packet */
        _3c509b_write_command(nic, _3C509B_CMD_RX_DISCARD);

        *length = packet_length; /* Return required size */
        return ERROR_NO_MEMORY;
    }

    /* Read packet data from RX FIFO */
    rx_fifo = nic->io_base + _3C509B_RX_FIFO;

    /* Read 16-bit words first */
    words = packet_length / 2;
    buffer_words = (uint16_t*)buffer;

    for (i = 0; i < words; i++) {
        buffer_words[i] = inw(rx_fifo);
    }

    /* Read remaining byte if length is odd */
    if (packet_length & 1) {
        buffer[packet_length - 1] = inb(rx_fifo);
    }

    /* Update statistics */
    nic->rx_packets++;
    nic->rx_bytes += packet_length;

    *length = packet_length;

    LOG_TRACE("Received packet of %d bytes", packet_length);

    return SUCCESS;
}

/**
 * Enhanced receive function using Group 2B buffer allocation and Group 2C API integration
 */
static int _3c509b_receive_packet_buffered(nic_info_t *nic) {
    buffer_desc_t *rx_buffer = NULL;
    uint16_t status, rx_status, packet_length;
    uint16_t rx_fifo;
    size_t words;
    int result = SUCCESS;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Ensure we're in Window 1 for RX operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Check if RX packet is available */
    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_RX_COMPLETE)) {
        return ERROR_NO_DATA; /* No packet available */
    }

    /* Read RX status and length */
    rx_status = _3c509b_read_reg(nic, _3C509B_RX_STATUS);
    packet_length = rx_status & _3C509B_RXSTAT_LEN_MASK;

    /* Check for errors */
    if (rx_status & (_3C509B_RXSTAT_ERROR | _3C509B_RXSTAT_INCOMPLETE)) {
        LOG_DEBUG("RX error: status=0x%X", rx_status);

        /* Discard the packet */
        _3c509b_write_command(nic, _3C509B_CMD_RX_DISCARD);

        nic->rx_errors++;
        return ERROR_IO;
    }

    /* Allocate buffer for received packet using RX_COPYBREAK optimization */
    rx_buffer = rx_copybreak_alloc(packet_length);
    if (!rx_buffer) {
        LOG_ERROR("Failed to allocate RX buffer for %d byte packet", packet_length);

        /* Discard the packet */
        _3c509b_write_command(nic, _3C509B_CMD_RX_DISCARD);

        nic->rx_dropped++;
        return ERROR_NO_MEMORY;
    }

    /* Read packet data from RX FIFO into allocated buffer */
    rx_fifo = nic->io_base + _3C509B_RX_FIFO;

    /* Read 16-bit words first */
    words = packet_length / 2;
    {
        uint16_t *buffer_words = (uint16_t*)rx_buffer->data;
        size_t i;

        for (i = 0; i < words; i++) {
            buffer_words[i] = inw(rx_fifo);
        }
    }

    /* Read remaining byte if length is odd */
    if (packet_length & 1) {
        ((uint8_t*)rx_buffer->data)[packet_length - 1] = inb(rx_fifo);
    }

    /* Update buffer descriptor */
    rx_buffer->used = packet_length;
    buffer_set_state(rx_buffer, BUFFER_STATE_IN_USE);

    /* Verify checksums with CPU optimization (Phase 2.1) */
    if (packet_length >= 34) { /* Minimum for Ethernet + IP header */
        int checksum_result = hw_checksum_verify_inbound_packet((uint8_t*)rx_buffer->data, packet_length);
        if (checksum_result < 0) {
            LOG_DEBUG("Checksum verification failed for inbound packet");
            /* Continue anyway - many stacks don't require perfect checksums */
        } else if (checksum_result > 0) {
            LOG_DEBUG("Checksum verification passed for inbound packet");
        }
    }

    /* Process received packet through Group 2C API */
    result = packet_process_received((uint8_t*)rx_buffer->data, packet_length, nic->index);
    if (result != SUCCESS) {
        LOG_WARNING("Packet processing failed: %d", result);
        nic->rx_dropped++;
    } else {
        /* Update statistics */
        nic->rx_packets++;
        nic->rx_bytes += packet_length;

        LOG_TRACE("Processed received packet of %d bytes", packet_length);
    }

    /* Free the buffer using RX_COPYBREAK */
    rx_copybreak_free(rx_buffer);

    return result;
}

/* ============================================================================
 * Interrupt Handling - Runtime Core
 * ============================================================================ */

int _3c509b_check_interrupt(nic_info_t *nic) {
    uint16_t status;

    if (!nic) {
        return 0;
    }

    /* Read interrupt status */
    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);

    /* Check if any interrupt bits are set - return 1 if set, 0 if not */
    return (status & _3C509B_STATUS_INT_LATCH) ? 1 : 0;
}

void _3c509b_handle_interrupt(nic_info_t *nic) {
    uint16_t status;
    uint16_t tx_status;
    int rx_result;

    if (!nic) {
        return;
    }

    /* Ensure we're in Window 1 for interrupt handling */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);

    LOG_TRACE("3C509B interrupt: status=0x%X", status);

    /* Handle TX completion */
    if (status & _3C509B_STATUS_TX_COMPLETE) {
        /* TX completed successfully */
        LOG_TRACE("TX complete");

        /* Clear interrupt by reading TX status */
        tx_status = _3c509b_read_reg(nic, _3C509B_TX_STATUS);

        /* Check for TX errors */
        if (tx_status & (_3C509B_TXSTAT_JABBER | _3C509B_TXSTAT_UNDERRUN | _3C509B_TXSTAT_MAX_COLLISIONS)) {
            LOG_DEBUG("TX error: status=0x%X", tx_status);
            nic->tx_errors++;
        }
    }

    /* Handle RX completion - process packets using buffer allocation */
    if (status & _3C509B_STATUS_RX_COMPLETE) {
        LOG_TRACE("RX complete - processing buffered");

        /* Process received packets with buffer allocation and API integration */
        rx_result = _3c509b_receive_packet_buffered(nic);
        if (rx_result != SUCCESS && rx_result != ERROR_NO_DATA) {
            LOG_DEBUG("RX processing failed: %d", rx_result);
        }
    }

    /* Handle adapter failure */
    if (status & _3C509B_STATUS_ADAPTER_FAILURE) {
        LOG_ERROR("3C509B adapter failure detected");

        /* Basic error handling implemented */
        nic->status |= NIC_STATUS_ERROR;
    }

    /* Acknowledge interrupt by writing to command register */
    _3c509b_write_command(nic, _3C509B_CMD_ACK_INTR | (status & 0x00FF));
}

/**
 * @brief Process single interrupt event for batching system (3C509B)
 */
int _3c509b_process_single_event(nic_info_t *nic, interrupt_event_type_t *event_type) {
    uint16_t status;
    uint16_t tx_status;
    int rx_result;

    if (!nic || !event_type) {
        return ERROR_INVALID_PARAM;
    }

    /* Ensure we're in Window 1 for interrupt handling */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);

    /* Process highest priority events first */

    /* Handle adapter failure (highest priority) */
    if (status & _3C509B_STATUS_ADAPTER_FAILURE) {
        *event_type = EVENT_TYPE_RX_ERROR; /* Treat as general error */
        LOG_ERROR("3C509B adapter failure detected");

        nic->status |= NIC_STATUS_ERROR;

        /* Acknowledge the interrupt */
        _3c509b_write_command(nic, _3C509B_CMD_ACK_INTR | _3C509B_STATUS_ADAPTER_FAILURE);

        return 1;
    }

    /* Handle TX completion */
    if (status & _3C509B_STATUS_TX_COMPLETE) {
        *event_type = EVENT_TYPE_TX_COMPLETE;

        /* Read TX status to check for errors */
        tx_status = _3c509b_read_reg(nic, _3C509B_TX_STATUS);

        if (tx_status & (_3C509B_TXSTAT_JABBER | _3C509B_TXSTAT_UNDERRUN | _3C509B_TXSTAT_MAX_COLLISIONS)) {
            LOG_DEBUG("TX error: status=0x%X", tx_status);
            nic->tx_errors++;
            *event_type = EVENT_TYPE_TX_ERROR;
        }

        /* Acknowledge the interrupt */
        _3c509b_write_command(nic, _3C509B_CMD_ACK_INTR | _3C509B_STATUS_TX_COMPLETE);

        return 1;
    }

    /* Handle RX completion */
    if (status & _3C509B_STATUS_RX_COMPLETE) {
        *event_type = EVENT_TYPE_RX_COMPLETE;

        /* Process one received packet */
        rx_result = _3c509b_receive_packet_buffered(nic);
        if (rx_result != SUCCESS && rx_result != ERROR_NO_DATA) {
            LOG_DEBUG("RX processing failed: %d", rx_result);
            *event_type = EVENT_TYPE_RX_ERROR;
        }

        /* Acknowledge the interrupt */
        _3c509b_write_command(nic, _3C509B_CMD_ACK_INTR | _3C509B_STATUS_RX_COMPLETE);

        return 1;
    }

    /* No work available */
    return 0;
}

/**
 * @brief Enhanced check interrupt function for 3C509B batching
 */
int _3c509b_check_interrupt_batched(nic_info_t *nic) {
    uint16_t status;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Ensure we're in Window 1 */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);

    /* Check if any interrupt bits are set that we handle */
    if (status & (_3C509B_STATUS_TX_COMPLETE |
                  _3C509B_STATUS_RX_COMPLETE |
                  _3C509B_STATUS_ADAPTER_FAILURE)) {
        return 1; /* Work available */
    }

    return 0; /* No work */
}

/**
 * @brief Enhanced interrupt handler with batching support for 3C509B
 */
int _3c509b_handle_interrupt_batched(nic_info_t *nic) {
    interrupt_mitigation_context_t *im_ctx;

    if (!nic || !nic->private_data) {
        return ERROR_INVALID_PARAM;
    }

    /* Get interrupt mitigation context from private data */
    im_ctx = (interrupt_mitigation_context_t *)nic->private_data;

    if (!is_interrupt_mitigation_enabled(im_ctx)) {
        /* Fall back to legacy single-event processing */
        _3c509b_handle_interrupt(nic);
        return 1;
    }

    /* Process batched interrupts */
    return process_batched_interrupts_3c509b(im_ctx);
}

int _3c509b_enable_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Select Window 1 and enable interrupt mask */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    _3c509b_write_command(nic, _3C509B_CMD_SET_INTR_ENABLE |
                         (_3C509B_IMASK_TX_COMPLETE | _3C509B_IMASK_RX_COMPLETE | _3C509B_IMASK_ADAPTER_FAILURE));

    return SUCCESS;
}

int _3c509b_disable_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Select Window 1 and disable all interrupts */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    _3c509b_write_command(nic, _3C509B_CMD_SET_INTR_ENABLE | 0);

    return SUCCESS;
}

/* ============================================================================
 * Link Status Functions - Runtime
 * ============================================================================ */

int _3c509b_get_link_status(nic_info_t *nic) {
    int link_status;
    uint16_t media_status;

    if (!nic) {
        return 0;
    }

    /* Use enhanced media control link detection */
    link_status = check_media_link_status(nic);
    if (link_status < 0) {
        LOG_DEBUG("Link status check failed, falling back to basic detection");

        /* Fallback to basic link detection */
        _3c509b_select_window(nic, _3C509B_WINDOW_4);
        media_status = _3c509b_read_reg(nic, _3C509B_W4_NETDIAG);

        /* Check link beat for 10Base-T - return 1 if up, 0 if down */
        return (media_status & 0x0800) ? 1 : 0;
    }

    return link_status ? 1 : 0;
}

int _3c509b_get_link_speed(nic_info_t *nic) {
    if (!nic) {
        return 0;
    }

    /* 3C509B is always 10 Mbps */
    return 10;
}

int _3c509b_set_promiscuous(nic_info_t *nic, bool enable) {
    uint16_t filter;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Select Window 1 for RX filter configuration */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    if (enable) {
        filter |= _3C509B_RX_FILTER_PROMISCUOUS;
    }

    _3c509b_write_command(nic, _3C509B_CMD_SET_RX_FILTER | filter);

    LOG_DEBUG("3C509B promiscuous mode %s", enable ? "enabled" : "disabled");

    return SUCCESS;
}

int _3c509b_set_multicast(nic_info_t *nic, const uint8_t *mc_list, int count) {
    uint16_t filter;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Select Window 1 for RX filter configuration */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    if (count > 0) {
        filter |= _3C509B_RX_FILTER_MULTICAST;
    }

    _3c509b_write_command(nic, _3C509B_CMD_SET_RX_FILTER | filter);

    LOG_DEBUG("3C509B multicast filter updated with %d addresses", count);

    return SUCCESS;
}

/* ============================================================================
 * Direct PIO Transmit Optimization Implementation
 * ============================================================================ */

/* Assembly function prototypes */
extern void direct_pio_outsw(const void* src_buffer, uint16_t dst_port, uint16_t word_count);
extern int send_packet_direct_pio_asm(const void* stack_buffer, uint16_t length, uint16_t io_base);
extern void direct_pio_header_and_payload(uint16_t io_port, const uint8_t* dest_mac,
                                         const uint8_t* src_mac, uint16_t ethertype,
                                         const void* payload, uint16_t payload_len);

/**
 * @brief Send packet directly via PIO (eliminates intermediate copy)
 */
int send_packet_direct_pio(const void* stack_buffer, uint16_t length, uint16_t io_base) {
    uint16_t tx_fifo;

    if (!stack_buffer || length == 0 || length > _3C509B_MAX_MTU) {
        LOG_ERROR("Invalid parameters for direct PIO send");
        return ERROR_INVALID_PARAM;
    }

    /* Calculate TX FIFO address */
    tx_fifo = io_base + _3C509B_TX_FIFO;

    /* Write packet length to TX FIFO - this starts transmission */
    outw(tx_fifo, length);

    /* Use CPU-optimized direct transfer with adaptive I/O sizing */
    if (should_use_enhanced_pio(length)) {
        /* Use enhanced CPU-optimized transfer for suitable packets on 386+ systems */
        return send_packet_direct_pio_enhanced(stack_buffer, length, io_base);
    } else if (length >= 32) {
        /* Use standard assembly optimization for larger packets on 286 systems */
        return send_packet_direct_pio_asm(stack_buffer, length, io_base);
    } else {
        /* Use C implementation for small packets to avoid call overhead */
        size_t words = length / 2;
        const uint16_t *packet_words = (const uint16_t*)stack_buffer;
        size_t i;

        for (i = 0; i < words; i++) {
            outw(tx_fifo, packet_words[i]);
        }

        /* Write remaining byte if length is odd */
        if (length & 1) {
            const uint8_t *packet_bytes = (const uint8_t*)stack_buffer;
            outb(tx_fifo, packet_bytes[length - 1]);
        }

        return SUCCESS;
    }
}

/**
 * @brief Direct PIO transmit with header construction on-the-fly
 */
int send_packet_direct_pio_with_header(nic_info_t *nic, const uint8_t *dest_mac,
                                      uint16_t ethertype, const void* payload, uint16_t payload_len) {
    uint16_t total_length;
    uint16_t tx_fifo;
    uint16_t status;
    uint16_t tx_free;
    uint16_t actual_length;
    uint16_t pad_bytes;
    uint16_t pad_words;
    uint16_t i;

    if (!nic || !dest_mac || !payload || payload_len == 0) {
        return ERROR_INVALID_PARAM;
    }

    /* Calculate total frame length */
    total_length = ETH_HEADER_LEN + payload_len;
    if (total_length > nic->mtu) {
        LOG_ERROR("Frame too large: %d > %d", total_length, nic->mtu);
        return ERROR_INVALID_PARAM;
    }

    /* Ensure minimum frame size */
    if (total_length < ETH_MIN_FRAME) {
        total_length = ETH_MIN_FRAME;
    }

    /* Ensure we're in Window 1 for TX operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Check if TX is ready */
    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        LOG_DEBUG("TX not available, status=0x%X", status);
        return ERROR_BUSY;
    }

    /* Check TX free space */
    tx_free = _3c509b_read_reg(nic, _3C509B_TX_FREE);
    if (tx_free < total_length) {
        LOG_DEBUG("Insufficient TX FIFO space: need %d, have %d", total_length, tx_free);
        return ERROR_BUSY;
    }

    /* Calculate TX FIFO address */
    tx_fifo = nic->io_base + _3C509B_TX_FIFO;

    /* Write total frame length to TX FIFO */
    outw(tx_fifo, total_length);

    /* Use assembly-optimized direct header and payload transfer */
    direct_pio_header_and_payload(tx_fifo, dest_mac, nic->mac, ethertype, payload, payload_len);

    /* Handle padding for minimum frame size if needed */
    actual_length = ETH_HEADER_LEN + payload_len;
    if (actual_length < ETH_MIN_FRAME) {
        pad_bytes = ETH_MIN_FRAME - actual_length;
        pad_words = pad_bytes / 2;

        /* Write padding words */
        for (i = 0; i < pad_words; i++) {
            outw(tx_fifo, 0);
        }

        /* Write padding byte if odd */
        if (pad_bytes & 1) {
            outb(tx_fifo, 0);
        }
    }

    /* Update statistics */
    nic->tx_packets++;
    nic->tx_bytes += total_length;

    LOG_TRACE("Sent packet of %d bytes via direct PIO with header", total_length);

    return SUCCESS;
}

/**
 * @brief Enhanced 3c509B send packet with direct PIO optimization
 */
int _3c509b_send_packet_direct_pio(nic_info_t *nic, const uint8_t *packet, size_t length) {
    uint16_t status;
    uint16_t tx_free;
    uint8_t *tx_packet;
    int checksum_result;
    int result;

    if (!nic || !packet || length == 0) {
        return ERROR_INVALID_PARAM;
    }

    if (length > nic->mtu) {
        LOG_ERROR("Packet too large: %zu > %d", length, nic->mtu);
        return ERROR_INVALID_PARAM;
    }

    /* Ensure we're in Window 1 for TX operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Check if TX is ready */
    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        LOG_DEBUG("TX not available, status=0x%X", status);
        return ERROR_BUSY;
    }

    /* Check TX free space */
    tx_free = _3c509b_read_reg(nic, _3C509B_TX_FREE);
    if (tx_free < (uint16_t)length) {
        LOG_DEBUG("Insufficient TX FIFO space: need %zu, have %d", length, tx_free);
        return ERROR_BUSY;
    }

    /* Calculate checksums with CPU optimization before transmission (Phase 2.1) */
    tx_packet = (uint8_t *)packet; /* Cast away const for checksum calculation */
    if (length >= 34) { /* Minimum for Ethernet + IP header */
        checksum_result = hw_checksum_process_outbound_packet(tx_packet, length);
        if (checksum_result != 0) {
            LOG_DEBUG("Checksum calculation completed for outbound packet");
        }
    }

    /* CACHE COHERENCY: Flush write-back cache before PIO to ensure
     * the NIC reads current data, not stale cached values (Sprint 4B) */
    _3c509b_pio_prepare_tx_buffer(nic, packet, length);

    /* Use direct PIO transmission - eliminates intermediate copy */
    result = send_packet_direct_pio(packet, (uint16_t)length, nic->io_base);
    if (result != SUCCESS) {
        LOG_ERROR("Direct PIO transmission failed: %d", result);
        return result;
    }

    /* Update statistics */
    nic->tx_packets++;
    nic->tx_bytes += length;

    LOG_TRACE("Sent packet of %zu bytes via direct PIO", length);

    return SUCCESS;
}

/* ============================================================================
 * Cache Coherency Helpers for PIO Operations
 * ============================================================================ */

/**
 * @brief Prepare RX buffer before PIO read operation
 */
static void _3c509b_pio_prepare_rx_buffer(nic_info_t *nic, void *buffer, size_t length) {
    if (!nic || !buffer || length == 0) {
        return;
    }

    /* Only apply protection if enabled for this NIC */
    if (!nic->pio_speculative_protection) {
        return;
    }

    /* Invalidate cache lines covering the buffer before PIO read */
    cache_management_invalidate_buffer(buffer, length);

    /* Memory fence to ensure invalidation completes before PIO */
    memory_fence();
}

/**
 * @brief Complete RX buffer after PIO read operation
 */
static void _3c509b_pio_complete_rx_buffer(nic_info_t *nic, void *buffer, size_t length) {
    if (!nic || !buffer || length == 0) {
        return;
    }

    /* Only apply protection if enabled for this NIC */
    if (!nic->pio_speculative_protection) {
        return;
    }

    /* Ensure cache coherency after PIO completes */
    cache_management_dma_complete(buffer, length);

    /* Memory fence to ensure subsequent reads see PIO data */
    memory_fence();
}

/**
 * @brief Prepare TX buffer before PIO write operation
 */
static void _3c509b_pio_prepare_tx_buffer(nic_info_t *nic, const void *buffer, size_t length) {
    if (!nic || !buffer || length == 0) {
        return;
    }

    /* Only apply protection if enabled for this NIC */
    if (!nic->pio_speculative_protection) {
        return;
    }

    /* Flush cache lines to ensure PIO sees current data */
    cache_management_flush_buffer((void *)buffer, length);

    /* Memory fence to ensure flush completes before PIO */
    memory_fence();
}

/**
 * @brief Enhanced receive with full PIO cache coherency management
 */
int _3c509b_receive_packet_cache_safe(nic_info_t *nic) {
    buffer_desc_t *rx_buffer;
    uint16_t status;
    uint16_t rx_status;
    uint16_t packet_length;
    uint16_t rx_fifo;
    size_t words;
    int result;
    int checksum_result;

    rx_buffer = NULL;
    result = SUCCESS;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Ensure we're in Window 1 for RX operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Check if RX packet is available */
    status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_RX_COMPLETE)) {
        return ERROR_NO_DATA;
    }

    /* Read RX status and length */
    rx_status = _3c509b_read_reg(nic, _3C509B_RX_STATUS);
    packet_length = rx_status & _3C509B_RXSTAT_LEN_MASK;

    /* Check for errors */
    if (rx_status & (_3C509B_RXSTAT_ERROR | _3C509B_RXSTAT_INCOMPLETE)) {
        LOG_DEBUG("RX error: status=0x%X", rx_status);
        _3c509b_write_command(nic, _3C509B_CMD_RX_DISCARD);
        nic->rx_errors++;
        return ERROR_IO;
    }

    /* Allocate buffer for received packet */
    rx_buffer = rx_copybreak_alloc(packet_length);
    if (!rx_buffer) {
        LOG_ERROR("Failed to allocate RX buffer for %d byte packet", packet_length);
        _3c509b_write_command(nic, _3C509B_CMD_RX_DISCARD);
        nic->rx_dropped++;
        return ERROR_NO_MEMORY;
    }

    /* CACHE COHERENCY: Invalidate cache before PIO read to prevent
     * speculative prefetcher from loading stale data */
    _3c509b_pio_prepare_rx_buffer(nic, rx_buffer->data, packet_length);

    /* Read packet data from RX FIFO via PIO */
    rx_fifo = nic->io_base + _3C509B_RX_FIFO;
    words = packet_length / 2;
    {
        uint16_t *buffer_words = (uint16_t *)rx_buffer->data;
        size_t i;

        for (i = 0; i < words; i++) {
            buffer_words[i] = inw(rx_fifo);
        }
    }

    /* Read remaining byte if length is odd */
    if (packet_length & 1) {
        ((uint8_t *)rx_buffer->data)[packet_length - 1] = inb(rx_fifo);
    }

    /* CACHE COHERENCY: Ensure cache sees new PIO data, not stale prefetched data */
    _3c509b_pio_complete_rx_buffer(nic, rx_buffer->data, packet_length);

    /* Update buffer descriptor */
    rx_buffer->used = packet_length;
    buffer_set_state(rx_buffer, BUFFER_STATE_IN_USE);

    /* Verify checksums with CPU optimization (Phase 2.1) */
    if (packet_length >= 34) {
        checksum_result = hw_checksum_verify_inbound_packet(
            (uint8_t *)rx_buffer->data, packet_length);
        if (checksum_result < 0) {
            LOG_DEBUG("Checksum verification failed for inbound packet");
        } else if (checksum_result > 0) {
            LOG_DEBUG("Checksum verification passed");
        }
    }

    /* Process received packet through API */
    result = packet_process_received((uint8_t *)rx_buffer->data, packet_length, nic->index);
    if (result != SUCCESS) {
        LOG_WARNING("Packet processing failed: %d", result);
        nic->rx_dropped++;
    } else {
        nic->rx_packets++;
        nic->rx_bytes += packet_length;
        LOG_TRACE("Processed cache-safe received packet of %d bytes", packet_length);
    }

    /* Free the buffer */
    rx_copybreak_free(rx_buffer);

    return result;
}
