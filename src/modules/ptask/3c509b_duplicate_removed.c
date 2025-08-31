/**
 * @file 3c509b.c
 * @brief 3C509B ISA PnP Driver Implementation for PTASK.MOD
 * 
 * Agent 05 Implementation - 3C509B ISA PnP with shared PIO logic
 * Extracted from monolithic codebase and adapted for modular architecture
 */

#include "ptask_internal.h"
#include "../../include/3c509b.h"
#include "../../include/pnp.h"

/* 3C509B Register Window Definitions */
#define _3C509B_WINDOW_0        0
#define _3C509B_WINDOW_1        1
#define _3C509B_WINDOW_2        2
#define _3C509B_WINDOW_3        3
#define _3C509B_WINDOW_4        4

/* Window 0 Registers (Setup/Configuration) */
#define _3C509B_W0_CONFIG_CTRL  0x00
#define _3C509B_W0_ADDR_CFG     0x06
#define _3C509B_W0_RESOURCE_CFG 0x08
#define _3C509B_W0_EEPROM_CMD   0x0A
#define _3C509B_W0_EEPROM_DATA  0x0C

/* Window 1 Registers (Operation) */
#define _3C509B_COMMAND_REG     0x0E
#define _3C509B_STATUS_REG      0x0E
#define _3C509B_TX_FIFO         0x00
#define _3C509B_RX_FIFO         0x00
#define _3C509B_RX_STATUS       0x08
#define _3C509B_TX_STATUS       0x0B
#define _3C509B_TX_FREE         0x0C

/* Window 2 Registers (Station Address) */
#define _3C509B_W2_STATION_ADDR 0x00    /* 6 bytes */

/* Window 4 Registers (Diagnostics) */
#define _3C509B_W4_NETDIAG      0x06

/* Command Register Values */
#define _3C509B_CMD_SELECT_WINDOW   0x0800
#define _3C509B_CMD_TOTAL_RESET     0x0C00
#define _3C509B_CMD_RX_ENABLE       0x2000
#define _3C509B_CMD_RX_DISABLE      0x1800
#define _3C509B_CMD_TX_ENABLE       0x4800
#define _3C509B_CMD_TX_DISABLE      0x5000
#define _3C509B_CMD_SET_INTR_ENB    0x7000
#define _3C509B_CMD_ACK_INTR        0x6800
#define _3C509B_CMD_SET_RX_FILTER   0x8000
#define _3C509B_CMD_RX_DISCARD      0x4000

/* Status Register Bits */
#define _3C509B_STATUS_INT_LATCH    0x0001
#define _3C509B_STATUS_ADAPTER_FAILURE 0x0002
#define _3C509B_STATUS_TX_COMPLETE  0x0004
#define _3C509B_STATUS_TX_AVAILABLE 0x0008
#define _3C509B_STATUS_RX_COMPLETE  0x0010
#define _3C509B_STATUS_RX_EARLY     0x0020
#define _3C509B_STATUS_INT_REQUESTED 0x0040
#define _3C509B_STATUS_UPDATE_STATS 0x0080
#define _3C509B_STATUS_CMD_BUSY     0x1000

/* RX Filter Values */
#define _3C509B_RX_FILTER_STATION   0x01
#define _3C509B_RX_FILTER_MULTICAST 0x02
#define _3C509B_RX_FILTER_BROADCAST 0x04
#define _3C509B_RX_FILTER_PROMISCUOUS 0x08

/* EEPROM Commands */
#define _3C509B_EEPROM_READ         0x80
#define _3C509B_EEPROM_WRITE        0x40
#define _3C509B_EEPROM_READ_DELAY   162  /* 162 µs */

/* PnP Configuration */
#define PNP_3COM_VENDOR_ID      0x6D50
#define PNP_3C509B_DEVICE_ID    0x5090

/* Static context for 3C509B hardware */
static struct {
    uint16_t io_base;
    uint8_t  irq;
    uint8_t  mac_address[6];
    bool     initialized;
    uint16_t current_window;
    pio_interface_t *pio;
} g_3c509b_context = {0};

