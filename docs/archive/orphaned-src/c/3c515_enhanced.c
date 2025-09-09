/**
 * @file 3c515_enhanced.c
 * @brief Enhanced 3Com 3C515-TX driver implementation with 16-descriptor rings
 * 
 * Sprint 0B.3: Enhanced Ring Buffer Management Integration
 * 
 * This enhanced driver implementation replaces the basic ring buffer management
 * with the sophisticated enhanced ring buffer system providing:
 * - 16-descriptor TX/RX rings (doubled from 8)
 * - Linux-style cur/dirty pointer tracking
 * - Zero memory leak guarantee
 * - Sophisticated buffer recycling
 * - Comprehensive statistics and monitoring
 * - Enhanced error handling and recovery
 */

#include "../include/3c515.h"
#include "../include/enhanced_ring_context.h"
#include "../include/dma.h"
#include "../include/eeprom.h"
#include "../include/logging.h"
#include "../include/error_handling.h"

/* Enhanced NIC information structure */
typedef struct {
    uint16_t io_base;                           /* Hardware I/O base address */
    uint8_t irq;                                /* IRQ number */
    enhanced_ring_context_t *ring_context;      /* Enhanced ring buffer context */
    uint8_t nic_index;                          /* NIC index for DMA operations */
    eeprom_config_t eeprom_config;              /* Hardware configuration from EEPROM */
    uint8_t hardware_ready;                     /* Hardware initialization complete flag */
    uint32_t packets_transmitted;               /* Packet transmission counter */
    uint32_t packets_received;                  /* Packet reception counter */
    uint32_t last_error;                        /* Last error code */
    bool driver_active;                         /* Driver active state */
    bool dma_enabled;                           /* DMA operations enabled */
    uint32_t scatter_gather_packets;            /* Packets sent using scatter-gather */
    uint32_t consolidated_packets;              /* Packets requiring consolidation */
} enhanced_nic_info_t;

/* Global enhanced NIC instance */
static enhanced_nic_info_t g_enhanced_nic;
static bool g_driver_initialized = false;

/* Internal helper functions */
static int setup_hardware_registers(enhanced_nic_info_t *nic);
static int configure_dma_descriptors(enhanced_nic_info_t *nic);
static int start_dma_engines(enhanced_nic_info_t *nic);
static void stop_dma_engines(enhanced_nic_info_t *nic);
static int process_tx_completions(enhanced_nic_info_t *nic);
static int process_rx_packets(enhanced_nic_info_t *nic);
static void update_driver_statistics(enhanced_nic_info_t *nic);
static int send_packet_scatter_gather(enhanced_nic_info_t *nic, const uint8_t *packet_data, 
                                     uint16_t packet_len, dma_fragment_t *fragments, uint16_t frag_count);
static int send_packet_single_buffer(enhanced_nic_info_t *nic, const uint8_t *packet_data, uint16_t packet_len);

