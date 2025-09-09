/**
 * @file ptask_internal.h
 * @brief PTASK.MOD Internal Definitions and Structures
 * 
 * Internal header for PTASK module implementation.
 * Contains structures and definitions not exposed to other modules.
 */

#ifndef PTASK_INTERNAL_H
#define PTASK_INTERNAL_H

#include "../../include/common.h"
#include "../../include/module_abi.h"
#include "../../include/memory_api.h"
#include "../../include/timing_measurement.h"

/* PTASK Module Constants */
#define PTASK_MAX_NICS          4       /* Maximum NICs supported */
#define PTASK_BUFFER_POOLS      2       /* Small and large buffer pools */
#define PTASK_ISR_TIMEOUT_US    60      /* ISR timeout limit */
#define PTASK_CLI_TIMEOUT_US    8       /* CLI duration limit */

/* PTASK API Function Numbers */
#define PTASK_API_DETECT_HARDWARE   0x01
#define PTASK_API_INITIALIZE_NIC    0x02
#define PTASK_API_SEND_PACKET       0x03
#define PTASK_API_RECEIVE_PACKET    0x04
#define PTASK_API_GET_STATISTICS    0x05
#define PTASK_API_CONFIGURE         0x06

/* Hardware Types */
typedef enum {
    PTASK_HARDWARE_UNKNOWN      = 0x00,
    PTASK_HARDWARE_3C509B       = 0x01,
    PTASK_HARDWARE_3C589        = 0x02,
    PTASK_HARDWARE_NE2000_COMPAT = 0x10  /* Week 1 compatibility */
} ptask_hardware_type_t;

/* Module States */
typedef enum {
    PTASK_STATE_UNLOADED        = 0x00,
    PTASK_STATE_LOADING         = 0x01,
    PTASK_STATE_INITIALIZING    = 0x02,
    PTASK_STATE_ACTIVE          = 0x03,
    PTASK_STATE_ERROR           = 0x04,
    PTASK_STATE_UNLOADING       = 0x05
} ptask_state_t;

/* CPU Information Structure */
typedef struct {
    uint16_t cpu_type;          /* CPU type (80286, 80386, etc.) */
    uint16_t features;          /* CPU feature flags */
    uint8_t  optimization_level; /* Applied optimization level */
    bool     has_fpu;           /* Floating point unit present */
} cpu_info_t;

/* PTASK Module Context */
typedef struct {
    /* Module identification */
    uint16_t module_id;         /* MODULE_ID_PTASK */
    ptask_state_t state;        /* Current module state */
    
    /* Hardware configuration */
    ptask_hardware_type_t hardware_type;
    uint16_t io_base;           /* Base I/O address */
    uint8_t  irq;               /* IRQ number */
    uint8_t  mac_address[6];    /* Hardware MAC address */
    
    /* CPU information */
    uint16_t cpu_type;          /* Detected CPU type */
    uint16_t cpu_features;      /* CPU feature flags */
    
    /* Runtime flags */
    bool hardware_initialized;  /* Hardware init completed */
    bool isr_registered;        /* ISR registered with system */
    bool buffer_pools_ready;    /* Buffer pools initialized */
    
    /* Statistics */
    uint32_t packets_sent;      /* Total packets sent */
    uint32_t packets_received;  /* Total packets received */
    uint32_t bytes_sent;        /* Total bytes sent */
    uint32_t bytes_received;    /* Total bytes received */
    uint32_t send_errors;       /* Send error count */
    uint32_t receive_errors;    /* Receive error count */
    
    /* Performance metrics */
    uint16_t avg_isr_time_us;   /* Average ISR time */
    uint16_t max_isr_time_us;   /* Maximum ISR time */
    uint16_t avg_cli_time_us;   /* Average CLI time */
    uint16_t max_cli_time_us;   /* Maximum CLI time */
} ptask_context_t;

/* API Parameter Structures */
typedef struct {
    uint16_t hardware_types;    /* Bitmask of supported types */
    uint16_t scan_flags;        /* Hardware scan flags */
} ptask_detect_params_t;

typedef struct {
    uint16_t hardware_type;     /* Detected hardware type */
    uint16_t io_base;           /* Base I/O address */
    uint8_t  irq;               /* IRQ number */
    uint8_t  mac_address[6];    /* Hardware MAC address */
    uint16_t capabilities;      /* Hardware capabilities */
} ptask_init_params_t;

typedef struct {
    void far *packet_data;      /* Packet data pointer */
    uint16_t  packet_length;    /* Packet length */
    uint16_t  send_flags;       /* Send operation flags */
    uint32_t  timeout_ms;       /* Send timeout */
} ptask_send_params_t;

typedef struct {
    void far *buffer;           /* Receive buffer */
    uint16_t  buffer_size;      /* Buffer size */
    uint16_t *received_length;  /* Actual received length */
    uint16_t  recv_flags;       /* Receive flags */
    uint32_t  timeout_ms;       /* Receive timeout */
} ptask_recv_params_t;

