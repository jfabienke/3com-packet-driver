/**
 * @file boomtex_internal.h
 * @brief BOOMTEX.MOD Internal Definitions and Structures
 * 
 * BOOMTEX.MOD Implementation for 3Com Packet Driver Modular Architecture
 * Team C (Agents 09-10) - Week 1 Critical Deliverable
 * 
 * Supports 3C900-TPO PCI and related Boomerang/Vortex/Cyclone/Tornado family NICs
 * Uses NE2000 compatibility layer for Week 1 emulator validation
 */

#ifndef BOOMTEX_INTERNAL_H
#define BOOMTEX_INTERNAL_H

#include "../../include/module_abi.h"
#include "../../include/memory_api.h"
#include "../../include/timing_measurement.h"
#include "../../../docs/agents/shared/error-codes.h"

/* BOOMTEX Module Constants */
#define BOOMTEX_MAX_NICS        4           /* Maximum NICs per module */
#define BOOMTEX_MAX_TX_RING     32          /* TX ring buffer size */
#define BOOMTEX_MAX_RX_RING     32          /* RX ring buffer size */
#define BOOMTEX_BUFFER_SIZE     1600        /* Maximum Ethernet frame size */

/* Hardware Types Supported by BOOMTEX - PCI/CardBus ONLY */
typedef enum {
    BOOMTEX_HARDWARE_UNKNOWN = 0,
    /* Vortex Family - 1st Generation PCI */
    BOOMTEX_HARDWARE_3C590_VORTEX,          /* 3C590 Vortex 10Mbps PCI */
    BOOMTEX_HARDWARE_3C595_VORTEX,          /* 3C595 Vortex 100Mbps PCI */
    
    /* Boomerang Family - Enhanced DMA */
    BOOMTEX_HARDWARE_3C900_BOOMERANG,       /* 3C900-TPO PCI 10Mbps */
    BOOMTEX_HARDWARE_3C905_BOOMERANG,       /* 3C905-TX PCI 100Mbps */
    
    /* Cyclone Family - Hardware Offload */
    BOOMTEX_HARDWARE_3C905B_CYCLONE,        /* 3C905B-TX PCI 100Mbps */
    
    /* Tornado Family - Advanced Features */
    BOOMTEX_HARDWARE_3C905C_TORNADO,        /* 3C905C-TX PCI 100Mbps */
    
    /* CardBus Variants - Hot Plug Support */
    BOOMTEX_HARDWARE_3C575_CARDBUS,         /* 3C575 CardBus 10/100 */
    BOOMTEX_HARDWARE_3C656_CARDBUS,         /* 3C656 CardBus 10/100 */
    
    /* Week 1 NE2000 compatibility - REMOVE after Week 1 */
    BOOMTEX_HARDWARE_NE2000_COMPAT          /* Week 1 NE2000 compatibility */
} boomtex_hardware_type_t;

/* Module States */
typedef enum {
    BOOMTEX_STATE_UNINITIALIZED = 0,
    BOOMTEX_STATE_INITIALIZING,
    BOOMTEX_STATE_ACTIVE,
    BOOMTEX_STATE_ERROR,
    BOOMTEX_STATE_UNLOADED
} boomtex_state_t;

/* Media Types and Speeds */
typedef enum {
    BOOMTEX_MEDIA_10BT = 1,                 /* 10BASE-T */
    BOOMTEX_MEDIA_100TX,                    /* 100BASE-TX */
    BOOMTEX_MEDIA_AUTO                      /* Auto-negotiation */
} boomtex_media_type_t;

typedef enum {
    BOOMTEX_DUPLEX_HALF = 0,
    BOOMTEX_DUPLEX_FULL,
    BOOMTEX_DUPLEX_AUTO
} boomtex_duplex_t;

/* Bus Master DMA Descriptor */
typedef struct {
    uint32_t next_pointer;                  /* Physical address of next descriptor */
    uint32_t frame_status;                  /* Status and flags */
    uint32_t fragment_pointer;              /* Physical buffer address */
    uint32_t fragment_length;               /* Buffer length and flags */
} __attribute__((packed)) boomtex_descriptor_t;

/* Descriptor Status Flags */
#define BOOMTEX_DESC_COMPLETE       0x80000000
#define BOOMTEX_DESC_ERROR          0x40000000
#define BOOMTEX_DESC_LAST_FRAG      0x80000000
#define BOOMTEX_DESC_IP_CHECKSUM    0x02000000
#define BOOMTEX_DESC_TCP_CHECKSUM   0x01000000

