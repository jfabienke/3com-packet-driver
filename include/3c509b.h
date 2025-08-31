/**
 * @file 3c509b.h
 * @brief Hardware-specific definitions for the 3Com 3C509B NIC
 *
 * This header provides register offsets, command codes, status bits, timing
 * constants, and operational parameters for the 3Com 3C509B, a 10 Mbps ISA NIC
 * using Programmed I/O. Definitions are derived from 3c509.asm, 3c509.c, 3Com
 * documentation, and disassembly analysis.
 *
 * The 3C509B uses a windowed register interface.  The Command/Status Register
 * (at offset 0x0E from the base I/O address) is *always* accessible.  Writing
 * to this register with the _3C509B_CMD_SELECT_WINDOW command changes the
 * "active window," which determines the meaning of subsequent I/O accesses
 * within a certain offset range.  This header file defines constants for each
 * window and a macro (_3C509B_SELECT_WINDOW) to simplify window selection.
 *
 * Timing constants are critical for managing delays and timeouts during
 * hardware operations, ensuring synchronization with the NIC.  These are based
 * on loop counts observed in the disassembly and, where possible, abstracted
 * into microseconds for portability.  The values presented here are *estimates*
 * based on a limited disassembly and *must* be validated through dynamic
 * analysis (running the driver in a debugger) or a better disassembly.
 *
 * Register definitions are grouped by their associated window to clarify their
 * use and context.  Note that all register offsets are *relative* to the
 * card's base I/O address, which is discovered during driver initialization.
 *
 * Assumptions:
 *   - Real-mode DOS environment.
 *   - Standard types (uint8_t, uint16_t, etc.) are available (assumed to be
 *     provided by <stdint.h> or a similar header).
 *   - I/O port access functions (inb, outb, inw, outw) are available.
 *
 * Dependencies:
 *   - <stdint.h> (or equivalent for standard integer types)
 */

#ifndef _3C509B_H_
#define _3C509B_H_

#include <stdint.h> // For uint8_t, uint16_t, etc.

// --- Function Prototypes (if not provided by common.h) ---
// These are the standard I/O port access functions.  If your environment
// provides these (e.g., through a DOS header file), you can remove these
// prototypes.  If your environment uses different names, adjust accordingly.

void outb(uint16_t port, uint8_t value);
void outw(uint16_t port, uint16_t value);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);

// --- Error Codes ---
typedef enum {
    _3C509B_SUCCESS = 0,
    _3C509B_ERR_NO_CARD,         // Card not detected
    _3C509B_ERR_INIT_FAIL,      // Initialization failed
    _3C509B_ERR_TX_TIMEOUT,      // Transmit timeout
    _3C509B_ERR_TX_ABORTED,      // Transmission aborted (too many collisions, etc.)
    _3C509B_ERR_TX_UNDERRUN,    // Transmit FIFO underrun
    _3C509B_ERR_TX_JABBER,     // Jabber condition
    _3C509B_ERR_RX_OVERRUN,    // Receive overrun
    _3C509B_ERR_RX_CRC,         // CRC error
    _3C509B_ERR_RX_FRAMING,    // Framing error
    _3C509B_ERR_RX_LENGTH,     // Incorrect length field
    _3C509B_ERR_RX_OVERSIZE,    // Packet exceeds MTU
     _3C509B_ERR_INVALID_PACKET,    // Packet with error
    _3C509B_ERR_RX_INCOMPLETE,  // Packet not fully received
    _3C509B_ERR_ADAPTER_FAILURE, // Hardware failure
    _3C509B_ERR_STATS_FULL,       // Statistics full
    _3C509B_ERR_OTHER          // Other error
} _3c509b_error_t;

// --- General Constants ---

#define _3C509B_MANUFACTURER_ID     0x6D50  // 3Com Manufacturer ID from EEPROM
#define _3C509B_PRODUCT_ID_509B     0x5090  // Product ID for 3C509B
#define _3C509B_PRODUCT_ID_MASK     0xF0FF  // Masks off revision nibble
#define _3C509B_MAX_MTU             1514    // Maximum Transmission Unit (bytes)
#define _3C509B_MIN_PACKET_SIZE     60      // Minimum packet size (bytes), excluding CRC
#define _3C509B_BUFFER_SIZE         0x2000  // 8192 bytes (8KB), total buffer space
#define _3C509B_IO_EXTENT           16      // Size of I/O port range (bytes)
#define _3C509B_ID_PORT             0x110   // Default port for NIC detection
#define _3C509B_RESIDENT_MEMORY_SIZE 0x1000 // Memory reserved by the TSR.

