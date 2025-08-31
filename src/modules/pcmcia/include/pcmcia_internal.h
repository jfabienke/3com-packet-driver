/**
 * @file pcmcia_internal.h
 * @brief Internal definitions for PCMCIA.MOD - Hybrid Card Services
 *
 * PCMCIA.MOD - Minimal Card Services implementation optimized for 3Com cards
 * Provides 87-90% memory savings over traditional DOS Card Services
 */

#ifndef PCMCIA_INTERNAL_H
#define PCMCIA_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

/* Module version and identification */
#define PCMCIA_MOD_VERSION_MAJOR    1
#define PCMCIA_MOD_VERSION_MINOR    0
#define PCMCIA_MOD_SIGNATURE        "3COM"

/* Maximum number of sockets supported */
#define MAX_PCMCIA_SOCKETS         4

/* CIS (Card Information Structure) definitions */

/* CIS Tuple types (only those we need for 3Com cards) */
#define CISTPL_END              0xFF
#define CISTPL_NULL             0x00
#define CISTPL_DEVICE           0x01
#define CISTPL_CHECKSUM         0x10
#define CISTPL_LONGLINK_A       0x11
#define CISTPL_LONGLINK_C       0x12
#define CISTPL_NO_LINK          0x14
#define CISTPL_VERS_1           0x15
#define CISTPL_ALTSTR           0x16
#define CISTPL_DEVICE_A         0x17
#define CISTPL_MANFID           0x20
#define CISTPL_FUNCID           0x21
#define CISTPL_FUNCE            0x22
#define CISTPL_CONFIG           0x1A
#define CISTPL_CFTABLE_ENTRY    0x1B

/* Function types */
#define CISTPL_FUNCID_NETWORK   0x06

/* 3Com manufacturer ID */
#define MANFID_3COM             0x0101

/* CIS parsing structures */
typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t data[1];  /* Variable length */
} __attribute__((packed)) tuple_header_t;

typedef struct {
    uint16_t manufacturer_id;
    uint16_t product_id;
} __attribute__((packed)) cistpl_manfid_t;

typedef struct {
    uint8_t function_type;
    uint8_t system_init_mask;
} __attribute__((packed)) cistpl_funcid_t;

typedef struct {
    uint8_t size_mask;
    uint8_t last_index;
    uint32_t config_base;
    uint8_t config_mask[1];  /* Variable length */
} __attribute__((packed)) cistpl_config_t;

/* Configuration table entry structure */
typedef struct {
    uint8_t index;
    uint8_t interface_type;
    uint8_t feature_selection;
    uint16_t io_base;
    uint16_t io_size;
    uint8_t io_ranges;
    uint16_t irq_mask;
    uint8_t mem_ranges;
    uint32_t mem_base;
    uint32_t mem_size;
} config_entry_t;

/* 3Com card types */
typedef enum {
    CARD_UNKNOWN = 0,
    
    /* 3C589 PCMCIA series */
    CARD_3C589 = 1,
    CARD_3C589B = 2,
    CARD_3C589C = 3,
    CARD_3C589D = 4,
    
    /* 3C562 LAN+Modem combo */
    CARD_3C562 = 5,
    CARD_3C562B = 6,
    
    /* 3C574 Fast EtherLink */
    CARD_3C574 = 7,
    
    /* 3C575 CardBus series */
    CARD_3C575 = 8,
    CARD_3C575C = 9,
    
    CARD_MAX_TYPE = 10
} card_type_t;

/* CIS signature for supported cards */
typedef struct {
    uint16_t manufacturer_id;
    uint16_t product_id;
    const char *name;
    card_type_t card_type;
} cis_signature_t;

/* Parsed CIS information for 3Com cards */
typedef struct {
    uint16_t manufacturer_id;    /* Should be 0x0101 (3Com) */
    uint16_t product_id;        /* Card-specific ID */
    char product_name[32];      /* e.g., "3Com 3C589" */
    uint8_t function_type;      /* Network interface (0x06) */
    uint16_t io_base_hint;      /* Preferred I/O base */
    uint8_t irq_mask;           /* Supported IRQs */
    config_entry_t configs[4];  /* Configuration options */
    uint8_t config_count;       /* Number of valid configs */
    card_type_t card_type;      /* Detected card type */
} cis_3com_info_t;

/* Socket Services definitions */

/* Socket Services function codes */
#define SS_GET_ADAPTER_COUNT    0x80
#define SS_GET_SOCKET_COUNT     0x81
#define SS_GET_SOCKET_INFO      0x82
#define SS_SET_SOCKET          0x83
#define SS_GET_SOCKET          0x84
#define SS_RESET_SOCKET        0x85
#define SS_INQUIRE_ADAPTER     0x86
#define SS_INQUIRE_SOCKET      0x87
#define SS_GET_WINDOW          0x88
#define SS_SET_WINDOW          0x89
#define SS_GET_PAGE            0x8A
#define SS_SET_PAGE            0x8B
#define SS_REGISTER_CALLBACK   0x8C

