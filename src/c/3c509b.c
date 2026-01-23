/**
 * @file 3c509b.c
 * @brief 3Com 3C509B NIC driver implementation
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "../include/3c509b.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/memory.h"
#include "../include/common.h"
#include "../include/bufaloc.h"
#include "../include/pktops.h"
#include "../include/medictl.h"
#include "../include/nic_defs.h"
#include "../include/irqmit.h"
#include "../include/hwchksm.h"  // Phase 2.1: Hardware checksumming
#include "../include/dirpioe.h"  // Phase 1: CPU-optimized I/O operations
#include <string.h>

/* Forward declarations for operations */
int _3c509b_init(nic_info_t *nic);
int _3c509b_cleanup(nic_info_t *nic);
int _3c509b_reset(nic_info_t *nic);
static int _3c509b_configure(nic_info_t *nic, const void *config);
int _3c509b_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length);
int _3c509b_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length);
bool _3c509b_check_interrupt(nic_info_t *nic);
void _3c509b_handle_interrupt(nic_info_t *nic);
static int _3c509b_enable_interrupts(nic_info_t *nic);
static int _3c509b_disable_interrupts(nic_info_t *nic);
static bool _3c509b_get_link_status(nic_info_t *nic);
static int _3c509b_get_link_speed(nic_info_t *nic);
static int _3c509b_set_promiscuous(nic_info_t *nic, bool enable);
static int _3c509b_set_multicast(nic_info_t *nic, const uint8_t *mc_list, int count);
int _3c509b_self_test(nic_info_t *nic);

/* Internal helper functions for 3C509B hardware compliance */
static void _3c509b_select_window(nic_info_t *nic, uint8_t window);
static int _3c509b_wait_for_cmd_busy(nic_info_t *nic, uint32_t timeout_ms);
static uint16_t _3c509b_read_eeprom(nic_info_t *nic, uint8_t address);
static void _3c509b_write_eeprom(nic_info_t *nic, uint8_t address, uint16_t data);
static int _3c509b_read_mac_from_eeprom(nic_info_t *nic, uint8_t *mac);
static int _3c509b_setup_media(nic_info_t *nic);
static int _3c509b_setup_rx_filter(nic_info_t *nic);

/* 3C509B operations vtable */
static nic_ops_t _3c509b_ops = {
    .init               = _3c509b_init,
    .cleanup            = _3c509b_cleanup,
    .reset              = _3c509b_reset,
    .configure          = (int (*)(struct nic_info *, const void *))_3c509b_configure,
    .send_packet        = _3c509b_send_packet_direct_pio,
    .receive_packet     = _3c509b_receive_packet,
    .check_interrupt    = _3c509b_check_interrupt,
    .handle_interrupt   = _3c509b_handle_interrupt,
    .enable_interrupts  = _3c509b_enable_interrupts,
    .disable_interrupts = _3c509b_disable_interrupts,
    .get_link_status    = _3c509b_get_link_status,
    .get_link_speed     = _3c509b_get_link_speed,
    .set_promiscuous    = _3c509b_set_promiscuous,
    .set_multicast      = _3c509b_set_multicast,
    .self_test          = _3c509b_self_test
};

/* Internal helper functions */
static uint16_t _3c509b_read_reg(nic_info_t *nic, uint16_t reg);
static void _3c509b_write_reg(nic_info_t *nic, uint16_t reg, uint16_t value);
static void _3c509b_write_command(nic_info_t *nic, uint16_t command);
static int _3c509b_wait_for_command(nic_info_t *nic, uint32_t timeout_ms);

/* Public interface functions */
nic_ops_t* get_3c509b_ops(void) {
    return &_3c509b_ops;
}

