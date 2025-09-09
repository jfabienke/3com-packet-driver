/**
 * @file module_bridge.h
 * @brief Module-to-Driver Bridge Infrastructure
 * 
 * This header provides the bridge infrastructure to connect Module ABI v1.0
 * compliant modules with the existing, tested driver implementations.
 * 
 * ARCHITECTURE: Instead of duplicating driver code in modules, this bridge
 * allows modules to wrap existing drivers (3c509b.c, 3c515.c) while
 * maintaining the modular interface.
 * 
 * BENEFITS:
 * - Eliminates ~2300 lines of duplicate code
 * - Preserves all existing features (cache coherency, chipset support, etc.)
 * - Maintains Module ABI v1.0 compliance
 * - Single maintenance point per driver
 */

#ifndef MODULE_BRIDGE_H
#define MODULE_BRIDGE_H

#include "../../include/module_abi.h"
#include "../../include/hardware.h"
#include "../../include/cpu_detect.h"
#include "../../include/driver_version.h"
#include "../../include/abi_packing.h"

/**
 * @brief Module initialization context passed from centralized detection
 * 
 * Contains all hardware detection results and system information
 * gathered by the module loader during startup.
 * 
 * ABI-STABLE: This structure is part of the stable module interface.
 */
ABI_STRUCT(module_init_context) {
    /* Hardware Detection Results */
    uint16_t detected_io_base;          /* I/O base address */
    uint8_t detected_irq;               /* IRQ line */
    uint8_t mac_address[6];             /* MAC address from EEPROM */
    uint16_t device_id;                 /* Hardware device ID */
    uint16_t vendor_id;                 /* Hardware vendor ID */
    uint8_t revision;                   /* Hardware revision */
    
    /* Bus and Connection Info */
    uint8_t bus_type;                   /* ISA/PCI/PCMCIA (see BUS_TYPE_*) */
    uint8_t pci_bus;                    /* PCI bus number (if PCI) */
    uint8_t pci_device;                 /* PCI device number (if PCI) */
    uint8_t pci_function;               /* PCI function number (if PCI) */
    
    /* System Environment */
    cpu_info_t *cpu_info;               /* Global CPU detection results */
    void *chipset_info;                 /* Chipset compatibility database */
    void *cache_coherency_info;         /* Cache coherency analysis */
    
    /* Configuration Overrides */
    uint8_t force_pio_mode;             /* Force PIO instead of DMA */
    uint8_t enable_bus_mastering;       /* Enable bus master DMA */
    uint8_t enable_checksums;           /* Enable hardware checksums */
    
    /* ABI compatibility padding */
    uint8_t reserved[4];                /* Reserved for future expansion */
    
} ABI_STRUCT_END;

typedef struct module_init_context module_init_context_t;

/**
 * @brief Bridge structure connecting Module ABI to existing drivers
 * 
 * This structure serves as the bridge between the modular interface
 * and the existing driver implementations.
 */
typedef struct {
    /* Module ABI Compliance */
    module_header_t *header;            /* Module header reference */
    uint16_t module_id;                 /* Module identifier */
    uint8_t module_state;               /* Current module state */
    
    /* Existing Driver Integration */
    nic_info_t *nic_context;            /* Existing driver context */
    nic_ops_t *nic_ops;                 /* Legacy driver operations */
    versioned_driver_ops_t *versioned_ops; /* Versioned driver interface */
    void *driver_private;               /* Driver-specific data */
    
    /* Initialization Context */
    module_init_context_t *init_context; /* Hardware detection results */
    
    /* Device Registry Integration */
    int device_registry_id;             /* Registry ID of claimed device */
    
    /* Module-Specific Extensions */
    void *module_private;               /* Module-specific state */
    uint32_t module_flags;              /* Module-specific flags */
    
    /* Performance Metrics */
    uint32_t packets_sent;              /* Statistics counter */
    uint32_t packets_received;          /* Statistics counter */
    uint32_t last_isr_time_us;          /* Last ISR execution time */
    
    /* ISR Safety Validation */
    uint16_t isr_nesting_level;         /* ISR nesting depth */
    uint32_t isr_entry_count;           /* Total ISR invocations */
    uint32_t isr_max_duration_us;       /* Maximum ISR execution time */
    void far *isr_stack_guard;          /* Stack overflow detection */
    
} module_bridge_t;

/* Bus Types */
#define BUS_TYPE_ISA            0x01    /* ISA bus */
#define BUS_TYPE_PCI            0x02    /* PCI bus */
#define BUS_TYPE_PCMCIA         0x03    /* PCMCIA/CardBus */
#define BUS_TYPE_USB            0x04    /* USB (future) */

/* Module States */
#define MODULE_STATE_UNINITIALIZED  0   /* Not initialized */
#define MODULE_STATE_INITIALIZING   1   /* Initialization in progress */
#define MODULE_STATE_ACTIVE         2   /* Operational */
#define MODULE_STATE_ERROR          3   /* Error state */
#define MODULE_STATE_SUSPENDING     4   /* Going to sleep */
#define MODULE_STATE_SUSPENDED      5   /* Sleep state */
#define MODULE_STATE_UNLOADING      6   /* Being unloaded */

