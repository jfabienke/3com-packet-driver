/**
 * @file hardware.h
 * @brief Enhanced hardware abstraction with vtable operations structure
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "error_handling.h"

/* Forward declarations */
struct nic_info;
struct nic_ops;

/* NIC Type Enumeration */
typedef enum {
    NIC_TYPE_UNKNOWN = 0,
    NIC_TYPE_3C509B,
    NIC_TYPE_3C515_TX
} nic_type_t;

/* NIC Status flags */
#define NIC_STATUS_PRESENT      BIT(0)   /* NIC is present */
#define NIC_STATUS_INITIALIZED  BIT(1)   /* NIC is initialized */
#define NIC_STATUS_ACTIVE       BIT(2)   /* NIC is active */
#define NIC_STATUS_ERROR        BIT(3)   /* NIC has error */
#define NIC_STATUS_LINK_UP      BIT(4)   /* Link is up */
#define NIC_STATUS_FULL_DUPLEX  BIT(5)   /* Full duplex mode */
#define NIC_STATUS_100MBPS      BIT(6)   /* 100 Mbps speed */
#define NIC_STATUS_PROMISCUOUS  BIT(7)   /* Promiscuous mode */

/* Hardware capabilities */
#define HW_CAP_DMA              BIT(0)   /* DMA support */
#define HW_CAP_BUS_MASTER       BIT(1)   /* Bus mastering */
#define HW_CAP_MULTICAST        BIT(2)   /* Multicast filtering */
#define HW_CAP_PROMISCUOUS      BIT(3)   /* Promiscuous mode */
#define HW_CAP_FULL_DUPLEX      BIT(4)   /* Full duplex */
#define HW_CAP_AUTO_SPEED       BIT(5)   /* Auto speed detection */
#define HW_CAP_WAKE_ON_LAN      BIT(6)   /* Wake on LAN */
#define HW_CAP_CHECKSUM_OFFLOAD BIT(7)   /* Checksum offload */

/* GPT-5 Critical: PIO fast path capabilities */
#define HW_CAP_PIO_ONLY         BIT(8)   /* PIO only - no DMA mapping needed */
#define HW_CAP_ISA_BUS_MASTER   BIT(9)   /* ISA bus mastering (needs DMA safety) */

/**
 * @brief NIC Operations Vtable - Core Architecture Foundation
 * 
 * ARCHITECTURAL DECISION (2025-08-19): This vtable structure is the definitive
 * interface for all hardware implementations, supporting both current direct
 * implementations and Phase 5 modular loading.
 * 
 * CRITICAL INTEGRATION POINTS:
 * - hardware.c: init_3c509b_ops() and init_3c515_ops() MUST populate these pointers
 * - api.c: Packet Driver API dispatches through this vtable  
 * - Phase 5: Modules export this structure at vtable_offset
 * 
 * FUNCTION SIGNATURES:
 * - All functions return int (0=success, negative=error code)
 * - First parameter is always nic_info_t *nic (context)
 * - Functions must be safe for DOS real mode environment
 */