/**
 * @brief Initialize the enhanced 3C515-TX NIC driver
 * @param io_base Hardware I/O base address
 * @param irq IRQ number
 * @param nic_index NIC index for multi-NIC support
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_init(uint16_t io_base, uint8_t irq, uint8_t nic_index) {
    int result;
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    
    log_info("Initializing enhanced 3C515-TX driver with scatter-gather DMA (NIC %d)", nic_index);
    
    if (g_driver_initialized) {
        log_warning("Driver already initialized, performing cleanup first");
        _3c515_enhanced_cleanup();
    }
    
    /* Clear NIC structure */
    memory_zero(nic, sizeof(enhanced_nic_info_t));
    
    /* Set basic configuration */
    nic->io_base = io_base;
    nic->irq = irq;
    nic->nic_index = nic_index;
    nic->driver_active = false;
    nic->dma_enabled = false;
    
    /* Initialize DMA subsystem if not already done */
    result = dma_init();
    if (result != 0) {
        log_error("Failed to initialize DMA subsystem: %d", result);
        return result;
    }
    
    /* Allocate and initialize enhanced ring context */
    nic->ring_context = &g_main_ring_context;  /* Use global context or allocate */
    if (!nic->ring_context) {
        log_error("Failed to allocate ring context");
        return -RING_ERROR_OUT_OF_MEMORY;
    }
    
    /* Initialize enhanced ring buffer management */
    result = enhanced_ring_init(nic->ring_context, io_base, irq);
    if (result != 0) {
        log_error("Failed to initialize enhanced ring management: %d", result);
        return result;
    }
    
    /* Initialize DMA context for this NIC */
    result = dma_init_nic_context(nic_index, 0x5051, io_base, nic->ring_context);
    if (result != 0) {
        log_error("Failed to initialize DMA context: %d", result);
        enhanced_ring_cleanup(nic->ring_context);
        return result;
    }
    
    nic->dma_enabled = true;
    log_info("DMA context initialized for 3C515-TX scatter-gather operations");
    
    /* Setup hardware registers */
    result = setup_hardware_registers(nic);
    if (result != 0) {
        log_error("Failed to setup hardware registers: %d", result);
        dma_cleanup_nic_context(nic_index);
        enhanced_ring_cleanup(nic->ring_context);
        return result;
    }
    
    /* Configure DMA descriptors */
    result = configure_dma_descriptors(nic);
    if (result != 0) {
        log_error("Failed to configure DMA descriptors: %d", result);
        dma_cleanup_nic_context(nic_index);
        enhanced_ring_cleanup(nic->ring_context);
        return result;
    }
    
    /* Start DMA engines */
    result = start_dma_engines(nic);
    if (result != 0) {
        log_error("Failed to start DMA engines: %d", result);
        enhanced_ring_cleanup(nic->ring_context);
        return result;
    }
    
    /* Mark hardware as ready */
    nic->hardware_ready = 1;
    nic->driver_active = true;
    g_driver_initialized = true;
    
    log_info("Enhanced 3C515-TX driver initialized successfully");
    log_info("  I/O Base: 0x%04X, IRQ: %d", io_base, irq);
    log_info("  TX Ring: %d descriptors, RX Ring: %d descriptors", TX_RING_SIZE, RX_RING_SIZE);
    log_info("  Enhanced features: Linux-style tracking, zero-leak guarantee, statistics");
    
    return 0;
}

/**
 * @brief Cleanup the enhanced 3C515-TX NIC driver
 */
void _3c515_enhanced_cleanup(void) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    
    if (!g_driver_initialized) {
        return;
    }
    
    log_info("Cleaning up enhanced 3C515-TX driver");
    
    /* Stop DMA engines */
    stop_dma_engines(nic);
    
    /* Cleanup enhanced ring management */
    if (nic->ring_context) {
        enhanced_ring_cleanup(nic->ring_context);
        nic->ring_context = NULL;
    }
    
    /* Print final statistics */
    log_info("Final driver statistics:");
    log_info("  Packets transmitted: %u", nic->packets_transmitted);
    log_info("  Packets received: %u", nic->packets_received);
    
    /* Reset driver state */
    nic->hardware_ready = 0;
    nic->driver_active = false;
    g_driver_initialized = false;
    
    log_info("Enhanced 3C515-TX driver cleanup completed");
}

