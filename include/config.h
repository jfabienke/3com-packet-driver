/**
 * @file config.h
 * @brief Configuration structures and parsing
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"

#define CONFIG_MAGIC 0x3C05  /* Config structure validation magic */

/* Forward declarations */
typedef struct busmaster_test_results busmaster_test_results_t;
typedef struct nic_context nic_context_t;

/* Error codes */
#define CONFIG_SUCCESS           0
#define CONFIG_ERR_INVALID_PARAM -1
#define CONFIG_ERR_INVALID_VALUE -2
#define CONFIG_ERR_MEMORY        -3
#define CONFIG_ERR_IO_CONFLICT   -4

/* CRITICAL SAFETY: Force 3C515 to PIO mode until DMA validated */
#ifndef FORCE_3C515_PIO_SAFETY
#define FORCE_3C515_PIO_SAFETY 1  /* Default: Force PIO for safety */
#endif

#if FORCE_3C515_PIO_SAFETY
    #define USE_3C515_DMA 0       /* Disabled until bus mastering validated */
    #define USE_3C515_PIO 1       /* Use safe PIO mode */
#else
    #define USE_3C515_DMA 1       /* Can be enabled after validation */
    #define USE_3C515_PIO 0       /* Use faster DMA mode */
#endif

/* Global flag for runtime PIO enforcement */
extern int global_force_pio_mode;
#define CONFIG_ERR_IRQ_CONFLICT  -5
#define CONFIG_ERR_CPU_REQUIRED  -6
#define CONFIG_ERR_ROUTE_SYNTAX  -7
#define CONFIG_ERR_TOO_MANY_ROUTES -8
#define CONFIG_ERR_INVALID_SPEED -9
#define CONFIG_ERR_INVALID_IO_RANGE -10
#define CONFIG_ERR_INVALID_IRQ_RANGE -11

/* Network speed enumeration - prefixed to avoid conflict with 3c515.h macros */
typedef enum {
    CFG_SPEED_AUTO,      /* 0: Auto-detect speed */
    CFG_SPEED_10,        /* 1: 10 Mbps */
    CFG_SPEED_100        /* 2: 100 Mbps */
} network_speed_t;

/* Actual speed values for runtime use */
#define SPEED_VALUE_10   10
#define SPEED_VALUE_100  100

/* Busmaster mode enumeration */
typedef enum {
    BUSMASTER_OFF,   /* 0: Bus mastering disabled */
    BUSMASTER_ON,    /* 1: Bus mastering enabled */
    BUSMASTER_AUTO   /* 2: Auto-detect bus mastering */
} busmaster_mode_t;

/* PCI support mode enumeration */
typedef enum {
    PCI_DISABLED,    /* 0: PCI support disabled */
    PCI_ENABLED,     /* 1: PCI support enabled if available */
    PCI_REQUIRED     /* 2: PCI support required (fail if not available) */
} pci_mode_t;

/* IP Route entry structure (renamed to avoid conflict with routing.h) */
typedef struct {
    uint32_t network;           /* Network address */
    uint32_t netmask;           /* Network mask */
    uint8_t nic_id;             /* NIC identifier (1 or 2) */
    bool active;                /* Route is active */
} ip_route_entry_t;

#define MAX_ROUTES 8