typedef struct nic_ops {
    /* Core operations */
    int (*init)(struct nic_info *nic);                     /* Initialize the NIC */
    int (*cleanup)(struct nic_info *nic);                  /* Cleanup/shutdown NIC */
    int (*reset)(struct nic_info *nic);                    /* Reset the NIC */
    int (*self_test)(struct nic_info *nic);                /* Self-test routine */
    
    /* Packet operations */
    int (*send_packet)(struct nic_info *nic, const uint8_t *packet, size_t len);
    int (*receive_packet)(struct nic_info *nic, uint8_t *buffer, size_t *len);
    int (*check_tx_complete)(struct nic_info *nic);        /* Check if TX complete */
    int (*check_rx_available)(struct nic_info *nic);       /* Check if RX available */
    
    /* Interrupt operations */
    void (*handle_interrupt)(struct nic_info *nic);        /* Handle an interrupt */
    int (*check_interrupt)(struct nic_info *nic);          /* Check if this NIC caused interrupt */
    int (*enable_interrupts)(struct nic_info *nic);        /* Enable interrupts */
    int (*disable_interrupts)(struct nic_info *nic);       /* Disable interrupts */
    
    /* Configuration operations */
    int (*set_mac_address)(struct nic_info *nic, const uint8_t *mac);
    int (*get_mac_address)(struct nic_info *nic, uint8_t *mac);
    int (*set_promiscuous)(struct nic_info *nic, bool enable);
    int (*set_multicast)(struct nic_info *nic, const uint8_t *addrs, int count);
    int (*set_receive_mode)(struct nic_info *nic, uint8_t mode);
    
    /* Status and statistics */
    int (*get_link_status)(struct nic_info *nic);          /* Get link status */
    int (*get_statistics)(struct nic_info *nic, void *stats);
    int (*clear_statistics)(struct nic_info *nic);         /* Clear statistics */
    
    /* Power management */
    int (*suspend)(struct nic_info *nic);                  /* Suspend NIC */
    int (*resume)(struct nic_info *nic);                   /* Resume NIC */
    int (*set_power_state)(struct nic_info *nic, int state);
    
    /* Advanced features */
    int (*set_speed_duplex)(struct nic_info *nic, int speed, bool full_duplex);
    int (*get_speed_duplex)(struct nic_info *nic, int *speed, bool *full_duplex);
    int (*set_flow_control)(struct nic_info *nic, bool enable);
    
    /* Error handling operations */
    int (*handle_error)(struct nic_info *nic, uint32_t error_status);
    int (*recover_from_error)(struct nic_info *nic, uint8_t error_type);
    int (*validate_recovery)(struct nic_info *nic);
} nic_ops_t;

/* Enhanced NIC Information Structure */
typedef struct nic_info {
    /* Basic information */
    nic_type_t type;                        /* Type of the NIC */
    nic_ops_t *ops;                         /* Pointer to the NIC's operations */
    uint8_t index;                          /* NIC index (0-based) */
    uint32_t status;                        /* Status flags */
    uint32_t capabilities;                  /* Hardware capabilities */
    
    /* Hardware addressing */
    uint16_t io_base;                       /* I/O base address */
    uint16_t io_range;                      /* I/O address range size */
    uint32_t mem_base;                      /* Memory base address (if any) */
    uint32_t mem_size;                      /* Memory size */
    uint8_t irq;                            /* Interrupt request line */
    uint8_t dma_channel;                    /* DMA channel (if used) */
    
    /* Network configuration */
    uint8_t mac[ETH_ALEN];                  /* MAC address */
    uint8_t perm_mac[ETH_ALEN];             /* Permanent MAC address */
    uint16_t mtu;                           /* Maximum Transmission Unit */
    uint8_t receive_mode;                   /* Current receive mode */
    
    /* Performance parameters */
    uint16_t tx_timeout;                    /* Transmit timeout (ms) */
    uint16_t rx_buffer_size;                /* Receive buffer size */
    uint16_t tx_buffer_size;                /* Transmit buffer size */
    uint8_t tx_fifo_threshold;              /* TX FIFO threshold */
    uint8_t rx_fifo_threshold;              /* RX FIFO threshold */
    
    /* Driver state */
    void *private_data;                     /* NIC-specific private data */
    uint32_t private_data_size;             /* Size of private data */
    uint8_t current_window;                 /* Current register window (0-7) */
    
    /* Statistics */
    uint32_t tx_packets;                    /* Transmitted packets */
    uint32_t rx_packets;                    /* Received packets */
    uint32_t tx_bytes;                      /* Transmitted bytes */
    uint32_t rx_bytes;                      /* Received bytes */
    uint32_t tx_errors;                     /* Transmit errors */
    uint32_t rx_errors;                     /* Receive errors */
    uint32_t tx_dropped;                    /* Dropped TX packets */
    uint32_t rx_dropped;                    /* Dropped RX packets */
    uint32_t interrupts;                    /* Interrupt count */
    
    /* Link information */
    bool link_up;                           /* Link status */
    int speed;                              /* Link speed (10/100) */
    bool full_duplex;                       /* Duplex mode */
    bool autoneg;                           /* Auto-negotiation enabled */
    
    /* DMA coherency information */
    bool bus_snooping_verified;             /* Bus snooping has been verified */
    void *tx_descriptor_ring;               /* TX descriptor ring for DMA */
    void *rx_descriptor_ring;               /* RX descriptor ring for DMA */
    
    /* Error tracking (legacy) */
    uint32_t last_error;                    /* Last error code */
    uint32_t error_count;                   /* Total error count */
    
    /* Comprehensive error handling context */
    nic_context_t *error_context;           /* Error handling context */
    
    /* Multicast support */
    uint8_t multicast_count;                /* Number of multicast addresses */
    uint8_t multicast_list[16][ETH_ALEN];   /* Multicast address list */
} nic_info_t;