/**
 * @brief Send a packet using enhanced ring buffer management
 * @param packet Data to send
 * @param len Length of the packet
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_send_packet(const uint8_t *packet, size_t len) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    enhanced_ring_context_t *ring;
    uint16_t entry;
    _3c515_tx_desc_t *desc;
    uint8_t *buffer;
    
    if (!g_driver_initialized || !nic->driver_active || !packet || len == 0) {
        log_error("Invalid parameters for packet transmission");
        return -RING_ERROR_INVALID_PARAM;
    }
    
    if (len > _3C515_TX_MAX_MTU) {
        log_error("Packet too large: %zu bytes (max %d)", len, _3C515_TX_MAX_MTU);
        return -RING_ERROR_INVALID_PARAM;
    }
    
    ring = nic->ring_context;
    
    /* Check if TX ring has space */
    if (get_tx_free_slots(ring) == 0) {
        /* Try to clean completed transmissions */
        process_tx_completions(nic);
        
        if (get_tx_free_slots(ring) == 0) {
            log_warning("TX ring full, packet dropped");
            ring->stats.ring_full_events++;
            return -RING_ERROR_RING_FULL;
        }
    }
    
    /* Get next TX descriptor */
    entry = ring->cur_tx % TX_RING_SIZE;
    desc = &ring->tx_ring[entry];
    
    /* Allocate buffer for this transmission */
    buffer = allocate_tx_buffer(ring, entry);
    if (!buffer) {
        log_error("Failed to allocate TX buffer");
        ring->stats.allocation_failures++;
        return -RING_ERROR_OUT_OF_MEMORY;
    }
    
    /* Copy packet data to buffer */
    memory_copy_optimized(buffer, packet, len);
    
    /* Configure descriptor */
    desc->addr = get_physical_address(buffer);
    desc->length = len | _3C515_TX_TX_INTR_BIT;  /* Request interrupt on completion */
    desc->status = 0;  /* Clear status, ready for hardware */
    
    /* Update ring pointers (Linux-style) */
    ring->cur_tx++;
    
    /* Update statistics */
    ring_stats_record_tx_packet(ring, len);
    nic->packets_transmitted++;
    
    /* Start DMA transmission */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);
    
    log_debug("Packet queued for transmission: %zu bytes, descriptor %d", len, entry);
    
    return 0;
}

/**
 * @brief Receive a packet using enhanced ring buffer management
 * @param buffer Buffer to store received packet
 * @param max_len Maximum buffer size
 * @param actual_len Pointer to store actual packet length
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_receive_packet(uint8_t *buffer, size_t max_len, size_t *actual_len) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    enhanced_ring_context_t *ring;
    uint16_t entry;
    _3c515_rx_desc_t *desc;
    uint8_t *rx_buffer;
    uint32_t packet_len;
    
    if (!g_driver_initialized || !nic->driver_active || !buffer || !actual_len) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    *actual_len = 0;
    ring = nic->ring_context;
    
    /* Process any completed receptions */
    process_rx_packets(nic);
    
    /* Check if RX ring has packets */
    entry = ring->dirty_rx % RX_RING_SIZE;
    desc = &ring->rx_ring[entry];
    
    /* Check if packet is ready */
    if (!(desc->status & _3C515_TX_RX_DESC_COMPLETE)) {
        return -RING_ERROR_RING_EMPTY;  /* No packet available */
    }
    
    /* Check for reception errors */
    if (desc->status & _3C515_TX_RX_DESC_ERROR) {
        log_warning("RX error on descriptor %d: status=0x%08x", entry, desc->status);
        ring_stats_record_rx_error(ring, desc->status);
        
        /* Recycle buffer and advance */
        recycle_rx_buffer(ring, entry);
        desc->status = 0;
        ring->dirty_rx++;
        
        /* Refill RX ring */
        refill_rx_ring(ring);
        
        return -RING_ERROR_BUFFER_CORRUPTION;
    }
    
    /* Extract packet length */
    packet_len = desc->length & _3C515_TX_RX_DESC_LEN_MASK;
    
    if (packet_len > max_len) {
        log_error("Received packet too large: %u bytes (buffer %zu)", packet_len, max_len);
        
        /* Still need to recycle the buffer */
        recycle_rx_buffer(ring, entry);
        desc->status = 0;
        ring->dirty_rx++;
        refill_rx_ring(ring);
        
        return -RING_ERROR_SIZE_MISMATCH;
    }
    
    /* Get buffer pointer */
    rx_buffer = ring->rx_buffers[entry];
    if (!rx_buffer) {
        log_error("RX buffer pointer is NULL for descriptor %d", entry);
        desc->status = 0;
        ring->dirty_rx++;
        return -RING_ERROR_BUFFER_CORRUPTION;
    }
    
    /* Copy packet data to user buffer */
    memory_copy_optimized(buffer, rx_buffer, packet_len);
    *actual_len = packet_len;
    
    /* Update statistics */
    ring_stats_record_rx_packet(ring, packet_len);
    nic->packets_received++;
    
    /* Recycle buffer for reuse */
    recycle_rx_buffer(ring, entry);
    desc->status = 0;
    ring->dirty_rx++;
    
    /* Refill RX ring */
    refill_rx_ring(ring);
    
    log_debug("Packet received: %u bytes from descriptor %d", packet_len, entry);
    
    return 0;
}

