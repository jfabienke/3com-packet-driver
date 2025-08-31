/**
 * @file pnp.h
 * @brief ISA Plug and Play (ISAPnP) definitions for 3Com NIC driver
 *
 * 3Com Packet Driver - ISAPnP Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Information derived from:
 *   - isapnp.c (from iPXE project) - Primary source for ISAPnP protocol
 *   - 3c509.asm (Nestor's 8086 packet driver for 3C509B)
 *   - drivers/net/ethernet/3com/3c509.c (Linux driver for 3C509B)
 *   - drivers/net/ethernet/3com/3c515.c (Linux driver for 3C515-TX)
 *
 * This file provides constants, tag definitions, and structures for
 * interacting with ISA Plug and Play (ISAPnP) devices, specifically tailored
 * to support the minimal PnP manager for 3Com 3C515-TX and 3C509B NICs.
 * It enables automatic detection and resource assignment (I/O addresses,
 * IRQs) as per the driver’s requirements.
 */

#ifndef _PNP_H_
#define _PNP_H_

#include <common.h>  // Assuming uint8_t, uint16_t, uint32_t, etc. are defined here

#ifdef __cplusplus
extern "C" {
#endif

// --- ISAPnP Constants ---
// Derived from iPXE isapnp.c and cross-referenced with 3Com/Linux sources
#define ISAPNP_ADDRESS          0x279  // ISAPnP Address Port (default write port)
                                       // Actual write port is (address | 0x3) after each write
#define ISAPNP_WRITE_DATA       0xA79  // ISAPnP Write Data Port (default)
                                       // Actual write port is 0x279 + (address >> 2)
#define ISAPNP_READ_PORT        0x203  // Default ISAPnP read port address

#define ISAPNP_SERIALISOLATION  0x6A   // Initiate serial isolation
#define ISAPNP_CONFIGCONTROL    0x02   // Configuration control register
#define ISAPNP_CONFIG_WAIT_FOR_KEY (0 << 3) // Enter Wait for Key state
#define ISAPNP_CONFIG_RESET_CSN   (1 << 3)  // Reset Card Select Number
#define ISAPNP_WAKE             0x03   // Wake card with Card Select Number (CSN)
#define ISAPNP_CARDSELECTNUMBER 0x06   // Card Select Number register
#define ISAPNP_LOGICALDEVICENUMBER 0x07 // Logical Device Number register
#define ISAPNP_ACTIVATE         0x30   // Activate/Deactivate logical device
#define ISAPNP_IOBASE(index)    (0x60 + (index * 2)) // I/O Base Address registers
#define ISAPNP_IRQNO(index)     (0x70 + index)      // IRQ Number registers
#define ISAPNP_RESOURCEDATA     0x74   // Resource Data register
#define ISAPNP_STATUS           0x74   // Status (same register as RESOURCEDATA)
#define ISAPNP_READ_PORT_START  0x203  // First possible read port address
#define ISAPNP_READ_PORT_MAX    0x3FF  // Last possible read port address
#define ISAPNP_READ_PORT_STEP   0x04   // Increment for read port addresses

// --- ISAPnP Tag Definitions ---
// Used to parse resource data during PnP configuration
#define ISAPNP_TAG_END          0x79   // End of resource data
#define ISAPNP_TAG_LOGDEVID     0x43   // Logical Device ID
#define ISAPNP_TAG_COMPATDEVID  0x38   // Compatible Device ID
#define ISAPNP_TAG_IRQ          0x42   // IRQ resource
#define ISAPNP_TAG_DMA          0x41   // DMA resource
#define ISAPNP_TAG_START_DEP    0x22   // Start dependent functions
#define ISAPNP_TAG_END_DEP      0x23   // End dependent functions
#define ISAPNP_TAG_IO_RANGE     0x47   // I/O range resource
#define ISAPNP_TAG_MEM_RANGE    0x48   // Memory range resource
#define ISAPNP_TAG_ANSI_IDENT   0x63   // ANSI identifier string
#define ISAPNP_TAG_UNICODE_IDENT 0x73  // Unicode identifier string
#define ISAPNP_TAG_VENDOR_DEF   0x70   // Vendor-defined tag

// Small Tag Parsing Macros
#define ISAPNP_TAG_SMALL_BITS     0xF8  // Mask for small tag type bits
#define ISAPNP_TAG_SMALL_NAME_BITS 0x78 // Mask for small tag name
#define ISAPNP_TAG_SMALL_RES_TYPE 0x04  // Resource type bit
#define ISAPNP_TAG_SMALL_END      0x00  // End tag indicator
#define ISAPNP_IS_SMALL_TAG(tag) \
    (((tag) & ISAPNP_TAG_SMALL_BITS) != 0)  // Check if tag is small
#define ISAPNP_SMALL_TAG_NAME(tag) \
    ((tag) & ISAPNP_TAG_SMALL_NAME_BITS)    // Extract small tag name
#define ISAPNP_SMALL_TAG_LEN(tag) \
    ((tag) & ~ISAPNP_TAG_SMALL_BITS)        // Extract small tag length

// Large Tag Parsing Macros
#define ISAPNP_TAG_LARGE_NAME_BITS 0x7F  // Mask for large tag name
#define ISAPNP_IS_LARGE_TAG(tag) \
    (((tag) & ~ISAPNP_TAG_LARGE_NAME_BITS) == 0)  // Check if tag is large
#define ISAPNP_LARGE_TAG_NAME(tag) \
    ((tag) & ISAPNP_TAG_LARGE_NAME_BITS)          // Extract large tag name

// Linear Feedback Shift Register (LFSR) Seed for Serial Isolation
#define ISAPNP_LFSR_SEED    0x6A  // Initial seed value for ISAPnP isolation

// --- ISAPnP Structures ---

/**
 * @struct isapnp_identifier
 * @brief ISAPnP card identifier structure
 *
 * Contains the serial identification data for an ISAPnP card, used during
 * the isolation process to uniquely identify devices.
 */
struct isapnp_identifier {
    uint8_t serial_id[9];  // Serial identification: 4-byte vendor ID,
                           // 4-byte serial number, 1-byte checksum
} __attribute__((packed)); // Packed for direct I/O reading

/**
 * @struct isapnp_logdevid
 * @brief ISAPnP logical device ID structure
 *
 * Defines the logical device identifier for an ISAPnP card, used to
 * distinguish devices within a card during configuration.
 */
struct isapnp_logdevid {
    uint8_t  type;        // Should always be 0x43 (logical device tag)
    uint16_t signature;   // Signature field (often unused)
    uint32_t vendor_id;   // Vendor ID (e.g., 3Com’s 0x10B7)
    uint32_t prod_id;     // Product ID (e.g., 3C509B or 3C515-TX specific)
} __attribute__((packed)); // Packed for direct I/O reading

#ifdef __cplusplus
}
#endif

#endif /* _PNP_H_ */