/* Hardware detection and enumeration */
struct pci_device_info;
struct isa_device_info;

/* Global hardware state */
extern nic_info_t g_nics[MAX_NICS];
extern int g_num_nics;
extern bool g_hardware_initialized;

/* Hardware initialization and cleanup */
int hardware_init(void);
void hardware_cleanup(void);
int hardware_detect_all(void);
int hardware_enumerate_nics(nic_info_t *nics, int max_nics);

/* NIC management */
nic_info_t* hardware_get_nic(int index);
int hardware_get_nic_count(void);
nic_info_t* hardware_find_nic_by_type(nic_type_t type);
nic_info_t* hardware_find_nic_by_mac(const uint8_t *mac);
nic_info_t* hardware_get_primary_nic(void);  /* Get first active NIC for testing */

/* NIC operations (using vtable) */
int hardware_init_nic(nic_info_t *nic);
int hardware_cleanup_nic(nic_info_t *nic);
int hardware_reset_nic(nic_info_t *nic);
int hardware_test_nic(nic_info_t *nic);

/* Packet operations */
int hardware_send_packet(nic_info_t *nic, const uint8_t *packet, size_t len);
int hardware_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *len);
int hardware_check_tx_complete(nic_info_t *nic);
int hardware_check_rx_ready(nic_info_t *nic);
int hardware_pio_read(nic_info_t *nic, void far *buffer, uint16_t size);
int hardware_dma_read(nic_info_t *nic, void far *buffer, uint16_t size);
int hardware_set_loopback_mode(nic_info_t *nic, bool enable);
int hardware_check_rx_available(nic_info_t *nic);

/* Interrupt handling */
void hardware_interrupt_handler(void);
void hardware_handle_nic_interrupt(nic_info_t *nic);
int hardware_check_interrupt_source(void);
int hardware_enable_interrupts(nic_info_t *nic);
int hardware_disable_interrupts(nic_info_t *nic);

/* Configuration functions */
int hardware_set_mac_address(nic_info_t *nic, const uint8_t *mac);
int hardware_get_mac_address(nic_info_t *nic, uint8_t *mac);
int hardware_set_promiscuous_mode(nic_info_t *nic, bool enable);
int hardware_set_multicast_list(nic_info_t *nic, const uint8_t *addrs, int count);
int hardware_set_receive_mode(nic_info_t *nic, uint8_t mode);

/* Status and diagnostics */
int hardware_get_link_status(nic_info_t *nic);
int hardware_get_nic_statistics(nic_info_t *nic, void *stats);
int hardware_clear_nic_statistics(nic_info_t *nic);
void hardware_print_nic_info(const nic_info_t *nic);
void hardware_dump_registers(const nic_info_t *nic);

/* NIC-specific operations retrieval */
nic_ops_t* get_3c509b_ops(void);                /* Get 3C509B operations */
nic_ops_t* get_3c515_ops(void);                 /* Get 3C515 operations */
nic_ops_t* get_nic_ops(nic_type_t type);        /* Get ops by type */

/* Utility functions */
const char* nic_type_to_string(nic_type_t type);
const char* nic_status_to_string(uint32_t status);
bool hardware_is_nic_present(int index);
bool hardware_is_nic_active(int index);

/* Power management */
int hardware_suspend_nic(nic_info_t *nic);
int hardware_resume_nic(nic_info_t *nic);
int hardware_set_power_state(nic_info_t *nic, int state);

/* Advanced configuration */
int hardware_set_speed_duplex(nic_info_t *nic, int speed, bool full_duplex);
int hardware_get_speed_duplex(nic_info_t *nic, int *speed, bool *full_duplex);
int hardware_set_flow_control(nic_info_t *nic, bool enable);

/* Multi-NIC testing functions */
int hardware_test_concurrent_operations(uint32_t test_duration_ms);
int hardware_test_load_balancing(uint32_t num_packets);
int hardware_test_failover(int primary_nic);
int hardware_test_resource_contention(uint32_t num_iterations);
int hardware_test_multi_nic_performance(uint32_t test_duration_ms);
int hardware_run_multi_nic_tests(void);

