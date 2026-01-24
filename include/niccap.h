/**
 * @file nic_capabilities.h
 * @brief NIC Capability Flags System for 3Com Packet Driver
 *
 * This header defines a comprehensive capability-driven system that replaces
 * scattered NIC type checks with unified capability flags. This improves
 * maintainability and makes adding new features easier.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _NIC_CAPABILITIES_H_
#define _NIC_CAPABILITIES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "nic_defs.h"
#include "stats.h"      /* For canonical nic_stats_t used in vtable */

/* Forward declarations */
struct nic_context;
struct nic_info;

/**
 * @brief NIC Capability Flags Enumeration
 * 
 * These flags define the capabilities of different NIC models, enabling
 * capability-driven logic instead of scattered NIC type checks.
 */
typedef enum {
    NIC_CAP_NONE            = 0x0000,  /* No special capabilities */
    NIC_CAP_BUSMASTER       = 0x0001,  /* DMA/Bus mastering support */
    NIC_CAP_PLUG_PLAY       = 0x0002,  /* Plug and Play support */
    NIC_CAP_EEPROM          = 0x0004,  /* EEPROM configuration */
    NIC_CAP_MII             = 0x0008,  /* MII interface */
    NIC_CAP_FULL_DUPLEX     = 0x0010,  /* Full duplex support */
    NIC_CAP_100MBPS         = 0x0020,  /* 100 Mbps support */
    NIC_CAP_HWCSUM          = 0x0040,  /* Hardware checksumming */
    NIC_CAP_WAKEUP          = 0x0080,  /* Wake on LAN */
    NIC_CAP_VLAN            = 0x0100,  /* VLAN tagging */
    NIC_CAP_MULTICAST       = 0x0200,  /* Multicast filtering */
    NIC_CAP_DIRECT_PIO      = 0x0400,  /* Direct PIO optimization */
    NIC_CAP_RX_COPYBREAK    = 0x0800,  /* RX copybreak optimization */
    NIC_CAP_INTERRUPT_MIT   = 0x1000,  /* Interrupt mitigation */
    NIC_CAP_RING_BUFFER     = 0x2000,  /* Ring buffer support */
    NIC_CAP_ENHANCED_STATS  = 0x4000,  /* Enhanced statistics */
    NIC_CAP_ERROR_RECOVERY  = 0x8000,  /* Advanced error recovery */
    NIC_CAP_FLOW_CONTROL    = 0x10000  /* 802.3x flow control support */
} nic_capability_flags_t;

/**
 * @brief NIC Virtual Function Table (VTable)
 * 
 * Enhanced vtable structure with capability-specific operations.
 * This extends the basic nic_ops_t with capability-aware functions.
 */
typedef struct nic_vtable {
    /* Basic operations */
    int (*init)(struct nic_context *ctx);
    int (*cleanup)(struct nic_context *ctx);
    int (*reset)(struct nic_context *ctx);
    int (*self_test)(struct nic_context *ctx);
    
    /* Packet operations */
    int (*send_packet)(struct nic_context *ctx, const uint8_t *packet, uint16_t length);
    int (*receive_packet)(struct nic_context *ctx, uint8_t **packet, uint16_t *length);
    int (*check_tx_status)(struct nic_context *ctx);
    int (*check_rx_status)(struct nic_context *ctx);
    
    /* Configuration operations */
    int (*set_promiscuous)(struct nic_context *ctx, bool enable);
    int (*set_multicast)(struct nic_context *ctx, const uint8_t *addrs, int count);
    int (*set_mac_address)(struct nic_context *ctx, const uint8_t *mac);
    int (*get_mac_address)(struct nic_context *ctx, uint8_t *mac);
    
    /* Statistics and status */
    int (*get_stats)(struct nic_context *ctx, nic_stats_t *stats);
    int (*clear_stats)(struct nic_context *ctx);
    int (*get_link_status)(struct nic_context *ctx);
    
    /* Capability-specific operations */
    int (*configure_busmaster)(struct nic_context *ctx, bool enable);
    int (*configure_mii)(struct nic_context *ctx, uint8_t phy_addr);
    int (*set_speed_duplex)(struct nic_context *ctx, int speed, bool full_duplex);
    int (*enable_wakeup)(struct nic_context *ctx, uint32_t wakeup_mask);
    int (*configure_vlan)(struct nic_context *ctx, uint16_t vlan_id);
    int (*tune_interrupt_mitigation)(struct nic_context *ctx, uint16_t delay_us);
    
    /* Error handling and recovery */
    int (*handle_error)(struct nic_context *ctx, uint32_t error_flags);
    int (*recover_from_error)(struct nic_context *ctx, uint8_t recovery_type);
    int (*validate_recovery)(struct nic_context *ctx);
} nic_vtable_t;