/**
 * @brief Handle interrupts from the enhanced 3C515-TX NIC
 */
void _3c515_enhanced_handle_interrupt(void) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    enhanced_ring_context_t *ring;
    uint16_t status;
    
    if (!g_driver_initialized || !nic->driver_active) {
        return;
    }
    
    ring = nic->ring_context;
    
    /* Read interrupt status */
    status = inw(nic->io_base + _3C515_TX_STATUS_REG);
    
    log_debug("Interrupt received: status=0x%04x", status);
    
    /* Handle TX completion */
    if (status & _3C515_TX_STATUS_DOWN_COMPLETE) {
        process_tx_completions(nic);
    }
    
    /* Handle RX completion */
    if (status & _3C515_TX_STATUS_UP_COMPLETE) {
        process_rx_packets(nic);
    }
    
    /* Handle DMA done */
    if (status & _3C515_TX_STATUS_DMA_DONE) {
        log_debug("DMA transfer completed");
    }
    
    /* Handle adapter failure */
    if (status & _3C515_TX_STATUS_ADAPTER_FAILURE) {
        log_error("Adapter failure detected");
        ring->stats.dma_stall_events++;
    }
    
    /* Update statistics */
    ring_stats_update(ring);
    update_driver_statistics(nic);
    
    /* Acknowledge interrupt */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_ACK_INTR | status);
}

/**
 * @brief Get enhanced driver statistics
 * @return Pointer to ring statistics or NULL
 */
const ring_stats_t *_3c515_enhanced_get_stats(void) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    
    if (!g_driver_initialized || !nic->ring_context) {
        return NULL;
    }
    
    return get_ring_stats(nic->ring_context);
}

/**
 * @brief Generate comprehensive driver report
 */
void _3c515_enhanced_generate_report(void) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    
    if (!g_driver_initialized) {
        log_info("Enhanced driver not initialized");
        return;
    }
    
    log_info("=== ENHANCED 3C515-TX DRIVER REPORT ===");
    
    /* Driver information */
    log_info("Driver Configuration:");
    log_info("  I/O Base: 0x%04X", nic->io_base);
    log_info("  IRQ: %d", nic->irq);
    log_info("  Hardware ready: %s", nic->hardware_ready ? "Yes" : "No");
    log_info("  Driver active: %s", nic->driver_active ? "Yes" : "No");
    
    /* Packet statistics */
    log_info("Packet Statistics:");
    log_info("  Transmitted: %u packets", nic->packets_transmitted);
    log_info("  Received: %u packets", nic->packets_received);
    
    /* Enhanced ring statistics */
    if (nic->ring_context) {
        ring_generate_stats_report(nic->ring_context);
    }
    
    log_info("=== END DRIVER REPORT ===");
}

/**
 * @brief Validate zero memory leaks in enhanced driver
 * @return 0 if no leaks, negative if leaks detected
 */
int _3c515_enhanced_validate_zero_leaks(void) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    
    if (!g_driver_initialized || !nic->ring_context) {
        return -RING_ERROR_INVALID_STATE;
    }
    
    return ring_validate_zero_leaks(nic->ring_context);
}

/* Internal helper function implementations */

static int setup_hardware_registers(enhanced_nic_info_t *nic) {
    /* Reset the NIC */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
    
    /* Wait for reset to complete */
    for (int i = 0; i < 1000; i++) {
        uint16_t status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if (!(status & _3C515_TX_STATUS_CMD_IN_PROGRESS)) {
            break;
        }
        /* Small delay */
        for (volatile int j = 0; j < 100; j++);
    }
    
    /* Select Window 7 for bus master control */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_7);
    
    log_debug("Hardware registers setup completed");
    return 0;
}

static int configure_dma_descriptors(enhanced_nic_info_t *nic) {
    enhanced_ring_context_t *ring = nic->ring_context;
    
    /* Set descriptor list pointers */
    outl(nic->io_base + _3C515_TX_DOWN_LIST_PTR, ring->tx_ring_phys);
    outl(nic->io_base + _3C515_TX_UP_LIST_PTR, ring->rx_ring_phys);
    
    log_debug("DMA descriptors configured: TX=0x%08x, RX=0x%08x", 
              ring->tx_ring_phys, ring->rx_ring_phys);
    
    return 0;
}