// --- Timing Constants ---
// Timing-related constants for hardware synchronization.

#define _3C509B_INIT_DELAY_LOOPS    0x100   // Loop count for initialization delay
                                            // Used during NIC reset and setup.
                                            // Exact time depends on ISA bus speed.
                                            // *VALIDATE THIS VALUE*

#define _3C509B_EEPROM_READ_DELAY   2000    // Microseconds. Delay after EEPROM read.  *ESTIMATE*
#define _3C509B_TX_TIMEOUT_LOOPS    0x1000  // Loop count for transmit timeout. *ESTIMATE*

#define _3C509B_PIT_COUNTER_PORT    0x40    // Programmable Interval Timer port.

// --- Window Definitions ---

#define _3C509B_WINDOW_0  0  // Configuration and EEPROM
#define _3C509B_WINDOW_1  1  // TX/RX operations
#define _3C509B_WINDOW_2  2  // Station address setup
#define _3C509B_WINDOW_4  4  // Media control
#define _3C509B_WINDOW_6  6  // Statistics

// --- Command Register (ALWAYS Accessible) ---
//
// IMPORTANT: The command and status registers share the SAME I/O offset.
// Writes to this offset send commands to the NIC.  Reads from this
// offset retrieve the NIC's status. The NIC's internal logic
// determines whether it's expecting a command or providing status.
// The currently selected window (see _3C509B_SELECT_WINDOW) affects
// the behavior of some commands.
#define _3C509B_COMMAND_REG     0x0E  // Write commands, read status.
#define _3C509B_STATUS_REG      0x0E  // Same as _3C509B_COMMAND_REG

// --- Commands (written to _3C509B_COMMAND_REG) ---
// These commands are *always* written to the _3C509B_COMMAND_REG, but their
// effect depends on the currently selected window.  The _3C509B_CMD_SELECT_WINDOW
// command is used to change the active window.

// Old command definitions removed - using corrected definitions below

// --- Status Bits (read from _3C509B_STATUS_REG) ---
// These bits are read from the _3C509B_STATUS_REG (same address as _3C509B_COMMAND_REG).

#define _3C509B_STATUS_INT_LATCH    0x0001  // Interrupt occurred
#define _3C509B_STATUS_ADAPTER_FAILURE 0x0002 // Hardware failure
#define _3C509B_STATUS_TX_COMPLETE  0x0004  // Transmission completed
#define _3C509B_STATUS_TX_AVAILABLE 0x0008  // TX FIFO has space
#define _3C509B_STATUS_RX_COMPLETE  0x0010  // Packet received
#define _3C509B_STATUS_RX_EARLY     0x0020  // RX early (unused)
#define _3C509B_STATUS_INT_REQ      0x0040  // Interrupt requested
#define _3C509B_STATUS_STATS_FULL   0x0080  // Statistics counters updated
#define _3C509B_STATUS_CMD_BUSY     0x1000  // Command in progress

// --- Window 0: Configuration and EEPROM ---

#define _3C509B_W0_CONFIG_CTRL  0x04  // Configuration control register
#define _3C509B_W0_ADDR_CONFIG  0x06  // Address configuration register (set I/O base)
#define _3C509B_W0_IRQ          0x08  // IRQ setting (bits 12-15)
#define _3C509B_EEPROM_CMD      0x0A  // EEPROM command register
#define _3C509B_EEPROM_DATA     0x0C  // EEPROM data register

// EEPROM Commands (written to _3C509B_EEPROM_CMD)
#define _3C509B_EEPROM_READ     0x80  // Read from EEPROM (OR with address)
#define _3C509B_EEPROM_WRITE    0x40  // Write to EEPROM (not used)
#define _3C509B_EEPROM_ERASE    0xC0  // Erase EEPROM (not used)
#define _3C509B_EEPROM_EWENB    0x30  // Enable EEPROM write/erase (not used)
#define _3C509B_EEPROM_EWDIS    0x00  // Disable EEPROM write/erase (not used)

// --- Window 1: Normal Operation (TX/RX) ---