/**
 * @brief NIC Information Entry
 * 
 * Comprehensive database entry describing a specific NIC model's
 * capabilities, characteristics, and operational parameters.
 */
typedef struct nic_info_entry {
    /* Basic identification */
    const char *name;                    /* NIC name */
    nic_type_t nic_type;                 /* NIC type enumeration */
    uint16_t device_id;                  /* PCI/PnP device ID */
    uint16_t vendor_id;                  /* PCI/PnP vendor ID */
    
    /* Capability information */
    nic_capability_flags_t capabilities; /* Capability flags */
    uint32_t feature_mask;               /* Additional feature mask */
    
    /* Hardware characteristics */
    uint16_t io_size;                    /* I/O space size */
    uint8_t max_irq;                     /* Maximum IRQ number */
    uint32_t buffer_alignment;           /* Required buffer alignment */
    uint16_t max_packet_size;            /* Maximum packet size */
    uint16_t min_packet_size;            /* Minimum packet size */
    
    /* Default configuration */
    uint8_t default_tx_ring_size;        /* Default TX ring size */
    uint8_t default_rx_ring_size;        /* Default RX ring size */
    uint16_t default_tx_timeout;         /* Default TX timeout (ms) */
    uint16_t default_rx_timeout;         /* Default RX timeout (ms) */
    
    /* Performance parameters */
    uint32_t max_throughput_mbps;        /* Maximum throughput */
    uint16_t interrupt_latency_us;       /* Typical interrupt latency */
    uint8_t dma_burst_size;              /* DMA burst size */
    uint8_t fifo_size_kb;                /* FIFO size in KB */
    
    /* Media support */
    uint16_t media_capabilities;         /* Supported media types */
    media_type_t default_media;          /* Default media type */
    
    /* Function table */
    nic_vtable_t *vtable;                /* Function pointers */
} nic_info_entry_t;

/**
 * @brief NIC Capability Context Structure
 *
 * Runtime context for a NIC instance, containing both static information
 * from the database and dynamic runtime state.
 *
 * Note: Renamed from nic_context to nic_cap_context to avoid conflict
 * with the primary nic_context_t defined in nicctx.h
 */
#ifndef NIC_CONTEXT_T_DEFINED
typedef struct nic_cap_context {
    /* Database reference */
    const nic_info_entry_t *info;       /* Pointer to static info */
    
    /* Runtime configuration */
    uint16_t io_base;                    /* Assigned I/O base */
    uint8_t irq;                         /* Assigned IRQ */
    uint8_t mac[6];                      /* Current MAC address */
    
    /* Capability state */
    nic_capability_flags_t active_caps;  /* Currently active capabilities */
    nic_capability_flags_t detected_caps; /* Runtime-detected capabilities */
    
    /* Performance tuning */
    uint8_t tx_ring_size;                /* Current TX ring size */
    uint8_t rx_ring_size;                /* Current RX ring size */
    uint16_t copybreak_threshold;        /* RX copybreak threshold */
    uint16_t interrupt_mitigation;       /* Interrupt mitigation setting */
    
    /* Media configuration */
    media_type_t current_media;          /* Current media type */
    bool link_up;                        /* Link status */
    int speed;                           /* Current speed */
    bool full_duplex;                    /* Current duplex mode */
    
    /* Statistics */
    uint32_t packets_sent;               /* Packets sent counter */
    uint32_t packets_received;           /* Packets received counter */
    uint32_t errors;                     /* Error counter */
    uint32_t capabilities_changed;       /* Capability change counter */
    
    /* Driver state */
    void *private_data;                  /* NIC-specific private data */
    uint32_t flags;                      /* Driver flags */
    uint8_t state;                       /* Current state */
} nic_cap_context_t;
#endif /* NIC_CONTEXT_T_DEFINED */

/**
 * @brief NIC Capability Statistics Structure
 *
 * Comprehensive statistics structure for capability-aware reporting.
 *
 * Note: Renamed from nic_stats to nic_cap_stats to avoid conflict
 * with the primary nic_stats_t defined in stats.h
 */