static int start_dma_engines(enhanced_nic_info_t *nic) {
    /* Enable transmitter and receiver */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_ENABLE);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);
    
    /* Start RX DMA */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_UP);
    
    log_debug("DMA engines started");
    return 0;
}

static void stop_dma_engines(enhanced_nic_info_t *nic) {
    /* Disable transmitter and receiver */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_DISABLE);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_DISABLE);
    
    /* Stall DMA engines */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_DOWN_STALL);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_UP_STALL);
    
    log_debug("DMA engines stopped");
}

static int process_tx_completions(enhanced_nic_info_t *nic) {
    enhanced_ring_context_t *ring = nic->ring_context;
    int completed = 0;
    
    /* Clean TX ring of completed transmissions */
    completed = clean_tx_ring(ring);
    
    if (completed > 0) {
        log_debug("Processed %d TX completions", completed);
    }
    
    return completed;
}

static int process_rx_packets(enhanced_nic_info_t *nic) {
    enhanced_ring_context_t *ring = nic->ring_context;
    int processed = 0;
    
    /* Process received packets (they will be handled by receive function) */
    /* Just ensure RX ring is filled */
    int result = refill_rx_ring(ring);
    if (result != 0) {
        log_warning("RX ring refill failed during interrupt processing");
    }
    
    return processed;
}

static void update_driver_statistics(enhanced_nic_info_t *nic) {
    /* Update any driver-specific statistics */
    /* Most statistics are handled by the enhanced ring management */
    
    /* Check for error conditions */
    if (nic->ring_context->stats.allocation_failures > 0) {
        log_debug("Driver statistics: %u allocation failures", 
                  nic->ring_context->stats.allocation_failures);
    }
}

/**
 * @brief Get current driver state information
 * @return Pointer to driver state structure
 */
const void *_3c515_enhanced_get_driver_info(void) {
    return g_driver_initialized ? &g_enhanced_nic : NULL;
}

/**
 * @brief Test enhanced driver functionality
 * @return 0 if tests pass, negative if failures
 */
int _3c515_enhanced_self_test(void) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    
    if (!g_driver_initialized) {
        log_error("Driver not initialized for self-test");
        return -RING_ERROR_INVALID_STATE;
    }
    
    log_info("Running enhanced driver self-test...");
    
    /* Test ring buffer system */
    int result = ring_run_self_test(nic->ring_context);
    if (result != 0) {
        log_error("Ring buffer self-test failed: %d", result);
        return result;
    }
    
    /* Validate zero leaks */
    result = _3c515_enhanced_validate_zero_leaks();
    if (result != 0) {
        log_error("Memory leak validation failed: %d", result);
        return result;
    }
    
    log_info("Enhanced driver self-test passed");
    return 0;
}

/* === Scatter-Gather DMA Functions === */

/**
 * @brief Send packet using scatter-gather DMA
 * @param nic NIC instance
 * @param packet_data Packet data (can be NULL if using fragments)
 * @param packet_len Total packet length
 * @param fragments Array of DMA fragments
 * @param frag_count Number of fragments
 * @return 0 on success, negative error code on failure
 */
static int send_packet_scatter_gather(enhanced_nic_info_t *nic, const uint8_t *packet_data, 
                                     uint16_t packet_len, dma_fragment_t *fragments, uint16_t frag_count) {
    int result;
    
    if (!nic->dma_enabled) {
        log_warning("DMA not enabled, falling back to single buffer mode");
        return send_packet_single_buffer(nic, packet_data, packet_len);
    }
    
    if (!fragments || frag_count == 0) {
        log_error("Invalid scatter-gather parameters");
        return -RING_ERROR_INVALID_PARAM;
    }
    
    if (frag_count > DMA_MAX_FRAGMENTS_3C515) {
        log_warning("Too many fragments (%u), consolidating", frag_count);
        nic->consolidated_packets++;
    } else {
        nic->scatter_gather_packets++;
    }
    
    log_debug("Sending packet using scatter-gather DMA: %u fragments, %u bytes total", 
              frag_count, packet_len);
    
    /* Use DMA scatter-gather system */
    result = dma_send_packet_sg(nic->nic_index, fragments, frag_count);
    if (result != 0) {
        log_error("Scatter-gather DMA send failed: %d", result);
        return result;
    }
    
    /* Update statistics */
    nic->packets_transmitted++;
    
    log_debug("Scatter-gather packet transmission completed successfully");
    
    return 0;
}