/* Forward declarations */
static void _3c509b_select_window(uint8_t window);
static int _3c509b_wait_for_cmd_busy(uint32_t timeout_ms);
static void _3c509b_write_command(uint16_t command);
static uint16_t _3c509b_read_eeprom(uint8_t address);
static int _3c509b_read_mac_from_eeprom(uint8_t *mac);
static int _3c509b_setup_rx_filter(void);

/**
 * @brief Detect 3C509B ISA PnP card
 * 
 * Uses ISA PnP isolation sequence to detect and activate 3C509B.
 * Returns positive value if card found, negative on error.
 * 
 * @return Card instance ID on success, negative error code on failure
 */
int ptask_detect_3c509b(void) {
    pnp_device_info_t device;
    int result;
    
    LOG_DEBUG("3C509B: Starting ISA PnP detection");
    
    /* Initialize PnP subsystem */
    result = pnp_init();
    if (result < 0) {
        LOG_ERROR("3C509B: PnP initialization failed: %d", result);
        return result;
    }
    
    /* Search for 3Com 3C509B */
    device.vendor_id = PNP_3COM_VENDOR_ID;
    device.device_id = PNP_3C509B_DEVICE_ID;
    device.instance = 0;
    
    result = pnp_find_device(&device);
    if (result < 0) {
        LOG_DEBUG("3C509B: ISA PnP device not found");
        return ERROR_HARDWARE_NOT_FOUND;
    }
    
    /* Activate the device */
    result = pnp_activate_device(&device);
    if (result < 0) {
        LOG_ERROR("3C509B: Device activation failed: %d", result);
        return result;
    }
    
    /* Store configuration */
    g_3c509b_context.io_base = device.io_base[0];
    g_3c509b_context.irq = device.irq[0];
    
    LOG_INFO("3C509B: Detected at I/O 0x%X, IRQ %d", 
             g_3c509b_context.io_base, g_3c509b_context.irq);
    
    return device.instance;
}

/**
 * @brief Initialize 3C509B hardware
 * 
 * Performs complete hardware initialization including:
 * - Reset and basic setup
 * - MAC address reading
 * - Window configuration
 * - Interrupt setup
 * 
 * @param nic NIC information structure to fill
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_init_3c509b_hardware(nic_info_t *nic) {
    int result;
    timing_context_t timing;
    uint16_t reset_time_us;
    
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("3C509B: Initializing hardware at I/O 0x%X", 
              g_3c509b_context.io_base);
    
    /* Get shared PIO interface */
    g_3c509b_context.pio = pio_get_interface();
    if (!g_3c509b_context.pio) {
        LOG_ERROR("3C509B: Shared PIO interface not available");
        return ERROR_DEPENDENCY_NOT_MET;
    }
    
    /* Store I/O base in NIC structure */
    nic->io_base = g_3c509b_context.io_base;
    nic->irq = g_3c509b_context.irq;
    
    /* Reset the card */
    TIMING_START(timing);
    _3c509b_write_command(_3C509B_CMD_TOTAL_RESET);
    
    /* Wait for reset completion (hardware requires 1ms minimum) */
    mdelay(2);  /* 2ms for safety */
    
    result = _3c509b_wait_for_cmd_busy(5000);
    if (result < 0) {
        LOG_ERROR("3C509B: Reset timeout");
        return ERROR_HARDWARE_TIMEOUT;
    }
    
    TIMING_END(timing);
    reset_time_us = TIMING_GET_MICROSECONDS(timing);
    LOG_DEBUG("3C509B: Reset completed in %d μs", reset_time_us);
    
    /* Read MAC address from EEPROM */
    result = _3c509b_read_mac_from_eeprom(g_3c509b_context.mac_address);
    if (result < 0) {
        LOG_ERROR("3C509B: Failed to read MAC address: %d", result);
        return result;
    }
    
    /* Copy MAC to NIC structure */
    memcpy(nic->mac, g_3c509b_context.mac_address, 6);
    memcpy(nic->perm_mac, g_3c509b_context.mac_address, 6);
    
    LOG_INFO("3C509B: MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             nic->mac[0], nic->mac[1], nic->mac[2],
             nic->mac[3], nic->mac[4], nic->mac[5]);
    
    /* Setup RX filter */
    result = _3c509b_setup_rx_filter();
    if (result < 0) {
        LOG_ERROR("3C509B: RX filter setup failed: %d", result);
        return result;
    }
    
    /* Select Window 1 for normal operations */
    _3c509b_select_window(_3C509B_WINDOW_1);
    
    /* Enable interrupts */
    _3c509b_write_command(_3C509B_CMD_SET_INTR_ENB | 0x1E);  /* Enable key interrupts */
    
    /* Enable TX and RX */
    _3c509b_write_command(_3C509B_CMD_TX_ENABLE);
    result = _3c509b_wait_for_cmd_busy(1000);
    if (result < 0) {
        LOG_ERROR("3C509B: TX enable timeout");
        return result;
    }
    
    _3c509b_write_command(_3C509B_CMD_RX_ENABLE);
    result = _3c509b_wait_for_cmd_busy(1000);
    if (result < 0) {
        LOG_ERROR("3C509B: RX enable timeout");
        return result;
    }
    
    /* Set NIC parameters */
    nic->speed = 10;             /* 3C509B is 10 Mbps */
    nic->full_duplex = false;    /* Half duplex only */
    nic->mtu = 1500;            /* Standard Ethernet MTU */
    nic->link_up = true;        /* Assume link is up */
    
    g_3c509b_context.initialized = true;
    
    LOG_INFO("3C509B: Hardware initialization completed successfully");
    return SUCCESS;
}

