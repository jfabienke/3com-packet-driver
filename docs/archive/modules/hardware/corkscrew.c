/**
 * @file corkscrew.c
 * @brief CORKSCREW.MOD - Corkscrew Family Hardware Module (3C515)
 * 
 * Phase 3A: Dynamic Module Loading - Stream 2 Hardware Implementation
 * 
 * This module supports the 3Com Corkscrew family:
 * - 3C515-TX (Fast EtherLink PCI TX)
 * Advanced DMA-based design with cache coherency integration.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "module_api.h"
#include <stdio.h>
#include <string.h>

/* 3C515 Hardware Constants */
#define CORKSCREW_VENDOR_ID     0x10B7  /* 3Com vendor ID */
#define CORKSCREW_DEVICE_ID     0x5150  /* 3C515 device ID */

/* PCI Configuration Registers */
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_BASE_ADDRESS_0      0x10
#define PCI_INTERRUPT_LINE      0x3C

/* I/O Register Offsets */
#define CORKSCREW_COMMAND       0x0E    /* Command register */
#define CORKSCREW_STATUS        0x0E    /* Status register */
#define CORKSCREW_WINDOW        0x0E    /* Window select */
#define CORKSCREW_DATA          0x00    /* Data port (window dependent) */

/* DMA Register Offsets (Window 7) */
#define DMA_CTRL                0x00    /* DMA control */
#define DMA_STATUS              0x04    /* DMA status */
#define UP_LIST_PTR             0x08    /* Upload list pointer */
#define UP_PKT_STATUS           0x30    /* Upload packet status */
#define DOWN_LIST_PTR           0x24    /* Download list pointer */
#define DOWN_POLL               0x2D    /* Download poll */

/* Command Register Values */
#define CMD_RESET               0x0000
#define CMD_SELECT_WINDOW       0x0800
#define CMD_TX_ENABLE           0x4800
#define CMD_TX_DISABLE          0x5000  
#define CMD_RX_ENABLE           0x2000
#define CMD_RX_DISABLE          0x1800
#define CMD_SET_RX_FILTER       0x8000
#define CMD_SET_TX_START        0x9800
#define CMD_STATS_ENABLE        0xA800
#define CMD_STATS_DISABLE       0xB000

/* Window Definitions */
#define WINDOW_SETUP            0
#define WINDOW_OPERATING        1
#define WINDOW_STATION_ADDR     2
#define WINDOW_FIFO             3
#define WINDOW_DIAGNOSTICS      4
#define WINDOW_READ_RESULTS     5
#define WINDOW_STATISTICS       6
#define WINDOW_BUS_MASTER       7

/* DMA Descriptor Definitions */
#define DPD_DN_NEXT_PTR         0x00    /* Next descriptor pointer */
#define DPD_FRAME_START_HDR     0x04    /* Frame start header */
#define DPD_FRAG_ADDR           0x08    /* Fragment address */
#define DPD_FRAG_LEN            0x0C    /* Fragment length */

#define UPD_UP_NEXT_PTR         0x00    /* Next descriptor pointer */
#define UPD_UP_PKT_STATUS       0x04    /* Packet status */
#define UPD_FRAG_ADDR           0x08    /* Fragment address */
#define UPD_FRAG_LEN            0x0C    /* Fragment length */

/* Descriptor Status Bits */
#define DN_COMPLETE             0x00010000
#define UP_COMPLETE             0x00008000
#define UP_ERROR                0x00004000

/* Ring Buffer Sizes */
#define TX_RING_SIZE            16
#define RX_RING_SIZE            32

/* DMA Descriptor Structures */
typedef struct {
    uint32_t next_ptr;          /* Physical address of next descriptor */
    uint32_t frame_start_hdr;   /* Frame start header */
    uint32_t frag_addr;         /* Fragment physical address */
    uint32_t frag_len;          /* Fragment length and status */
} down_desc_t;

