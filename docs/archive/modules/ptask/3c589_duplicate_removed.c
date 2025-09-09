/**
 * @file 3c589.c
 * @brief 3C589 PCMCIA Driver Implementation for PTASK.MOD
 * 
 * Agent 06 Implementation - 3C589 PCMCIA with CIS integration and shared PIO
 * Includes PCMCIA Card Services interface and hot-plug support
 */

#include "ptask_internal.h"
#include "../../include/3c509b.h"  /* Shares registers with 3C509B */
#include "../pcmcia/include/pcmcia_internal.h"

/* 3C589 PCMCIA Specific Definitions */
#define MANFID_3COM             0x0101
#define PRODID_3C589            0x0589
#define PRODID_3C589B           0x058A
#define PRODID_3C589C           0x058B
#define PRODID_3C589D           0x058C

/* PCMCIA I/O Window Configuration */
#define PCMCIA_IO_WINDOW_SIZE   16      /* 16-byte I/O window */
#define PCMCIA_MAX_IO_WINDOWS   2       /* Maximum I/O windows */

/* Card Services Functions */
#define CS_REQUEST_IO           0x1F
#define CS_RELEASE_IO           0x20
#define CS_REQUEST_IRQ          0x21
#define CS_RELEASE_IRQ          0x22
#define CS_REQUEST_CONFIGURATION 0x23
#define CS_RELEASE_CONFIGURATION 0x24

/* Static context for 3C589 hardware */
static struct {
    uint8_t  socket;            /* PCMCIA socket number */
    uint16_t io_base;          /* Base I/O address */
    uint8_t  irq;              /* IRQ number */
    uint8_t  mac_address[6];   /* Hardware MAC address */
    bool     initialized;      /* Hardware initialized */
    bool     card_present;     /* Card insertion status */
    bool     card_services_available; /* Card Services detected */
    uint16_t current_window;   /* Current register window */
    pio_interface_t *pio;      /* Shared PIO interface */
    
    /* PCMCIA configuration */
    uint8_t  config_index;     /* Configuration index */
    uint16_t config_base;      /* Configuration base address */
    cis_3com_info_t cis_info;  /* Parsed CIS information */
} g_3c589_context = {0};

/* Forward declarations */
static int _3c589_detect_card_services(void);
static int _3c589_parse_card_cis(void);
static int _3c589_allocate_resources(void);
static int _3c589_configure_card(void);
static int _3c589_setup_io_window(void);
static void _3c589_select_window(uint8_t window);
static int _3c589_read_mac_from_card(uint8_t *mac);

/**
 * @brief Detect 3C589 PCMCIA card
 * 
 * Detects PCMCIA card presence and identifies 3C589 variants.
 * Uses Card Services if available, falls back to direct controller access.
 * 
 * @return Card type ID on success, negative error code on failure
 */
int ptask_detect_3c589(void) {
    int result;
    uint8_t socket;
    
    LOG_DEBUG("3C589: Starting PCMCIA detection");
    
    /* Initialize PCMCIA context */
    memset(&g_3c589_context, 0, sizeof(g_3c589_context));
    
    /* Detect Card Services availability */
    result = _3c589_detect_card_services();
    if (result < 0) {
        LOG_DEBUG("3C589: Card Services not available, using direct access");
        g_3c589_context.card_services_available = false;
    } else {
        LOG_DEBUG("3C589: Card Services detected");
        g_3c589_context.card_services_available = true;
    }
    
    /* Scan PCMCIA sockets for 3Com cards */
    for (socket = 0; socket < 4; socket++) {
        result = _3c589_check_socket(socket);
        if (result > 0) {
            g_3c589_context.socket = socket;
            g_3c589_context.card_present = true;
            LOG_INFO("3C589: Found card in socket %d", socket);
            
            /* Parse CIS to identify card variant */
            result = _3c589_parse_card_cis();
            if (result < 0) {
                LOG_ERROR("3C589: CIS parsing failed: %d", result);
                continue;
            }
            
            /* Validate this is a supported 3Com card */
            if (g_3c589_context.cis_info.card_type >= CARD_3C589 &&
                g_3c589_context.cis_info.card_type <= CARD_3C589D) {
                LOG_INFO("3C589: Detected %s", 
                         card_type_name(g_3c589_context.cis_info.card_type));
                return g_3c589_context.cis_info.card_type;
            }
        }
    }
    
    LOG_DEBUG("3C589: No supported PCMCIA cards found");
    return ERROR_HARDWARE_NOT_FOUND;
}

