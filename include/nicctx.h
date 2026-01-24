/**
 * @file nic_context.h
 * @brief NIC context structures and EEPROM configuration definitions
 *
 * Groups 6A & 6B - C Interface Architecture
 * Defines packed structures for DOS compatibility including nic_context_t,
 * eeprom_config_t, and related data structures for hardware abstraction.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _NIC_CONTEXT_H_
#define _NIC_CONTEXT_H_

/* Guard for nic_context_t to prevent redefinition conflicts */
#define NIC_CONTEXT_T_DEFINED 1

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "errhndl.h"
#include "nic_defs.h"    /* Canonical nic_type_t definition */
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct hardware_hal_vtable;
typedef struct hardware_hal_vtable hardware_hal_vtable_t;

/* Pack structures for DOS compatibility */
#pragma pack(push, 1)

/* NIC type compatibility - use canonical enum from nic_defs.h */
/* NIC_TYPE_3C515TX maps to NIC_TYPE_3C515_TX from nic_defs.h */
#ifndef NIC_TYPE_3C515TX
#define NIC_TYPE_3C515TX NIC_TYPE_3C515_TX
#endif

/* NIC operational states */
typedef enum {
    NIC_STATE_UNINITIALIZED = 0,
    NIC_STATE_DETECTED      = 1,
    NIC_STATE_INITIALIZED   = 2,
    NIC_STATE_ACTIVE        = 3,
    NIC_STATE_SUSPENDED     = 4,
    NIC_STATE_ERROR         = 5
} nic_state_t;

/* DMA descriptor structure for 3C515-TX */
typedef struct dma_descriptor {
    uint32_t next_ptr;          /* Physical address of next descriptor */
    uint32_t status;            /* Status and control bits */
    uint32_t buffer_addr;       /* Physical buffer address */
    uint16_t buffer_length;     /* Buffer length */
    uint16_t packet_length;     /* Packet length (RX only) */
} dma_descriptor_t;

/* Ring buffer management */
typedef struct ring_buffer {
    dma_descriptor_t *descriptors;  /* Descriptor array */
    uint8_t **buffers;             /* Buffer pointer array */
    uint16_t size;                 /* Ring size */
    uint16_t head;                 /* Producer index */
    uint16_t tail;                 /* Consumer index */
    uint16_t count;                /* Current count */
    uint32_t base_phys;            /* Physical base address */
} ring_buffer_t;

/* EEPROM configuration structure */
typedef struct eeprom_config {
    /* Header information */
    uint16_t checksum;          /* EEPROM checksum */
    uint16_t product_id;        /* Product identification */
    uint16_t manufacture_date;  /* Manufacturing date code */
    uint16_t manufacture_div;   /* Manufacturing division */
    
    /* Station address */
    uint8_t  station_addr[6];   /* Permanent station address */
    uint16_t addr_checksum;     /* Station address checksum */
    
    /* Configuration options */
    uint16_t config_control;    /* Configuration control word */
    uint16_t resource_config;   /* Resource configuration */
    uint16_t software_info;     /* Software information */
    uint16_t compatibility;     /* Compatibility level */
    
    /* 3C515-TX specific fields */
    uint16_t bus_master_ctrl;   /* Bus master control */
    uint16_t media_options;     /* Media options available */
    uint16_t full_duplex;       /* Full duplex capabilities */
    uint16_t auto_select;       /* Auto media selection */
    
    /* 3C509B specific fields */
    uint16_t connector_type;    /* Physical connector type */
    uint16_t xcvr_select;       /* Transceiver selection */
    uint16_t link_beat;         /* Link beat enable */
    uint16_t jabber_guard;      /* Jabber protection */
    
    /* Additional configuration */
    uint16_t reserved[4];       /* Reserved for future use */
    
    /* Validation fields */
    bool valid;                 /* Configuration is valid */
    uint8_t version;           /* Configuration version */
    uint8_t flags;             /* Configuration flags */
    uint8_t reserved2;         /* Padding for alignment */
} eeprom_config_t;

