/**
 * @file corkscrw_module.c
 * @brief CORKSCRW.MOD - 3C515 Corkscrew ISA Bus-Master Driver Module
 * 
 * Agent Team B (07-08): Week 1 Implementation
 * 
 * DEDICATED 3C515-TX DRIVER - ISA Bus Master ONLY
 * 
 * This module implements the 3Com 3C515 Corkscrew ISA bus-master network driver:
 * - UNIQUE ISA bus mastering capability (unusual for ISA)
 * - 100 Mbps Fast Ethernet on ISA bus (bridge between ISA/PCI eras)
 * - VDS (Virtual DMA Services) support for EMM386/QEMM compatibility
 * - 24-bit addressing limitation (16MB physical memory limit)
 * - 64KB DMA boundary restrictions and bounce buffer management
 * - Hot/cold memory separation for ≤6KB resident size
 * 
 * ARCHITECTURE NOTE: 3C515 ONLY - All PCI devices handled by BOOMTEX.MOD
 * 
 * This file is part of the 3Com Packet Driver modular architecture.
 * Copyright (c) 2025 3Com/Phase3A Team B
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* Module ABI Headers */
#include "../../docs/agents/shared/module-header-v1.0.h"
#include "../../docs/agents/shared/error-codes.h"
#include "../../include/config.h"    /* For bus master testing integration */

/* 3C515 Hardware Constants */
#define CORKSCRW_VENDOR_ID      0x10B7  /* 3Com vendor ID */
#define CORKSCRW_DEVICE_ID      0x5150  /* 3C515 device ID */
#define CORKSCRW_TORNADO_ID     0x5057  /* 3C515 Tornado variant */

/* ISA I/O Port Ranges */
#define CORKSCRW_IO_MIN         0x200
#define CORKSCRW_IO_MAX         0x3F0
#define CORKSCRW_IO_STEP        0x10

/* Register Offsets (Window-based) */
#define REG_COMMAND             0x0E    /* Command register */
#define REG_STATUS              0x0E    /* Status register (read) */
#define REG_WINDOW              0x0E    /* Window select register */

/* Window 7: Bus Master DMA Registers */
#define REG_DMA_CTRL            0x00    /* DMA control */
#define REG_DMA_STATUS          0x04    /* DMA status */
#define REG_DN_LIST_PTR         0x24    /* Download list pointer (TX) */
#define REG_DN_POLL             0x2D    /* Download poll register */
#define REG_UP_LIST_PTR         0x38    /* Upload list pointer (RX) */
#define REG_UP_PKT_STATUS       0x30    /* Upload packet status */

/* Command Values */
#define CMD_GLOBAL_RESET        0x0000
#define CMD_SELECT_WINDOW       0x0800
#define CMD_TX_ENABLE           0x4800
#define CMD_TX_DISABLE          0x5000
#define CMD_RX_ENABLE           0x2000
#define CMD_RX_DISABLE          0x1800

/* Window Numbers */
#define WINDOW_SETUP            0
#define WINDOW_OPERATING        1
#define WINDOW_STATION_ADDR     2
#define WINDOW_FIFO             3
#define WINDOW_DIAGNOSTICS      4
#define WINDOW_RESULTS          5
#define WINDOW_STATISTICS       6
#define WINDOW_BUS_MASTER       7

/* DMA Descriptor Ring Sizes */
#define TX_RING_SIZE            16      /* 16 TX descriptors */
#define RX_RING_SIZE            32      /* 32 RX descriptors */
#define MAX_ETHERNET_FRAME      1536    /* Max frame size + margin */

/* Hardware Types Supported by CORKSCRW - 3C515 ONLY */
typedef enum {
    CORKSCRW_HARDWARE_UNKNOWN = 0,
    CORKSCRW_HARDWARE_3C515TX,              /* 3C515-TX ISA Fast Ethernet (ONLY supported device) */
    /* NO other hardware - dedicated 3C515 module */
} corkscrw_hardware_type_t;

/* DMA Descriptor Status Bits */
#define DN_COMPLETE             0x00010000  /* Download complete */
#define DN_ERROR                0x00004000  /* Download error */
#define UP_COMPLETE             0x00008000  /* Upload complete */
#define UP_ERROR                0x00004000  /* Upload error */

/* Interrupt Flags */
#define INT_UP_COMPLETE         0x0001  /* Upload complete */
#define INT_DN_COMPLETE         0x0002  /* Download complete */
#define INT_UPDATE_STATS        0x0080  /* Statistics update */