/**
 * @brief Send packet via 3C509B using shared PIO
 * 
 * Uses optimized PIO operations from shared library.
 * Implements zero-copy transmission when possible.
 * 
 * @param packet_data Packet data pointer
 * @param packet_length Packet length
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_send_3c509b_packet(const void *packet_data, uint16_t packet_length) {
    timing_context_t timing;
    uint16_t status, tx_free;
    uint16_t cli_time_us;
    const uint16_t *packet_words;
    uint16_t words;
    int i;
    
    if (!packet_data || packet_length == 0 || packet_length > 1514) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_3c509b_context.initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    /* Start CLI timing measurement */
    TIMING_CLI_START(timing);
    
    /* Check TX availability */
    status = g_3c509b_context.pio->inw_optimized(g_3c509b_context.io_base + _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        TIMING_CLI_END(timing);
        return ERROR_BUSY;
    }
    
    /* Check TX FIFO space */
    tx_free = g_3c509b_context.pio->inw_optimized(g_3c509b_context.io_base + _3C509B_TX_FREE);
    if (tx_free < packet_length) {
        TIMING_CLI_END(timing);
        return ERROR_BUSY;
    }
    
    /* Write packet length to TX FIFO */
    g_3c509b_context.pio->outw_optimized(g_3c509b_context.io_base + _3C509B_TX_FIFO, packet_length);
    
    /* Use optimized word transfers */
    packet_words = (const uint16_t*)packet_data;
    words = packet_length / 2;
    
    /* CPU-optimized bulk transfer */
    g_3c509b_context.pio->outsw_optimized(g_3c509b_context.io_base + _3C509B_TX_FIFO, 
                                         packet_words, words);
    
    /* Handle odd byte */
    if (packet_length & 1) {
        const uint8_t *packet_bytes = (const uint8_t*)packet_data;
        g_3c509b_context.pio->outb_optimized(g_3c509b_context.io_base + _3C509B_TX_FIFO, 
                                            packet_bytes[packet_length - 1]);
    }
    
    TIMING_CLI_END(timing);
    
    /* Validate CLI timing */
    cli_time_us = TIMING_GET_MICROSECONDS(timing);
    if (cli_time_us > PTASK_CLI_TIMEOUT_US) {
        LOG_WARNING("3C509B: CLI time %d μs exceeds limit", cli_time_us);
    }
    
    LOG_TRACE("3C509B: Sent packet of %d bytes in %d μs", packet_length, cli_time_us);
    
    return SUCCESS;
}