/**
 * @brief Send packet using single buffer (fallback)
 * @param nic NIC instance
 * @param packet_data Packet data
 * @param packet_len Packet length
 * @return 0 on success, negative error code on failure
 */
static int send_packet_single_buffer(enhanced_nic_info_t *nic, const uint8_t *packet_data, uint16_t packet_len) {
    enhanced_ring_context_t *ring;
    uint16_t entry;
    _3c515_tx_desc_t *desc;
    uint8_t *buffer;
    
    if (!packet_data || packet_len == 0) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    ring = nic->ring_context;
    
    /* Check if TX ring has space */
    if (get_tx_free_slots(ring) == 0) {
        process_tx_completions(nic);
        if (get_tx_free_slots(ring) == 0) {
            log_warning("TX ring full for single buffer transmission");
            return -RING_ERROR_RING_FULL;
        }
    }
    
    /* Get next TX descriptor */
    entry = ring->cur_tx % TX_RING_SIZE;
    desc = &ring->tx_ring[entry];
    
    /* Allocate buffer */
    buffer = allocate_tx_buffer(ring, entry);
    if (!buffer) {
        log_error("Failed to allocate TX buffer for single buffer transmission");
        return -RING_ERROR_OUT_OF_MEMORY;
    }
    
    /* Copy packet data */
    memory_copy_optimized(buffer, packet_data, packet_len);
    
    /* Configure descriptor */
    desc->addr = get_physical_address(buffer);
    desc->length = packet_len | _3C515_TX_TX_INTR_BIT;
    desc->status = 0;
    
    /* Update ring pointers */
    ring->cur_tx++;
    
    /* Update statistics */
    ring_stats_record_tx_packet(ring, packet_len);
    nic->packets_transmitted++;
    
    /* Start DMA transmission */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);
    
    log_debug("Single buffer packet queued: %u bytes, descriptor %d", packet_len, entry);
    
    return 0;
}

/**
 * @brief Send packet with automatic scatter-gather detection
 * @param packet_data Packet data
 * @param packet_len Packet length
 * @param fragments Optional fragments (NULL for single buffer)
 * @param frag_count Number of fragments (0 for single buffer)
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_send_packet_sg(const uint8_t *packet_data, uint16_t packet_len,
                                   dma_fragment_t *fragments, uint16_t frag_count) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    
    if (!g_driver_initialized || !nic->driver_active) {
        log_error("Driver not ready for scatter-gather transmission");
        return -RING_ERROR_INVALID_STATE;
    }
    
    if (packet_len > _3C515_TX_MAX_MTU) {
        log_error("Packet too large for transmission: %u bytes", packet_len);
        return -RING_ERROR_INVALID_PARAM;
    }
    
    /* Decide between scatter-gather and single buffer mode */
    if (fragments && frag_count > 1) {
        /* Use scatter-gather DMA */
        return send_packet_scatter_gather(nic, packet_data, packet_len, fragments, frag_count);
    } else {
        /* Use single buffer mode */
        return send_packet_single_buffer(nic, packet_data, packet_len);
    }
}

/**
 * @brief Create fragments from large packet data
 * @param packet_data Packet data to fragment
 * @param packet_len Total packet length
 * @param fragments Output fragment array
 * @param max_fragments Maximum number of fragments
 * @param fragment_size Size of each fragment
 * @return Number of fragments created, negative on error
 */
