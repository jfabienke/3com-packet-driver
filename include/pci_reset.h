/**
 * @file pci_reset.h
 * @brief PCI device reset and interrupt management functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Last updated: 2026-01-24 (auto-generated)
 */

#ifndef _PCI_RESET_H_
#define _PCI_RESET_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RESET_SUCCESS = 0,
    RESET_TIMEOUT = -1,
    RESET_FAILED = -2,
    RESET_PARTIAL = -3,
    RESET_NOT_NEEDED = 1
} reset_status_t;

bool pci_enable_intx_interrupts(uint8_t bus, uint8_t device, uint8_t function);
reset_status_t pci_reset_device(uint8_t bus, uint8_t device, uint8_t function, uint16_t iobase);
bool pci_enable_bus_mastering(uint8_t bus, uint8_t device, uint8_t function, uint16_t iobase);
const char* pci_reset_status_string(reset_status_t status);

#endif /* _PCI_RESET_H_ */