/* Hardware capabilities structure */
typedef struct nic_capabilities {
    /* Basic capabilities */
    bool has_dma;               /* DMA support */
    bool has_bus_master;        /* Bus mastering */
    bool has_multicast;         /* Multicast filtering */
    bool has_promiscuous;       /* Promiscuous mode */
    bool has_full_duplex;       /* Full duplex */
    bool has_auto_negotiate;    /* Auto-negotiation */
    bool has_wake_on_lan;       /* Wake on LAN */
    bool has_checksum_offload;  /* Hardware checksum */
    
    /* Speed capabilities */
    bool supports_10mbps;       /* 10 Mbps operation */
    bool supports_100mbps;      /* 100 Mbps operation */
    
    /* Media types supported */
    bool supports_10base_t;     /* 10BASE-T (twisted pair) */
    bool supports_10base_2;     /* 10BASE-2 (coax) */
    bool supports_100base_tx;   /* 100BASE-TX */
    bool supports_aui;          /* AUI connector */
    
    /* Buffer and DMA limits */
    uint16_t max_tx_buffers;    /* Maximum TX buffers */
    uint16_t max_rx_buffers;    /* Maximum RX buffers */
    uint16_t dma_alignment;     /* DMA alignment requirement */
    uint16_t max_packet_size;   /* Maximum packet size */
    
    /* Hardware limits */
    uint8_t multicast_filter_size; /* Multicast filter entries */
    uint8_t tx_fifo_size;       /* TX FIFO size (KB) */
    uint8_t rx_fifo_size;       /* RX FIFO size (KB) */
    uint8_t reserved;           /* Padding */
} nic_capabilities_t;

/* NIC runtime configuration */
typedef struct nic_runtime_config {
    /* Current operational settings */
    uint16_t current_speed;     /* Current speed (10/100) */
    bool full_duplex;           /* Current duplex mode */
    bool promiscuous_mode;      /* Promiscuous mode active */
    uint8_t rx_mode;           /* Current receive mode */
    
    /* Buffer configuration */
    uint16_t tx_ring_size;      /* TX ring buffer size */
    uint16_t rx_ring_size;      /* RX ring buffer size */
    uint16_t tx_buffer_size;    /* TX buffer size */
    uint16_t rx_buffer_size;    /* RX buffer size */
    
    /* Interrupt configuration */
    uint16_t interrupt_mask;    /* Enabled interrupts */
    bool interrupt_mitigation;  /* Interrupt mitigation */
    uint16_t interrupt_delay;   /* Interrupt delay (usec) */
    
    /* Performance tuning */
    uint8_t tx_threshold;       /* TX threshold */
    uint8_t rx_threshold;       /* RX threshold */
    uint16_t dma_burst_size;    /* DMA burst size */
    
    /* Flow control */
    bool flow_control;          /* Flow control enabled */
    uint16_t pause_time;        /* Pause frame time */
    uint16_t reserved;          /* Padding */
} nic_runtime_config_t;

/* Primary NIC context structure */
typedef struct nic_context {
    /* Basic identification */
    nic_type_t nic_type;               /* NIC type identifier */
    nic_state_t state;                 /* Current operational state */
    uint8_t nic_index;                 /* NIC index (0-based) */
    uint8_t irq_line;                  /* IRQ line number */
    
    /* Hardware addressing */
    uint16_t io_base;                  /* Base I/O address */
    uint16_t io_range;                 /* I/O address range */
    uint32_t mem_base;                 /* Memory base (if used) */
    uint32_t mem_size;                 /* Memory size */
    
    /* HAL vtable pointer */
    hardware_hal_vtable_t *hal_vtable; /* Function pointer table */
    
    /* Configuration data */
    eeprom_config_t eeprom_config;     /* EEPROM configuration */
    nic_capabilities_t capabilities;    /* Hardware capabilities */
    nic_runtime_config_t runtime_config; /* Runtime configuration */
    
    /* DMA resources (3C515-TX only) */
    ring_buffer_t tx_ring;             /* TX descriptor ring */
    ring_buffer_t rx_ring;             /* RX descriptor ring */
    uint32_t dma_coherent_base;        /* DMA coherent memory base */
    uint16_t dma_coherent_size;        /* DMA coherent memory size */
    
    /* Buffer management */
    void *tx_buffer_pool;              /* TX buffer pool */
    void *rx_buffer_pool;              /* RX buffer pool */
    uint16_t tx_buffer_count;          /* Available TX buffers */
    uint16_t rx_buffer_count;          /* Available RX buffers */
    
    /* Statistics and counters */
    uint32_t packets_tx;               /* Packets transmitted */
    uint32_t packets_rx;               /* Packets received */
    uint32_t bytes_tx;                 /* Bytes transmitted */
    uint32_t bytes_rx;                 /* Bytes received */
    uint32_t errors_tx;                /* TX errors */
    uint32_t errors_rx;                /* RX errors */
    uint32_t interrupts_handled;       /* Interrupts handled */
    
    /* Link state tracking */
    bool link_up;                      /* Link is up */
    uint32_t link_up_time;             /* Time link came up */
    uint32_t link_down_time;           /* Time link went down */
    uint32_t link_state_changes;       /* Link state changes */
    
    /* Error handling integration */
    nic_context_t *error_context;      /* Error handling context */
    uint32_t last_error_time;          /* Last error timestamp */
    uint16_t consecutive_errors;        /* Consecutive error count */
    uint8_t recovery_level;            /* Current recovery level */
    uint8_t error_flags;               /* Error status flags */
    
    /* Private data pointer for NIC-specific extensions */
    void *private_data;                /* NIC-specific data */
    uint16_t private_data_size;        /* Size of private data */
    
    /* Timing and performance */
    uint32_t init_time;                /* Initialization timestamp */
    uint32_t last_activity_time;       /* Last activity timestamp */
    uint16_t performance_flags;        /* Performance optimization flags */
    
    /* Reserved for future expansion */
    uint8_t reserved[16];              /* Reserved bytes */
} nic_context_t;