/* NIC operation implementations */
static int _3c509b_init(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Initializing 3C509B at I/O 0x%X", nic->io_base);
    
    /* Reset the NIC first */
    int result = _3c509b_reset(nic);
    if (result != SUCCESS) {
        LOG_ERROR("3C509B reset failed: %d", result);
        return result;
    }
    
    /* Read MAC address from EEPROM */
    result = _3c509b_read_mac_from_eeprom(nic, nic->mac);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to read MAC address from EEPROM: %d", result);
        return result;
    }
    
    /* Copy to permanent MAC */
    memcpy(nic->perm_mac, nic->mac, ETH_ALEN);
    
    /* Setup media and transceiver */
    result = _3c509b_setup_media(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to setup media: %d", result);
        return result;
    }
    
    /* Setup RX filter */
    result = _3c509b_setup_rx_filter(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to setup RX filter: %d", result);
        return result;
    }
    
    /* Select Window 1 for operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    /* Set up interrupt mask */
    _3c509b_write_command(nic, _3C509B_CMD_SET_INTR_ENABLE | 
                         (_3C509B_IMASK_TX_COMPLETE | _3C509B_IMASK_RX_COMPLETE | _3C509B_IMASK_ADAPTER_FAILURE));
    
    /* Enable RX and TX */
    _3c509b_write_command(nic, _3C509B_CMD_RX_ENABLE);
    result = _3c509b_wait_for_cmd_busy(nic, 1000);
    if (result != SUCCESS) {
        LOG_ERROR("RX enable command timeout");
        return result;
    }
    
    _3c509b_write_command(nic, _3C509B_CMD_TX_ENABLE);
    result = _3c509b_wait_for_cmd_busy(nic, 1000);
    if (result != SUCCESS) {
        LOG_ERROR("TX enable command timeout");
        return result;
    }
    
    
    /* Set initial link status */
    nic->link_up = _3c509b_get_link_status(nic);
    nic->speed = _3c509b_get_link_speed(nic);
    
    /* Initialize CPU detection for enhanced PIO operations (Phase 1) */
    direct_pio_init_cpu_detection();
    LOG_DEBUG("CPU-optimized PIO initialized: level %d, 32-bit support: %s", 
              direct_pio_get_optimization_level(),
              direct_pio_get_cpu_support_info() ? "Yes" : "No");
    
    /* Initialize hardware checksumming with CPU-aware optimization */
    result = hw_checksum_init();
    if (result != SUCCESS) {
        LOG_WARNING("Hardware checksum initialization failed: %d, continuing without optimization", result);
        /* Continue - checksum is optional feature */
    } else {
        LOG_DEBUG("Hardware checksum module initialized with CPU optimization");
    }
    
    LOG_INFO("3C509B initialized successfully, link %s, speed %d Mbps", 
             nic->link_up ? "UP" : "DOWN", nic->speed);
    
    return SUCCESS;
}

static int _3c509b_cleanup(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Cleaning up 3C509B at I/O 0x%X", nic->io_base);
    
    /* Disable interrupts */
    _3c509b_disable_interrupts(nic);
    
    /* Disable TX and RX */
    _3c509b_write_command(nic, _3C509B_CMD_RX_DISABLE);
    _3c509b_wait_for_cmd_busy(nic, 500);
    
    _3c509b_write_command(nic, _3C509B_CMD_TX_DISABLE);
    _3c509b_wait_for_cmd_busy(nic, 500);
    
    /* Cleanup media control subsystem */
    media_control_cleanup(nic);
    
    return SUCCESS;
}

static int _3c509b_reset(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Resetting 3C509B at I/O 0x%X", nic->io_base);
    
    /* Issue global reset command */
    _3c509b_write_command(nic, _3C509B_CMD_GLOBAL_RESET);
    
    /* Wait for reset to complete - hardware requires 1ms */
    mdelay(1);
    
    /* Wait for the NIC to become ready */
    return _3c509b_wait_for_cmd_busy(nic, 5000); /* 5 second timeout */
}

static int _3c509b_configure(nic_info_t *nic, const void *config) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Basic configuration - media setup and RX filter already done in init */
    LOG_DEBUG("Configuring 3C509B");
    
    /* Configuration can include speed/duplex settings, but 3C509B is 10Mbps half-duplex only */
    nic->speed = 10;
    nic->full_duplex = false;
    nic->mtu = _3C509B_MAX_MTU;
    
    return SUCCESS;
}