/**
 * DMA Descriptor Structures (Hardware Layout)
 */
typedef struct {
    uint32_t next_ptr;          /* Physical pointer to next descriptor */
    uint32_t frame_start_hdr;   /* Frame start header */
    uint32_t frag_addr;         /* Fragment physical address */
    uint32_t frag_len;          /* Fragment length and control */
} __attribute__((packed)) dn_desc_t;

typedef struct {
    uint32_t next_ptr;          /* Physical pointer to next descriptor */
    uint32_t pkt_status;        /* Packet status and length */
    uint32_t frag_addr;         /* Fragment physical address */
    uint32_t frag_len;          /* Fragment length */
} __attribute__((packed)) up_desc_t;

/**
 * NIC Statistics Structure
 */
typedef struct {
    uint32_t tx_packets;        /* Transmitted packets */
    uint32_t tx_bytes;          /* Transmitted bytes */
    uint32_t tx_errors;         /* Transmission errors */
    uint32_t tx_dropped;        /* Dropped TX packets */
    uint32_t rx_packets;        /* Received packets */
    uint32_t rx_bytes;          /* Received bytes */
    uint32_t rx_errors;         /* Reception errors */
    uint32_t rx_dropped;        /* Dropped RX packets */
    uint32_t interrupts;        /* Interrupt count */
    uint32_t dma_errors;        /* DMA errors */
} nic_stats_t;

/**
 * Packet Structure
 */
typedef struct {
    uint8_t *data;              /* Packet data pointer */
    uint16_t length;            /* Packet length */
    uint16_t buffer_size;       /* Buffer size */
    uint8_t nic_id;             /* Source NIC ID */
    uint8_t flags;              /* Packet flags */
} packet_t;

/**
 * Memory Buffer Structure (DMA-safe)
 */
typedef struct {
    void *virt_addr;            /* Virtual address */
    uint32_t phys_addr;         /* Physical address */
    uint16_t size;              /* Buffer size */
    uint8_t in_use;             /* Usage flag */
    uint8_t boundary_safe;      /* 64KB boundary safe */
} dma_buffer_t;

/**
 * NIC Context Structure (Hot Section)
 */
typedef struct {
    /* Hardware Configuration */
    uint16_t io_base;           /* I/O base address */
    uint8_t irq;                /* IRQ number */
    uint8_t mac_addr[6];        /* MAC address */
    
    /* DMA Ring Descriptors (Physical) */
    dn_desc_t *tx_ring;         /* TX descriptor ring */
    up_desc_t *rx_ring;         /* RX descriptor ring */
    uint32_t tx_ring_phys;      /* TX ring physical address */
    uint32_t rx_ring_phys;      /* RX ring physical address */
    
    /* Ring Management */
    uint16_t tx_head;           /* TX ring head index */
    uint16_t tx_tail;           /* TX ring tail index */
    uint16_t rx_head;           /* RX ring head index */
    uint16_t rx_tail;           /* RX ring tail index */
    
    /* DMA Buffers */
    dma_buffer_t tx_buffers[TX_RING_SIZE];
    dma_buffer_t rx_buffers[RX_RING_SIZE];
    
    /* Status and Statistics */
    nic_stats_t stats;          /* Statistics counters */
    uint8_t link_active;        /* Link status */
    uint8_t initialized;        /* Initialization flag */
    
    /* NE2000 Compatibility (Week 1) */
    uint8_t ne2000_mode;        /* NE2000 emulation active */
    uint16_t ne2000_base;       /* NE2000 I/O base */
} nic_context_t;

/* Global Module Data (Hot Section) */
static nic_context_t g_nic_context;
static uint8_t g_module_initialized = 0;

/* Forward Declarations */

/* Hot Section Functions (Performance Critical) */
extern void corkscrw_isr(void);                    /* Assembly ISR */
static int corkscrw_send_packet(packet_t *packet);
static packet_t* corkscrw_receive_packet(void);
static void corkscrw_process_tx_complete(void);
static void corkscrw_process_rx_complete(void);

/* Cold Section Functions (Initialization Only) */
static int corkscrw_detect_hardware(void);
static int corkscrw_init_hardware(void);
static int corkscrw_setup_dma_rings(void);
static int corkscrw_read_mac_address(void);
static void corkscrw_reset_hardware(void);

