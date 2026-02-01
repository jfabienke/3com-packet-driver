/**
 * @file pci_bios.h
 * @brief PCI BIOS INT 1Ah wrapper functions for DOS real mode
 * 
 * Header file for PCI BIOS services that provide real-mode safe
 * PCI configuration space access using INT 1Ah.
 */

#ifndef _PCI_BIOS_H_
#define _PCI_BIOS_H_

/* C89 compatibility - include portability header first */
#include "portabl.h"

/* Try standard headers, fallback to portabl.h types */
#ifdef __WATCOMC__
/* Watcom C89 mode: portabl.h provides types */
#else
#include <stdint.h>
#include <stdbool.h>
#endif

/* Standard PCI configuration register offsets */
#define PCI_VENDOR_ID           0x00    /* 16-bit Vendor ID */
#define PCI_DEVICE_ID           0x02    /* 16-bit Device ID */
#define PCI_COMMAND             0x04    /* 16-bit Command register */
#define PCI_STATUS              0x06    /* 16-bit Status register */
#define PCI_REVISION            0x08    /* 8-bit Revision ID */
#define PCI_PROG_IF             0x09    /* 8-bit Programming Interface */
#define PCI_SUBCLASS            0x0A    /* 8-bit Subclass code */
#define PCI_CLASS               0x0B    /* 8-bit Class code */
#define PCI_CACHE_LINE_SIZE     0x0C    /* 8-bit Cache line size */
#define PCI_LATENCY_TIMER       0x0D    /* 8-bit Latency timer */
#define PCI_HEADER_TYPE         0x0E    /* 8-bit Header type */
#define PCI_BIST                0x0F    /* 8-bit Built-in self test */

/* Base Address Registers */
#define PCI_BAR0                0x10    /* 32-bit BAR 0 */
#define PCI_BAR1                0x14    /* 32-bit BAR 1 */
#define PCI_BAR2                0x18    /* 32-bit BAR 2 */
#define PCI_BAR3                0x1C    /* 32-bit BAR 3 */
#define PCI_BAR4                0x20    /* 32-bit BAR 4 */
#define PCI_BAR5                0x24    /* 32-bit BAR 5 */

/* Other configuration registers */
#define PCI_CARDBUS_CIS         0x28    /* 32-bit CardBus CIS pointer */
#define PCI_SUBSYSTEM_VENDOR    0x2C    /* 16-bit Subsystem vendor ID */
#define PCI_SUBSYSTEM_ID        0x2E    /* 16-bit Subsystem ID */
#define PCI_ROM_BASE            0x30    /* 32-bit Expansion ROM base */
#define PCI_CAP_POINTER         0x34    /* 8-bit Capabilities pointer */
#define PCI_INTERRUPT_LINE      0x3C    /* 8-bit Interrupt line (IRQ) */
#define PCI_INTERRUPT_PIN       0x3D    /* 8-bit Interrupt pin */
#define PCI_MIN_GRANT           0x3E    /* 8-bit Minimum grant */
#define PCI_MAX_LATENCY         0x3F    /* 8-bit Maximum latency */

/* Command register bits */
#define PCI_CMD_IO_ENABLE       0x0001  /* I/O Space Enable */
#define PCI_CMD_MEM_ENABLE      0x0002  /* Memory Space Enable */
#define PCI_CMD_BUS_MASTER      0x0004  /* Bus Master Enable */
#define PCI_CMD_SPECIAL_CYCLE   0x0008  /* Special Cycle Enable */
#define PCI_CMD_MEM_WR_INVAL    0x0010  /* Memory Write & Invalidate */
#define PCI_CMD_VGA_PALETTE     0x0020  /* VGA Palette Snoop */
#define PCI_CMD_PARITY_ERROR    0x0040  /* Parity Error Response */
#define PCI_CMD_WAIT_CYCLE      0x0080  /* Wait Cycle Control */
#define PCI_CMD_SERR_ENABLE     0x0100  /* SERR# Enable */
#define PCI_CMD_FAST_B2B        0x0200  /* Fast Back-to-Back Enable */

/* Status register bits */
#define PCI_STAT_CAP_LIST       0x0010  /* Capabilities List */
#define PCI_STAT_66MHZ          0x0020  /* 66MHz Capable */
#define PCI_STAT_FAST_B2B       0x0080  /* Fast Back-to-Back Capable */
#define PCI_STAT_PARITY_ERROR   0x0100  /* Master Data Parity Error */
#define PCI_STAT_DEVSEL_MASK    0x0600  /* DEVSEL Timing Mask */
#define PCI_STAT_SIG_ABORT      0x0800  /* Signaled Target Abort */
#define PCI_STAT_RCV_ABORT      0x1000  /* Received Target Abort */
#define PCI_STAT_RCV_MASTER     0x2000  /* Received Master Abort */
#define PCI_STAT_SIG_SERR       0x4000  /* Signaled System Error */
#define PCI_STAT_PARITY_DET     0x8000  /* Detected Parity Error */