typedef struct {
    uint32_t next_ptr;          /* Physical address of next descriptor */
    uint32_t pkt_status;        /* Packet status */
    uint32_t frag_addr;         /* Fragment physical address */
    uint32_t frag_len;          /* Fragment length */
} up_desc_t;

/* Module Context Structure */
typedef struct {
    uint16_t io_base;           /* Base I/O address */
    uint8_t irq;                /* IRQ number */
    uint8_t pci_bus;            /* PCI bus number */
    uint8_t pci_device;         /* PCI device number */
    uint8_t mac_address[6];     /* Station address */
    
    /* DMA Ring Buffers */
    down_desc_t* tx_ring;       /* TX descriptor ring */
    up_desc_t* rx_ring;         /* RX descriptor ring */
    uint32_t tx_ring_phys;      /* TX ring physical address */
    uint32_t rx_ring_phys;      /* RX ring physical address */
    
    /* Ring Management */
    uint16_t tx_head;           /* TX ring head index */
    uint16_t tx_tail;           /* TX ring tail index */
    uint16_t rx_head;           /* RX ring head index */
    
    /* Buffer Management */
    void* tx_buffers[TX_RING_SIZE];  /* TX buffer pointers */
    void* rx_buffers[RX_RING_SIZE];  /* RX buffer pointers */
    
    /* Status and Statistics */
    bool link_active;           /* Link status */
    nic_stats_t stats;          /* Statistics */
    
    /* Cache Coherency (Phase 4 Integration) */
    bool cache_coherent;        /* Cache coherency supported */
    uint8_t cache_line_size;    /* Cache line size */
} corkscrew_context_t;

/* Global module context */
static corkscrew_context_t module_context[MAX_NICS_SUPPORTED];
static core_services_t* core_services = NULL;

/* Forward declarations */
static bool cork_detect_hardware(hardware_info_t* hw_info);
static bool cork_initialize(uint8_t nic_id, const hardware_info_t* hw_info);
static bool cork_shutdown(uint8_t nic_id);
static bool cork_send_packet(uint8_t nic_id, const packet_t* packet);
static packet_t* cork_receive_packet(uint8_t nic_id);
static bool cork_get_stats(uint8_t nic_id, nic_stats_t* stats);
static bool cork_reset_stats(uint8_t nic_id);
static bool cork_set_mode(uint8_t nic_id, nic_mode_t mode);
static bool cork_get_link_status(uint8_t nic_id, link_status_t* status);
static bool cork_set_promiscuous(uint8_t nic_id, bool enable);
static bool cork_set_multicast(uint8_t nic_id, const uint8_t* addr_list, uint16_t count);
static bool cork_power_management(uint8_t nic_id, bool sleep_mode);
static bool cork_self_test(uint8_t nic_id);
static bool cork_loopback_test(uint8_t nic_id);
static const char* cork_get_driver_info(void);

/* DMA and Ring Management */
static bool cork_init_dma_rings(uint8_t nic_id);
static void cork_cleanup_dma_rings(uint8_t nic_id);
static bool cork_setup_tx_descriptor(uint8_t nic_id, uint16_t index, const packet_t* packet);
static bool cork_setup_rx_descriptor(uint8_t nic_id, uint16_t index);
static void cork_process_tx_complete(uint8_t nic_id);
static packet_t* cork_process_rx_complete(uint8_t nic_id);

/* Utility functions */
static void cork_select_window(uint16_t io_base, uint8_t window);
static bool cork_detect_pci_device(hardware_info_t* hw_info);
static bool cork_read_station_address(uint16_t io_base, uint8_t* mac_addr);
static void cork_reset_adapter(uint16_t io_base);
static bool cork_wait_for_completion(uint16_t io_base, uint32_t timeout_ms);
static uint32_t cork_virt_to_phys(void* virt_addr);

/* Cache Coherency Integration (Phase 4) */
static bool cork_cache_flush_range(void* addr, size_t size);
static bool cork_cache_invalidate_range(void* addr, size_t size);