/**
 * @brief Check specific PCMCIA socket for card presence
 * 
 * @param socket Socket number to check
 * @return Positive if card present, 0 if empty, negative on error
 */
static int _3c589_check_socket(uint8_t socket) {
    if (g_3c589_context.card_services_available) {
        /* Use Card Services to check socket status */
        return cs_get_socket_status(socket);
    } else {
        /* Direct PCIC controller access */
        return pcic_check_socket_status(socket);
    }
}

/**
 * @brief Initialize 3C589 PCMCIA hardware
 * 
 * Performs complete PCMCIA card initialization:
 * - Resource allocation (I/O, IRQ)
 * - Card configuration
 * - Hardware setup
 * - MAC address reading
 * 
 * @param nic NIC information structure to fill
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_init_3c589_hardware(nic_info_t *nic) {
    int result;
    timing_context_t timing;
    uint16_t init_time_us;
    
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_3c589_context.card_present) {
        LOG_ERROR("3C589: No card present for initialization");
        return ERROR_HARDWARE_NOT_FOUND;
    }
    
    LOG_DEBUG("3C589: Initializing PCMCIA hardware in socket %d", 
              g_3c589_context.socket);
    
    /* Start timing measurement */
    TIMING_START(timing);
    
    /* Get shared PIO interface */
    g_3c589_context.pio = pio_get_interface();
    if (!g_3c589_context.pio) {
        LOG_ERROR("3C589: Shared PIO interface not available");
        return ERROR_DEPENDENCY_NOT_MET;
    }
    
    /* Allocate PCMCIA resources */
    result = _3c589_allocate_resources();
    if (result < 0) {
        LOG_ERROR("3C589: Resource allocation failed: %d", result);
        return result;
    }
    
    /* Configure the card */
    result = _3c589_configure_card();
    if (result < 0) {
        LOG_ERROR("3C589: Card configuration failed: %d", result);
        return result;
    }
    
    /* Setup I/O window */
    result = _3c589_setup_io_window();
    if (result < 0) {
        LOG_ERROR("3C589: I/O window setup failed: %d", result);
        return result;
    }
    
    /* Store configuration in NIC structure */
    nic->io_base = g_3c589_context.io_base;
    nic->irq = g_3c589_context.irq;
    
    /* Read MAC address from card */
    result = _3c589_read_mac_from_card(g_3c589_context.mac_address);
    if (result < 0) {
        LOG_ERROR("3C589: Failed to read MAC address: %d", result);
        return result;
    }
    
    /* Copy MAC to NIC structure */
    memcpy(nic->mac, g_3c589_context.mac_address, 6);
    memcpy(nic->perm_mac, g_3c589_context.mac_address, 6);
    
    LOG_INFO("3C589: MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             nic->mac[0], nic->mac[1], nic->mac[2],
             nic->mac[3], nic->mac[4], nic->mac[5]);
    
    /* Initialize card hardware (similar to 3C509B) */
    _3c589_select_window(_3C509B_WINDOW_1);
    
    /* Reset the card */
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_TOTAL_RESET);
    mdelay(2);  /* Wait for reset */
    
    /* Enable interrupts */
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_SET_INTR_ENB | 0x1E);
    
    /* Enable TX and RX */
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_TX_ENABLE);
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_RX_ENABLE);
    
    /* Set NIC parameters */
    nic->speed = 10;             /* 3C589 is 10 Mbps */
    nic->full_duplex = false;    /* Half duplex only */
    nic->mtu = 1500;            /* Standard Ethernet MTU */
    nic->link_up = true;        /* Assume link is up */
    
    g_3c589_context.initialized = true;
    
    TIMING_END(timing);
    init_time_us = TIMING_GET_MICROSECONDS(timing);
    
    LOG_INFO("3C589: Hardware initialization completed in %d μs", init_time_us);
    return SUCCESS;
}