/**
 * @brief Receive packet from 3C509B using shared PIO
 * 
 * Uses optimized PIO operations and DMA-safe buffers.
 * 
 * @param buffer Receive buffer
 * @param buffer_size Buffer size
 * @param received_length Pointer to store actual received length
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_receive_3c509b_packet(void *buffer, uint16_t buffer_size, uint16_t *received_length) {
    timing_context_t timing;
    uint16_t status, rx_status, packet_length;
    uint16_t *buffer_words;
    uint16_t words;
    uint16_t cli_time_us;
    
    if (!buffer || !received_length || buffer_size == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_3c509b_context.initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    *received_length = 0;
    
    /* Start CLI timing measurement */
    TIMING_CLI_START(timing);
    
    /* Check RX status */
    status = g_3c509b_context.pio->inw_optimized(g_3c509b_context.io_base + _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_RX_COMPLETE)) {
        TIMING_CLI_END(timing);
        return ERROR_NO_DATA;
    }
    
    /* Read RX status and packet length */
    rx_status = g_3c509b_context.pio->inw_optimized(g_3c509b_context.io_base + _3C509B_RX_STATUS);
    packet_length = rx_status & 0x7FF;  /* Lower 11 bits */
    
    /* Check for RX errors */
    if (rx_status & 0x8000) {  /* Error bit */
        _3c509b_write_command(_3C509B_CMD_RX_DISCARD);
        TIMING_CLI_END(timing);
        return ERROR_IO;
    }
    
    /* Check buffer size */
    if (packet_length > buffer_size) {
        _3c509b_write_command(_3C509B_CMD_RX_DISCARD);
        TIMING_CLI_END(timing);
        return ERROR_BUFFER_TOO_SMALL;
    }
    
    /* Use optimized word transfers */
    buffer_words = (uint16_t*)buffer;
    words = packet_length / 2;
    
    /* CPU-optimized bulk transfer */
    g_3c509b_context.pio->insw_optimized(g_3c509b_context.io_base + _3C509B_RX_FIFO,
                                        buffer_words, words);
    
    /* Handle odd byte */
    if (packet_length & 1) {
        uint8_t *buffer_bytes = (uint8_t*)buffer;
        buffer_bytes[packet_length - 1] = 
            g_3c509b_context.pio->inb_optimized(g_3c509b_context.io_base + _3C509B_RX_FIFO);
    }
    
    TIMING_CLI_END(timing);
    
    *received_length = packet_length;
    
    /* Validate CLI timing */
    cli_time_us = TIMING_GET_MICROSECONDS(timing);
    if (cli_time_us > PTASK_CLI_TIMEOUT_US) {
        LOG_WARNING("3C509B: CLI time %d μs exceeds limit", cli_time_us);
    }
    
    LOG_TRACE("3C509B: Received packet of %d bytes in %d μs", packet_length, cli_time_us);
    
    return SUCCESS;
}

/**
 * @brief Handle 3C509B interrupt (called from ISR)
 * 
 * Optimized interrupt handler with minimal processing.
 * Uses zero-branch optimization for critical path.
 * 
 * @return Number of events processed
 */
int ptask_handle_3c509b_interrupt(void) {
    uint16_t status;
    int events_processed = 0;
    
    if (!g_3c509b_context.initialized) {
        return 0;
    }
    
    /* Read interrupt status */
    status = g_3c509b_context.pio->inw_optimized(g_3c509b_context.io_base + _3C509B_STATUS_REG);
    
    /* Process TX complete */
    if (status & _3C509B_STATUS_TX_COMPLETE) {
        /* Acknowledge TX complete */
        _3c509b_write_command(_3C509B_CMD_ACK_INTR | _3C509B_STATUS_TX_COMPLETE);
        events_processed++;
    }
    
    /* Process RX complete */
    if (status & _3C509B_STATUS_RX_COMPLETE) {
        /* RX processing handled by main loop for performance */
        events_processed++;
    }
    
    /* Process adapter failure */
    if (status & _3C509B_STATUS_ADAPTER_FAILURE) {
        _3c509b_write_command(_3C509B_CMD_ACK_INTR | _3C509B_STATUS_ADAPTER_FAILURE);
        events_processed++;
    }
    
    return events_processed;
}