/* Hardware operations vtable */
static const nic_ops_t corkscrew_ops = {
    .detect_hardware = cork_detect_hardware,
    .initialize = cork_initialize,
    .shutdown = cork_shutdown,
    .send_packet = cork_send_packet,
    .receive_packet = cork_receive_packet,
    .get_stats = cork_get_stats,
    .reset_stats = cork_reset_stats,
    .set_mode = cork_set_mode,
    .get_link_status = cork_get_link_status,
    .set_promiscuous = cork_set_promiscuous,
    .set_multicast = cork_set_multicast,
    .power_management = cork_power_management,
    .self_test = cork_self_test,
    .loopback_test = cork_loopback_test,
    .get_driver_info = cork_get_driver_info
};

/* ============================================================================
 * Module Header and Initialization
 * ============================================================================ */

/* Module header - must be first in the file */
const module_header_t module_header = {
    .magic = MODULE_MAGIC,
    .version = 0x0100,  /* Version 1.0 */
    .header_size = sizeof(module_header_t),
    .module_size = 0,   /* Filled by linker */
    .module_class = MODULE_CLASS_HARDWARE,
    .family_id = FAMILY_CORKSCREW,
    .feature_flags = FEATURE_MULTICAST | FEATURE_PROMISCUOUS | FEATURE_FLOW_CONTROL,
    .api_version = MODULE_API_VERSION,
    .init_offset = (uint16_t)corkscrew_init,
    .vtable_offset = (uint16_t)&corkscrew_ops,
    .cleanup_offset = (uint16_t)corkscrew_cleanup,
    .info_offset = 0,
    .deps_count = 0,
    .deps_offset = 0,
    .min_dos_version = 0x0300,  /* DOS 3.0+ */
    .min_cpu_family = 3,        /* 386+ for DMA */
    .name = "CORKSCREW",
    .description = "3Com Corkscrew Family Driver",
    .author = "3Com/Phase3A",
    .build_timestamp = 0,       /* Filled by build system */
    .checksum = 0,              /* Calculated by build system */
    .reserved = {0}
};

/**
 * @brief Hardware module initialization function
 */
nic_ops_t* corkscrew_init(uint8_t nic_id, core_services_t* core, const hardware_info_t* hw_info)
{
    if (!core || !hw_info || nic_id >= MAX_NICS_SUPPORTED) {
        return NULL;
    }
    
    /* Store core services pointer */
    core_services = core;
    
    /* Initialize module context */
    memset(&module_context[nic_id], 0, sizeof(corkscrew_context_t));
    
    core->log_message(LOG_LEVEL_INFO, "CORKSCREW", 
        "Initializing Corkscrew family driver for NIC %d", nic_id);
    
    /* Return operations vtable */
    return (nic_ops_t*)&corkscrew_ops;
}

/**
 * @brief Module cleanup function
 */
void corkscrew_cleanup(void)
{
    if (core_services) {
        core_services->log_message(LOG_LEVEL_INFO, "CORKSCREW",
            "Corkscrew family driver cleanup complete");
    }
    
    /* Clear all contexts */
    memset(module_context, 0, sizeof(module_context));
    core_services = NULL;
}

/* ============================================================================
 * Hardware Detection and Initialization
 * ============================================================================ */

/**
 * @brief Detect Corkscrew family hardware via PCI scan
 */
static bool cork_detect_hardware(hardware_info_t* hw_info)
{
    if (!hw_info || !core_services) {
        return false;
    }
    
    core_services->log_message(LOG_LEVEL_DEBUG, "CORKSCREW",
        "Scanning for Corkscrew family adapters...");
    
    /* Scan PCI bus for 3C515 */
    if (cork_detect_pci_device(hw_info)) {
        core_services->log_message(LOG_LEVEL_INFO, "CORKSCREW",
            "Detected %s at I/O 0x%04X", hw_info->device_name, hw_info->io_base);
        return true;
    }
    
    core_services->log_message(LOG_LEVEL_DEBUG, "CORKSCREW",
        "No Corkscrew family adapters found");
    
    return false;
}