/**
 * @brief Send packet via 3C589 using shared PIO
 * 
 * Uses same PIO interface as 3C509B since register layout is compatible.
 * 
 * @param packet_data Packet data pointer
 * @param packet_length Packet length
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_send_3c589_packet(const void *packet_data, uint16_t packet_length) {
    timing_context_t timing;
    uint16_t status, tx_free;
    uint16_t cli_time_us;
    const uint16_t *packet_words;
    uint16_t words;
    
    if (!packet_data || packet_length == 0 || packet_length > 1514) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_3c589_context.initialized || !g_3c589_context.card_present) {
        return ERROR_NOT_INITIALIZED;
    }
    
    /* Start CLI timing measurement */
    TIMING_CLI_START(timing);
    
    /* Check TX availability */
    status = g_3c589_context.pio->inw_optimized(g_3c589_context.io_base + _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_TX_AVAILABLE)) {
        TIMING_CLI_END(timing);
        return ERROR_BUSY;
    }
    
    /* Check TX FIFO space */
    tx_free = g_3c589_context.pio->inw_optimized(g_3c589_context.io_base + _3C509B_TX_FREE);
    if (tx_free < packet_length) {
        TIMING_CLI_END(timing);
        return ERROR_BUSY;
    }
    
    /* Write packet length to TX FIFO */
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_TX_FIFO, packet_length);
    
    /* Use optimized word transfers */
    packet_words = (const uint16_t*)packet_data;
    words = packet_length / 2;
    
    /* CPU-optimized bulk transfer */
    g_3c589_context.pio->outsw_optimized(g_3c589_context.io_base + _3C509B_TX_FIFO,
                                        packet_words, words);
    
    /* Handle odd byte */
    if (packet_length & 1) {
        const uint8_t *packet_bytes = (const uint8_t*)packet_data;
        g_3c589_context.pio->outb_optimized(g_3c589_context.io_base + _3C509B_TX_FIFO,
                                           packet_bytes[packet_length - 1]);
    }
    
    TIMING_CLI_END(timing);
    
    /* Validate CLI timing */
    cli_time_us = TIMING_GET_MICROSECONDS(timing);
    if (cli_time_us > PTASK_CLI_TIMEOUT_US) {
        LOG_WARNING("3C589: CLI time %d μs exceeds limit", cli_time_us);
    }
    
    LOG_TRACE("3C589: Sent packet of %d bytes in %d μs", packet_length, cli_time_us);
    
    return SUCCESS;
}

/**
 * @brief Receive packet from 3C589 using shared PIO
 * 
 * Uses same PIO interface as 3C509B since register layout is compatible.
 * 
 * @param buffer Receive buffer
 * @param buffer_size Buffer size
 * @param received_length Pointer to store actual received length
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_receive_3c589_packet(void *buffer, uint16_t buffer_size, uint16_t *received_length) {
    timing_context_t timing;
    uint16_t status, rx_status, packet_length;
    uint16_t *buffer_words;
    uint16_t words;
    uint16_t cli_time_us;
    
    if (!buffer || !received_length || buffer_size == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_3c589_context.initialized || !g_3c589_context.card_present) {
        return ERROR_NOT_INITIALIZED;
    }
    
    *received_length = 0;
    
    /* Start CLI timing measurement */
    TIMING_CLI_START(timing);
    
    /* Check RX status */
    status = g_3c589_context.pio->inw_optimized(g_3c589_context.io_base + _3C509B_STATUS_REG);
    if (!(status & _3C509B_STATUS_RX_COMPLETE)) {
        TIMING_CLI_END(timing);
        return ERROR_NO_DATA;
    }
    
    /* Read RX status and packet length */
    rx_status = g_3c589_context.pio->inw_optimized(g_3c589_context.io_base + _3C509B_RX_STATUS);
    packet_length = rx_status & 0x7FF;  /* Lower 11 bits */
    
    /* Check for RX errors */
    if (rx_status & 0x8000) {  /* Error bit */
        g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                           _3C509B_CMD_RX_DISCARD);
        TIMING_CLI_END(timing);
        return ERROR_IO;
    }
    
    /* Check buffer size */
    if (packet_length > buffer_size) {
        g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                           _3C509B_CMD_RX_DISCARD);
        TIMING_CLI_END(timing);
        return ERROR_BUFFER_TOO_SMALL;
    }
    
    /* Use optimized word transfers */
    buffer_words = (uint16_t*)buffer;
    words = packet_length / 2;
    
    /* CPU-optimized bulk transfer */
    g_3c589_context.pio->insw_optimized(g_3c589_context.io_base + _3C509B_RX_FIFO,
                                       buffer_words, words);
    
    /* Handle odd byte */
    if (packet_length & 1) {
        uint8_t *buffer_bytes = (uint8_t*)buffer;
        buffer_bytes[packet_length - 1] = 
            g_3c589_context.pio->inb_optimized(g_3c589_context.io_base + _3C509B_RX_FIFO);
    }
    
    TIMING_CLI_END(timing);
    
    *received_length = packet_length;
    
    /* Validate CLI timing */
    cli_time_us = TIMING_GET_MICROSECONDS(timing);
    if (cli_time_us > PTASK_CLI_TIMEOUT_US) {
        LOG_WARNING("3C589: CLI time %d μs exceeds limit", cli_time_us);
    }
    
    LOG_TRACE("3C589: Received packet of %d bytes in %d μs", packet_length, cli_time_us);
    
    return SUCCESS;
}

