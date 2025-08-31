/**
 * @file hardware_mock.h
 * @brief Hardware mocking interface for testing network card drivers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This header provides a comprehensive mocking system that simulates
 * hardware behavior for testing network card drivers without requiring
 * actual NICs. It supports both the 3C509B ISA card with PIO operations
 * and the 3C515-TX with bus mastering DMA capabilities.
 *
 * NOTE: This file is ONLY for testing purposes and should not be included
 * in production builds. It requires -DTESTING compilation flag.
 */

#ifndef _HARDWARE_MOCK_H_
#define _HARDWARE_MOCK_H_

/* This entire file is only available in testing builds */
#ifdef TESTING

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "3c509b.h"
#include "3c515.h"
#include <stdint.h>
#include <stdbool.h>

/* Maximum number of mock devices that can be created */
#define MAX_MOCK_DEVICES        8
#define MAX_MOCK_MEMORY_SIZE    (64 * 1024)    /* 64KB mock memory per device */
#define MAX_MOCK_PACKETS        32              /* Maximum packets in queue */
#define MAX_EEPROM_SIZE         256             /* EEPROM simulation size */

/* Mock device types */
typedef enum {
    MOCK_DEVICE_NONE = 0,
    MOCK_DEVICE_3C509B,
    MOCK_DEVICE_3C515,
    MOCK_DEVICE_GENERIC
} mock_device_type_t;

/* Mock I/O operation types */
typedef enum {
    MOCK_IO_READ_BYTE = 0,
    MOCK_IO_READ_WORD,
    MOCK_IO_READ_DWORD,
    MOCK_IO_WRITE_BYTE,
    MOCK_IO_WRITE_WORD,
    MOCK_IO_WRITE_DWORD
} mock_io_operation_t;

/* Mock interrupt types */
typedef enum {
    MOCK_INTR_NONE = 0,
    MOCK_INTR_TX_COMPLETE,
    MOCK_INTR_RX_COMPLETE,
    MOCK_INTR_ADAPTER_FAILURE,
    MOCK_INTR_DMA_COMPLETE,
    MOCK_INTR_LINK_CHANGE
} mock_interrupt_type_t;

/* Mock error injection types */
typedef enum {
    MOCK_ERROR_NONE = 0,
    MOCK_ERROR_TX_TIMEOUT,
    MOCK_ERROR_TX_UNDERRUN,
    MOCK_ERROR_RX_OVERRUN,
    MOCK_ERROR_CRC_ERROR,
    MOCK_ERROR_FRAME_ERROR,
    MOCK_ERROR_DMA_ERROR,
    MOCK_ERROR_ADAPTER_FAILURE
} mock_error_type_t;

/* Mock packet structure for simulation */
typedef struct {
    uint8_t data[1600];     /* Packet data */
    size_t length;          /* Packet length */
    uint32_t timestamp;     /* Injection timestamp */
    uint16_t status;        /* Packet status flags */
    bool valid;             /* Packet is valid */
} mock_packet_t;

/* Mock register state */
typedef struct {
    uint16_t registers[32]; /* Register values */
    uint8_t current_window; /* Current register window */
    uint16_t status_reg;    /* Status register */
    uint16_t command_reg;   /* Last command sent */
    bool cmd_busy;          /* Command in progress */
} mock_register_state_t;

/* Mock EEPROM simulation */
typedef struct {
    uint16_t data[MAX_EEPROM_SIZE]; /* EEPROM contents */
    uint8_t last_address;           /* Last accessed address */
    bool write_enabled;             /* Write enable state */
    uint32_t read_delay_us;         /* Read delay simulation */
} mock_eeprom_t;

/* Mock DMA simulation for 3C515 */
typedef struct {
    uint32_t tx_desc_base;          /* TX descriptor base address */
    uint32_t rx_desc_base;          /* RX descriptor base address */
    uint32_t current_tx_desc;       /* Current TX descriptor */
    uint32_t current_rx_desc;       /* Current RX descriptor */
    bool dma_in_progress;           /* DMA transfer active */
    mock_interrupt_type_t pending_interrupt; /* Pending DMA interrupt */
} mock_dma_state_t;