/* Module-Specific Flags */
#define MODULE_BRIDGE_FLAG_DMA_ACTIVE       BIT(0)
#define MODULE_BRIDGE_FLAG_ISR_REGISTERED   BIT(1)
#define MODULE_BRIDGE_FLAG_CACHE_COHERENT   BIT(2)
#define MODULE_BRIDGE_FLAG_BUS_MASTER       BIT(3)
#define MODULE_BRIDGE_FLAG_ISR_SAFE         BIT(4)  /* ISR safety validated */
#define MODULE_BRIDGE_FLAG_ISR_REENTRANT    BIT(5)  /* ISR is reentrant */
#define MODULE_BRIDGE_FLAG_ISR_LOCKED       BIT(6)  /* ISR currently executing */

/**
 * @brief Initialize a module bridge structure
 * 
 * @param bridge Pointer to bridge structure to initialize
 * @param header Module header
 * @param init_context Initialization context from loader
 * @return SUCCESS on success, negative error code on failure
 */
int module_bridge_init(module_bridge_t *bridge, 
                      module_header_t *header,
                      module_init_context_t *init_context);

/**
 * @brief Connect bridge to existing NIC driver
 * 
 * @param bridge Initialized bridge structure
 * @param nic_type NIC type (NIC_TYPE_3C509B, NIC_TYPE_3C515_TX)
 * @return SUCCESS on success, negative error code on failure
 */
int module_bridge_connect_driver(module_bridge_t *bridge, uint8_t nic_type);

/**
 * @brief Cleanup bridge and associated resources
 * 
 * @param bridge Bridge structure to cleanup
 * @return SUCCESS on success, negative error code on failure
 */
int module_bridge_cleanup(module_bridge_t *bridge);

/**
 * @brief Generic API dispatcher for bridged modules
 * 
 * Routes module API calls to appropriate existing driver functions.
 * 
 * @param bridge Bridge structure
 * @param function API function number
 * @param params Function parameters
 * @return Function-specific result
 */
int module_bridge_api_dispatch(module_bridge_t *bridge, 
                              uint16_t function, 
                              void far *params);

/**
 * @brief Generic packet send wrapper
 * 
 * @param bridge Bridge structure
 * @param packet_data Packet data
 * @param packet_length Packet length
 * @return SUCCESS on success, negative error code on failure
 */
int module_bridge_send_packet(module_bridge_t *bridge,
                             const void far *packet_data,
                             uint16_t packet_length);

/**
 * @brief Generic packet receive wrapper
 * 
 * @param bridge Bridge structure
 * @param buffer Receive buffer
 * @param buffer_size Buffer size
 * @param bytes_received Actual bytes received
 * @return SUCCESS on success, negative error code on failure
 */
int module_bridge_receive_packet(module_bridge_t *bridge,
                                void far *buffer,
                                uint16_t buffer_size,
                                uint16_t *bytes_received);

/**
 * @brief Generic interrupt handler wrapper
 * 
 * @param bridge Bridge structure
 */
void far module_bridge_handle_interrupt(module_bridge_t *bridge);

/**
 * @brief Get module statistics
 * 
 * @param bridge Bridge structure
 * @param stats Statistics structure to fill
 * @return SUCCESS on success, negative error code on failure
 */
int module_bridge_get_statistics(module_bridge_t *bridge, void far *stats);

/**
 * @brief Get module context from centralized detection
 * 
 * This is the preferred method for modules to get their initialization
 * context from the centralized detection service.
 * 
 * @param module_id Module ID requesting context
 * @param nic_type NIC type (NIC_TYPE_3C509B, NIC_TYPE_3C515_TX)
 * @return Pointer to context or NULL if not available
 */
module_init_context_t* module_get_context_from_detection(uint16_t module_id, uint8_t nic_type);

/* ISR Safety Validation Functions */

/**
 * @brief Validate ISR safety for bridge
 * 
 * Performs comprehensive ISR safety validation including:
 * - Reentrancy checking
 * - Stack usage analysis  
 * - Timing validation
 * - Resource conflict detection
 * 
 * @param bridge Bridge structure to validate
 * @return SUCCESS if ISR is safe, negative error code otherwise
 */
int module_bridge_validate_isr_safety(module_bridge_t *bridge);

/**
 * @brief ISR entry point with safety validation
 * 
 * Safe wrapper around ISR that validates reentrancy and timing.
 * Should be called at the beginning of ISR execution.
 * 
 * @param bridge Bridge structure
 * @return SUCCESS if safe to proceed, ERROR_ISR_UNSAFE otherwise
 */
int module_bridge_isr_enter(module_bridge_t *bridge);

/**
 * @brief ISR exit point with metrics update
 * 
 * Updates ISR metrics and validates execution time.
 * Should be called at the end of ISR execution.
 * 
 * @param bridge Bridge structure
 * @param start_time_us ISR start time in microseconds
 * @return SUCCESS on normal exit, WARNING_ISR_SLOW if execution was slow
 */
int module_bridge_isr_exit(module_bridge_t *bridge, uint32_t start_time_us);

/**
 * @brief Check if ISR is currently executing
 * 
 * @param bridge Bridge structure
 * @return 1 if ISR is executing, 0 otherwise
 */
int module_bridge_isr_is_active(module_bridge_t *bridge);

#endif /* MODULE_BRIDGE_H */