/**
 * @brief Handle 3C589 interrupt (called from ISR)
 * 
 * Uses same interrupt handling as 3C509B since register layout is compatible.
 * 
 * @return Number of events processed
 */
int ptask_handle_3c589_interrupt(void) {
    uint16_t status;
    int events_processed = 0;
    
    if (!g_3c589_context.initialized || !g_3c589_context.card_present) {
        return 0;
    }
    
    /* Read interrupt status */
    status = g_3c589_context.pio->inw_optimized(g_3c589_context.io_base + _3C509B_STATUS_REG);
    
    /* Process TX complete */
    if (status & _3C509B_STATUS_TX_COMPLETE) {
        g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                           _3C509B_CMD_ACK_INTR | _3C509B_STATUS_TX_COMPLETE);
        events_processed++;
    }
    
    /* Process RX complete */
    if (status & _3C509B_STATUS_RX_COMPLETE) {
        /* RX processing handled by main loop */
        events_processed++;
    }
    
    /* Process adapter failure */
    if (status & _3C509B_STATUS_ADAPTER_FAILURE) {
        g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                           _3C509B_CMD_ACK_INTR | _3C509B_STATUS_ADAPTER_FAILURE);
        events_processed++;
    }
    
    return events_processed;
}

/**
 * @brief Handle PCMCIA hot-plug events
 * 
 * Responds to card insertion/removal events.
 * 
 * @param event Hot-plug event type
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_handle_3c589_hotplug(uint8_t event) {
    switch (event) {
        case PCMCIA_EVENT_CARD_INSERTION:
            LOG_INFO("3C589: Card insertion detected in socket %d", 
                     g_3c589_context.socket);
            g_3c589_context.card_present = true;
            /* Re-initialize if needed */
            break;
            
        case PCMCIA_EVENT_CARD_REMOVAL:
            LOG_INFO("3C589: Card removal detected in socket %d", 
                     g_3c589_context.socket);
            g_3c589_context.card_present = false;
            g_3c589_context.initialized = false;
            /* Cleanup resources */
            break;
            
        default:
            LOG_WARNING("3C589: Unknown hot-plug event: %d", event);
            return ERROR_UNSUPPORTED;
    }
    
    return SUCCESS;
}

/**
 * @brief Cleanup 3C589 PCMCIA hardware
 * 
 * Releases PCMCIA resources and disables card.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_cleanup_3c589_hardware(void) {
    if (!g_3c589_context.initialized) {
        return SUCCESS;
    }
    
    LOG_DEBUG("3C589: Cleaning up PCMCIA hardware");
    
    /* Disable interrupts */
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_SET_INTR_ENB | 0);
    
    /* Disable TX and RX */
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_TX_DISABLE);
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_RX_DISABLE);
    
    /* Release PCMCIA resources */
    if (g_3c589_context.card_services_available) {
        cs_release_configuration(g_3c589_context.socket);
        cs_release_irq(g_3c589_context.socket);
        cs_release_io(g_3c589_context.socket);
    }
    
    g_3c589_context.initialized = false;
    
    LOG_INFO("3C589: PCMCIA hardware cleanup completed");
    return SUCCESS;
}

/* Private helper functions */

/**
 * @brief Detect Card Services availability
 */
static int _3c589_detect_card_services(void) {
    /* Try to call Card Services GetStatus function */
    return cs_get_card_services_info();
}

/**
 * @brief Parse card CIS information
 */
