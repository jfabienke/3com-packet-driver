/**
 * @file helper_mock_hardware.h
 * @brief Hardware mocking implementation for testing network card drivers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#ifndef HELPER_MOCK_HARDWARE_H
#define HELPER_MOCK_HARDWARE_H

#include "../../include/hardware_mock.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Global mock framework instance */
extern mock_framework_t g_mock_framework;

/* Framework initialization and cleanup */
int mock_framework_init(void);
void mock_framework_cleanup(void);
void mock_framework_reset(void);

/* Device management */
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

/* Packet injection and extraction */
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
void mock_interrupt_clear(uint8_t device_id);

/* I/O port simulation */
uint8_t mock_inb(uint16_t port);
uint16_t mock_inw(uint16_t port);
uint32_t mock_inl(uint16_t port);
void mock_outb(uint16_t port, uint8_t value);
void mock_outw(uint16_t port, uint16_t value);
void mock_outl(uint16_t port, uint32_t value);

/* Statistics and logging */
int mock_get_statistics(mock_statistics_t *stats);
void mock_io_log_enable(bool enable);
bool mock_io_log_is_enabled(void);
void mock_io_log_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* HELPER_MOCK_HARDWARE_H */