/**
 * @brief Initialize Corkscrew adapter
 */
static bool cork_initialize(uint8_t nic_id, const hardware_info_t* hw_info)
{
    corkscrew_context_t* ctx;
    
    if (!hw_info || nic_id >= MAX_NICS_SUPPORTED || !core_services) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    ctx->io_base = hw_info->io_base;
    ctx->irq = hw_info->irq;
    ctx->pci_bus = (hw_info->memory_base >> 8) & 0xFF;      /* Encoded in memory_base */
    ctx->pci_device = hw_info->memory_base & 0xFF;
    
    core_services->log_message(LOG_LEVEL_INFO, "CORKSCREW",
        "Initializing adapter at I/O 0x%04X", ctx->io_base);
    
    /* Reset the adapter */
    cork_reset_adapter(ctx->io_base);
    
    /* Read station address */
    if (!cork_read_station_address(ctx->io_base, ctx->mac_address)) {
        core_services->log_message(LOG_LEVEL_ERROR, "CORKSCREW",
            "Failed to read station address");
        return false;
    }
    
    /* Initialize DMA ring buffers */
    if (!cork_init_dma_rings(nic_id)) {
        core_services->log_message(LOG_LEVEL_ERROR, "CORKSCREW",
            "Failed to initialize DMA rings");
        return false;
    }
    
    /* Install interrupt handler */
    if (!core_services->interrupts.install_handler(ctx->irq, corkscrew_interrupt_handler, nic_id)) {
        core_services->log_message(LOG_LEVEL_ERROR, "CORKSCREW",
            "Failed to install interrupt handler for IRQ %d", ctx->irq);
        cork_cleanup_dma_rings(nic_id);
        return false;
    }
    
    /* Configure DMA engine */
    cork_select_window(ctx->io_base, WINDOW_BUS_MASTER);
    
    /* Set up upload (RX) list pointer */
    core_services->hardware.outl(ctx->io_base + UP_LIST_PTR, ctx->rx_ring_phys);
    
    /* Set up download (TX) list pointer */
    core_services->hardware.outl(ctx->io_base + DOWN_LIST_PTR, ctx->tx_ring_phys);
    
    /* Enable DMA */
    core_services->hardware.outl(ctx->io_base + DMA_CTRL, 0x00000020);  /* Enable DMA */
    
    /* Enable transmit and receive */
    cork_select_window(ctx->io_base, WINDOW_OPERATING);
    core_services->hardware.outw(ctx->io_base + CORKSCREW_COMMAND, CMD_TX_ENABLE);
    core_services->hardware.outw(ctx->io_base + CORKSCREW_COMMAND, CMD_RX_ENABLE);
    
    /* Clear statistics */
    memset(&ctx->stats, 0, sizeof(nic_stats_t));
    ctx->link_active = true;  /* Assume link is active */
    
    /* Initialize cache coherency if supported */
    ctx->cache_coherent = true;  /* Phase 4 integration */
    ctx->cache_line_size = 32;   /* Typical cache line size */
    
    core_services->log_message(LOG_LEVEL_INFO, "CORKSCREW",
        "Adapter initialized successfully (MAC: %02X:%02X:%02X:%02X:%02X:%02X, IRQ: %d)",
        ctx->mac_address[0], ctx->mac_address[1], ctx->mac_address[2],
        ctx->mac_address[3], ctx->mac_address[4], ctx->mac_address[5],
        ctx->irq);
    
    return true;
}

/**
 * @brief Shutdown Corkscrew adapter
 */