static int _3c589_parse_card_cis(void) {
    int result;
    
    result = parse_3com_cis(g_3c589_context.socket, &g_3c589_context.cis_info);
    if (result < 0) {
        LOG_ERROR("3C589: CIS parsing failed: %d", result);
        return result;
    }
    
    LOG_DEBUG("3C589: CIS parsed - %s (ID: %04X)",
              g_3c589_context.cis_info.product_name,
              g_3c589_context.cis_info.product_id);
    
    return result;
}

/**
 * @brief Allocate PCMCIA resources
 */
static int _3c589_allocate_resources(void) {
    config_entry_t *config;
    int result;
    
    if (g_3c589_context.cis_info.config_count == 0) {
        LOG_ERROR("3C589: No valid configurations found in CIS");
        return ERROR_CONFIGURATION_NOT_FOUND;
    }
    
    /* Use first available configuration */
    config = &g_3c589_context.cis_info.configs[0];
    g_3c589_context.config_index = config->index;
    
    if (g_3c589_context.card_services_available) {
        /* Use Card Services for resource allocation */
        result = cs_request_io(g_3c589_context.socket, config->io_base, config->io_size);
        if (result < 0) {
            LOG_ERROR("3C589: I/O allocation failed: %d", result);
            return result;
        }
        
        result = cs_request_irq(g_3c589_context.socket, config->irq_mask);
        if (result < 0) {
            LOG_ERROR("3C589: IRQ allocation failed: %d", result);
            return result;
        }
        
        g_3c589_context.io_base = config->io_base;
        g_3c589_context.irq = find_first_set_bit(config->irq_mask);
    } else {
        /* Direct resource allocation */
        g_3c589_context.io_base = config->io_base ? config->io_base : 0x300;
        g_3c589_context.irq = find_first_set_bit(config->irq_mask);
        if (g_3c589_context.irq == 0) {
            g_3c589_context.irq = 3;  /* Default IRQ */
        }
    }
    
    LOG_DEBUG("3C589: Resources allocated - I/O: 0x%X, IRQ: %d",
              g_3c589_context.io_base, g_3c589_context.irq);
    
    return SUCCESS;
}

/**
 * @brief Configure PCMCIA card
 */
static int _3c589_configure_card(void) {
    if (g_3c589_context.card_services_available) {
        return cs_request_configuration(g_3c589_context.socket, g_3c589_context.config_index);
    } else {
        /* Direct configuration via PCIC controller */
        return pcic_configure_socket(g_3c589_context.socket, g_3c589_context.config_index);
    }
}

/**
 * @brief Setup I/O window for card access
 */
static int _3c589_setup_io_window(void) {
    /* I/O window setup handled by Card Services or direct PCIC access */
    LOG_DEBUG("3C589: I/O window setup at 0x%X", g_3c589_context.io_base);
    return SUCCESS;
}

/**
 * @brief Select register window (same as 3C509B)
 */
static void _3c589_select_window(uint8_t window) {
    if (g_3c589_context.current_window == window) {
        return;
    }
    
    g_3c589_context.pio->outw_optimized(g_3c589_context.io_base + _3C509B_COMMAND_REG,
                                       _3C509B_CMD_SELECT_WINDOW | window);
    g_3c589_context.current_window = window;
}

/**
 * @brief Read MAC address from card
 */
static int _3c589_read_mac_from_card(uint8_t *mac) {
    int i;
    
    if (!mac) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select Window 2 for station address */
    _3c589_select_window(_3C509B_WINDOW_2);
    
    /* Read MAC address from station address registers */
    for (i = 0; i < 6; i++) {
        mac[i] = g_3c589_context.pio->inb_optimized(g_3c589_context.io_base + 
                                                   _3C509B_W2_STATION_ADDR + i);
    }
    
    /* Validate MAC address (not all zeros or all FFs) */
    bool valid = false;
    for (i = 0; i < 6; i++) {
        if (mac[i] != 0x00 && mac[i] != 0xFF) {
            valid = true;
            break;
        }
    }
    
    if (!valid) {
        LOG_ERROR("3C589: Invalid MAC address read from card");
        return ERROR_INVALID_MAC_ADDRESS;
    }
    
    return SUCCESS;
}

/**
 * @brief Find first set bit in mask
 */
static uint8_t find_first_set_bit(uint16_t mask) {
    uint8_t bit = 0;
    
    while (bit < 16 && !(mask & (1 << bit))) {
        bit++;
    }
    
    return (bit < 16) ? bit : 0;
}