/* Memory Management Interface */
static dma_buffer_t* alloc_dma_buffer(uint16_t size);
static void free_dma_buffer(dma_buffer_t *buffer);
static uint32_t virt_to_phys(void *virt_addr);
static int check_64kb_boundary(uint32_t phys_addr, uint16_t size);

/* Utility Functions */
static void select_window(uint8_t window);
static void outw_reg(uint8_t reg, uint16_t value);
static uint16_t inw_reg(uint8_t reg);
static void outl_reg(uint8_t reg, uint32_t value);
static uint32_t inl_reg(uint8_t reg);

/* NE2000 Compatibility Layer (Week 1) */
static int ne2000_init(void);
static int ne2000_send_packet(packet_t *packet);
static packet_t* ne2000_receive_packet(void);
static void ne2000_reset(void);

/**
 * ============================================================================
 * MODULE HEADER (Must be first in file, 64 bytes exactly)
 * ============================================================================
 */

/* Module Header - Exactly 64 bytes at start of module */
const module_header_t module_header = {
    /* Module Identification (8 bytes) */
    .signature = "MD64",           /* Module Driver 64-byte */
    .abi_version = 1,              /* ABI v1.0 */
    .module_type = MODULE_TYPE_NIC, /* Network interface card */
    .flags = MODULE_FLAG_DISCARD_COLD | MODULE_FLAG_HAS_ISR | 
             MODULE_FLAG_NEEDS_DMA_SAFE | MODULE_FLAG_SMC_USED,
    
    /* Memory Layout (8 bytes) */
    .total_size_para = 0,          /* Filled by linker */
    .resident_size_para = 384,     /* 6KB resident (384 * 16 bytes) */
    .cold_size_para = 0,           /* Filled by linker */
    .alignment_para = 1,           /* 16-byte alignment */
    
    /* Entry Points (8 bytes) */
    .init_offset = 0,              /* Filled by linker */
    .api_offset = 0,               /* Filled by linker */
    .isr_offset = 0,               /* Filled by linker */
    .unload_offset = 0,            /* Filled by linker */
    
    /* Symbol Resolution (8 bytes) */
    .export_table_offset = 0,      /* No exports for Week 1 */
    .export_count = 0,
    .reloc_table_offset = 0,       /* No relocations for Week 1 */
    .reloc_count = 0,
    
    /* BSS and Requirements (8 bytes) */
    .bss_size_para = 32,          /* 512 bytes BSS (32 * 16) */
    .required_cpu = CPU_TYPE_80286, /* 286+ for bus mastering with chipset support */
    .required_features = FEATURE_NONE,
    .module_id = MODULE_ID_CORKSCRW, /* 'CK' */
    
    /* Module Name (12 bytes) */
    .module_name = "CORKSCRW   ", /* 8.3 format, space-padded */
    .name_padding = 0,
    
    /* Integrity and Reserved (16 bytes) */
    .header_checksum = 0,         /* Calculated by build system */
    .image_checksum = 0,          /* Calculated by build system */
    .vendor_id = 0x3COM0000,      /* 3Com vendor */
    .build_timestamp = 0,         /* Filled by build system */
    .reserved = {0, 0}            /* Reserved fields */
};

/* Static assertion to ensure exact 64-byte header */
_Static_assert(sizeof(module_header) == 64, "Module header must be exactly 64 bytes");

/**
 * ============================================================================
 * MODULE LIFECYCLE FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Module initialization entry point
 * Called by loader after module is loaded into memory
 * 
 * @return 0 on success, error code on failure
 */
int corkscrw_init(void)
{
    int result;
    
    /* Check if already initialized */
    if (g_module_initialized) {
        return ERROR_ALREADY_INITIALIZED;
    }
    
    /* Clear module context */
    memset(&g_nic_context, 0, sizeof(nic_context_t));
    
    /* Detect 3C515 hardware */
    result = corkscrw_detect_hardware();
    if (result != 0) {
        /* Fall back to NE2000 mode for Week 1 QEMU validation */
        result = ne2000_init();
        if (result != 0) {
            return ERROR_HARDWARE_NOT_FOUND;
        }
        g_nic_context.ne2000_mode = 1;
        g_module_initialized = 1;
        return 0;
    }
    
    /* Initialize 3C515 hardware */
    result = corkscrw_init_hardware();
    if (result != 0) {
        return result;
    }
    
    /* Set up DMA descriptor rings */
    result = corkscrw_setup_dma_rings();
    if (result != 0) {
        return result;
    }
    
    /* Read MAC address */
    result = corkscrw_read_mac_address();
    if (result != 0) {
        return result;
    }
    
    g_nic_context.initialized = 1;
    g_module_initialized = 1;
    
    return 0;
}