int _3c509b_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length) {
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
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        LOG_DEBUG("TX not available, status=0x%X", status);
        return ERROR_BUSY;
    }
    
    /* Check TX free space */
    uint16_t tx_free = _3c509b_read_reg(nic, _3C509B_TX_FREE);
    if (tx_free < (uint16_t)length) {
        LOG_DEBUG("Insufficient TX FIFO space: need %zu, have %d", length, tx_free);
        return ERROR_BUSY;
    }
    
    /* Write packet length to TX FIFO - this starts transmission */
    uint16_t tx_fifo = nic->io_base + _3C509B_TX_FIFO;
    outw(tx_fifo, (uint16_t)length);
    
    /* Write packet data to TX FIFO using PIO */
    size_t words = length / 2;
    const uint16_t *packet_words = (const uint16_t*)packet;
    
    for (size_t i = 0; i < words; i++) {
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
    if (!nic || !buffer || !length) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Ensure we're in Window 1 for RX operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    /* Check if RX packet is available */
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_RX_COMPLETE)) {
        *length = 0;
        return ERROR_NO_DATA; /* No packet available */
    }
    
    /* Read RX status and length */
    uint16_t rx_status = _3c509b_read_reg(nic, _3C509B_RX_STATUS);
    uint16_t packet_length = rx_status & _3C509B_RXSTAT_LEN_MASK;
    
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
    uint16_t rx_fifo = nic->io_base + _3C509B_RX_FIFO;
    
    /* Read 16-bit words first */
    size_t words = packet_length / 2;
    uint16_t *buffer_words = (uint16_t*)buffer;
    
    for (size_t i = 0; i < words; i++) {
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
    uint16_t *buffer_words = (uint16_t*)rx_buffer->data;
    
    for (size_t i = 0; i < words; i++) {
        buffer_words[i] = inw(rx_fifo);
    }
    
    /* Read remaining byte if length is odd */
    if (packet_length & 1) {
        ((uint8_t*)rx_buffer->data)[packet_length - 1] = inb(rx_fifo);
    }
    
    /* Update buffer descriptor */
    rx_buffer->used = packet_length;
    buffer_set_state(rx_buffer, BUFFER_STATE_IN_USE);
    
    /* Verify checksums with CPU optimization (Phase 2.1) */
    if (packet_length >= 34) { // Minimum for Ethernet + IP header
        int checksum_result = hw_checksum_verify_inbound_packet((uint8_t*)rx_buffer->data, packet_length);
        if (checksum_result < 0) {
            LOG_DEBUG("Checksum verification failed for inbound packet");
            // Continue anyway - many stacks don't require perfect checksums
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

static bool _3c509b_check_interrupt(nic_info_t *nic) {
    if (!nic) {
        return false;
    }
    
    /* Read interrupt status */
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    
    /* Check if any interrupt bits are set */
    return (status & _3C509B_STATUS_INT_LATCH) != 0;
}

static void _3c509b_handle_interrupt(nic_info_t *nic) {
    if (!nic) {
        return;
    }
    
    /* Ensure we're in Window 1 for interrupt handling */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    
    LOG_TRACE("3C509B interrupt: status=0x%X", status);
    
    /* Handle TX completion */
    if (status & _3C509B_STATUS_TX_COMPLETE) {
        /* TX completed successfully */
        LOG_TRACE("TX complete");
        
        /* Clear interrupt by reading TX status */
        uint16_t tx_status = _3c509b_read_reg(nic, _3C509B_TX_STATUS);
        
        /* Check for TX errors */
        if (tx_status & (_3C509B_TXSTAT_JABBER | _3C509B_TXSTAT_UNDERRUN | _3C509B_TXSTAT_MAXCOLL)) {
            LOG_DEBUG("TX error: status=0x%X", tx_status);
            nic->tx_errors++;
        }
    }
    
    /* Handle RX completion - process packets using buffer allocation */
    if (status & _3C509B_STATUS_RX_COMPLETE) {
        LOG_TRACE("RX complete - processing buffered");
        
        /* Process received packets with buffer allocation and API integration */
        int rx_result = _3c509b_receive_packet_buffered(nic);
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
 * @param nic Pointer to NIC info structure
 * @param event_type Pointer to store event type processed
 * @return 1 if event processed, 0 if no work, negative on error
 */
int _3c509b_process_single_event(nic_info_t *nic, interrupt_event_type_t *event_type) {
    if (!nic || !event_type) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Ensure we're in Window 1 for interrupt handling */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    
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
        uint16_t tx_status = _3c509b_read_reg(nic, _3C509B_TX_STATUS);
        
        if (tx_status & (_3C509B_TXSTAT_JABBER | _3C509B_TXSTAT_UNDERRUN | _3C509B_TXSTAT_MAXCOLL)) {
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
        int rx_result = _3c509b_receive_packet_buffered(nic);
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
 * @param nic Pointer to NIC info structure  
 * @return 1 if interrupt work available, 0 if none, negative on error
 */
int _3c509b_check_interrupt_batched(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Ensure we're in Window 1 */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    
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
 * @param nic Pointer to NIC info structure
 * @return Number of events processed, or negative error code
 */
int _3c509b_handle_interrupt_batched(nic_info_t *nic) {
    interrupt_mitigation_context_t *im_ctx;
    
    if (!nic || !nic->private_data) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get interrupt mitigation context from private data */
    /* Note: This assumes the private data structure contains the IM context */
    /* In a real implementation, this would be properly structured */
    im_ctx = (interrupt_mitigation_context_t *)nic->private_data;
    
    if (!is_interrupt_mitigation_enabled(im_ctx)) {
        /* Fall back to legacy single-event processing */
        _3c509b_handle_interrupt(nic);
        return 1;
    }
    
    /* Process batched interrupts */
    return process_batched_interrupts_3c509b(im_ctx);
}

static int _3c509b_enable_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select Window 1 and enable interrupt mask */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    _3c509b_write_command(nic, _3C509B_CMD_SET_INTR_ENABLE | 
                         (_3C509B_IMASK_TX_COMPLETE | _3C509B_IMASK_RX_COMPLETE | _3C509B_IMASK_ADAPTER_FAILURE));
    
    return SUCCESS;
}

static int _3c509b_disable_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select Window 1 and disable all interrupts */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    _3c509b_write_command(nic, _3C509B_CMD_SET_INTR_ENABLE | 0);
    
    return SUCCESS;
}

static bool _3c509b_get_link_status(nic_info_t *nic) {
    if (!nic) {
        return false;
    }
    
    /* Use enhanced media control link detection */
    int link_status = check_media_link_status(nic);
    if (link_status < 0) {
        LOG_DEBUG("Link status check failed, falling back to basic detection");
        
        /* Fallback to basic link detection */
        _3c509b_select_window(nic, _3C509B_WINDOW_4);
        uint16_t media_status = _3c509b_read_reg(nic, _3C509B_W4_NETDIAG);
        
        /* Check link beat for 10Base-T */
        return (media_status & 0x0800) != 0;
    }
    
    return link_status ? true : false;
}

static int _3c509b_get_link_speed(nic_info_t *nic) {
    if (!nic) {
        return 0;
    }
    
    /* 3C509B is always 10 Mbps */
    return 10;
}

static int _3c509b_set_promiscuous(nic_info_t *nic, bool enable) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select Window 1 for RX filter configuration */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    uint16_t filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    if (enable) {
        filter |= _3C509B_RX_FILTER_PROMISCUOUS;
    }
    
    _3c509b_write_command(nic, _3C509B_CMD_SET_RX_FILTER | filter);
    
    LOG_DEBUG("3C509B promiscuous mode %s", enable ? "enabled" : "disabled");
    
    return SUCCESS;
}

static int _3c509b_set_multicast(nic_info_t *nic, const uint8_t *mc_list, int count) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select Window 1 for RX filter configuration */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    uint16_t filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    if (count > 0) {
        filter |= _3C509B_RX_FILTER_MULTICAST;
    }
    
    _3c509b_write_command(nic, _3C509B_CMD_SET_RX_FILTER | filter);
    
    LOG_DEBUG("3C509B multicast filter updated with %d addresses", count);
    
    return SUCCESS;
}

static int _3c509b_self_test(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Running 3C509B self-test");
    
    /* Check if registers are accessible */
    
    /* Select Window 0 for configuration register access */
    _3c509b_select_window(nic, _3C509B_WINDOW_0);
    
    uint16_t original_value = _3c509b_read_reg(nic, _3C509B_W0_CONFIG_CTRL);
    _3c509b_write_reg(nic, _3C509B_W0_CONFIG_CTRL, 0x5AA5);
    uint16_t test_value = _3c509b_read_reg(nic, _3C509B_W0_CONFIG_CTRL);
    _3c509b_write_reg(nic, _3C509B_W0_CONFIG_CTRL, original_value);
    
    if (test_value != 0x5AA5) {
        LOG_ERROR("3C509B register test failed: wrote 0x5AA5, read 0x%X", test_value);
        return ERROR_HARDWARE;
    }
    
    LOG_INFO("3C509B self-test passed");
    
    return SUCCESS;
}

/* Private helper function implementations */
static uint16_t _3c509b_read_reg(nic_info_t *nic, uint16_t reg) {
    return inw(nic->io_base + reg);
}

static void _3c509b_write_reg(nic_info_t *nic, uint16_t reg, uint16_t value) {
    outw(nic->io_base + reg, value);
}

static int _3c509b_wait_for_command(nic_info_t *nic, uint32_t timeout_ms) {
    while (timeout_ms > 0) {
        uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
        
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            return SUCCESS; /* Command completed */
        }
        
        udelay(1000); /* 1ms delay */
        timeout_ms--;
    }
    
    LOG_ERROR("3C509B command timeout");
    return ERROR_TIMEOUT;
}

/* Implementation of helper functions for 3C509B hardware compliance */

/**
 * Select register window with proper CMD_BUSY checking
 */
static void _3c509b_select_window(nic_info_t *nic, uint8_t window) {
    /* Wait for any pending command to complete */
    _3c509b_wait_for_cmd_busy(nic, 100);
    
    /* Select the window */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SELECT_WINDOW | window);
}

/**
 * Wait for CMD_BUSY to clear
 */
static int _3c509b_wait_for_cmd_busy(nic_info_t *nic, uint32_t timeout_ms) {
    while (timeout_ms > 0) {
        uint16_t status = inw(nic->io_base + _3C509B_STATUS_REG);
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
static void _3c509b_write_command(nic_info_t *nic, uint16_t command) {
    /* Wait for any pending command to complete */
    _3c509b_wait_for_cmd_busy(nic, 100);
    
    /* Write the command */
    outw(nic->io_base + _3C509B_COMMAND_REG, command);
}

/**
 * Read from EEPROM
 */
static uint16_t _3c509b_read_eeprom(nic_info_t *nic, uint8_t address) {
    /* Select Window 0 for EEPROM access */
    _3c509b_select_window(nic, _3C509B_WINDOW_0);
    
    /* Write EEPROM read command */
    _3c509b_write_reg(nic, _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | address);
    
    /* Wait for EEPROM read to complete */
    udelay(_3C509B_EEPROM_READ_DELAY);
    
    /* Read the data */
    return _3c509b_read_reg(nic, _3C509B_EEPROM_DATA);
}

/**
 * Write to EEPROM (typically not used in driver operation)
 */
static void _3c509b_write_eeprom(nic_info_t *nic, uint8_t address, uint16_t data) {
    /* Select Window 0 for EEPROM access */
    _3c509b_select_window(nic, _3C509B_WINDOW_0);
    
    /* Write the data first */
    _3c509b_write_reg(nic, _3C509B_EEPROM_DATA, data);
    
    /* Write EEPROM write command */
    _3c509b_write_reg(nic, _3C509B_EEPROM_CMD, _3C509B_EEPROM_WRITE | address);
    
    /* Wait for EEPROM write to complete */
    udelay(_3C509B_EEPROM_READ_DELAY * 10); /* Write takes longer */
}

/**
 * Read MAC address from EEPROM
 */
static int _3c509b_read_mac_from_eeprom(nic_info_t *nic, uint8_t *mac) {
    if (!nic || !mac) {
        return ERROR_INVALID_PARAM;
    }
    
    /* MAC address is stored in EEPROM words 0, 1, 2 */
    for (int i = 0; i < 3; i++) {
        uint16_t word = _3c509b_read_eeprom(nic, i);
        mac[i * 2] = word & 0xFF;
        mac[i * 2 + 1] = (word >> 8) & 0xFF;
    }
    
    LOG_INFO("3C509B MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return SUCCESS;
}

/**
 * Setup media type and transceiver using enhanced media control
 */
static int _3c509b_setup_media(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Setting up media for 3C509B using enhanced media control");
    
    /* Initialize media control subsystem */
    int result = media_control_init(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize media control: %d", result);
        return result;
    }
    
    /* Initialize NIC variant information and capabilities */
    NIC_INFO_INIT_DEFAULTS(nic);
    
    /* Set default capabilities for 3C509B family */
    nic->media_capabilities = MEDIA_CAPS_3C509B_COMBO;
    nic->variant_id = VARIANT_3C509B_COMBO; // Default to combo variant
    
    /* Try to detect media automatically if this is a combo card */
    if (nic->media_capabilities & MEDIA_CAP_AUTO_SELECT) {
        LOG_INFO("Attempting auto-detection for combo card");
        
        media_detect_config_t detect_config = MEDIA_DETECT_CONFIG_DEFAULT;
        media_type_t detected = auto_detect_media(nic, &detect_config);
        
        if (detected != MEDIA_TYPE_UNKNOWN) {
            LOG_INFO("Auto-detected media: %s", media_type_to_string(detected));
            nic->current_media = detected;
            nic->media_config_source = MEDIA_CONFIG_AUTO_DETECT;
        } else {
            LOG_WARNING("Auto-detection failed, using default media");
            nic->current_media = MEDIA_TYPE_10BASE_T;
            nic->media_config_source = MEDIA_CONFIG_DEFAULT;
        }
    } else {
        /* For non-combo cards, use the default supported media */
        nic->current_media = get_default_media_for_nic(nic);
        nic->media_config_source = MEDIA_CONFIG_DEFAULT;
        LOG_INFO("Using default media: %s", media_type_to_string(nic->current_media));
    }
    
    /* Configure the selected media */
    if (nic->current_media != MEDIA_TYPE_UNKNOWN) {
        result = select_media_transceiver(nic, nic->current_media, 0);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to configure media %s: %d", 
                     media_type_to_string(nic->current_media), result);
            
            /* Fallback to 10BaseT if auto-detection failed */
            if (nic->current_media != MEDIA_TYPE_10BASE_T && 
                is_media_supported_by_nic(nic, MEDIA_TYPE_10BASE_T)) {
                LOG_INFO("Falling back to 10BaseT");
                result = select_media_transceiver(nic, MEDIA_TYPE_10BASE_T, 
                                                MEDIA_CTRL_FLAG_FORCE);
                if (result == SUCCESS) {
                    nic->current_media = MEDIA_TYPE_10BASE_T;
                    nic->media_config_source = MEDIA_CONFIG_DRIVER_FORCED;
                }
            }
        }
    }
    
    if (result != SUCCESS) {
        LOG_ERROR("Media setup failed completely");
        return result;
    }
    
    /* Test the configured media */
    link_test_result_t test_result;
    result = test_link_beat(nic, nic->current_media, 2000, &test_result);
    if (result == SUCCESS) {
        LOG_INFO("Media link test passed: quality=%d%%", test_result.signal_quality);
        nic->media_detection_state |= MEDIA_DETECT_COMPLETED;
    } else {
        LOG_WARNING("Media link test failed, but continuing");
        nic->media_detection_state |= MEDIA_DETECT_FAILED;
    }
    
    LOG_INFO("3C509B media setup complete: %s", media_type_to_string(nic->current_media));
    return SUCCESS;
}

/**
 * Setup RX filter for normal operation
 */
static int _3c509b_setup_rx_filter(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select Window 1 for RX filter */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);
    
    /* Set basic RX filter: station address + broadcast */
    uint16_t filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    _3c509b_write_command(nic, _3C509B_CMD_SET_RX_FILTER | filter);
    
    /* Wait for command to complete */
    _3c509b_wait_for_cmd_busy(nic, 1000);
    
    /* Select Window 2 to program station address */
    _3c509b_select_window(nic, _3C509B_WINDOW_2);
    
    /* Write MAC address to station address registers */
    for (int i = 0; i < ETH_ALEN; i++) {
        _3c509b_write_reg(nic, i, nic->mac[i]);
    }
    
    LOG_DEBUG("3C509B RX filter and station address configured");
    
    return SUCCESS;
}

/* ============================================================================
 * Direct PIO Transmit Optimization Implementation
 * Sprint 1.2: Eliminates intermediate memcpy operations
 * ============================================================================ */

/* Assembly function prototypes */
extern void direct_pio_outsw(const void* src_buffer, uint16_t dst_port, uint16_t word_count);
extern int send_packet_direct_pio_asm(const void* stack_buffer, uint16_t length, uint16_t io_base);
extern void direct_pio_header_and_payload(uint16_t io_port, const uint8_t* dest_mac,
                                         const uint8_t* src_mac, uint16_t ethertype,
                                         const void* payload, uint16_t payload_len);

/**
 * @brief Send packet directly via PIO (eliminates intermediate copy)
 * @param stack_buffer Network stack's buffer pointer
 * @param length Packet length
 * @param io_base NIC I/O base address  
 * @return 0 on success, negative on error
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
        
        for (size_t i = 0; i < words; i++) {
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
 * @param nic NIC information structure
 * @param dest_mac Destination MAC address
 * @param ethertype Ethernet type
 * @param payload Payload data
 * @param payload_len Payload length
 * @return 0 on success, negative on error
 */
int send_packet_direct_pio_with_header(nic_info_t *nic, const uint8_t *dest_mac, 
                                      uint16_t ethertype, const void* payload, uint16_t payload_len) {
    uint16_t total_length, tx_fifo;
    
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
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        LOG_DEBUG("TX not available, status=0x%X", status);
        return ERROR_BUSY;
    }
    
    /* Check TX free space */
    uint16_t tx_free = _3c509b_read_reg(nic, _3C509B_TX_FREE);
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
    uint16_t actual_length = ETH_HEADER_LEN + payload_len;
    if (actual_length < ETH_MIN_FRAME) {
        uint16_t pad_bytes = ETH_MIN_FRAME - actual_length;
        uint16_t pad_words = pad_bytes / 2;
        
        /* Write padding words */
        for (uint16_t i = 0; i < pad_words; i++) {
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
 * @param nic NIC information structure
 * @param packet Packet data (stack buffer)
 * @param length Packet length
 * @return 0 on success, negative on error
 */
static int _3c509b_send_packet_direct_pio(nic_info_t *nic, const uint8_t *packet, size_t length) {
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
    uint16_t status = _3c509b_read_reg(nic, _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        LOG_DEBUG("TX not available, status=0x%X", status);
        return ERROR_BUSY;
    }
    
    /* Check TX free space */
    uint16_t tx_free = _3c509b_read_reg(nic, _3C509B_TX_FREE);
    if (tx_free < (uint16_t)length) {
        LOG_DEBUG("Insufficient TX FIFO space: need %zu, have %d", length, tx_free);
        return ERROR_BUSY;
    }
    
    /* Calculate checksums with CPU optimization before transmission (Phase 2.1) */
    uint8_t *tx_packet = (uint8_t *)packet; // Cast away const for checksum calculation
    if (length >= 34) { // Minimum for Ethernet + IP header
        int checksum_result = hw_checksum_process_outbound_packet(tx_packet, length);
        if (checksum_result != 0) {
            LOG_DEBUG("Checksum calculation completed for outbound packet");
        }
    }
    
    /* Note: 3C509B is PIO-only - scatter-gather DMA not supported (Phase 2.2)
     * Fragmented packets are automatically consolidated by upper layers
     * For multi-fragment packets, use CPU-optimized memcpy for consolidation */
    
    /* Use direct PIO transmission - eliminates intermediate copy */
    int result = send_packet_direct_pio(packet, (uint16_t)length, nic->io_base);
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
 * Phase 4: Cache Coherency Integration Implementation
 * Sprint 4B: Comprehensive DMA safety for 3C509B operations
 * ============================================================================ */

/**
 * @brief Initialize cache coherency management for 3C509B
 * @param nic NIC information structure
 * @return SUCCESS on success, error code on failure
 */
static int _3c509b_initialize_cache_coherency(nic_info_t *nic) {
    coherency_analysis_t analysis;
    chipset_detection_result_t chipset_result;
    
    if (!nic) {
        LOG_ERROR("Invalid NIC pointer for cache coherency initialization");
        return ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Initializing cache coherency management for 3C509B...");
    
    /* Perform comprehensive coherency analysis */
    analysis = perform_complete_coherency_analysis();
    
    if (analysis.selected_tier == TIER_DISABLE_BUS_MASTER) {
        LOG_WARNING("Cache coherency analysis recommends disabling bus mastering");
        LOG_WARNING("3C509B uses PIO-only operation - this is optimal for this system");
        nic->status |= NIC_STATUS_CACHE_COHERENCY_OK;
        return SUCCESS;
    }
    
    /* Detect chipset for diagnostic purposes */
    chipset_result = detect_system_chipset();
    
    /* Initialize cache management system with selected tier */
    bool cache_init_result = initialize_cache_management(analysis.selected_tier);
    if (!cache_init_result) {
        LOG_ERROR("Failed to initialize cache management system");
        return ERROR_HARDWARE;
    }
    
    /* Record test results in community database */
    bool record_result = record_chipset_test_result(&analysis, &chipset_result);
    if (!record_result) {
        LOG_WARNING("Failed to record test results in chipset database");
    }
    
    /* Store analysis results in NIC structure for runtime use */
    nic->cache_coherency_tier = analysis.selected_tier;
    nic->cache_management_available = true;
    nic->status |= NIC_STATUS_CACHE_COHERENCY_OK;
    
    LOG_INFO("Cache coherency initialized: tier %d, confidence %d%%", 
             analysis.selected_tier, analysis.confidence);
    
    /* Display performance opportunity information if relevant */
    if (should_offer_performance_guidance(&analysis)) {
        display_performance_opportunity_analysis();
    }
    
    return SUCCESS;
}

/**
 * @brief Prepare buffers for DMA operation (3C509B PIO)
 * @param buffer Buffer pointer
 * @param length Buffer length
 */
static void _3c509b_dma_prepare_buffers(void *buffer, size_t length) {
    dma_operation_t operation;
    
    if (!buffer || length == 0) {
        return;
    }
    
    /* Configure DMA operation for PIO read */
    operation.buffer = buffer;
    operation.length = length;
    operation.direction = DMA_DIRECTION_FROM_DEVICE;
    operation.device_type = DMA_DEVICE_NETWORK;
    
    /* Apply cache management before PIO operation */
    cache_management_dma_prepare(&operation);
}

/**
 * @brief Complete DMA operation and ensure cache coherency (3C509B PIO)
 * @param buffer Buffer pointer
 * @param length Buffer length  
 */
static void _3c509b_dma_complete_buffers(void *buffer, size_t length) {
    dma_operation_t operation;
    
    if (!buffer || length == 0) {
        return;
    }
    
    /* Configure DMA operation for PIO completion */
    operation.buffer = buffer;
    operation.length = length;
    operation.direction = DMA_DIRECTION_FROM_DEVICE;
    operation.device_type = DMA_DEVICE_NETWORK;
    
    /* Apply cache management after PIO operation */
    cache_management_dma_complete(&operation);
}

/* ============================================================================
 * Additional 3C509B Cache Integration Functions
 * ============================================================================ */

/**
 * @brief Enhanced receive with full cache coherency management
 * @param nic NIC information structure
 * @return SUCCESS on success, error code on failure
 */
int _3c509b_receive_packet_cache_safe(nic_info_t *nic) {
    buffer_desc_t *rx_buffer = NULL;
    uint16_t status, rx_status, packet_length;
    uint16_t rx_fifo;
    size_t words;
    int result = SUCCESS;
    
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if cache management is available */
    if (!nic->cache_management_available) {
        LOG_DEBUG("Cache management not available, using legacy receive");
        return _3c509b_receive_packet_buffered(nic);
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
    
    /* Allocate buffer with cache-aligned allocation */
    rx_buffer = rx_copybreak_alloc(packet_length);
    if (!rx_buffer) {
        LOG_ERROR("Failed to allocate cache-safe RX buffer for %d byte packet", packet_length);
        _3c509b_write_command(nic, _3C509B_CMD_RX_DISCARD);
        nic->rx_dropped++;
        return ERROR_NO_MEMORY;
    }
    
    /* Comprehensive cache management for packet reception */
    _3c509b_dma_prepare_buffers(rx_buffer->data, packet_length);
    
    /* Read packet data with cache-safe PIO operations */
    rx_fifo = nic->io_base + _3C509B_RX_FIFO;
    words = packet_length / 2;
    uint16_t *buffer_words = (uint16_t*)rx_buffer->data;
    
    for (size_t i = 0; i < words; i++) {
        buffer_words[i] = inw(rx_fifo);
    }
    
    if (packet_length & 1) {
        ((uint8_t*)rx_buffer->data)[packet_length - 1] = inb(rx_fifo);
    }
    
    /* Complete cache management */
    _3c509b_dma_complete_buffers(rx_buffer->data, packet_length);
    
    /* Update buffer descriptor */
    rx_buffer->used = packet_length;
    buffer_set_state(rx_buffer, BUFFER_STATE_IN_USE);
    
    /* Verify checksums with CPU optimization */
    if (packet_length >= 34) {
        int checksum_result = hw_checksum_verify_inbound_packet((uint8_t*)rx_buffer->data, packet_length);
        if (checksum_result < 0) {
            LOG_DEBUG("Checksum verification failed for inbound packet");
        }
    }
    
    /* Process received packet */
    result = packet_process_received((uint8_t*)rx_buffer->data, packet_length, nic->index);
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