/* Mock device state */
typedef struct {
    mock_device_type_t type;        /* Device type */
    uint16_t io_base;               /* I/O base address */
    uint8_t irq;                    /* IRQ number */
    uint8_t mac_address[6];         /* MAC address */
    
    /* Device state */
    bool enabled;                   /* Device enabled */
    bool link_up;                   /* Link status */
    uint16_t link_speed;            /* Link speed (10/100) */
    bool full_duplex;               /* Duplex mode */
    bool promiscuous;               /* Promiscuous mode */
    
    /* Register simulation */
    mock_register_state_t registers;
    
    /* EEPROM simulation */
    mock_eeprom_t eeprom;
    
    /* DMA simulation (3C515 only) */
    mock_dma_state_t dma;
    
    /* Packet queues */
    mock_packet_t tx_queue[MAX_MOCK_PACKETS];
    mock_packet_t rx_queue[MAX_MOCK_PACKETS];
    uint16_t tx_queue_head, tx_queue_tail;
    uint16_t rx_queue_head, rx_queue_tail;
    
    /* Error injection */
    mock_error_type_t injected_error;
    uint32_t error_trigger_count;
    uint32_t operation_count;
    
    /* Statistics */
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t interrupts_generated;
    
    /* Memory simulation */
    uint8_t memory[MAX_MOCK_MEMORY_SIZE];
} mock_device_t;

/* Mock I/O operation log entry */
typedef struct {
    mock_io_operation_t operation;
    uint16_t port;
    uint32_t value;
    uint32_t timestamp;
    uint8_t device_id;
} mock_io_log_entry_t;

/* Mock framework state */
typedef struct {
    mock_device_t devices[MAX_MOCK_DEVICES];
    uint8_t device_count;
    bool logging_enabled;
    bool strict_mode;               /* Fail on undefined behavior */
    mock_io_log_entry_t io_log[1024];
    uint16_t io_log_head;
    uint32_t global_timestamp;
} mock_framework_t;

/* Global mock framework instance */
extern mock_framework_t g_mock_framework;

/* Hardware mock framework functions */
int mock_framework_init(void);
void mock_framework_cleanup(void);
void mock_framework_reset(void);

/* Mock device management */
int mock_device_create(mock_device_type_t type, uint16_t io_base, uint8_t irq);
int mock_device_destroy(uint8_t device_id);
mock_device_t* mock_device_get(uint8_t device_id);
mock_device_t* mock_device_find_by_io(uint16_t io_base);

/* Device configuration */
int mock_device_set_mac_address(uint8_t device_id, const uint8_t *mac);
int mock_device_set_link_status(uint8_t device_id, bool link_up, uint16_t speed);
int mock_device_set_promiscuous(uint8_t device_id, bool enable);
int mock_device_enable(uint8_t device_id, bool enable);

/* EEPROM simulation */
int mock_eeprom_init(uint8_t device_id, const uint16_t *initial_data, size_t size);
uint16_t mock_eeprom_read(uint8_t device_id, uint8_t address);
int mock_eeprom_write(uint8_t device_id, uint8_t address, uint16_t data);

/* Packet injection and simulation */
int mock_packet_inject_rx(uint8_t device_id, const uint8_t *packet, size_t length);
int mock_packet_extract_tx(uint8_t device_id, uint8_t *packet, size_t *length);
int mock_packet_queue_count_rx(uint8_t device_id);
int mock_packet_queue_count_tx(uint8_t device_id);
void mock_packet_queue_clear(uint8_t device_id);

/* Error injection */
int mock_error_inject(uint8_t device_id, mock_error_type_t error, uint32_t trigger_count);
void mock_error_clear(uint8_t device_id);