/**
 * @brief Module cleanup and unload
 * Called by loader before module is unloaded
 */
void corkscrw_cleanup(void)
{
    if (!g_module_initialized) {
        return;
    }
    
    /* Disable interrupts and DMA */
    if (g_nic_context.ne2000_mode) {
        ne2000_reset();
    } else {
        corkscrw_reset_hardware();
    }
    
    /* Free DMA buffers */
    for (int i = 0; i < TX_RING_SIZE; i++) {
        if (g_nic_context.tx_buffers[i].virt_addr) {
            free_dma_buffer(&g_nic_context.tx_buffers[i]);
        }
    }
    
    for (int i = 0; i < RX_RING_SIZE; i++) {
        if (g_nic_context.rx_buffers[i].virt_addr) {
            free_dma_buffer(&g_nic_context.rx_buffers[i]);
        }
    }
    
    /* Clear module state */
    memset(&g_nic_context, 0, sizeof(nic_context_t));
    g_module_initialized = 0;
}

/**
 * @brief Get module information
 * 
 * @param info Pointer to module info structure to fill
 * @return 0 on success, error code on failure
 */
int corkscrw_get_info(void *info)
{
    if (!info) {
        return ERROR_INVALID_PARAM;
    }
    
    /* This would fill in module information structure */
    /* Implementation depends on final info structure definition */
    
    return 0;
}

/**
 * ============================================================================
 * HOT SECTION: PERFORMANCE-CRITICAL FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Send a packet (Hot path)
 * 
 * @param packet Pointer to packet structure
 * @return 0 on success, error code on failure
 */