/* Socket Services return codes */
#define SS_SUCCESS              0x00
#define SS_BAD_ADAPTER          0x01
#define SS_BAD_ATTRIBUTE        0x02
#define SS_BAD_BASE             0x03
#define SS_BAD_EDC              0x04
#define SS_BAD_IRQ              0x06
#define SS_BAD_OFFSET           0x07
#define SS_BAD_PAGE             0x08
#define SS_READ_FAILURE         0x09
#define SS_BAD_SIZE             0x0A
#define SS_BAD_SOCKET           0x0B
#define SS_BAD_TYPE             0x0C
#define SS_BAD_VCC              0x0D
#define SS_BAD_VPP              0x0E
#define SS_NO_CARD              0x14
#define SS_UNSUPPORTED_MODE     0x15
#define SS_UNSUPPORTED_VOLTAGE  0x16
#define SS_WRITE_FAILURE        0x17

/* Socket Services request structure */
typedef struct {
    uint16_t function;
    uint16_t socket;
    void far *buffer;
    uint16_t attributes;
} socket_services_req_t;

/* Socket status bits */
#define SOCKET_STATUS_CARD_DETECT   0x01
#define SOCKET_STATUS_READY_CHANGE  0x02
#define SOCKET_STATUS_BATTERY_WARN  0x04
#define SOCKET_STATUS_BATTERY_DEAD  0x08
#define SOCKET_STATUS_WRITE_PROTECT 0x10
#define SOCKET_STATUS_CARD_LOCK     0x20

/* Point Enabler definitions */

/* PCMCIA controller types */
typedef enum {
    CONTROLLER_UNKNOWN = 0,
    CONTROLLER_82365 = 1,      /* Intel 82365SL */
    CONTROLLER_CIRRUS = 2,     /* Cirrus Logic */
    CONTROLLER_VADEM = 3,      /* Vadem */
    CONTROLLER_RICOH = 4       /* Ricoh */
} controller_type_t;

/* Intel 82365 register definitions */
#define PCIC_ID_REVISION        0x00
#define PCIC_STATUS             0x01
#define PCIC_POWER_CONTROL      0x02
#define PCIC_INT_GEN_CTRL       0x03
#define PCIC_CARD_STATUS        0x04
#define PCIC_CARD_CHANGE        0x05
#define PCIC_IO_WIN0_START_LOW  0x08
#define PCIC_IO_WIN0_START_HIGH 0x09
#define PCIC_IO_WIN0_END_LOW    0x0A
#define PCIC_IO_WIN0_END_HIGH   0x0B
#define PCIC_IO_WIN1_START_LOW  0x0C
#define PCIC_IO_WIN1_START_HIGH 0x0D
#define PCIC_IO_WIN1_END_LOW    0x0E
#define PCIC_IO_WIN1_END_HIGH   0x0F

/* Status register bits */
#define PCIC_STATUS_CD1         0x01
#define PCIC_STATUS_CD2         0x02
#define PCIC_STATUS_READY       0x20
#define PCIC_STATUS_WP          0x10
#define PCIC_STATUS_POWER       0x40

/* Power control bits */
#define PCIC_POWER_OFF          0x00
#define PCIC_POWER_VCC_5V       0x10
#define PCIC_POWER_VCC_3V       0x18
#define PCIC_POWER_AUTO         0x20
#define PCIC_POWER_OUTPUT       0x80

/* Socket information structure */
typedef struct {
    uint8_t socket_id;
    controller_type_t controller_type;
    uint16_t controller_base;
    uint8_t status;
    uint8_t flags;
    card_type_t inserted_card;
    cis_3com_info_t cis_info;
} socket_info_t;

/* Point Enabler context */
typedef struct {
    uint16_t io_base;           /* Controller I/O base (e.g., 0x3E0) */
    controller_type_t controller_type;
    uint8_t socket_count;       /* Number of sockets */
    socket_info_t sockets[MAX_PCMCIA_SOCKETS];
} point_enabler_context_t;

/* Resource management */

/* Resource allocation structure */
typedef struct {
    uint16_t io_base;
    uint8_t irq;
    uint32_t mem_base;
    uint16_t mem_size;
    uint8_t config_index;
    uint8_t socket;
} resource_allocation_t;

/* Resource tracking */
typedef struct {
    uint16_t io_ranges_used;    /* Bit mask of used I/O ranges */
    uint8_t irq_used;          /* Bit mask of used IRQs */
    uint32_t mem_used;         /* Memory ranges in use */
} resource_tracker_t;

/* Hot-plug event handling */

typedef struct pcmcia_context pcmcia_context_t;