/* Enhanced configuration structure */
typedef struct config {
    uint16_t magic;             /* Config magic for validation */
    /* Original settings */
    int debug_level;            /* Debug verbosity (0-3) */
    int use_xms;                /* Use XMS memory if available */
    int enable_routing;         /* Enable packet routing */
    int enable_static_routing;  /* Enable static routing tables */
    int buffer_count;           /* Number of packet buffers */
    int buffer_size;            /* Size of each buffer */
    uint8_t interrupt_vector;   /* Interrupt vector to use */
    uint16_t io_base;           /* I/O base address (legacy, use io1_base) */
    uint8_t irq;                /* IRQ number (legacy, use irq1) */
    int enable_stats;           /* Enable statistics collection */
    int promiscuous_mode;       /* Enable promiscuous mode */
    int enable_logging;         /* Enable logging */
    int test_mode;              /* Test mode */
    
    /* 3Com packet driver specific settings */
    
    /* Buffer auto-configuration overrides */
    uint16_t override_buffer_size;    /* Override buffer size (0=auto) */
    uint8_t override_tx_ring_count;   /* Override TX ring count (0=auto) */
    uint8_t override_rx_ring_count;   /* Override RX ring count (0=auto) */
    uint8_t force_pio_mode;           /* Force PIO mode (no bus master) */
    uint8_t force_minimal_buffers;    /* Force minimal 3KB configuration */
    uint8_t force_optimal_buffers;    /* Force maximum performance config */
    
    /* Existing 3Com settings continue below */
    uint16_t io1_base;          /* First NIC I/O base address */
    uint16_t io2_base;          /* Second NIC I/O base address */
    uint8_t irq1;               /* First NIC IRQ */
    uint8_t irq2;               /* Second NIC IRQ */
    network_speed_t speed;      /* Network speed setting */
    busmaster_mode_t busmaster; /* Bus mastering mode */
    pci_mode_t pci;            /* PCI support mode */
    bool log_enabled;           /* Logging enabled */
    ip_route_entry_t routes[MAX_ROUTES]; /* Static routes */
    uint8_t route_count;        /* Number of configured routes */
    
    /* IRQ handling settings */
    uint16_t poll_interval;    /* Polling interval in ms (0=auto) */
    bool shared_irq;           /* Allow IRQ sharing */

    /* Enhanced settings */
    uint8_t mac_address[ETH_ALEN];          /* MAC address override */
    bool use_custom_mac;                    /* Use custom MAC address */
    uint16_t mtu;                           /* Maximum Transmission Unit */
    uint8_t receive_mode;                   /* Receive mode setting */
    uint16_t tx_timeout;                    /* Transmit timeout (ms) */
    uint16_t rx_buffer_count;               /* RX buffer count */
    uint16_t tx_buffer_count;               /* TX buffer count */
    uint8_t tx_threshold;                   /* TX threshold */
    uint8_t rx_threshold;                   /* RX threshold */
    bool auto_detect;                       /* Auto-detect NICs */
    bool load_balancing;                    /* Load balancing enabled */
    bool packet_routing;                    /* Packet routing enabled */
    bool statistics_enabled;                /* Statistics collection */
    uint8_t log_level;                      /* Logging level */
    uint16_t resident_size;                 /* TSR size in paragraphs */
    bool install_tsr;                       /* Install as TSR */
    bool enable_multicast;                  /* Multicast support */
    bool enable_broadcast;                  /* Broadcast support */
    bool enable_full_duplex;                /* Full duplex mode */
    bool enable_flow_control;               /* Flow control */
    bool enable_checksums;                  /* Checksum validation */
    uint16_t link_check_interval;           /* Link check interval (ms) */
    uint16_t statistics_interval;           /* Statistics update interval */
    uint16_t watchdog_timeout;              /* Watchdog timeout (ms) */
    bool debug_enabled;                     /* Debug mode enabled */
    uint32_t debug_flags;                   /* Debug flags */
    char debug_output[16];                  /* Debug output destination */
    bool verbose_mode;                      /* Verbose output */
    char config_file[64];                   /* Configuration file path (DOS 8.3 max ~80) */
    bool save_on_exit;                      /* Save config on exit */
    bool load_defaults;                     /* Load default values */
} config_t;

/* Configuration defaults */
#define CONFIG_DEFAULT_MTU              1514
#define CONFIG_DEFAULT_RX_BUFFERS       16
#define CONFIG_DEFAULT_TX_BUFFERS       8
#define CONFIG_DEFAULT_BUFFER_SIZE      1600
#define CONFIG_DEFAULT_TX_TIMEOUT       1000
#define CONFIG_DEFAULT_LOG_LEVEL        LOG_LEVEL_INFO
#define CONFIG_DEFAULT_TSR_SIZE         64      /* Paragraphs */
#define CONFIG_DEFAULT_INTERRUPT        0x60    /* Default packet driver interrupt */
#define CONFIG_DEFAULT_LINK_CHECK       1000    /* ms */
#define CONFIG_DEFAULT_STATS_INTERVAL   5000    /* ms */
#define CONFIG_DEFAULT_WATCHDOG         10000   /* ms */