#pragma pack(pop)

/* NIC context management functions */
int nic_context_init(nic_context_t *context, nic_type_t type, uint8_t index);
void nic_context_cleanup(nic_context_t *context);
int nic_context_validate(const nic_context_t *context);
int nic_context_reset(nic_context_t *context);

/* EEPROM configuration functions */
int eeprom_config_read(nic_context_t *context);
int eeprom_config_validate(const eeprom_config_t *config);
int eeprom_config_apply(nic_context_t *context);
void eeprom_config_dump(const eeprom_config_t *config);

/* Capabilities management */
int nic_detect_capabilities(nic_context_t *context);
bool nic_has_capability(const nic_context_t *context, uint32_t capability);
void nic_capabilities_dump(const nic_capabilities_t *caps);

/* Ring buffer management (3C515-TX) */
int ring_buffer_init(ring_buffer_t *ring, uint16_t size, uint16_t buffer_size);
void ring_buffer_cleanup(ring_buffer_t *ring);
int ring_buffer_alloc_descriptor(ring_buffer_t *ring);
void ring_buffer_free_descriptor(ring_buffer_t *ring, int index);
bool ring_buffer_is_full(const ring_buffer_t *ring);
bool ring_buffer_is_empty(const ring_buffer_t *ring);

/* Runtime configuration */
int nic_runtime_config_init(nic_context_t *context);
int nic_runtime_config_apply(nic_context_t *context);
int nic_runtime_config_update(nic_context_t *context, const nic_runtime_config_t *config);

/* State management */
int nic_set_state(nic_context_t *context, nic_state_t new_state);
nic_state_t nic_get_state(const nic_context_t *context);
const char* nic_state_to_string(nic_state_t state);
const char* nic_type_to_string(nic_type_t type);

/* Context validation and debugging */
bool nic_context_is_valid(const nic_context_t *context);
void nic_context_dump(const nic_context_t *context);
int nic_context_self_test(nic_context_t *context);

/* Memory management helpers */
int nic_alloc_dma_memory(nic_context_t *context, uint32_t size);
void nic_free_dma_memory(nic_context_t *context);
int nic_setup_buffer_pools(nic_context_t *context);
void nic_cleanup_buffer_pools(nic_context_t *context);

/* Utility macros */
#define NIC_CONTEXT_MAGIC       0x4E49  /* "NI" */
#define NIC_CONTEXT_VERSION     1

#define NIC_IS_3C509B(ctx)      ((ctx)->nic_type == NIC_TYPE_3C509B)
#define NIC_IS_3C515TX(ctx)     ((ctx)->nic_type == NIC_TYPE_3C515TX)
#define NIC_HAS_DMA(ctx)        ((ctx)->capabilities.has_dma)
#define NIC_HAS_BUS_MASTER(ctx) ((ctx)->capabilities.has_bus_master)

#define NIC_STATE_IS_ACTIVE(ctx) \
    ((ctx)->state == NIC_STATE_ACTIVE || (ctx)->state == NIC_STATE_INITIALIZED)

#define NIC_CONTEXT_VALIDATE(ctx) \
    do { \
        if (!nic_context_is_valid(ctx)) return HAL_ERROR_INVALID_PARAM; \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* _NIC_CONTEXT_H_ */