#define _3C509B_TX_FIFO         0x00  // Transmit FIFO (write packet data here)
#define _3C509B_RX_FIFO         0x00  // Receive FIFO (read packet data here)
#define _3C509B_RX_STATUS       0x08  // RX status register
#define _3C509B_TX_STATUS       0x0B  // TX status register
#define _3C509B_TX_FREE         0x0C  // Free bytes in TX FIFO

// RX Filter Bits (for _3C509B_CMD_SET_RX_FILTER, used in Window 1)
// Based on 3Com 3C509B Reference Manual Table 3-9
#define _3C509B_RX_FILTER_STATION   0x0001  // Accept individual address packets (bit 0)
#define _3C509B_RX_FILTER_MULTICAST 0x0002  // Accept multicast packets (bit 1)
#define _3C509B_RX_FILTER_BROADCAST 0x0004  // Accept broadcast packets (bit 2)
#define _3C509B_RX_FILTER_PROMISCUOUS 0x0008  // Promiscuous mode (bit 3)
// Note: Bits 4-15 are reserved and should be set to 0

// Interrupt Masks (for _3C509B_CMD_SET_INTR_ENB, used in Window 1)
#define _3C509B_IMASK_ADAPTER_FAILURE    _3C509B_STATUS_ADAPTER_FAILURE
#define _3C509B_IMASK_TX_COMPLETE        _3C509B_STATUS_TX_COMPLETE
#define _3C509B_IMASK_TX_AVAILABLE       _3C509B_STATUS_TX_AVAILABLE
#define _3C509B_IMASK_RX_COMPLETE        _3C509B_STATUS_RX_COMPLETE
#define _3C509B_IMASK_RX_EARLY           _3C509B_STATUS_RX_EARLY
#define _3C509B_IMASK_STATS_FULL         _3C509B_STATUS_STATS_FULL
#define _3C509B_IMASK_INT_LATCHED 		_3C509B_STATUS_INT_LATCH	 // An interrupt has been latched
#define _3C509B_IMASK_ALL 			0x00bf // all masks.

// RX_STATUS bits (read from _3C509B_RX_STATUS in Window 1)
#define _3C509B_RXSTAT_INCOMPLETE   0x8000 // Packet not fully received
#define _3C509B_RXSTAT_ERROR        0x4000 // Packet has an error
#define _3C509B_RXSTAT_LEN_MASK     0x07FF // Packet length (lower 11 bits)

// RX Error bits (in upper byte of _3C509B_RX_STATUS in Window 1)
#define _3C509B_RXERR_OVERRUN       0x0000 // DMA or FIFO overrun
#define _3C509B_RXERR_OVERSIZE      0x0800 // Packet exceeds MTU
#define _3C509B_RXERR_DRIBBLE       0x1000 // Extra bits (not an error)
#define _3C509B_RXERR_RUNT          0x1800 // Packet too small
#define _3C509B_RXERR_CRC           0x2800 // CRC mismatch
#define _3C509B_RXERR_FRAMING       0x2000 // Alignment error
#define _3C509B_RXERR_LENGTH        0x1800 // Incorrect length field

// TX_STATUS bits (read from _3C509B_TX_STATUS in Window 1)
// Based on 3Com 3C509B Reference Manual Table 3-11
#define _3C509B_TXSTAT_COMPLETE     0x80  // Transmission complete (bit 7)
#define _3C509B_TXSTAT_INTERRUPT    0x40  // Interrupt on successful completion (bit 6)
#define _3C509B_TXSTAT_JABBER       0x20  // Jabber error (bit 5)
#define _3C509B_TXSTAT_UNDERRUN     0x10  // Transmit underrun (bit 4)
#define _3C509B_TXSTAT_MAX_COLLISIONS 0x08  // Maximum collisions (bit 3)
#define _3C509B_TXSTAT_STATUS_OVERFLOW 0x04  // Status overflow (bit 2)
#define _3C509B_TXSTAT_RX_STATUS_OVERFLOW 0x02  // RX status overflow (bit 1)
#define _3C509B_TXSTAT_RX_OVERRUN   0x01  // Receiver overrun (bit 0)

// TX status error detection masks
#define _3C509B_TXSTAT_ERROR_MASK   0x3F  // Any error condition (bits 5-0)
#define _3C509B_TXSTAT_SERIOUS_ERROR_MASK 0x3C  // Serious errors requiring intervention
#define _3C509B_TXSTAT_OVERFLOW_MASK 0x06  // Status or RX overflow errors