#ifndef NIC_STATS_T_DEFINED
typedef struct nic_cap_stats {
    /* Basic counters */
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t tx_dropped;
    uint32_t rx_dropped;
    
    /* Capability-specific counters */
    uint32_t dma_transfers;              /* DMA transfer count (if NIC_CAP_BUSMASTER) */
    uint32_t pio_transfers;              /* PIO transfer count (if NIC_CAP_DIRECT_PIO) */
    uint32_t copybreak_hits;             /* Copybreak optimization hits */
    uint32_t interrupt_mitigations;      /* Interrupt mitigation events */
    uint32_t multicast_packets;          /* Multicast packets (if NIC_CAP_MULTICAST) */
    uint32_t vlan_packets;               /* VLAN packets (if NIC_CAP_VLAN) */
    uint32_t checksum_offloads;          /* Checksum offloads (if NIC_CAP_HWCSUM) */
    uint32_t wakeup_events;              /* Wake-on-LAN events (if NIC_CAP_WAKEUP) */
    uint32_t pause_frames_sent;          /* PAUSE frames sent (if NIC_CAP_FLOW_CONTROL) */
    uint32_t pause_frames_received;      /* PAUSE frames received (if NIC_CAP_FLOW_CONTROL) */
    uint32_t flow_control_events;        /* Flow control activation events */
    
    /* Error breakdown */
    uint32_t link_errors;
    uint32_t frame_errors;
    uint32_t crc_errors;
    uint32_t fifo_errors;
    uint32_t dma_errors;
    uint32_t timeout_errors;
    
    /* Performance metrics */
    uint32_t avg_latency_us;             /* Average packet latency */
    uint32_t peak_throughput_kbps;       /* Peak throughput */
    uint32_t utilization_percent;        /* Link utilization */
} nic_cap_stats_t;
#endif /* NIC_STATS_T_DEFINED */

/* ========================================================================== */
/* CAPABILITY QUERY FUNCTIONS                                                */
/* ========================================================================== */

/**
 * @brief Check if NIC has specific capability
 * @param ctx NIC context
 * @param capability Capability flag to check
 * @return true if capability is supported, false otherwise
 */
bool nic_has_capability(const nic_context_t *ctx, nic_capability_flags_t capability);

/**
 * @brief Get all capabilities for a NIC
 * @param ctx NIC context
 * @return Bitmask of all supported capabilities
 */
nic_capability_flags_t nic_get_capabilities(const nic_context_t *ctx);

/**
 * @brief Get NIC information entry by type
 * @param type NIC type
 * @return Pointer to info entry, or NULL if not found
 */
const nic_info_entry_t* nic_get_info_entry(nic_type_t type);

/**
 * @brief Get NIC information entry by device ID
 * @param device_id Device ID to search for
 * @return Pointer to info entry, or NULL if not found
 */
const nic_info_entry_t* nic_get_info_by_device_id(uint16_t device_id);

/**
 * @brief Convert capability flags to string representation
 * @param capabilities Capability flags
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written
 */
int nic_get_capability_string(nic_capability_flags_t capabilities, char *buffer, size_t buffer_size);

/* ========================================================================== */
/* RUNTIME CAPABILITY DETECTION                                              */
/* ========================================================================== */

/**
 * @brief Detect runtime capabilities of a NIC
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
int nic_detect_runtime_capabilities(nic_context_t *ctx);

/**
 * @brief Update NIC capabilities dynamically
 * @param ctx NIC context
 * @param new_caps New capability flags to add
 * @return 0 on success, negative on error
 */
int nic_update_capabilities(nic_context_t *ctx, nic_capability_flags_t new_caps);

/**
 * @brief Validate that NIC supports required capabilities
 * @param ctx NIC context
 * @param required_caps Required capability flags
 * @return true if all required capabilities are supported
 */
bool nic_validate_capabilities(const nic_context_t *ctx, nic_capability_flags_t required_caps);

/* ========================================================================== */
/* CONTEXT MANAGEMENT                                                        */
/* ========================================================================== */

/**
 * @brief Initialize NIC context from info entry
 * @param ctx NIC context to initialize
 * @param info_entry Static NIC information
 * @param io_base I/O base address
 * @param irq IRQ number
 * @return 0 on success, negative on error
 */
int nic_context_init(nic_context_t *ctx, const nic_info_entry_t *info_entry, 
                     uint16_t io_base, uint8_t irq);

/**
 * @brief Cleanup NIC context
 * @param ctx NIC context to cleanup
 */
void nic_context_cleanup(nic_context_t *ctx);