/* NIC Context Structure */
typedef struct {
    /* Hardware identification */
    boomtex_hardware_type_t hardware_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    
    /* I/O and interrupt configuration */
    uint32_t io_base;                       /* I/O base address */
    uint32_t mem_base;                      /* Memory-mapped base address */
    uint8_t irq;                           /* IRQ line */
    uint8_t pci_bus;                       /* PCI bus number */
    uint8_t pci_device;                    /* PCI device number */
    uint8_t pci_function;                  /* PCI function number */
    
    /* MAC address and configuration */
    uint8_t mac_address[6];
    boomtex_media_type_t media_type;
    boomtex_duplex_t duplex_mode;
    uint16_t link_speed;                   /* 10 or 100 Mbps */
    uint8_t link_status;                   /* Link up/down */
    
    /* Bus mastering and DMA */
    uint8_t bus_mastering_enabled;
    boomtex_descriptor_t *tx_ring;
    boomtex_descriptor_t *rx_ring;
    uint32_t tx_ring_phys;
    uint32_t rx_ring_phys;
    uint16_t tx_head, tx_tail;
    uint16_t rx_head, rx_tail;
    
    /* Buffer management */
    void *tx_buffers[BOOMTEX_MAX_TX_RING];
    void *rx_buffers[BOOMTEX_MAX_RX_RING];
    uint32_t tx_buffer_phys[BOOMTEX_MAX_TX_RING];
    uint32_t rx_buffer_phys[BOOMTEX_MAX_RX_RING];
    
    /* Statistics */
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t interrupts_handled;
    
    /* Performance metrics */
    uint32_t isr_timing_us;                /* Last ISR execution time */
    uint32_t cli_timing_us;                /* Last CLI section time */
    
} boomtex_nic_context_t;

/* Module Global Context */
typedef struct {
    /* Module state */
    uint16_t module_id;
    boomtex_state_t state;
    uint8_t hardware_initialized;
    uint8_t isr_registered;
    
    /* CPU optimization - DEPRECATED: Use global g_cpu_info instead */
    uint16_t cpu_type;          /* DEPRECATED: Use g_cpu_info.type */
    uint16_t cpu_features;      /* DEPRECATED: Use g_cpu_info.features */
    
    /* Hardware contexts (multi-NIC support) */
    uint8_t nic_count;
    boomtex_nic_context_t nics[BOOMTEX_MAX_NICS];
    
    /* Memory management */
    void *dma_pool;
    uint32_t dma_pool_size;
    
    /* Performance statistics */
    timing_stats_t isr_timing_stats;
    timing_stats_t cli_timing_stats;
    
} boomtex_context_t;

/* API Function Numbers */
#define BOOMTEX_API_DETECT_HARDWARE     0x01
#define BOOMTEX_API_INITIALIZE_NIC      0x02
#define BOOMTEX_API_SEND_PACKET         0x03
#define BOOMTEX_API_RECEIVE_PACKET      0x04
#define BOOMTEX_API_GET_STATISTICS      0x05
#define BOOMTEX_API_CONFIGURE           0x06
#define BOOMTEX_API_SET_MEDIA           0x07
#define BOOMTEX_API_GET_LINK_STATUS     0x08

/* API Parameter Structures */
typedef struct {
    uint8_t nic_index;                     /* NIC index (0-3) */
    uint16_t detected_hardware;            /* Hardware type detected */
    uint32_t io_base;                      /* I/O base address */
    uint8_t irq;                          /* IRQ line */
    uint8_t mac_address[6];               /* MAC address */
} __attribute__((packed)) boomtex_detect_params_t;

typedef struct {
    uint8_t nic_index;
    uint32_t io_base;
    uint8_t irq;
    boomtex_media_type_t media_type;
    boomtex_duplex_t duplex_mode;
    uint8_t enable_checksums;
    uint8_t enable_bus_mastering;
} __attribute__((packed)) boomtex_init_params_t;

typedef struct {
    uint8_t nic_index;
    void far *packet_data;
    uint16_t packet_length;
    uint16_t packet_type;                  /* Ethernet type */
} __attribute__((packed)) boomtex_send_params_t;

typedef struct {
    uint8_t nic_index;
    void far *buffer;
    uint16_t buffer_size;
    uint16_t bytes_received;
    uint16_t packet_type;
} __attribute__((packed)) boomtex_recv_params_t;

typedef struct {
    uint8_t nic_index;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t interrupts;
    uint32_t isr_avg_timing_us;
} __attribute__((packed)) boomtex_stats_params_t;

typedef struct {
    uint8_t nic_index;
    boomtex_media_type_t media_type;
    boomtex_duplex_t duplex_mode;
    uint8_t enable_checksums;
} __attribute__((packed)) boomtex_config_params_t;

/* Hardware Register Definitions */

/* 3C900/3C905 PCI Registers (Memory-Mapped) */
#define BOOMTEX_3C900_COMMAND           0x00
#define BOOMTEX_3C900_STATUS            0x02
#define BOOMTEX_3C900_INT_STATUS        0x04
#define BOOMTEX_3C900_FIFO_DIAG         0x08
#define BOOMTEX_3C900_TX_DESC_PTR       0x10
#define BOOMTEX_3C900_RX_DESC_PTR       0x14
#define BOOMTEX_3C900_MAC_ADDR          0x20