// --- Window 2: Station Address ---
// Offsets 0-5 are used to *write* the MAC address into the NIC.
// This is typically done *once* during initialization, *after*
// reading the MAC address from the EEPROM.

// --- Window 4: Media Control ---

#define _3C509B_MEDIA_CTRL      0x0A  // Media control register
#define _3C509B_W4_NETDIAG      0x06  // Network diagnostics

// Media Control Bits (written to _3C509B_MEDIA_CTRL)
// Based on 3Com 3C509B Reference Manual Table 3-16
#define _3C509B_MEDIA_SQE_DISABLE   0x8000 // Disable SQE for AUI (bit 15)
#define _3C509B_MEDIA_COLLISION_DETECT 0x2000 // Collision detect (bit 13)
#define _3C509B_MEDIA_COLLISION_SOURCE 0x1000 // Collision source (bit 12)
#define _3C509B_MEDIA_UTP_DISABLE   0x0800 // Disable UTP (bit 11)
#define _3C509B_MEDIA_JABBER_GUARD_DISABLE 0x0400 // Disable jabber guard timer (bit 10)
#define _3C509B_MEDIA_GUARD_TIMER_DISABLE 0x0200 // Disable guard timer (bit 9)
#define _3C509B_MEDIA_LINK_BEAT_DISABLE 0x0080 // Disable link beat for TP (bit 7)
#define _3C509B_MEDIA_JABBER_DISABLE 0x0040 // Disable jabber for TP (bit 6)
#define _3C509B_MEDIA_XCVR_MASK     0x003C // Transceiver select mask (bits 5-2)
#define _3C509B_MEDIA_XCVR_SHIFT    2      // Transceiver select bit position

// Transceiver selection values (bits 5-2)
#define _3C509B_XCVR_AUTO           (0x0 << 2)  // Auto-select
#define _3C509B_XCVR_10BASE_T       (0x0 << 2)  // 10BaseT (when auto-select)
#define _3C509B_XCVR_AUI_EXT        (0x1 << 2)  // AUI or External transceiver
#define _3C509B_XCVR_10BASE2        (0x3 << 2)  // 10Base2 (BNC)
#define _3C509B_XCVR_INTERNAL       (0x8 << 2)  // Internal transceiver

// Network Diagnostics Register Bits (_3C509B_W4_NETDIAG)
// Based on 3Com 3C509B Reference Manual Table 3-17
#define _3C509B_NETDIAG_ASIC_REVMASK   0xF000  // ASIC revision mask (bits 15-12)
#define _3C509B_NETDIAG_ASIC_REVSHIFT  12      // ASIC revision bit position
#define _3C509B_NETDIAG_UPPER_BYTES_OK 0x0800  // Upper bytes test OK (bit 11)
#define _3C509B_NETDIAG_STATS_ENABLED  0x0400  // Statistics enabled (bit 10)
#define _3C509B_NETDIAG_RX_ENABLED     0x0200  // Receiver enabled (bit 9)
#define _3C509B_NETDIAG_TX_ENABLED     0x0100  // Transmitter enabled (bit 8)
#define _3C509B_NETDIAG_EXTERNAL_LOOP  0x0080  // External loopback (bit 7)
#define _3C509B_NETDIAG_INTERNAL_LOOP  0x0040  // Internal loopback (bit 6)
#define _3C509B_NETDIAG_FIFO_LOOPBACK  0x0020  // FIFO loopback (bit 5)
#define _3C509B_NETDIAG_MAC_LOOPBACK   0x0010  // MAC loopback (bit 4)
#define _3C509B_NETDIAG_ENDEC_LOOPBACK 0x0008  // ENDEC loopback (bit 3)
// Bits 2-0 are reserved

// --- Window 6: Statistics ---
// Based on 3Com 3C509B Reference Manual Table 3-18