typedef struct {
    uint32_t packets_sent;      /* Total packets sent */
    uint32_t packets_received;  /* Total packets received */
    uint32_t bytes_sent;        /* Total bytes sent */
    uint32_t bytes_received;    /* Total bytes received */
    uint32_t send_errors;       /* Send errors */
    uint32_t receive_errors;    /* Receive errors */
    uint16_t avg_isr_time_us;   /* Average ISR time */
    uint16_t max_isr_time_us;   /* Maximum ISR time */
} ptask_stats_params_t;

typedef struct {
    uint16_t config_type;       /* Configuration type */
    uint16_t config_flags;      /* Configuration flags */
    void far *config_data;      /* Configuration data */
    uint16_t config_length;     /* Configuration data length */
} ptask_config_params_t;

/* NE2000 Compatibility Structures (Week 1) */
typedef struct {
    uint16_t io_base;           /* Base I/O address */
    uint8_t  interrupt_line;    /* IRQ number */
    uint8_t  mac_address[6];    /* MAC address */
} ne2000_config_t;

/* NE2000 Register Definitions */
#define NE_COMMAND              0x00    /* Command register */
#define NE_DATAPORT             0x10    /* Data port */
#define NE_RESET                0x1F    /* Reset register */

/* NE2000 Command Register Bits */
#define NE_CMD_STOP             0x01    /* Stop chip */
#define NE_CMD_START            0x02    /* Start chip */
#define NE_CMD_TRANSMIT         0x04    /* Transmit packet */
#define NE_CMD_READ             0x08    /* Remote read */
#define NE_CMD_WRITE            0x10    /* Remote write */

/* Buffer Pool Management */
typedef struct {
    packet_buffer_t *buffers;   /* Buffer array */
    uint16_t buffer_count;      /* Number of buffers */
    uint16_t buffer_size;       /* Size of each buffer */
    uint16_t free_count;        /* Number of free buffers */
    uint16_t alloc_index;       /* Next allocation index */
    uint16_t free_index;        /* Next free index */
} buffer_pool_t;

/* Shared PIO Library Interface */
typedef struct {
    void (*outb_optimized)(uint16_t port, uint8_t value);
    void (*outw_optimized)(uint16_t port, uint16_t value);
    uint8_t (*inb_optimized)(uint16_t port);
    uint16_t (*inw_optimized)(uint16_t port);
    void (*outsw_optimized)(uint16_t port, const void *buffer, uint16_t count);
    void (*insw_optimized)(uint16_t port, void *buffer, uint16_t count);
} pio_interface_t;

/* Function Declarations */

/* Core module functions */
extern int far ptask_module_init(void);
extern int far ptask_module_api(uint16_t function, void far *params);
extern void far ptask_module_isr(void);
extern int far ptask_module_cleanup(void);

/* Hardware detection functions */
extern int ptask_detect_target_hardware(void);
extern int ptask_detect_3c509b(void);
extern int ptask_detect_3c589(void);
extern int ptask_detect_ne2000(void);

/* Hardware initialization functions */
extern int ptask_init_hardware(nic_info_t *nic);
extern int ptask_cleanup_hardware(void);
extern int ptask_disable_interrupts(void);

/* API implementation functions */
extern int ptask_api_detect_hardware(ptask_detect_params_t far *params);
extern int ptask_api_initialize_nic(ptask_init_params_t far *params);
extern int ptask_api_send_packet(ptask_send_params_t far *params);
extern int ptask_api_receive_packet(ptask_recv_params_t far *params);
extern int ptask_api_get_statistics(ptask_stats_params_t far *params);
extern int ptask_api_configure(ptask_config_params_t far *params);

/* Week 1 NE2000 compatibility functions */
extern int ptask_init_ne2000_compat(void);
extern int ne2000_init_hardware(ne2000_config_t *config);
extern int ne2000_read_mac_address(uint8_t *mac_address);
extern int ne2000_send_packet(const void *packet, uint16_t length);
extern int ne2000_receive_packet(void *buffer, uint16_t *length);

/* Memory management functions */
extern int ptask_register_memory_services(memory_services_t *memory_services);
extern int ptask_init_memory_pools(void);
extern int ptask_create_buffer_pools(buffer_pool_config_t *config);
extern void ptask_free_allocated_memory(void);

/* Shared PIO library functions */
extern int pio_lib_init(cpu_info_t *cpu_info);
extern pio_interface_t* pio_get_interface(void);

/* CPU optimization functions */
extern int detect_cpu_capabilities(cpu_info_t *cpu_info);
extern void ptask_patch_286_optimizations(void);
extern void ptask_patch_386_optimizations(void);
extern void ptask_patch_486_optimizations(void);
extern void ptask_patch_pentium_optimizations(void);
extern void flush_prefetch_queue(void);

/* ISR implementation */
extern void ptask_isr_asm_entry(void);
extern int ptask_handle_interrupt(void);

/* Statistics and monitoring */
extern void ptask_update_statistics(uint32_t packets_sent, uint32_t packets_received,
                                   uint32_t bytes_sent, uint32_t bytes_received);
extern void ptask_update_timing_stats(uint16_t isr_time_us, uint16_t cli_time_us);

/* Utility functions */
extern void ptask_log_module_info(void);
extern int ptask_validate_parameters(void far *params, uint16_t param_size);

/* Global context access */
extern ptask_context_t* ptask_get_context(void);

#endif /* PTASK_INTERNAL_H */