/* Command Register Bits */
#define BOOMTEX_CMD_TX_ENABLE           0x4000
#define BOOMTEX_CMD_RX_ENABLE           0x2000
#define BOOMTEX_CMD_TX_RESET            0x1000
#define BOOMTEX_CMD_RX_RESET            0x0800
#define BOOMTEX_CMD_SET_RX_FILTER       0x0400
#define BOOMTEX_CMD_ACK_INTR            0x6800

/* Status Register Bits */
#define BOOMTEX_STAT_TX_COMPLETE        0x0004
#define BOOMTEX_STAT_RX_COMPLETE        0x0010
#define BOOMTEX_STAT_INT_LATCH          0x0001

/* Function Prototypes */

/* Hardware detection and initialization */
int boomtex_detect_pci_family(void);
int boomtex_detect_3c900tpo(void);
int boomtex_detect_ne2000(void);
int boomtex_init_pci_nic(boomtex_nic_context_t *nic);
int boomtex_init_3c900tpo(boomtex_nic_context_t *nic);
int boomtex_init_ne2000_compat(boomtex_nic_context_t *nic);

/* Auto-negotiation and media control */
int boomtex_autonegotiate(boomtex_nic_context_t *nic);
int boomtex_set_media(boomtex_nic_context_t *nic, boomtex_media_type_t media, boomtex_duplex_t duplex);
int boomtex_get_link_status(boomtex_nic_context_t *nic);

/* Bus mastering and DMA */
int boomtex_setup_bus_mastering(boomtex_nic_context_t *nic);
int boomtex_setup_dma_rings(boomtex_nic_context_t *nic);
int boomtex_cleanup_dma_resources(boomtex_nic_context_t *nic);

/* Packet operations */
int boomtex_transmit_packet(boomtex_nic_context_t *nic, void *data, uint16_t length);
int boomtex_receive_packet(boomtex_nic_context_t *nic, void *buffer, uint16_t buffer_size);
int boomtex_process_rx_ring(boomtex_nic_context_t *nic);
int boomtex_cleanup_tx_ring(boomtex_nic_context_t *nic);

/* ISR and interrupt handling */
void far boomtex_isr_asm_entry(void);
void boomtex_handle_interrupt(boomtex_nic_context_t *nic);

/* CPU optimization and self-modifying code */
void boomtex_apply_cpu_optimizations(void);
void boomtex_patch_286_optimizations(void);
void boomtex_patch_386_optimizations(void);
void boomtex_patch_486_optimizations(void);
void boomtex_patch_pentium_optimizations(void);
void flush_prefetch_queue(void);

/* Memory management */
int boomtex_init_memory_pools(void);
void *boomtex_alloc_dma_buffer(uint32_t size, uint32_t *phys_addr);
void boomtex_free_dma_buffer(void *buffer);
void boomtex_free_allocated_memory(void);

/* API implementations */
int boomtex_api_detect_hardware(boomtex_detect_params_t far *params);
int boomtex_api_initialize_nic(boomtex_init_params_t far *params);
int boomtex_api_send_packet(boomtex_send_params_t far *params);
int boomtex_api_receive_packet(boomtex_recv_params_t far *params);
int boomtex_api_get_statistics(boomtex_stats_params_t far *params);
int boomtex_api_configure(boomtex_config_params_t far *params);

/* Hardware cleanup */
int boomtex_cleanup_hardware(void);
int boomtex_disable_interrupts(void);

/* Week 1 compatibility */
#ifdef WEEK1_EMULATOR_TESTING
int boomtex_init_ne2000_compat(void);
int boomtex_ne2000_read_mac_address(uint8_t *mac);
int boomtex_ne2000_init_hardware(void *config);
#endif

/* Utility functions */
static inline uint16_t boomtex_read_reg16(uint32_t base, uint16_t offset) {
    return *(volatile uint16_t*)(base + offset);
}

static inline void boomtex_write_reg16(uint32_t base, uint16_t offset, uint16_t value) {
    *(volatile uint16_t*)(base + offset) = value;
}

static inline uint32_t boomtex_read_reg32(uint32_t base, uint16_t offset) {
    return *(volatile uint32_t*)(base + offset);
}

static inline void boomtex_write_reg32(uint32_t base, uint16_t offset, uint32_t value) {
    *(volatile uint32_t*)(base + offset) = value;
}

/* Logging macros */
#define LOG_INFO(fmt, ...)      /* Implementation depends on logging system */
#define LOG_WARNING(fmt, ...)   /* Implementation depends on logging system */  
#define LOG_ERROR(fmt, ...)     /* Implementation depends on logging system */
#define LOG_DEBUG(fmt, ...)     /* Implementation depends on logging system */

#endif /* BOOMTEX_INTERNAL_H */