#define _3C509B_W6_CARRIER_LOST    0x00 // Carrier lost during transmission (byte)
#define _3C509B_W6_SQE_ERRORS      0x01 // SQE test errors (byte)
#define _3C509B_W6_MULTIPLE_COLLS  0x02 // Multiple collision frames (byte)
#define _3C509B_W6_SINGLE_COLLS    0x03 // Single collision frames (byte)
#define _3C509B_W6_LATE_COLLS      0x04 // Late collision errors (byte)
#define _3C509B_W6_RX_OVERRUNS     0x05 // Receive overrun errors (byte)
#define _3C509B_W6_GOOD_TX         0x06 // Good frames transmitted (byte)
#define _3C509B_W6_GOOD_RX         0x07 // Good frames received (byte)
#define _3C509B_W6_TX_DEFERRALS    0x08 // Transmit deferrals (byte)
#define _3C509B_W6_RX_OCTETS_LO    0x0A // Receive octets low byte (word)
#define _3C509B_W6_TX_OCTETS_LO    0x0C // Transmit octets low byte (word)

// --- ID Sequence (for non-PnP detection) ---

#define _3C509B_ID_GLOBAL_RESET     0xC0   // Global Reset (to ID port)
#define _3C509B_SET_TAG_REGISTER    0xD0   // Set Tag Register (to ID port)
#define _3C509B_TEST_TAG_REGISTER   0xD8   // Test Tag Register (to ID port)
#define _3C509B_ACTIVATE_AND_SET_IO 0xE0   // Activate and Set I/O (to ID port)
#define _3C509B_ACTIVATE_VULCAN     0xFF   // Activate Vulcan (to ID port)

// --- Media Type Detection Constants ---

// EEPROM Word Offsets (for reading configuration data)
// Based on 3Com 3C509B Reference Manual Table 3-20
#define _3C509B_EEPROM_STATION_ADDR_LO  0x00  // Station address low word
#define _3C509B_EEPROM_STATION_ADDR_MID 0x01  // Station address middle word  
#define _3C509B_EEPROM_STATION_ADDR_HI  0x02  // Station address high word
#define _3C509B_EEPROM_PRODUCT_ID      0x03  // Product ID
#define _3C509B_EEPROM_MFG_DATE        0x04  // Manufacturing date
#define _3C509B_EEPROM_MFG_DIVISION    0x05  // Manufacturing division
#define _3C509B_EEPROM_MFG_PRODUCT     0x06  // Manufacturing product code
#define _3C509B_EEPROM_MFG_ID          0x07  // Manufacturer ID (should be 0x6D50)
#define _3C509B_EEPROM_ADDR_CONFIG     0x08  // Address configuration
#define _3C509B_EEPROM_RESOURCE_CONFIG 0x09  // Resource configuration
#define _3C509B_EEPROM_OEM_NODE_ADDR_LO 0x0A // OEM station address low
#define _3C509B_EEPROM_OEM_NODE_ADDR_MID 0x0B // OEM station address middle
#define _3C509B_EEPROM_OEM_NODE_ADDR_HI 0x0C // OEM station address high
#define _3C509B_EEPROM_SW_CONFIG_INFO  0x0D  // Software configuration info
#define _3C509B_EEPROM_CHECKSUM        0x0F  // Checksum

// Media capability detection masks
#define _3C509B_CONFIG_XCVR_MASK        0x4000  // Transceiver type mask
#define _3C509B_CONFIG_XCVR_SHIFT       14      // Transceiver type bit position
#define _3C509B_CONFIG_AUTO_SELECT      0x0100  // Auto-select capability
#define _3C509B_CONFIG_FULL_DUPLEX      0x0020  // Full-duplex capability

// EEPROM Configuration Control definitions (corrected)
#define _3C509B_EEPROM_XCVR_MASK        0xC000  // Transceiver mask in config word
#define _3C509B_EEPROM_XCVR_SHIFT       14      // Transceiver bit position
#define _3C509B_EEPROM_AUTO_SELECT      0x0100  // Auto-select enabled
#define _3C509B_EEPROM_FULL_DUPLEX      0x0020  // Full-duplex enabled

// --- DOS-Specific Timing and Hardware Access ---

// EEPROM access timing and ports
#define _3C509B_EEPROM_BUSY_BIT      0x8000  // Busy bit in EEPROM status
#define _3C509B_EEPROM_CMD_PORT      0x0A    // EEPROM command register offset
#define _3C509B_EEPROM_DATA_PORT     0x0C    // EEPROM data register offset
#define _3C509B_EEPROM_DELAY_US      162     // Typical EEPROM read delay
#define _3C509B_EEPROM_TIMEOUT_MS    1       // Maximum wait time

// ISA bus I/O delay macro (~3.3 microseconds per read)
#define _3C509B_ISA_IO_DELAY()       inb(0x80)