/* Header type bits */
#define PCI_HEADER_MULTI_FUNC   0x80    /* Multi-function device */
#define PCI_HEADER_TYPE_MASK    0x7F    /* Header type mask */
#define PCI_HEADER_TYPE_NORMAL  0x00    /* Normal device */
#define PCI_HEADER_TYPE_BRIDGE  0x01    /* PCI-to-PCI bridge */
#define PCI_HEADER_TYPE_CARDBUS 0x02    /* CardBus bridge */

/* Class codes */
#define PCI_CLASS_NETWORK       0x02    /* Network controller */
#define PCI_SUBCLASS_ETHERNET   0x00    /* Ethernet controller */

/* 3Com specific vendor ID */
#define PCI_VENDOR_3COM         0x10B7  /* 3Com Corporation */

/* Command register bits (alternative naming) */
#define PCI_CMD_IO              0x0001
#define PCI_CMD_MEMORY          0x0002
#define PCI_CMD_MASTER          0x0004
#define PCI_CMD_PARITY          0x0040
#define PCI_CMD_SERR            0x0100

/* Status register bits (alternative naming) */
#define PCI_STATUS_CAP_LIST         0x0010
#define PCI_STATUS_PARITY           0x0100
#define PCI_STATUS_SIG_TARGET_ABORT 0x0800
#define PCI_STATUS_REC_TARGET_ABORT 0x1000
#define PCI_STATUS_REC_MASTER_ABORT 0x2000
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define PCI_STATUS_DETECTED_PARITY  0x8000

/* Capability registers */
#define PCI_CAPABILITY_LIST     0x34
#define PCI_CAP_LIST_ID         0x00
#define PCI_CAP_LIST_NEXT       0x01

/* Power states */
#define PCI_POWER_D0            0
#define PCI_POWER_D1            1
#define PCI_POWER_D2            2
#define PCI_POWER_D3HOT         3

/* Function prototypes */

/* Basic PCI BIOS detection */
bool pci_bios_present(void);
uint8_t pci_get_last_bus(void);

/* Configuration space access */
uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

bool pci_write_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value);
bool pci_write_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
bool pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

/* Device discovery */
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, uint16_t index,
                    uint8_t *bus, uint8_t *device, uint8_t *function);
bool pci_find_class(uint32_t class_code, uint16_t index,
                   uint8_t *bus, uint8_t *device, uint8_t *function);

/* Device configuration */
bool pci_enable_device(uint8_t bus, uint8_t device, uint8_t function,
                      bool enable_io, bool enable_memory, bool enable_bus_master);
bool pci_read_bar(uint8_t bus, uint8_t device, uint8_t function,
                 uint8_t bar_index, uint32_t *bar_value, bool *is_io);
uint8_t pci_get_irq(uint8_t bus, uint8_t device, uint8_t function);

/* Configuration hardening - production quality setup */
bool pci_set_command_bits(uint8_t bus, uint8_t device, uint8_t function, uint16_t bits);
bool pci_clear_status_bits(uint8_t bus, uint8_t device, uint8_t function);
bool pci_set_cache_line_size(uint8_t bus, uint8_t device, uint8_t function, uint8_t cls);
bool pci_set_latency_timer(uint8_t bus, uint8_t device, uint8_t function, uint8_t latency);
bool pci_device_setup(uint8_t bus, uint8_t device, uint8_t function,
                     bool enable_io, bool enable_mem, bool enable_master);

/* Helper functions for common operations (C89 compatible - no inline keyword) */

/**
 * @brief Check if device exists at given BDF
 */
static bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    return (pci_read_config_word(bus, device, function, PCI_VENDOR_ID) != 0xFFFF);
}

/**
 * @brief Check if device is multi-function
 */
static bool pci_is_multi_function(uint8_t bus, uint8_t device) {
    uint8_t header = pci_read_config_byte(bus, device, 0, PCI_HEADER_TYPE);
    return (header & PCI_HEADER_MULTI_FUNC) ? true : false;
}

/**
 * @brief Build BDF (Bus/Device/Function) byte for BIOS calls
 */
static uint8_t pci_make_bdf(uint8_t device, uint8_t function) {
    return ((device & 0x1F) << 3) | (function & 0x07);
}

/**
 * @brief Extract device number from BDF byte
 */
static uint8_t pci_get_device(uint8_t bdf) {
    return (bdf >> 3) & 0x1F;
}

/**
 * @brief Extract function number from BDF byte
 */
static uint8_t pci_get_function(uint8_t bdf) {
    return bdf & 0x07;
}

#endif /* _PCI_BIOS_H_ */