/**
 * @brief Copy NIC context
 * @param dest Destination context
 * @param src Source context
 * @return 0 on success, negative on error
 */
int nic_context_copy(nic_context_t *dest, const nic_context_t *src);

/* ========================================================================== */
/* CAPABILITY-DRIVEN OPERATIONS                                              */
/* ========================================================================== */

/**
 * @brief Send packet using capability-appropriate method
 * @param ctx NIC context
 * @param packet Packet data
 * @param length Packet length
 * @return 0 on success, negative on error
 */
int nic_send_packet_caps(nic_context_t *ctx, const uint8_t *packet, uint16_t length);

/**
 * @brief Receive packet using capability-appropriate method
 * @param ctx NIC context
 * @param buffer Buffer for packet data
 * @param length Buffer size on input, packet length on output
 * @return 0 on success, negative on error
 */
int nic_receive_packet_caps(nic_context_t *ctx, uint8_t *buffer, uint16_t *length);

/**
 * @brief Configure NIC based on capabilities
 * @param ctx NIC context
 * @param config Configuration parameters
 * @return 0 on success, negative on error
 */
int nic_configure_caps(nic_context_t *ctx, const nic_config_t *config);

/* ========================================================================== */
/* PERFORMANCE OPTIMIZATION                                                  */
/* ========================================================================== */

/**
 * @brief Optimize NIC configuration based on capabilities
 * @param ctx NIC context
 * @param optimization_flags Optimization flags
 * @return 0 on success, negative on error
 */
int nic_optimize_performance(nic_context_t *ctx, uint32_t optimization_flags);

/**
 * @brief Tune specific capability features
 * @param ctx NIC context
 * @param capability Capability to tune
 * @param parameters Tuning parameters
 * @return 0 on success, negative on error
 */
int nic_tune_capability(nic_context_t *ctx, nic_capability_flags_t capability, void *parameters);

/* ========================================================================== */
/* DATABASE ACCESS                                                           */
/* ========================================================================== */

/**
 * @brief Get the NIC database
 * @param count Output parameter for database size
 * @return Pointer to NIC database array
 */
const nic_info_entry_t* nic_get_database(int *count);

/**
 * @brief Register a new NIC entry in the database
 * @param entry NIC information entry to register
 * @return 0 on success, negative on error
 */
int nic_register_entry(const nic_info_entry_t *entry);

/* ========================================================================== */
/* COMPATIBILITY LAYER                                                       */
/* ========================================================================== */

/**
 * @brief Convert nic_info_t to nic_context_t for compatibility
 * @param nic_info Legacy NIC info structure
 * @param ctx Output NIC context
 * @return 0 on success, negative on error
 */
int nic_info_to_context(const nic_info_t *nic_info, nic_context_t *ctx);

/**
 * @brief Convert nic_context_t to nic_info_t for compatibility
 * @param ctx NIC context
 * @param nic_info Output legacy NIC info structure
 * @return 0 on success, negative on error
 */
int nic_context_to_info(const nic_context_t *ctx, nic_info_t *nic_info);

/* ========================================================================== */
/* CONSTANTS AND MACROS                                                      */
/* ========================================================================== */

/* Maximum number of NICs supported by capability system */
#define NIC_CAP_MAX_NICS            8

/* Capability check macros */
#define NIC_HAS_CAP(ctx, cap)       nic_has_capability(ctx, cap)
#define NIC_CAP_IS_SET(caps, cap)   ((caps & cap) != 0)
#define NIC_CAP_SET(caps, cap)      (caps |= cap)
#define NIC_CAP_CLEAR(caps, cap)    (caps &= ~cap)

/* Performance optimization flags */
#define NIC_OPT_LATENCY             0x0001  /* Optimize for latency */
#define NIC_OPT_THROUGHPUT          0x0002  /* Optimize for throughput */
#define NIC_OPT_POWER               0x0004  /* Optimize for power */
#define NIC_OPT_COMPATIBILITY       0x0008  /* Optimize for compatibility */

/* Error codes */
#define NIC_CAP_SUCCESS             0
#define NIC_CAP_ERROR               -1
#define NIC_CAP_INVALID_PARAM       -2
#define NIC_CAP_NOT_SUPPORTED       -3
#define NIC_CAP_NO_MEMORY           -4
#define NIC_CAP_DEVICE_NOT_FOUND    -5
#define NIC_CAP_CAPABILITY_MISSING  -6

#ifdef __cplusplus
}
#endif

#endif /* _NIC_CAPABILITIES_H_ */