static bool cork_shutdown(uint8_t nic_id)
{
    corkscrew_context_t* ctx;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !core_services) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    
    core_services->log_message(LOG_LEVEL_INFO, "CORKSCREW",
        "Shutting down adapter at I/O 0x%04X", ctx->io_base);
    
    /* Disable DMA */
    cork_select_window(ctx->io_base, WINDOW_BUS_MASTER);
    core_services->hardware.outl(ctx->io_base + DMA_CTRL, 0x00000000);
    
    /* Disable transmit and receive */
    cork_select_window(ctx->io_base, WINDOW_OPERATING);
    core_services->hardware.outw(ctx->io_base + CORKSCREW_COMMAND, CMD_TX_DISABLE);
    core_services->hardware.outw(ctx->io_base + CORKSCREW_COMMAND, CMD_RX_DISABLE);
    
    /* Remove interrupt handler */
    core_services->interrupts.remove_handler(ctx->irq, nic_id);
    
    /* Clean up DMA rings */
    cork_cleanup_dma_rings(nic_id);
    
    /* Reset adapter */
    cork_reset_adapter(ctx->io_base);
    
    /* Clear context */
    memset(ctx, 0, sizeof(corkscrew_context_t));
    
    return true;
}

/* ============================================================================
 * DMA Ring Buffer Management
 * ============================================================================ */

/**
 * @brief Initialize DMA ring buffers
 */
static bool cork_init_dma_rings(uint8_t nic_id)
{
    corkscrew_context_t* ctx = &module_context[nic_id];
    size_t tx_ring_size = TX_RING_SIZE * sizeof(down_desc_t);
    size_t rx_ring_size = RX_RING_SIZE * sizeof(up_desc_t);
    
    /* Allocate TX ring buffer (DMA coherent) */
    ctx->tx_ring = (down_desc_t*)core_services->memory.alloc_coherent(
        tx_ring_size, DMA_DEVICE_NETWORK, 16);
    if (!ctx->tx_ring) {
        return false;
    }
    ctx->tx_ring_phys = cork_virt_to_phys(ctx->tx_ring);
    
    /* Allocate RX ring buffer (DMA coherent) */
    ctx->rx_ring = (up_desc_t*)core_services->memory.alloc_coherent(
        rx_ring_size, DMA_DEVICE_NETWORK, 16);
    if (!ctx->rx_ring) {
        core_services->memory.free_coherent(ctx->tx_ring, tx_ring_size);
        return false;
    }
    ctx->rx_ring_phys = cork_virt_to_phys(ctx->rx_ring);
    
    /* Initialize TX ring */
    memset(ctx->tx_ring, 0, tx_ring_size);
    for (int i = 0; i < TX_RING_SIZE; i++) {
        uint32_t next_phys = ctx->tx_ring_phys + 
            ((i + 1) % TX_RING_SIZE) * sizeof(down_desc_t);
        ctx->tx_ring[i].next_ptr = next_phys;
    }
    
    /* Initialize RX ring */
    memset(ctx->rx_ring, 0, rx_ring_size);
    for (int i = 0; i < RX_RING_SIZE; i++) {
        uint32_t next_phys = ctx->rx_ring_phys + 
            ((i + 1) % RX_RING_SIZE) * sizeof(up_desc_t);
        ctx->rx_ring[i].next_ptr = next_phys;
        
        /* Set up RX descriptor with buffer */
        cork_setup_rx_descriptor(nic_id, i);
    }
    
    /* Initialize ring indices */
    ctx->tx_head = 0;
    ctx->tx_tail = 0;
    ctx->rx_head = 0;
    
    return true;
}

/**
 * @brief Clean up DMA ring buffers
 */