// DOS-specific delay macro using I/O port reads
#define _3C509B_DELAY_US(n) { \
    uint16_t _loops = (n) / 3; \
    while(_loops--) _3C509B_ISA_IO_DELAY(); \
}

// Interrupt acknowledgment helpers (updated for correct command)
#define _3C509B_ACK_INTERRUPT(base, mask) \
    outw((uint16_t)((base) + _3C509B_COMMAND_REG), (uint16_t)(_3C509B_CMD_ACK_INTR | (mask)))

// PIC EOI commands for DOS interrupt handling
#define _3C509B_SEND_EOI_MASTER()    outb(0x20, 0x20)  // EOI to master PIC
#define _3C509B_SEND_EOI_SLAVE()     outb(0xA0, 0x20)  // EOI to slave PIC

// TX/RX FIFO direct access ports
#define _3C509B_TX_FIFO_PORT         0x00    // TX FIFO data port
#define _3C509B_RX_FIFO_PORT         0x00    // RX FIFO data port (same as TX)
#define _3C509B_TX_FREE_PORT         0x0C    // TX free bytes register
#define _3C509B_RX_STATUS_PORT       0x08    // RX status register
#define _3C509B_TX_STATUS_PORT       0x0B    // TX status register

// --- Corrected Commands (written to _3C509B_COMMAND_REG) ---
// Based on 3Com 3C509B Reference Manual Table 3-8
// Commands use bits 15-11, with parameters in bits 10-0

#define _3C509B_CMD_GLOBAL_RESET        0x0000  // Global reset (command 0)
#define _3C509B_CMD_SELECT_WINDOW       0x0800  // Select register window (OR with window #)
#define _3C509B_CMD_START_COAX          0x1000  // Start coaxial transceiver  
#define _3C509B_CMD_RX_DISABLE          0x1800  // Disable receiver
#define _3C509B_CMD_RX_ENABLE           0x2000  // Enable receiver
#define _3C509B_CMD_RX_RESET            0x2800  // Reset receiver
#define _3C509B_CMD_RX_DISCARD_TOP      0x4000  // Discard top RX packet
#define _3C509B_CMD_TX_ENABLE           0x4800  // Enable transmitter
#define _3C509B_CMD_TX_DISABLE          0x5000  // Disable transmitter  
#define _3C509B_CMD_TX_RESET            0x5800  // Reset transmitter
#define _3C509B_CMD_REQUEST_INTR        0x6000  // Request interrupt
#define _3C509B_CMD_ACK_INTR            0x6800  // Acknowledge interrupt (OR with status)
#define _3C509B_CMD_SET_INTR_ENABLE     0x7000  // Set interrupt enable (OR with mask)
#define _3C509B_CMD_SET_STATUS_ENABLE   0x7800  // Set status enable (OR with mask)
#define _3C509B_CMD_SET_RX_FILTER       0x8000  // Set RX filter (OR with filter bits)
#define _3C509B_CMD_SET_RX_EARLY_THRESH 0x8800  // Set RX early threshold
#define _3C509B_CMD_SET_TX_AVAIL_THRESH 0x9000  // Set TX available threshold
#define _3C509B_CMD_SET_TX_START_THRESH 0x9800  // Set TX start threshold
#define _3C509B_CMD_STATS_ENABLE        0xA800  // Enable statistics
#define _3C509B_CMD_STATS_DISABLE       0xB000  // Disable statistics  
#define _3C509B_CMD_STOP_COAX           0xB800  // Stop coaxial transceiver
#define _3C509B_CMD_SET_TX_RECLAIM      0xC000  // Set TX reclaim threshold

// Command parameter masks and helpers
#define _3C509B_CMD_MASK            0xF800  // Command field mask (bits 15-11)
#define _3C509B_CMD_PARAM_MASK      0x07FF  // Parameter field mask (bits 10-0)
#define _3C509B_MAKE_CMD(cmd, param) ((cmd) | ((param) & _3C509B_CMD_PARAM_MASK))

// Window management helpers
#define _3C509B_WINDOW_CMD_PORT      0x0E    // Command register for window select
#define _3C509B_SELECT_WINDOW_DIRECT(base, w) \
    outw((uint16_t)((base) + _3C509B_WINDOW_CMD_PORT), (uint16_t)(0x0800 | (w)))