/* Event handler function types */
typedef void (*card_inserted_handler_t)(uint8_t socket);
typedef void (*card_removed_handler_t)(uint8_t socket);
typedef void (*status_changed_handler_t)(uint8_t socket, uint8_t status);

/* Event handlers structure */
typedef struct {
    card_inserted_handler_t card_inserted;
    card_removed_handler_t card_removed;
    status_changed_handler_t status_changed;
} pcmcia_event_handlers_t;

/* Main PCMCIA context */
struct pcmcia_context {
    /* Socket Services or Point Enabler mode */
    bool socket_services_available;
    point_enabler_context_t point_enabler;
    
    /* Socket management */
    uint8_t socket_count;
    socket_info_t *sockets;
    uint8_t socket_status[MAX_PCMCIA_SOCKETS];
    
    /* Resource management */
    resource_tracker_t resources;
    
    /* Event handling */
    pcmcia_event_handlers_t event_handlers;
    void (__interrupt *prev_interrupt_handler)(void);
    
    /* Statistics */
    struct {
        uint32_t cards_inserted;
        uint32_t cards_removed;
        uint32_t cis_parse_errors;
        uint32_t resource_allocation_failures;
    } stats;
};

/* Error codes */
typedef enum {
    PCMCIA_SUCCESS = 0,
    PCMCIA_ERR_NO_SOCKETS = -1,
    PCMCIA_ERR_NO_CONTROLLER = -2,
    PCMCIA_ERR_NO_RESOURCES = -3,
    PCMCIA_ERR_CIS_PARSE = -4,
    PCMCIA_ERR_NOT_3COM = -5,
    PCMCIA_ERR_UNSUPPORTED = -6,
    PCMCIA_ERR_HARDWARE = -7,
    PCMCIA_ERR_CONFIG = -8,
    PCMCIA_ERR_INVALID_PARAM = -9,
    PCMCIA_ERR_MEMORY = -10
} pcmcia_error_t;

/* Function prototypes */

/* CIS parsing functions */
int parse_3com_cis(uint8_t socket, cis_3com_info_t *info);
int validate_3com_card(cis_3com_info_t *info);
uint8_t *map_attribute_memory(uint8_t socket, uint32_t offset, uint32_t size);
void unmap_attribute_memory(uint8_t *mapped_ptr);

/* Socket Services interface */
int call_socket_services(socket_services_req_t *req);
int pcmcia_detect_sockets(void);
uint8_t get_socket_status(uint8_t socket);

/* Point Enabler functions */
int init_point_enabler_mode(void);
int detect_intel_82365(uint16_t io_base);
int detect_cirrus_logic(uint16_t io_base);
int detect_vadem(uint16_t io_base);
int detect_controller_sockets(point_enabler_context_t *ctx);
uint8_t pcic_read_reg(uint16_t io_base, uint8_t socket, uint8_t reg);
void pcic_write_reg(uint16_t io_base, uint8_t socket, uint8_t reg, uint8_t value);
const char* controller_type_name(controller_type_t type);

/* Resource management functions */
int allocate_card_resources(uint8_t socket, cis_3com_info_t *cis_info, 
                           resource_allocation_t *resources);
void free_card_resources(uint8_t socket, resource_allocation_t *resources);
uint16_t allocate_io_range(uint16_t preferred, uint16_t size);
uint8_t allocate_irq_from_mask(uint8_t irq_mask);
uint32_t allocate_memory_range(uint32_t size);
bool is_io_range_available(uint16_t base, uint16_t size);
bool is_irq_available(uint8_t irq);
void mark_io_range_used(uint16_t base, uint16_t size);
void mark_irq_used(uint8_t irq);
void free_io_range(uint16_t base);
void free_irq(uint8_t irq);

/* Event handling functions */
int register_pcmcia_events(pcmcia_event_handlers_t *handlers);
void handle_card_insertion(uint8_t socket);
void handle_card_removal(uint8_t socket);
void __interrupt pcmcia_card_status_isr(void);
void enable_card_status_interrupts(void);
void acknowledge_pcmcia_interrupt(void);
void chain_interrupt(void);

/* Card configuration functions */
int configure_card(uint8_t socket, resource_allocation_t *resources, 
                   cis_3com_info_t *cis_info);

/* Utility functions */
const char* pcmcia_error_string(pcmcia_error_t error);
const char* card_type_name(card_type_t type);
void delay_ms(uint16_t milliseconds);
void log_info(const char *format, ...);
void log_error(const char *format, ...);
void log_debug(const char *format, ...);

/* Integration functions for NIC modules */
int initialize_ptask_pcmcia(uint8_t socket, resource_allocation_t *resources);
int initialize_boomtex_cardbus(uint8_t socket, resource_allocation_t *resources);

/* Global context */
extern pcmcia_context_t g_pcmcia_context;
extern const cis_signature_t supported_3com_cards[];
extern uint8_t max_sockets;

#endif /* PCMCIA_INTERNAL_H */