/* Interrupt simulation */
int mock_interrupt_generate(uint8_t device_id, mock_interrupt_type_t type);
bool mock_interrupt_pending(uint8_t device_id);
mock_interrupt_type_t mock_interrupt_get_type(uint8_t device_id);
void mock_interrupt_clear(uint8_t device_id);

/* DMA simulation (3C515 specific) */
int mock_dma_set_descriptors(uint8_t device_id, uint32_t tx_base, uint32_t rx_base);
int mock_dma_start_transfer(uint8_t device_id, bool is_tx);
bool mock_dma_is_active(uint8_t device_id);

/* Statistics and monitoring */
typedef struct {
    uint32_t total_io_operations;
    uint32_t read_operations;
    uint32_t write_operations;
    uint32_t packets_injected;
    uint32_t packets_extracted;
    uint32_t interrupts_generated;
    uint32_t errors_injected;
} mock_statistics_t;

int mock_get_statistics(mock_statistics_t *stats);
int mock_get_device_statistics(uint8_t device_id, mock_statistics_t *stats);
void mock_reset_statistics(void);

/* I/O operation mocking - these replace real hardware I/O */
uint8_t mock_inb(uint16_t port);
uint16_t mock_inw(uint16_t port);
uint32_t mock_inl(uint16_t port);
void mock_outb(uint16_t port, uint8_t value);
void mock_outw(uint16_t port, uint16_t value);
void mock_outl(uint16_t port, uint32_t value);

/* I/O operation logging */
void mock_io_log_enable(bool enable);
bool mock_io_log_is_enabled(void);
int mock_io_log_get_entries(mock_io_log_entry_t *entries, int max_entries);
void mock_io_log_clear(void);

/* Helper functions for device-specific behavior */
int mock_3c509b_simulate_window_select(mock_device_t *device, uint8_t window);
int mock_3c509b_simulate_eeprom_read(mock_device_t *device, uint8_t address);
int mock_3c509b_simulate_tx_operation(mock_device_t *device, const uint8_t *data, size_t length);
int mock_3c509b_simulate_rx_operation(mock_device_t *device);

int mock_3c515_simulate_dma_setup(mock_device_t *device, uint32_t desc_base, bool is_tx);
int mock_3c515_simulate_dma_transfer(mock_device_t *device, bool is_tx);
int mock_3c515_simulate_descriptor_update(mock_device_t *device);

/* Test scenario helpers */
typedef struct {
    const char *name;
    int (*setup)(uint8_t device_id);
    int (*execute)(uint8_t device_id);
    int (*verify)(uint8_t device_id);
    int (*cleanup)(uint8_t device_id);
} mock_test_scenario_t;

int mock_scenario_run(const mock_test_scenario_t *scenario, uint8_t device_id);

/* Pre-defined test scenarios */
extern const mock_test_scenario_t mock_scenario_basic_init;
extern const mock_test_scenario_t mock_scenario_packet_tx_rx;
extern const mock_test_scenario_t mock_scenario_error_injection;
extern const mock_test_scenario_t mock_scenario_link_state_change;
extern const mock_test_scenario_t mock_scenario_dma_stress_test;

/* Validation and debugging */
int mock_validate_device_state(uint8_t device_id);
int mock_validate_register_access(uint8_t device_id, uint16_t reg, bool is_write);
void mock_dump_device_state(uint8_t device_id);
void mock_dump_io_log(void);

/* Configuration macros for test builds */
#ifdef TESTING
#define inb(port)       mock_inb(port)
#define inw(port)       mock_inw(port)
#define inl(port)       mock_inl(port)
#define outb(port, val) mock_outb(port, val)
#define outw(port, val) mock_outw(port, val)
#define outl(port, val) mock_outl(port, val)
#endif

#ifdef __cplusplus
}
#endif

#endif /* TESTING */

#endif /* _HARDWARE_MOCK_H_ */