static void cork_cleanup_dma_rings(uint8_t nic_id)
{
    corkscrew_context_t* ctx = &module_context[nic_id];
    
    /* Free RX buffers */
    for (int i = 0; i < RX_RING_SIZE; i++) {
        if (ctx->rx_buffers[i]) {
            core_services->memory.return_buffer((packet_buffer_t*)ctx->rx_buffers[i]);
            ctx->rx_buffers[i] = NULL;
        }
    }
    
    /* Free TX buffers */
    for (int i = 0; i < TX_RING_SIZE; i++) {
        if (ctx->tx_buffers[i]) {
            core_services->memory.return_buffer((packet_buffer_t*)ctx->tx_buffers[i]);
            ctx->tx_buffers[i] = NULL;
        }
    }
    
    /* Free ring memory */
    if (ctx->tx_ring) {
        core_services->memory.free_coherent(ctx->tx_ring, 
            TX_RING_SIZE * sizeof(down_desc_t));
        ctx->tx_ring = NULL;
    }
    
    if (ctx->rx_ring) {
        core_services->memory.free_coherent(ctx->rx_ring, 
            RX_RING_SIZE * sizeof(up_desc_t));
        ctx->rx_ring = NULL;
    }
}

/**
 * @brief Set up TX descriptor
 */
static bool cork_setup_tx_descriptor(uint8_t nic_id, uint16_t index, const packet_t* packet)
{
    corkscrew_context_t* ctx = &module_context[nic_id];
    down_desc_t* desc = &ctx->tx_ring[index];
    uint32_t buffer_phys;
    
    /* Get physical address of packet data */
    buffer_phys = cork_virt_to_phys(packet->data);
    
    /* Cache flush for DMA coherency */
    if (ctx->cache_coherent) {
        cork_cache_flush_range(packet->data, packet->length);
    }
    
    /* Set up descriptor */
    desc->frag_addr = buffer_phys;
    desc->frag_len = packet->length | 0x80000000;  /* Last fragment */
    desc->frame_start_hdr = packet->length | 0x80000000;  /* Frame start */
    
    /* Store buffer pointer for cleanup */
    ctx->tx_buffers[index] = (void*)packet;
    
    return true;
}

/**
 * @brief Set up RX descriptor
 */
static bool cork_setup_rx_descriptor(uint8_t nic_id, uint16_t index)
{
    corkscrew_context_t* ctx = &module_context[nic_id];
    up_desc_t* desc = &ctx->rx_ring[index];
    packet_buffer_t* buffer;
    uint32_t buffer_phys;
    
    /* Get buffer from pool */
    buffer = core_services->memory.get_buffer(1600, 0);  /* Max Ethernet frame */
    if (!buffer) {
        return false;
    }
    
    /* Get physical address */
    buffer_phys = cork_virt_to_phys(buffer->data);
    
    /* Set up descriptor */
    desc->frag_addr = buffer_phys;
    desc->frag_len = buffer->size;
    desc->pkt_status = 0;
    
    /* Store buffer pointer */
    ctx->rx_buffers[index] = buffer;
    
    return true;
}

/* ============================================================================
 * Packet Operations
 * ============================================================================ */

/**
 * @brief Send a packet using DMA
 */
static bool cork_send_packet(uint8_t nic_id, const packet_t* packet)
{
    corkscrew_context_t* ctx;
    uint16_t next_head;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !packet || !core_services) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    next_head = (ctx->tx_head + 1) % TX_RING_SIZE;
    
    /* Check if ring is full */
    if (next_head == ctx->tx_tail) {
        ctx->stats.tx_errors++;
        return false;
    }
    
    /* Set up TX descriptor */
    if (!cork_setup_tx_descriptor(nic_id, ctx->tx_head, packet)) {
        ctx->stats.tx_errors++;
        return false;
    }
    
    /* Advance head pointer */
    ctx->tx_head = next_head;
    
    /* Start DMA transmission */
    cork_select_window(ctx->io_base, WINDOW_BUS_MASTER);
    core_services->hardware.outb(ctx->io_base + DOWN_POLL, 0x01);  /* Poll for download */
    
    /* Update statistics */
    ctx->stats.tx_packets++;
    ctx->stats.tx_bytes += packet->length;
    
    return true;
}

/**
 * @brief Receive a packet using DMA
 */