/* 3Com packet driver defaults */
#define CONFIG_DEFAULT_IO1_BASE         0x300   /* First NIC I/O base */
#define CONFIG_DEFAULT_IO2_BASE         0x320   /* Second NIC I/O base */
#define CONFIG_DEFAULT_IRQ1             5       /* First NIC IRQ */
#define CONFIG_DEFAULT_IRQ2             10      /* Second NIC IRQ */
#define CONFIG_DEFAULT_SPEED            CFG_SPEED_AUTO
#define CONFIG_DEFAULT_BUSMASTER        BUSMASTER_AUTO
#define CONFIG_DEFAULT_LOG_ENABLED      true

/* I/O address ranges */
#define CONFIG_MIN_IO_BASE              0x200
#define CONFIG_MAX_IO_BASE              0x3F0
#define CONFIG_IO_RANGE_SIZE            0x20    /* Each NIC needs 32 bytes */

/* Valid IRQ numbers for ISA devices */
#define CONFIG_VALID_IRQS               0x9EA8  /* Bits: 3,5,7,9,10,11,12,15 */

/* Parameter validation */
bool config_is_valid_io_address(uint16_t io_base);
bool config_is_valid_irq_number(uint8_t irq);
bool config_check_io_conflict(uint16_t io1, uint16_t io2);
bool config_check_irq_conflict(uint8_t irq1, uint8_t irq2);
bool config_cpu_supports_busmaster(void);
int config_parse_route_entry(const char* route_str, ip_route_entry_t* route);
int config_validate_cross_parameters(const config_t* config);

/* Bus mastering auto-configuration functions */
int config_perform_busmaster_auto_test(config_t *config, nic_context_t *ctx, bool quick_mode);
int apply_busmaster_configuration(nic_context_t *ctx, 
                                const busmaster_test_results_t *results,
                                config_t *config);
int generate_busmaster_test_report(const busmaster_test_results_t *results,
                                 char *buffer, size_t buffer_size);

/* Global configuration */
extern config_t __far g_config;
extern bool g_config_loaded;

/* Enhanced function prototypes */
int config_parse_params(const char *params, config_t *config);
int config_validate(const config_t *config);
int config_get_defaults(config_t *config);
void config_print(const config_t *config, int level);

/* New configuration functions */
int config_init(void);
void config_cleanup(void);
void config_set_defaults(config_t *config);
int config_load_file(const char *filename, config_t *config);
int config_save_file(const char *filename, const config_t *config);
int config_parse_command_line(int argc, char *argv[], config_t *config);
int config_parse_parameter(const char *param, const char *value, config_t *config);
void config_print_usage(const char *program_name);
void config_print_help(void);
int config_set_string(config_t *config, const char *name, const char *value);
int config_set_int(config_t *config, const char *name, int value);
int config_set_bool(config_t *config, const char *name, bool value);
const char* config_get_string(const config_t *config, const char *name);
int config_get_int(const config_t *config, const char *name);
bool config_get_bool(const config_t *config, const char *name);
int config_detect_hardware(config_t *config);
int config_validate_hardware(const config_t *config);
int config_apply_hardware_settings(const config_t *config);
bool config_is_valid_io_base(uint16_t io_base);
bool config_is_valid_irq(uint8_t irq);
bool config_is_valid_mac(const uint8_t *mac);
int config_parse_mac_address(const char *mac_str, uint8_t *mac);
int config_format_mac_address(const uint8_t *mac, char *mac_str, size_t size);
void config_print_current(void);
void config_print_defaults(void);

#ifdef __cplusplus
}
#endif

#endif /* _CONFIG_H_ */