// RX packet validation
#define _3C509B_MIN_PACKET_SIZE      14      // Minimum valid packet (headers only)
#define _3C509B_MAX_PACKET_SIZE      1514    // Maximum Ethernet frame size

// Hardware state flags
#define _3C509B_FLAG_CONFIGURED      0x01    // NIC is configured
#define _3C509B_FLAG_ENABLED         0x02    // NIC is enabled
#define _3C509B_FLAG_PROMISCUOUS     0x04    // Promiscuous mode enabled
#define _3C509B_FLAG_FULL_DUPLEX     0x08    // Full duplex mode

// --- Helper Macros ---

// Selects a register window by writing to the _3C509B_COMMAND_REG.
// This macro *must* be used before accessing registers within a specific window.
#define _3C509B_SELECT_WINDOW(io_base, win) \
    outw((uint16_t)(io_base + _3C509B_COMMAND_REG), (uint16_t)(_3C509B_CMD_SELECT_WINDOW | (win)))

// Media control helper macros (updated for corrected bit definitions)
#define _3C509B_SET_MEDIA_10BASE_T(io_base) do { \
    _3C509B_SELECT_WINDOW(io_base, 4); \
    outw((uint16_t)(io_base + _3C509B_MEDIA_CTRL), _3C509B_XCVR_10BASE_T); \
} while(0)

#define _3C509B_SET_MEDIA_BNC(io_base) do { \
    _3C509B_SELECT_WINDOW(io_base, 4); \
    outw((uint16_t)(io_base + _3C509B_MEDIA_CTRL), _3C509B_XCVR_10BASE2); \
    outw((uint16_t)(io_base + _3C509B_COMMAND_REG), _3C509B_CMD_START_COAX); \
} while(0)

#define _3C509B_SET_MEDIA_AUI(io_base) do { \
    _3C509B_SELECT_WINDOW(io_base, 4); \
    outw((uint16_t)(io_base + _3C509B_MEDIA_CTRL), _3C509B_XCVR_AUI_EXT); \
} while(0)

// Media detection helpers (updated for corrected register definitions)
#define _3C509B_READ_XCVR_TYPE_FROM_EEPROM(io_base, addr) ({ \
    uint16_t config; \
    _3C509B_SELECT_WINDOW(io_base, 0); \
    outw((uint16_t)(io_base + _3C509B_EEPROM_CMD), _3C509B_EEPROM_READ | (addr)); \
    do { config = inw((uint16_t)(io_base + _3C509B_EEPROM_DATA)); } \
    while (config & _3C509B_EEPROM_BUSY_BIT); \
    ((config & _3C509B_EEPROM_XCVR_MASK) >> _3C509B_EEPROM_XCVR_SHIFT); \
})

#define _3C509B_GET_CURRENT_XCVR_TYPE(io_base) ({ \
    uint16_t media_ctrl; \
    _3C509B_SELECT_WINDOW(io_base, 4); \
    media_ctrl = inw((uint16_t)(io_base + _3C509B_MEDIA_CTRL)); \
    ((media_ctrl & _3C509B_MEDIA_XCVR_MASK) >> _3C509B_MEDIA_XCVR_SHIFT); \
})

// --- Direct PIO Transmit Optimization Functions ---

/**
 * @brief Send packet directly via PIO (eliminates intermediate copy)
 * @param stack_buffer Network stack's buffer pointer
 * @param length Packet length
 * @param io_base NIC I/O base address
 * @return 0 on success, negative on error
 */
int send_packet_direct_pio(const void* stack_buffer, uint16_t length, uint16_t io_base);

/**
 * @brief Direct PIO transfer with assembly optimization
 * @param src_buffer Source buffer (stack buffer)
 * @param dst_port Destination I/O port
 * @param word_count Number of 16-bit words to transfer
 */
void direct_pio_outsw(const void* src_buffer, uint16_t dst_port, uint16_t word_count);

/**
 * @brief Direct PIO transmit with header construction on-the-fly
 * @param nic NIC information structure
 * @param dest_mac Destination MAC address
 * @param ethertype Ethernet type
 * @param payload Payload data
 * @param payload_len Payload length
 * @return 0 on success, negative on error
 */
int send_packet_direct_pio_with_header(nic_info_t *nic, const uint8_t *dest_mac, 
                                      uint16_t ethertype, const void* payload, uint16_t payload_len);

#endif /* _3C509B_H_ */