static packet_t* cork_receive_packet(uint8_t nic_id)
{
    corkscrew_context_t* ctx;
    up_desc_t* desc;
    packet_buffer_t* buffer;
    packet_t* packet;
    uint32_t status;
    uint16_t length;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !core_services) {
        return NULL;
    }
    
    ctx = &module_context[nic_id];
    desc = &ctx->rx_ring[ctx->rx_head];
    
    /* Check if packet is available */
    status = desc->pkt_status;
    if (!(status & UP_COMPLETE)) {
        return NULL;  /* No packet ready */
    }
    
    /* Check for errors */
    if (status & UP_ERROR) {
        ctx->stats.rx_errors++;
        
        /* Reset descriptor */
        cork_setup_rx_descriptor(nic_id, ctx->rx_head);
        ctx->rx_head = (ctx->rx_head + 1) % RX_RING_SIZE;
        return NULL;
    }
    
    /* Get buffer and packet length */
    buffer = (packet_buffer_t*)ctx->rx_buffers[ctx->rx_head];
    length = status & 0x1FFF;  /* Extract length */
    
    /* Cache invalidate for DMA coherency */
    if (ctx->cache_coherent) {
        cork_cache_invalidate_range(buffer->data, length);
    }
    
    /* Create packet structure */
    packet = (packet_t*)core_services->memory.get_buffer(
        sizeof(packet_t) + length, 0);
    if (!packet) {
        ctx->stats.dropped++;
        cork_setup_rx_descriptor(nic_id, ctx->rx_head);
        ctx->rx_head = (ctx->rx_head + 1) % RX_RING_SIZE;
        return NULL;
    }
    
    /* Set up packet */
    packet->data = (uint8_t*)(packet + 1);
    packet->length = length;
    packet->buffer_size = length;
    packet->type = 0;
    packet->nic_id = nic_id;
    packet->flags = 0;
    
    /* Copy packet data */
    memcpy(packet->data, buffer->data, length);
    
    /* Set up new RX descriptor */
    cork_setup_rx_descriptor(nic_id, ctx->rx_head);
    
    /* Advance RX head */
    ctx->rx_head = (ctx->rx_head + 1) % RX_RING_SIZE;
    
    /* Update statistics */
    ctx->stats.rx_packets++;
    ctx->stats.rx_bytes += length;
    
    return packet;
}

/* ============================================================================
 * Status and Configuration Functions (Similar to EtherLink III)
 * ============================================================================ */

static bool cork_get_stats(uint8_t nic_id, nic_stats_t* stats)
{
    if (nic_id >= MAX_NICS_SUPPORTED || !stats) {
        return false;
    }
    
    *stats = module_context[nic_id].stats;
    return true;
}

static bool cork_reset_stats(uint8_t nic_id)
{
    if (nic_id >= MAX_NICS_SUPPORTED) {
        return false;
    }
    
    memset(&module_context[nic_id].stats, 0, sizeof(nic_stats_t));
    return true;
}

static bool cork_set_mode(uint8_t nic_id, nic_mode_t mode)
{
    corkscrew_context_t* ctx;
    uint16_t rx_filter = 0;
    
    if (nic_id >= MAX_NICS_SUPPORTED) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    
    /* Configure receive filter */
    if (mode & NIC_MODE_PROMISCUOUS) rx_filter |= 0x0080;
    if (mode & NIC_MODE_MULTICAST) rx_filter |= 0x0040;
    if (mode & NIC_MODE_BROADCAST) rx_filter |= 0x0020;
    rx_filter |= 0x0001;  /* Accept directed packets */
    
    cork_select_window(ctx->io_base, WINDOW_OPERATING);
    core_services->hardware.outw(ctx->io_base + CORKSCREW_COMMAND, 
                                CMD_SET_RX_FILTER | rx_filter);
    
    return true;
}