int _3c515_enhanced_create_fragments(const uint8_t *packet_data, uint16_t packet_len,
                                     dma_fragment_t *fragments, uint16_t max_fragments,
                                     uint16_t fragment_size) {
    uint16_t frag_count = 0;
    uint16_t remaining = packet_len;
    const uint8_t *data_ptr = packet_data;
    
    if (!packet_data || !fragments || max_fragments == 0 || fragment_size == 0) {
        return -RING_ERROR_INVALID_PARAM;
    }
    
    if (fragment_size > DMA_MAX_TRANSFER_SIZE) {
        log_warning("Fragment size too large, limiting to %u bytes", DMA_MAX_TRANSFER_SIZE);
        fragment_size = DMA_MAX_TRANSFER_SIZE;
    }
    
    /* Create fragments */
    while (remaining > 0 && frag_count < max_fragments) {
        uint16_t this_frag_size = (remaining > fragment_size) ? fragment_size : remaining;
        
        fragments[frag_count].physical_addr = dma_virt_to_phys((void*)data_ptr);
        fragments[frag_count].length = this_frag_size;
        fragments[frag_count].flags = 0;
        
        if (frag_count == 0) {
            fragments[frag_count].flags |= DMA_FRAG_FIRST;
        }
        
        if (remaining <= fragment_size) {
            fragments[frag_count].flags |= DMA_FRAG_LAST;
        }
        
        if (fragments[frag_count].physical_addr == 0) {
            log_error("Failed to get physical address for fragment %u", frag_count);
            return -RING_ERROR_MAPPING_FAILED;
        }
        
        data_ptr += this_frag_size;
        remaining -= this_frag_size;
        frag_count++;
    }
    
    if (remaining > 0) {
        log_warning("Packet truncated: %u bytes remaining after %u fragments", 
                    remaining, frag_count);
    }
    
    log_debug("Created %u fragments from %u byte packet", frag_count, packet_len);
    
    return frag_count;
}

/**
 * @brief Test scatter-gather DMA functionality
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_test_scatter_gather(void) {
    enhanced_nic_info_t *nic = &g_enhanced_nic;
    uint8_t test_data[1024];
    dma_fragment_t fragments[4];
    int result;
    
    if (!g_driver_initialized || !nic->dma_enabled) {
        log_error("Driver or DMA not ready for scatter-gather test");
        return -RING_ERROR_INVALID_STATE;
    }
    
    log_info("Running scatter-gather DMA test");
    
    /* Prepare test data */
    for (int i = 0; i < sizeof(test_data); i++) {
        test_data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Test 1: Single fragment transmission */
    result = _3c515_enhanced_create_fragments(test_data, 256, fragments, 1, 256);
    if (result != 1) {
        log_error("Failed to create single fragment: %d", result);
        return result;
    }
    
    result = _3c515_enhanced_send_packet_sg(test_data, 256, fragments, 1);
    if (result != 0) {
        log_error("Single fragment transmission failed: %d", result);
        return result;
    }
    
    log_info("Single fragment test passed");
    
    /* Test 2: Multiple fragment transmission */
    result = _3c515_enhanced_create_fragments(test_data, sizeof(test_data), fragments, 4, 256);
    if (result != 4) {
        log_error("Failed to create multiple fragments: expected 4, got %d", result);
        return -RING_ERROR_FRAGMENT_TOO_LARGE;
    }
    
    result = _3c515_enhanced_send_packet_sg(test_data, sizeof(test_data), fragments, 4);
    if (result != 0) {
        log_error("Multiple fragment transmission failed: %d", result);
        return result;
    }
    
    log_info("Multiple fragment test passed");
    
    /* Test DMA subsystem */
    result = dma_self_test(nic->nic_index);
    if (result != 0) {
        log_error("DMA self-test failed: %d", result);
        return result;
    }
    
    log_info("Scatter-gather DMA test completed successfully");
    
    /* Print statistics */
    log_info("Scatter-gather statistics:");
    log_info("  SG packets: %u", nic->scatter_gather_packets);
    log_info("  Consolidated packets: %u", nic->consolidated_packets);
    
    uint32_t sg_ops, consolidations, zero_copy, errors;
    result = dma_get_statistics(nic->nic_index, &sg_ops, &consolidations, &zero_copy, &errors);
    if (result == 0) {
        log_info("  DMA SG operations: %u", sg_ops);
        log_info("  DMA consolidations: %u", consolidations);
        log_info("  DMA zero-copy: %u", zero_copy);
        log_info("  DMA errors: %u", errors);
    }
    
    return 0;
}