/* Error handling integration functions */
int hardware_init_error_handling(void);
void hardware_cleanup_error_handling(void);
int hardware_create_error_context(nic_info_t *nic);
void hardware_destroy_error_context(nic_info_t *nic);

/* Error handling wrapper functions */
int hardware_handle_rx_error(nic_info_t *nic, uint32_t rx_status);
int hardware_handle_tx_error(nic_info_t *nic, uint32_t tx_status);
int hardware_handle_adapter_error(nic_info_t *nic, uint8_t failure_type);
int hardware_attempt_recovery(nic_info_t *nic);

/* Error statistics and monitoring */
void hardware_print_error_statistics(nic_info_t *nic);
void hardware_print_global_error_summary(void);
int hardware_get_system_health_status(void);
int hardware_export_error_log(char *buffer, size_t buffer_size);

/* Error threshold configuration */
int hardware_configure_error_thresholds(nic_info_t *nic, uint32_t max_error_rate, 
                                       uint32_t max_consecutive, uint32_t recovery_timeout);

/* === Per-NIC Buffer Pool Integration === */

/* Enhanced packet operations with buffer pool optimization */
int hardware_send_packet_buffered(nic_info_t *nic, const uint8_t *packet, uint16_t length);
int hardware_receive_packet_buffered(nic_info_t *nic, uint8_t *buffer, uint16_t *length);

/* Buffer statistics and management */
int hardware_get_nic_buffer_stats(int nic_index, buffer_pool_stats_t* stats);
int hardware_rebalance_buffer_resources(void);

/* Comprehensive monitoring and statistics */
void hardware_print_comprehensive_stats(void);
void hardware_monitor_and_maintain(void);

/* Hardware constants */
#define MAX_NICS                8       /* Maximum supported NICs */
#define NIC_RESET_TIMEOUT       1000    /* NIC reset timeout (ms) */
#define NIC_INIT_TIMEOUT        5000    /* NIC init timeout (ms) */
#define LINK_CHECK_INTERVAL     1000    /* Link check interval (ms) */

/* 3Com EtherLink III command codes */
#define SelectWindow            0x0800  /* SelectWindow command (1<<11) */
#define AckIntr                 0x6800  /* AckIntr command (13<<11) */
#define RxDiscard               0x4000  /* RxDiscard command (8<<11) */
#define TxComplete              0x0004  /* TxComplete status bit */
#define RxComplete              0x0010  /* RxComplete status bit */

/* EtherLink III register offsets */
#define EL3_CMD                 0x0E    /* Command register */
#define EL3_STATUS              0x0E    /* Status register (read) */

/* Window management macro */
#define EL3WINDOW(nic, win) do { \
    outw(SelectWindow | (win), (nic)->io_base + EL3_CMD); \
    (nic)->current_window = (win); \
} while(0)

/* Bus detection functions (stub implementations for unsupported buses) */
int is_mca_system(void);               /* Check if system has MicroChannel bus */
int is_eisa_system(void);              /* Check if system has EISA bus */
int is_vlb_system(void);               /* Check if system has VESA Local Bus */
int get_ps2_model(void);               /* Get PS/2 model number (0 if not PS/2) */

/* Stub functions for unsupported bus NICs - detect but don't support */
int nic_detect_mca_3c523(void);        /* Returns 1 if 3C523 found but unsupported */
int nic_detect_mca_3c529(void);        /* Returns 1 if 3C529 found but unsupported */
int nic_detect_eisa_3c592(void);       /* Returns 1 if 3C592 found but unsupported */
int nic_detect_eisa_3c597(void);       /* Returns 1 if 3C597 found but unsupported */
int nic_detect_eisa_3c509b(void);      /* Returns 1 if EISA card found (existing stub) */
int nic_detect_vlb(void);              /* Returns 1 if VLB NIC found but unsupported */

#ifdef __cplusplus
}
#endif

#endif /* _HARDWARE_H_ */
uint32_t hardware_get_last_error_time(uint8_t nic_index);
/* Dynamic attach/detach for hot-plug (PCMCIA/CardBus) */
int hardware_attach_pcmcia_nic(uint16_t io_base, uint8_t irq, uint8_t socket);
int hardware_detach_nic_by_index(int index);
int hardware_find_nic_by_io_irq(uint16_t io_base, uint8_t irq);