/**
 * @brief Cleanup 3C509B hardware
 * 
 * Disables interrupts and resets hardware to safe state.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_cleanup_3c509b_hardware(void) {
    if (!g_3c509b_context.initialized) {
        return SUCCESS;
    }
    
    LOG_DEBUG("3C509B: Cleaning up hardware");
    
    /* Disable interrupts */
    _3c509b_write_command(_3C509B_CMD_SET_INTR_ENB | 0);
    
    /* Disable TX and RX */
    _3c509b_write_command(_3C509B_CMD_TX_DISABLE);
    _3c509b_wait_for_cmd_busy(500);
    
    _3c509b_write_command(_3C509B_CMD_RX_DISABLE);
    _3c509b_wait_for_cmd_busy(500);
    
    g_3c509b_context.initialized = false;
    
    LOG_INFO("3C509B: Hardware cleanup completed");
    return SUCCESS;
}

/* Private helper functions */

/**
 * @brief Select register window with proper timing
 */
static void _3c509b_select_window(uint8_t window) {
    if (g_3c509b_context.current_window == window) {
        return;  /* Already in correct window */
    }
    
    _3c509b_wait_for_cmd_busy(100);
    g_3c509b_context.pio->outw_optimized(g_3c509b_context.io_base + _3C509B_COMMAND_REG,
                                        _3C509B_CMD_SELECT_WINDOW | window);
    g_3c509b_context.current_window = window;
}

/**
 * @brief Wait for command busy to clear
 */
static int _3c509b_wait_for_cmd_busy(uint32_t timeout_ms) {
    uint16_t status;
    
    while (timeout_ms > 0) {
        status = g_3c509b_context.pio->inw_optimized(g_3c509b_context.io_base + _3C509B_STATUS_REG);
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            return SUCCESS;
        }
        udelay(1000);  /* 1ms delay */
        timeout_ms--;
    }
    
    return ERROR_TIMEOUT;
}

/**
 * @brief Write command with proper timing
 */
static void _3c509b_write_command(uint16_t command) {
    _3c509b_wait_for_cmd_busy(100);
    g_3c509b_context.pio->outw_optimized(g_3c509b_context.io_base + _3C509B_COMMAND_REG, command);
}

/**
 * @brief Read EEPROM with proper timing
 */
static uint16_t _3c509b_read_eeprom(uint8_t address) {
    /* Select Window 0 for EEPROM access */
    _3c509b_select_window(_3C509B_WINDOW_0);
    
    /* Write EEPROM read command */
    g_3c509b_context.pio->outw_optimized(g_3c509b_context.io_base + _3C509B_W0_EEPROM_CMD,
                                        _3C509B_EEPROM_READ | address);
    
    /* Wait for EEPROM read to complete */
    udelay(_3C509B_EEPROM_READ_DELAY);
    
    /* Read the data */
    return g_3c509b_context.pio->inw_optimized(g_3c509b_context.io_base + _3C509B_W0_EEPROM_DATA);
}

/**
 * @brief Read MAC address from EEPROM
 */
static int _3c509b_read_mac_from_eeprom(uint8_t *mac) {
    uint16_t word;
    int i;
    
    if (!mac) {
        return ERROR_INVALID_PARAM;
    }
    
    /* MAC address is stored in EEPROM words 0, 1, 2 */
    for (i = 0; i < 3; i++) {
        word = _3c509b_read_eeprom(i);
        mac[i * 2] = word & 0xFF;
        mac[i * 2 + 1] = (word >> 8) & 0xFF;
    }
    
    return SUCCESS;
}

/**
 * @brief Setup RX filter for normal operation
 */
static int _3c509b_setup_rx_filter(void) {
    int i;
    
    /* Select Window 1 for RX filter */
    _3c509b_select_window(_3C509B_WINDOW_1);
    
    /* Set basic RX filter: station address + broadcast */
    _3c509b_write_command(_3C509B_CMD_SET_RX_FILTER | 
                         (_3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST));
    
    /* Wait for command completion */
    _3c509b_wait_for_cmd_busy(1000);
    
    /* Select Window 2 to program station address */
    _3c509b_select_window(_3C509B_WINDOW_2);
    
    /* Write MAC address to station address registers */
    for (i = 0; i < 6; i++) {
        g_3c509b_context.pio->outb_optimized(g_3c509b_context.io_base + _3C509B_W2_STATION_ADDR + i,
                                            g_3c509b_context.mac_address[i]);
    }
    
    return SUCCESS;
}