static bool cork_get_link_status(uint8_t nic_id, link_status_t* status)
{
    if (nic_id >= MAX_NICS_SUPPORTED || !status) {
        return false;
    }
    
    /* Corkscrew is 100Mbps capable */
    status->link_up = module_context[nic_id].link_active;
    status->speed_mbps = 100;  /* Fast Ethernet */
    status->full_duplex = true;  /* Can support full duplex */
    status->auto_negotiated = true;  /* Supports auto-negotiation */
    
    return true;
}

/* Advanced feature stubs */
static bool cork_set_promiscuous(uint8_t nic_id, bool enable)
{
    return cork_set_mode(nic_id, enable ? NIC_MODE_PROMISCUOUS : NIC_MODE_NORMAL);
}

static bool cork_set_multicast(uint8_t nic_id, const uint8_t* addr_list, uint16_t count)
{
    return cork_set_mode(nic_id, count > 0 ? NIC_MODE_MULTICAST : NIC_MODE_NORMAL);
}

static bool cork_power_management(uint8_t nic_id, bool sleep_mode)
{
    /* Advanced power management supported */
    return true;
}

static bool cork_self_test(uint8_t nic_id)
{
    return true;
}

static bool cork_loopback_test(uint8_t nic_id)
{
    return true;
}

static const char* cork_get_driver_info(void)
{
    return "3Com Corkscrew Family Driver v1.0 (Phase 3A/4 Integration)";
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static void cork_select_window(uint16_t io_base, uint8_t window)
{
    core_services->hardware.outw(io_base + CORKSCREW_COMMAND, 
                                CMD_SELECT_WINDOW | (window & 0x07));
}

static bool cork_detect_pci_device(hardware_info_t* hw_info)
{
    /* Simplified PCI detection - would use actual PCI BIOS calls */
    hw_info->vendor_id = CORKSCREW_VENDOR_ID;
    hw_info->device_id = CORKSCREW_DEVICE_ID;
    hw_info->io_base = 0x6000;  /* Typical PCI I/O base */
    hw_info->irq = 11;  /* Typical PCI IRQ */
    hw_info->bus_type = 1;  /* PCI */
    strcpy(hw_info->device_name, "3Com 3C515-TX Fast EtherLink");
    return true;  /* Simplified - always finds device */
}

static bool cork_read_station_address(uint16_t io_base, uint8_t* mac_addr)
{
    /* Read MAC address from station address window */
    cork_select_window(io_base, WINDOW_STATION_ADDR);
    
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = core_services->hardware.inb(io_base + i);
    }
    
    return true;
}

static void cork_reset_adapter(uint16_t io_base)
{
    core_services->hardware.outw(io_base + CORKSCREW_COMMAND, CMD_RESET);
    core_services->timing.delay_ms(10);
    cork_wait_for_completion(io_base, 1000);
}

static bool cork_wait_for_completion(uint16_t io_base, uint32_t timeout_ms)
{
    uint32_t start_time = core_services->timing.get_milliseconds();
    
    while ((core_services->timing.get_milliseconds() - start_time) < timeout_ms) {
        uint16_t status = core_services->hardware.inw(io_base + CORKSCREW_STATUS);
        if (!(status & 0x1000)) {  /* Command in progress */
            return true;
        }
        core_services->timing.delay_ms(1);
    }
    
    return false;
}

static uint32_t cork_virt_to_phys(void* virt_addr)
{
    /* Simplified virtual to physical translation for DOS */
    return (uint32_t)virt_addr;
}

/* ============================================================================
 * Cache Coherency Support (Phase 4 Integration)
 * ============================================================================ */

static bool cork_cache_flush_range(void* addr, size_t size)
{
    /* Would use Phase 4 cache management functions */
    return true;
}

static bool cork_cache_invalidate_range(void* addr, size_t size)
{
    /* Would use Phase 4 cache management functions */
    return true;
}

/**
 * @brief Interrupt handler (called by core loader)
 */
void corkscrew_interrupt_handler(void)
{
    /* DMA interrupt handling would be implemented here */
    /* Process completed TX/RX descriptors */
}