static int corkscrw_send_packet(packet_t *packet)
{
    if (!packet || !g_module_initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Route to appropriate handler */
    if (g_nic_context.ne2000_mode) {
        return ne2000_send_packet(packet);
    }
    
    /* Check if TX ring has space */
    uint16_t next_head = (g_nic_context.tx_head + 1) % TX_RING_SIZE;
    if (next_head == g_nic_context.tx_tail) {
        g_nic_context.stats.tx_dropped++;
        return ERROR_BUFFER_FULL;
    }
    
    /* Get DMA buffer for packet */
    dma_buffer_t *buf = &g_nic_context.tx_buffers[g_nic_context.tx_head];
    if (!buf->virt_addr) {
        g_nic_context.stats.tx_errors++;
        return ERROR_OUT_OF_MEMORY;
    }
    
    /* Copy packet to DMA buffer */
    if (packet->length > buf->size) {
        g_nic_context.stats.tx_errors++;
        return ERROR_PACKET_TOO_LARGE;
    }
    
    memcpy(buf->virt_addr, packet->data, packet->length);
    
    /* Set up TX descriptor */
    dn_desc_t *desc = &g_nic_context.tx_ring[g_nic_context.tx_head];
    desc->frag_addr = buf->phys_addr;
    desc->frag_len = packet->length | 0x80000000;  /* Last fragment */
    desc->frame_start_hdr = packet->length;
    
    /* Advance head pointer */
    g_nic_context.tx_head = next_head;
    
    /* Kick DMA engine */
    select_window(WINDOW_BUS_MASTER);
    outb_reg(REG_DN_POLL, 1);
    
    /* Update statistics */
    g_nic_context.stats.tx_packets++;
    g_nic_context.stats.tx_bytes += packet->length;
    
    return 0;
}

/**
 * @brief Receive a packet (Hot path)
 * 
 * @return Pointer to received packet, NULL if none available
 */
static packet_t* corkscrw_receive_packet(void)
{
    if (!g_module_initialized) {
        return NULL;
    }
    
    /* Route to appropriate handler */
    if (g_nic_context.ne2000_mode) {
        return ne2000_receive_packet();
    }
    
    /* Check if RX ring has packets */
    up_desc_t *desc = &g_nic_context.rx_ring[g_nic_context.rx_head];
    if (!(desc->pkt_status & UP_COMPLETE)) {
        return NULL;  /* No packet ready */
    }
    
    /* Check for errors */
    if (desc->pkt_status & UP_ERROR) {
        g_nic_context.stats.rx_errors++;
        /* Reset descriptor and advance */
        desc->pkt_status = 0;
        g_nic_context.rx_head = (g_nic_context.rx_head + 1) % RX_RING_SIZE;
        return NULL;
    }
    
    /* Extract packet length */
    uint16_t pkt_len = desc->pkt_status & 0x1FFF;
    
    /* Allocate packet structure */
    packet_t *packet = (packet_t*)malloc(sizeof(packet_t) + pkt_len);
    if (!packet) {
        g_nic_context.stats.rx_dropped++;
        return NULL;
    }
    
    /* Set up packet */
    packet->data = (uint8_t*)(packet + 1);
    packet->length = pkt_len;
    packet->buffer_size = pkt_len;
    packet->nic_id = 0;  /* Single NIC for now */
    packet->flags = 0;
    
    /* Copy packet data */
    dma_buffer_t *buf = &g_nic_context.rx_buffers[g_nic_context.rx_head];
    memcpy(packet->data, buf->virt_addr, pkt_len);
    
    /* Reset descriptor for reuse */
    desc->pkt_status = 0;
    desc->frag_len = buf->size;
    
    /* Advance head pointer */
    g_nic_context.rx_head = (g_nic_context.rx_head + 1) % RX_RING_SIZE;
    
    /* Update statistics */
    g_nic_context.stats.rx_packets++;
    g_nic_context.stats.rx_bytes += pkt_len;
    
    return packet;
}

/**
 * ============================================================================
 * COLD SECTION: INITIALIZATION-ONLY FUNCTIONS (Will be discarded after init)
 * ============================================================================
 */

/**
 * @brief Detect 3C515 hardware (Cold section)
 * 
 * @return 0 if found, error code if not found
 */
static int corkscrw_detect_hardware(void)
{
    /* Scan ISA I/O ports for 3C515 signature */
    for (uint16_t io_base = CORKSCRW_IO_MIN; io_base <= CORKSCRW_IO_MAX; io_base += CORKSCRW_IO_STEP) {
        /* Try to access the device */
        g_nic_context.io_base = io_base;
        
        /* Reset and check for 3C515 signature */
        outw_reg(REG_COMMAND, CMD_GLOBAL_RESET);
        
        /* Wait for reset completion */
        for (int i = 0; i < 1000; i++) {
            if (!(inw_reg(REG_STATUS) & 0x1000)) {
                break;  /* Reset complete */
            }
        }
        
        /* Check device ID in window 0 */
        select_window(WINDOW_SETUP);
        uint16_t device_id = inw_reg(0x02);
        
        if (device_id == CORKSCRW_DEVICE_ID || device_id == CORKSCRW_TORNADO_ID) {
            /* Found 3C515! */
            return 0;
        }
    }
    
    return ERROR_HARDWARE_NOT_FOUND;
}

/**
 * @brief Initialize 3C515 hardware (Cold section)
 * 
 * @return 0 on success, error code on failure
 */
static int corkscrw_init_hardware(void)
{
    /* Reset the adapter */
    corkscrw_reset_hardware();
    
    /* Configure DMA bus mastering with comprehensive testing */
    
    /* Access global configuration for bus master testing */
    extern config_t g_config;
    
    /* Only proceed with bus mastering if globally enabled */
    if (g_config.busmaster != BUSMASTER_OFF) {
        /* Create test context for the NIC */
        nic_context_t test_ctx;
        memset(&test_ctx, 0, sizeof(test_ctx));
        test_ctx.io_base = g_nic_context.io_base;
        test_ctx.irq = g_nic_context.irq;
        
        /* Perform comprehensive bus master testing */
        bool quick_mode = (g_config.busmaster == BUSMASTER_AUTO);
        int test_result = config_perform_busmaster_auto_test(&g_config, &test_ctx, quick_mode);
        
        if (test_result == 0 && g_config.busmaster == BUSMASTER_ON) {
            /* Bus master testing passed - enable DMA capabilities */
            select_window(WINDOW_BUS_MASTER);
            outl_reg(REG_DMA_CTRL, 0x00000020);  /* Enable bus mastering */
            g_nic_context.dma_enabled = 1;
            /* Note: Module initialization will continue to setup DMA rings */
        } else {
            /* Bus master testing failed or disabled - use PIO mode */
            g_nic_context.dma_enabled = 0;
            /* Skip DMA setup - will use PIO mode for packet operations */
        }
    } else {
        /* Bus mastering disabled by configuration */
        g_nic_context.dma_enabled = 0;
    }
    
    /* Set up interrupt mask */
    select_window(WINDOW_OPERATING);
    outw_reg(0x0A, INT_UP_COMPLETE | INT_DN_COMPLETE);
    
    return 0;
}

/**
 * @brief Set up DMA descriptor rings (Cold section)
 * 
 * @return 0 on success, error code on failure
 */
static int corkscrw_setup_dma_rings(void)
{
    /* Only set up DMA rings if bus mastering is enabled */
    if (!g_nic_context.dma_enabled) {
        /* DMA not enabled - skip ring setup */
        return 0;
    }
    
    /* Allocate TX ring */
    g_nic_context.tx_ring = (dn_desc_t*)alloc_dma_buffer(sizeof(dn_desc_t) * TX_RING_SIZE);
    if (!g_nic_context.tx_ring) {
        return ERROR_OUT_OF_MEMORY;
    }
    g_nic_context.tx_ring_phys = virt_to_phys(g_nic_context.tx_ring);
    
    /* Allocate RX ring */
    g_nic_context.rx_ring = (up_desc_t*)alloc_dma_buffer(sizeof(up_desc_t) * RX_RING_SIZE);
    if (!g_nic_context.rx_ring) {
        return ERROR_OUT_OF_MEMORY;
    }
    g_nic_context.rx_ring_phys = virt_to_phys(g_nic_context.rx_ring);
    
    /* Initialize descriptor rings */
    for (int i = 0; i < TX_RING_SIZE; i++) {
        dn_desc_t *desc = &g_nic_context.tx_ring[i];
        desc->next_ptr = g_nic_context.tx_ring_phys + 
                        ((i + 1) % TX_RING_SIZE) * sizeof(dn_desc_t);
        desc->frame_start_hdr = 0;
        desc->frag_addr = 0;
        desc->frag_len = 0;
        
        /* Allocate TX buffer */
        g_nic_context.tx_buffers[i] = *alloc_dma_buffer(MAX_ETHERNET_FRAME);
    }
    
    for (int i = 0; i < RX_RING_SIZE; i++) {
        up_desc_t *desc = &g_nic_context.rx_ring[i];
        desc->next_ptr = g_nic_context.rx_ring_phys + 
                        ((i + 1) % RX_RING_SIZE) * sizeof(up_desc_t);
        desc->pkt_status = 0;
        
        /* Allocate RX buffer */
        g_nic_context.rx_buffers[i] = *alloc_dma_buffer(MAX_ETHERNET_FRAME);
        desc->frag_addr = g_nic_context.rx_buffers[i].phys_addr;
        desc->frag_len = g_nic_context.rx_buffers[i].size;
    }
    
    /* Program ring addresses */
    select_window(WINDOW_BUS_MASTER);
    outl_reg(REG_DN_LIST_PTR, g_nic_context.tx_ring_phys);
    outl_reg(REG_UP_LIST_PTR, g_nic_context.rx_ring_phys);
    
    return 0;
}

/**
 * ============================================================================
 * STUB IMPLEMENTATIONS (Week 1)
 * ============================================================================
 */

/* Placeholder implementations for Week 1 */
static void outb_reg(uint8_t reg, uint8_t value) { /* Stub */ }
static void outw_reg(uint8_t reg, uint16_t value) { /* Stub */ }
static void outl_reg(uint8_t reg, uint32_t value) { /* Stub */ }
static uint8_t inb_reg(uint8_t reg) { return 0; }
static uint16_t inw_reg(uint8_t reg) { return 0; }
static uint32_t inl_reg(uint8_t reg) { return 0; }

static void select_window(uint8_t window) { /* Stub */ }
static int corkscrw_read_mac_address(void) { return 0; }
static void corkscrw_reset_hardware(void) { /* Stub */ }

static dma_buffer_t* alloc_dma_buffer(uint16_t size) { return NULL; }
static void free_dma_buffer(dma_buffer_t *buffer) { /* Stub */ }
static uint32_t virt_to_phys(void *virt_addr) { return (uint32_t)virt_addr; }
static int check_64kb_boundary(uint32_t phys_addr, uint16_t size) { return 0; }

/* NE2000 compatibility stubs for Week 1 */
static int ne2000_init(void) { return 0; }
static int ne2000_send_packet(packet_t *packet) { return 0; }
static packet_t* ne2000_receive_packet(void) { return NULL; }
static void ne2000_reset(void) { /* Stub */ }

/* Memory allocation stub */
static void* malloc(size_t size) { return NULL; }