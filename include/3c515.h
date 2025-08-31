/**
 * @file 3c515.h
 * @brief Hardware-specific definitions for the 3Com 3C515-TX NIC
 *
 * This header provides register offsets, command codes, status bits, and DMA
 * structures for the 3C515-TX, a 10/100 Mbps ISA NIC with bus mastering
 * capabilities. Definitions are organized by register window to clarify
 * their usage and context within the driver.
 */

#ifndef _3C515_H_
#define _3C515_H_

#include <common.h>  // Assumes uint8_t, uint16_t, uint32_t, etc.

/* Forward declarations for DMA types */
typedef struct dma_fragment dma_fragment_t;
typedef struct dma_mapping dma_mapping_t;  /* GPT-5 Critical: DMA mapping lifetime fix */

// --- General Constants ---

#define _3C515_TX_PRODUCT_ID       0x5051  // Product ID (verify with EEPROM)
#define _3C515_TX_PRODUCT_ID_MASK  0xF0FF  // Mask off revision nibble
#define _3C515_TX_MAX_MTU          1514    // Maximum Transmission Unit (bytes)
#define _3C515_TX_MIN_PACKET_SIZE  60      // Minimum packet size (w/o CRC)
#define _3C515_TX_IO_EXTENT        32      // I/O port range size
#define _3C515_TX_TX_RING_SIZE     16      // Number of transmit descriptors
#define _3C515_TX_RX_RING_SIZE     16      // Number of receive descriptors

// --- Window Definitions ---

#define _3C515_TX_WINDOW_0  0 // EEPROM access
#define _3C515_TX_WINDOW_1  1 // Normal operation (TX/RX)
#define _3C515_TX_WINDOW_2  2 // Station address (set during init).
#define _3C515_TX_WINDOW_3  3 // Configuration
#define _3C515_TX_WINDOW_4  4 // Media Control
#define _3C515_TX_WINDOW_6  6 // Statistics
#define _3C515_TX_WINDOW_7  7 // Bus master control

// --- Command Register (ALWAYS Accessible) ---

#define _3C515_TX_COMMAND_REG   0x0E  // Command register (all windows)
#define _3C515_TX_STATUS_REG    0x0E  // Status register (same address)

// --- Commands (written to _3C515_TX_COMMAND_REG) ---
// These are *always* written to _3C515_TX_COMMAND_REG, but their effect
// depends on the currently selected window.

#define _3C515_TX_CMD_TOTAL_RESET   (0 << 11)   // Reset NIC (any window)
#define _3C515_TX_CMD_SELECT_WINDOW (1 << 11)   // Select register window (OR with #)
#define _3C515_TX_CMD_START_COAX    (2 << 11)   // Start Coax (Window 4)
#define _3C515_TX_CMD_RX_ENABLE     (4 << 11)   // Enable receiver (Window 1)
#define _3C515_TX_CMD_RX_DISABLE    (3 << 11)   // Disable receiver (Window 1)
#define _3C515_TX_CMD_RX_RESET      (5 << 11)   // Reset receiver (Window 1)
#define _3C515_TX_CMD_UP_STALL      (6 << 11)  // Stall Rx DMA (Window 7)
#define _3C515_TX_CMD_UP_UNSTALL    ((6 << 11) + 1) // Unstall Rx DMA (Window 7)
#define _3C515_TX_CMD_DOWN_STALL    ((6 << 11) + 2) // Stall Tx DMA (Window 7)
#define _3C515_TX_CMD_DOWN_UNSTALL  ((6 << 11) + 3) // Unstall Tx DMA (Window 7)
#define _3C515_TX_CMD_RX_DISCARD    (8 << 11)  // Discard RX packet (Window 1)
#define _3C515_TX_CMD_TX_ENABLE     (9 << 11)   // Enable transmitter (Window 1)
#define _3C515_TX_CMD_TX_DISABLE    (10 << 11)  // Disable transmitter (Window 1)
#define _3C515_TX_CMD_TX_RESET      (11 << 11)  // Reset transmitter (Window 1)
#define _3C515_TX_CMD_FAKE_INTR     (12 << 11)  // Fake interrupt (unused)
#define _3C515_TX_CMD_ACK_INTR      (13 << 11)  // Acknowledge interrupt (Window 1)
#define _3C515_TX_CMD_SET_INTR_ENB  (14 << 11)  // Set Interrupt Enable (Window 1)
#define _3C515_TX_CMD_SET_STATUS_ENB (15 << 11) // Set Status Enable (Window 1)
#define _3C515_TX_CMD_SET_RX_FILTER (16 << 11)  // Set Rx Filter (Window 1)
#define _3C515_TX_CMD_SET_RX_THRESHOLD (17 << 11) // Not used
#define _3C515_TX_CMD_SET_TX_THRESHOLD (18 << 11) // Boomerang Only (Window 1)
#define _3C515_TX_CMD_SET_TX_START (19 << 11) // Boomerang Only (Window 1)
#define _3C515_TX_CMD_START_DMA_UP  (20 << 11)        // DMA upload (host to NIC) (Window 7)
#define _3C515_TX_CMD_START_DMA_DOWN ((20 << 11) + 1) // DMA download (NIC to host) (Window 7)
#define _3C515_TX_CMD_STATS_ENABLE  (21 << 11)  // Enable statistics (Window 6)
#define _3C515_TX_CMD_STATS_DISABLE (22 << 11)  // Disable statistics (Window 6)
#define _3C515_TX_CMD_STOP_COAX     (23 << 11)  // Stop coax (Window 4)

// --- Status Bits (read from _3C515_TX_STATUS_REG) ---
// These bits are read from the _3C515_TX_STATUS_REG (same address as _3C515_TX_COMMAND_REG).

#define _3C515_TX_STATUS_INT_LATCH      0x0001  // Interrupt latch
#define _3C515_TX_STATUS_ADAPTER_FAILURE 0x0002  // Adapter Failure
#define _3C515_TX_STATUS_TX_COMPLETE    0x0004  // Transmit complete
#define _3C515_TX_STATUS_TX_AVAILABLE   0x0008  // TX FIFO available
#define _3C515_TX_STATUS_RX_COMPLETE    0x0010  // Receive complete
#define _3C515_TX_STATUS_RX_EARLY       0x0020  // Rx early (unused).
#define _3C515_TX_STATUS_INT_REQ        0x0040  // Interrupt request
#define _3C515_TX_STATUS_STATS_FULL     0x0080  // Update statistics
#define _3C515_TX_STATUS_DMA_DONE       (1 << 8)  // DMA transfer complete
#define _3C515_TX_STATUS_DOWN_COMPLETE  (1 << 9)  // Tx DMA complete
#define _3C515_TX_STATUS_UP_COMPLETE    (1 << 10) // Rx DMA complete
#define _3C515_TX_STATUS_DMA_IN_PROGRESS (1 << 11) // DMA in progress
#define _3C515_TX_STATUS_CMD_IN_PROGRESS (1 << 12) // Command in progress

// --- Interrupt Masks (for _3C515_TX_CMD_SET_INTR_ENB, Window 1) ---

#define _3C515_TX_IMASK_ADAPTER_FAILURE    _3C515_TX_STATUS_ADAPTER_FAILURE
#define _3C515_TX_IMASK_TX_COMPLETE        _3C515_TX_STATUS_TX_COMPLETE
#define _3C515_TX_IMASK_TX_AVAILABLE       _3C515_TX_STATUS_TX_AVAILABLE
#define _3C515_TX_IMASK_RX_COMPLETE        _3C515_TX_STATUS_RX_COMPLETE
#define _3C515_TX_IMASK_RX_EARLY           _3C515_TX_STATUS_RX_EARLY
#define _3C515_TX_IMASK_STATS_FULL         _3C515_TX_STATUS_STATS_FULL
#define _3C515_TX_IMASK_DMA_DONE           _3C515_TX_STATUS_DMA_DONE
#define _3C515_TX_IMASK_DOWN_COMPLETE      _3C515_TX_STATUS_DOWN_COMPLETE
#define _3C515_TX_IMASK_UP_COMPLETE        _3C515_TX_STATUS_UP_COMPLETE

// --- RX Filter Bits (for _3C515_TX_CMD_SET_RX_FILTER, Window 1) ---

#define _3C515_TX_RX_FILTER_STATION    1  // Individual address
#define _3C515_TX_RX_FILTER_MULTICAST  2  // Multicast/group addresses
#define _3C515_TX_RX_FILTER_BROADCAST  4  // Broadcast address
#define _3C515_TX_RX_FILTER_PROM       8  // Promiscuous mode

// --- Window 0: EEPROM Access ---

#define _3C515_TX_W0_IRQ         0x08   // Window 0 IRQ register
#define _3C515_TX_W0_EEPROM_CMD  0x200A // EEPROM command
#define _3C515_TX_W0_EEPROM_DATA 0x200C // EEPROM data

// EEPROM Commands (written to _3C515_TX_W0_EEPROM_CMD)
#define _3C515_TX_EEPROM_READ     0x80  // EEPROM read command
#define _3C515_TX_EEPROM_WRITE    0x40  // EEPROM write command (not used)
#define _3C515_TX_EEPROM_ERASE    0xC0  // EEPROM erase command (not used)
#define _3C515_TX_EEPROM_EWENB    0x30  // EEPROM enable writing/erasing (not used)
#define _3C515_TX_EEPROM_EWDIS    0x00  // EEPROM disable writing/erasing (not used)

// EEPROM Read Timings
#define _3C515_TX_EEPROM_READ_DELAY  162 // microseconds

// EEPROM locations for MAC address and configuration
enum eeprom_offset {
    PhysAddr01 = 0,    // MAC bytes 0-1
    PhysAddr23 = 1,    // MAC bytes 2-3  
    PhysAddr45 = 2,    // MAC bytes 4-5
    ModelID = 3,       // Device model ID
    EtherLink3ID = 7   // 3Com ID (0x6d50)
};

// Transceiver types for media selection
enum xcvr_types {
    XCVR_10baseT = 0,
    XCVR_AUI = 1,
    XCVR_10baseTOnly = 2,
    XCVR_10base2 = 3,
    XCVR_100baseTx = 4,
    XCVR_100baseFx = 5,
    XCVR_MII = 6,
    XCVR_Default = 8
};

// Performance and timing constants
#define MAX_INTERRUPT_WORK 32    // Max events per interrupt
#define WAIT_TX_AVAIL     200    // Iterations to wait for TX FIFO
#define RX_COPYBREAK      200    // Copy threshold in bytes

// --- DOS-Specific DMA and Bus Master Definitions ---

// DMA list pointer registers (Window 7)
#define _3C515_TX_DMA_UP_LIST_PTR      0x38   // RX DMA list pointer
#define _3C515_TX_DMA_DOWN_LIST_PTR    0x24   // TX DMA list pointer
#define _3C515_TX_DMA_UP_PKT_STATUS    0x30   // RX packet status
#define _3C515_TX_DMA_DOWN_PKT_STATUS  0x20   // TX packet status

// Bus master control commands
#define _3C515_TX_CMD_DOWN_STALL       0x5000  // Stall TX DMA
#define _3C515_TX_CMD_DOWN_UNSTALL     0x5002  // Start TX DMA
#define _3C515_TX_CMD_UP_STALL         0x5100  // Stall RX DMA
#define _3C515_TX_CMD_UP_UNSTALL       0x5102  // Start RX DMA

// ISA Bus Master DMA Registers (Corkscrew-specific)
// These registers are accessed at base_addr + 0x400 offset for DMA control
#define _3C515_TX_PKT_STATUS           0x400   // TX packet status
#define _3C515_TX_DOWN_LIST_PTR        0x404   // TX descriptor list pointer
#define _3C515_TX_FRAG_ADDR            0x408   // Fragment address
#define _3C515_TX_FRAG_LEN             0x40C   // Fragment length
#define _3C515_TX_TX_FREE_THRESHOLD    0x40F   // TX free threshold
#define _3C515_TX_UP_PKT_STATUS        0x410   // RX packet status
#define _3C515_TX_UP_LIST_PTR          0x418   // RX descriptor list pointer

// Window 7 DMA Status Register (at base + 0x20, NOT PCI!)
#define _3C515_TX_DMA_DOWN_PKT_STATUS  0x20    // TX DMA packet status (Window 7)

// Window 7 DMA control registers (offsets from base)
#define _3C515_TX_W7_UP_LIST_PTR       0x418   // RX descriptor list
#define _3C515_TX_W7_DOWN_LIST_PTR     0x404   // TX descriptor list
#define _3C515_TX_W7_DMA_CTRL          0x400   // DMA control register
#define _3C515_TX_W7_UP_POLL           0x41C   // RX poll demand
#define _3C515_TX_W7_DOWN_POLL         0x408   // TX poll demand

// DMA descriptor format (bus master mode)
typedef struct {
    uint32_t next;          // Physical address of next descriptor
    uint32_t status;        // Status and packet length
    uint32_t addr;          // Physical address of data buffer
    uint32_t length;        // Buffer length and control bits
} dma_descriptor_t;

// DMA descriptor status bits
#define _3C515_TX_DMA_DESC_COMPLETE    0x00008000  // Descriptor complete
#define _3C515_TX_DMA_DESC_ERROR       0x00004000  // Error occurred
#define _3C515_TX_DMA_DESC_LAST        0x00002000  // Last descriptor
#define _3C515_TX_DMA_DESC_FIRST       0x00001000  // First descriptor
#define _3C515_TX_DMA_DESC_DN_COMPLETE 0x00010000  // Download complete
#define _3C515_TX_DMA_DESC_UP_COMPLETE 0x00020000  // Upload complete

// ISA bus timing operations
#define _3C515_TX_ISA_IO_DELAY()       outp(0x80, 0)     // ISA I/O delay (~1us)
#define _3C515_TX_EEPROM_DELAY_US      162               // ISA EEPROM delay
#define _3C515_TX_RESET_DELAY_MS       10                // Hardware reset delay

// ISA DMA constraints
#define _3C515_TX_ISA_DMA_MAX_ADDR     0xFFFFFF          // 16MB limit (24-bit)
#define _3C515_TX_ISA_DMA_BOUNDARY     0x10000           // 64KB boundary
#define _3C515_TX_EEPROM_DELAY_US      200         // EEPROM delay for 3C515
#define _3C515_TX_RESET_DELAY_MS       10          // Reset delay

// DOS real-mode DMA helpers
#define _3C515_TX_PHYS_TO_SEGMENT(addr) ((uint16_t)((addr) >> 4))
#define _3C515_TX_PHYS_TO_OFFSET(addr)  ((uint16_t)((addr) & 0x0F))
#define _3C515_TX_MAKE_PHYSICAL(seg, off) (((uint32_t)(seg) << 4) + (uint16_t)(off))

// Hardware flags for 3C515
#define _3C515_TX_FLAG_BUS_MASTER      0x01   // Bus master DMA enabled
#define _3C515_TX_FLAG_100MBPS         0x02   // 100Mbps mode active
#define _3C515_TX_FLAG_FULL_DUPLEX     0x04   // Full duplex active
#define _3C515_TX_FLAG_MII_XCVR        0x08   // MII transceiver in use
#define _3C515_TX_FLAG_AUTO_NEG        0x10   // Auto-negotiation enabled

// --- Window 1: Normal Operation (TX/RX) ---

#define _3C515_TX_TX_FIFO       0x10  // Transmit FIFO
#define _3C515_TX_RX_FIFO       0x10  // Receive FIFO (same address as TX_FIFO)
#define _3C515_TX_RX_STATUS     0x18  // Receive status
#define _3C515_TX_TX_STATUS     0x1B  // Transmit status
#define _3C515_TX_TX_FREE       0x1C  // Free bytes in TX FIFO
#define _3C515_TX_RX_ERRORS    0x14  // RX Errors
#define _3C515_TX_W1_TIMER     0x1A  // Timer

// RX_STATUS bits (read from _3C515_TX_RX_STATUS)
#define _3C515_TX_RXSTAT_INCOMPLETE   0x8000 // Not completely received
#define _3C515_TX_RXSTAT_ERROR        0x4000 // Error in packet
#define _3C515_TX_RXSTAT_LEN_MASK     0x1FFF // Packet length (13 bits)

// RX Error bits (in upper byte of _3C515_TX_RX_STATUS)
#define _3C515_TX_RXERR_OVERRUN       0x01
#define _3C515_TX_RXERR_LENGTH        0x02
#define _3C515_TX_RXERR_FRAME         0x04
#define _3C515_TX_RXERR_CRC           0x08
#define _3C515_TX_RXERR_DRIBBLE       0x10

// TX_STATUS bits (read from _3C515_TX_TX_STATUS)
#define _3C515_TX_TXSTAT_COMPLETE	0x01	// Packet completed
#define _3C515_TX_TXSTAT_DEFERRED	0x02	// Deferred
#define _3C515_TX_TXSTAT_ABORTED	    0x04    // Aborted
#define _3C515_TX_TXSTAT_SCOLL		0x08    // Single Collision
#define _3C515_TX_TXSTAT_MCOLL		0x10    // Multiple Collision
#define _3C515_TX_TXSTAT_UNDERRUN	0x20    // FIFO Underrun
#define _3C515_TX_TXSTAT_JABBER		0x40    // Jabber error
#define _3C515_TX_TXSTAT_MAXCOLL	0x80    // Max Collisions

// --- Window 2: Station Address ---
// Offsets 0-5 are used to *write* the MAC address into the NIC.
// This is typically done *once* during initialization.

// --- Window 3: Configuration ---

#define _3C515_TX_W3_CONFIG     0x00 // Configuration register
#define _3C515_TX_W3_MAC_CTRL   0x06 // MAC control register
#define _3C515_TX_W3_OPTIONS    0x08 // Options register

// Config Register Bits (_3C515_TX_W3_CONFIG)
#define _3C515_TX_RAM_SIZE          0x00000007 // RAM size
#define _3C515_TX_RAM_WIDTH         0x00000008 // RAM width
#define _3C515_TX_RAM_SPEED         0x00000030 // RAM speed
#define _3C515_TX_ROM_SIZE          0x000000C0 // ROM size
#define _3C515_TX_RAM_SPLIT_SHIFT   16         // RAM split shift
#define _3C515_TX_RAM_SPLIT         (3 << _3C515_TX_RAM_SPLIT_SHIFT) // RAM split
#define _3C515_TX_XCVR_SHIFT        20         // Transceiver shift
#define _3C515_TX_XCVR              (7 << _3C515_TX_XCVR_SHIFT)      // Transceiver
#define _3C515_TX_AUTOSELECT        0x1000000  // Autoselect

// Wn3_MAC_Ctrl bits (_3C515_TX_W3_MAC_CTRL)
#define _3C515_TX_FULL_DUPLEX_BIT   0x20 // Set full-duplex

// --- Window 4: Media Control ---

#define _3C515_TX_W4_NETDIAG    0x06 // Network diagnostics
#define _3C515_TX_W4_MEDIA      0x0A // Media control
#define _3C515_TX_W4_MII_READ   0x0800 // MII read command
#define _3C515_TX_W4_MII_WRITE  0x0A00 // MII write command

// Media Bits (_3C515_TX_W4_MEDIA)
#define _3C515_TX_MEDIA_SQE         0x0008 // Enable SQE error counting for AUI
#define _3C515_TX_MEDIA_10TP        0x00C0 // Enable link beat/jabber for 10baseT
#define _3C515_TX_MEDIA_LNK         0x0080 // Enable just link beat
#define _3C515_TX_MEDIA_LNKBEAT     0x0800 // Link beat

// --- Window 6: Statistics ---

#define _3C515_TX_W6_TX_CARR_ERRS    0x00 // TX Carrier Errors
#define _3C515_TX_W6_TX_HRTBT_ERRS   0x01 // TX Heartbeat Errors
#define _3C515_TX_W6_TX_MULT_COLLS   0x02 // TX Multiple Collisions
#define _3C515_TX_W6_TX_TOT_COLLS    0x03 // TX Total Collisions
#define _3C515_TX_W6_TX_WIN_ERRS     0x04 // TX Window Errors
#define _3C515_TX_W6_RX_FIFO_ERRS    0x05 // RX FIFO Errors
#define _3C515_TX_W6_TX_PACKETS      0x06 // TX Packets
#define _3C515_TX_W6_RX_PACKETS      0x07 // RX Packets
#define _3C515_TX_W6_TX_DEFERRALS    0x08 // TX Deferrals
#define _3C515_TX_W6_BADSSD         0x0C // Bad SSD

// --- Window 7: Bus Master Control ---

#define _3C515_TX_W7_MASTER_ADDR    0x00 // Bus Master transfer physical address
#define _3C515_TX_W7_MASTER_LEN     0x06 // Bus Master transfer length (bytes)
#define _3C515_TX_W7_MASTER_STATUS  0x0C // Bus Master status

// Aliased registers (Window 7 and others)
#define _3C515_TX_PKT_STATUS        0x400  // Packet status (alias for W7_MASTER_ADDR)
#define _3C515_TX_DOWN_LIST_PTR     0x404  // Transmit descriptor list pointer
#define _3C515_TX_UP_LIST_PTR       0x418  // Receive descriptor list pointer
#define _3C515_TX_FRAG_ADDR         0x408  // Fragment address (unused)
#define _3C515_TX_FRAG_LEN          0x40C  // Fragment length (unused)
#define _3C515_TX_TX_FREE_THRESH    0x40F  // TX free threshold
#define _3C515_TX_UP_PKT_STATUS     0x410  // Up packet status (alias for W7_MASTER_STATUS)

// --- DMA Descriptor Structures ---

typedef struct {
    uint32_t next;    // Physical address of next descriptor
    int32_t  status;  // Status and control bits
    uint32_t addr;    // Physical buffer address
    int32_t  length;  // Buffer length
    /* GPT-5: RX buffers use pre-allocated pool with persistent mappings */
    dma_mapping_t *mapping;  // Pre-allocated DMA mapping for RX buffer pool
} _3c515_tx_rx_desc_t;

typedef struct {
    uint32_t next;    // Physical address of next descriptor
    int32_t  status;  // Status and control bits
    uint32_t addr;    // Physical buffer address
    int32_t  length;  // Buffer length / flags
    /* GPT-5 CRITICAL: Attach mapping to descriptor for proper lifetime management */
    dma_mapping_t *mapping;  // DMA mapping attached to this descriptor (freed on completion)
} _3c515_tx_tx_desc_t;

// --- Descriptor Status Bits ---

// Receive Descriptor Status Bits
#define _3C515_TX_RX_DESC_COMPLETE  0x80000000 // Packet received/processed
#define _3C515_TX_RX_DESC_ERROR     0x40000000 // Error occurred
#define _3C515_TX_RX_DESC_LEN_MASK  0x00001FFF // Packet length mask (13 bits)

// Linux driver compatible descriptor status bits
#define RxDComplete    0x00008000  // Packet complete in descriptor (Linux compat)
#define RxDError       0x00004000  // Error in descriptor (Linux compat)

// Transmit Descriptor Status Bits
#define _3C515_TX_TX_DESC_COMPLETE  0x80000000 // Packet transmitted
#define _3C515_TX_TX_DESC_ERROR     0x40000000 // Error occurred.
#define _3C515_TX_TX_DESC_LEN_MASK  0x00001FFF // Packet length mask (13 bits)
#define _3C515_TX_TX_INTR_BIT       0x20000000 // Request interrupt

// --- Helper Macro ---

// Selects a register window by writing to the _3C515_TX_COMMAND_REG.
#define _3C515_TX_SELECT_WINDOW(io_base, win) \
    outw(io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SELECT_WINDOW | (win))

// --- Sprint 0B.4: Complete Hardware Initialization API ---

/**
 * @brief Media configuration structure matching Linux driver standards
 */
typedef struct {
    uint8_t media_type;          // 10Base-T, 100Base-TX, Auto
    uint8_t duplex_mode;         // Half, Full, Auto  
    uint8_t transceiver_type;    // Internal, External, Auto
    uint16_t link_speed;         // 10, 100, or 0 for auto
    uint8_t link_active;         // Link status
    uint8_t auto_negotiation;    // Auto-negotiation enabled
    uint16_t advertised_modes;   // Advertised capabilities
} media_config_t;

/**
 * @brief Enhanced NIC context structure with complete configuration state
 */
typedef struct {
    /* Basic hardware configuration */
    uint16_t io_base;                       // I/O base address
    uint8_t irq;                            // IRQ number
    
    /* Ring buffer management */
    _3c515_tx_tx_desc_t *tx_desc_ring;      // TX descriptor ring
    _3c515_tx_rx_desc_t *rx_desc_ring;      // RX descriptor ring
    uint32_t tx_index;                      // Current transmit descriptor index
    uint32_t rx_index;                      // Current receive descriptor index
    uint8_t *buffers;                       // Contiguous buffer memory
    
    /* Enhanced configuration */
    eeprom_config_t eeprom_config;          // Hardware configuration from EEPROM
    media_config_t media_config;            // Media configuration state
    
    /* Hardware state tracking */
    uint8_t hardware_ready;                 // Hardware initialization complete flag
    uint8_t driver_active;                  // Driver active state
    uint32_t last_config_validation;        // Last configuration validation time
    uint32_t last_stats_update;             // Last statistics update time
    uint32_t last_link_check;               // Last link status check time
    
    /* Statistics collection */
    uint32_t tx_packets;                    // Transmitted packets
    uint32_t rx_packets;                    // Received packets
    uint32_t tx_bytes;                      // Transmitted bytes
    uint32_t rx_bytes;                      // Received bytes
    uint32_t tx_errors;                     // Transmission errors
    uint32_t rx_errors;                     // Reception errors
    uint32_t link_changes;                  // Link state changes
    uint32_t config_errors;                 // Configuration errors
    
    /* Advanced features */
    uint16_t interrupt_mask;                // Current interrupt mask
    uint8_t full_duplex_enabled;            // Full duplex mode active
    uint8_t dma_enabled;                    // Bus master DMA enabled
    uint8_t stats_enabled;                  // Hardware statistics enabled
    uint8_t link_monitoring_enabled;        // Link monitoring active
    
    /* Error handling integration */
    void *error_context;                    // Error handling context
} nic_context_t;

// --- Complete Hardware Initialization API ---

/**
 * @brief Complete 3C515-TX hardware initialization sequence
 * 
 * This function implements the complete hardware initialization sequence
 * matching Linux driver standards with comprehensive configuration of
 * all hardware features and capabilities.
 * 
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int complete_3c515_initialization(nic_context_t *ctx);

/**
 * @brief Enhanced initialization with complete hardware setup
 * @param io_base I/O base address
 * @param irq IRQ number
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_init(uint16_t io_base, uint8_t irq, uint8_t nic_index);

/**
 * @brief Enhanced cleanup function
 */
void _3c515_enhanced_cleanup(void);

/* Scatter-gather DMA function declarations */

/**
 * @brief Send packet with optional scatter-gather DMA
 * @param packet_data Packet data
 * @param packet_len Packet length
 * @param fragments Optional fragments (NULL for single buffer)
 * @param frag_count Number of fragments (0 for single buffer)
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_send_packet_sg(const uint8_t *packet_data, uint16_t packet_len,
                                   dma_fragment_t *fragments, uint16_t frag_count);

/**
 * @brief Create fragments from large packet data
 * @param packet_data Packet data to fragment
 * @param packet_len Total packet length
 * @param fragments Output fragment array
 * @param max_fragments Maximum number of fragments
 * @param fragment_size Size of each fragment
 * @return Number of fragments created, negative on error
 */
int _3c515_enhanced_create_fragments(const uint8_t *packet_data, uint16_t packet_len,
                                     dma_fragment_t *fragments, uint16_t max_fragments,
                                     uint16_t fragment_size);

/**
 * @brief Test scatter-gather DMA functionality
 * @return 0 on success, negative error code on failure
 */
int _3c515_enhanced_test_scatter_gather(void);

/**
 * @brief Periodic configuration validation
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int periodic_configuration_validation(nic_context_t *ctx);

/**
 * @brief Get current NIC context for integration with other systems
 * @return Pointer to NIC context or NULL if not initialized
 */
nic_context_t *get_3c515_context(void);

/**
 * @brief Get hardware configuration information
 * @param ctx Pointer to NIC context structure
 * @param buffer Buffer to store configuration information
 * @param buffer_size Size of the buffer
 * @return Number of characters written to buffer, negative on error
 */
int get_hardware_config_info(nic_context_t *ctx, char *buffer, size_t buffer_size);

// --- Hardware Configuration Step Functions ---

/**
 * @brief Read and parse EEPROM configuration
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int read_and_parse_eeprom(nic_context_t *ctx);

/**
 * @brief Configure media type from EEPROM data
 * @param ctx Pointer to NIC context structure
 * @param media Pointer to media configuration structure
 * @return 0 on success, negative error code on failure
 */
int configure_media_type(nic_context_t *ctx, media_config_t *media);

/**
 * @brief Configure full-duplex support (Window 3, MAC Control)
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int configure_full_duplex(nic_context_t *ctx);

/**
 * @brief Setup comprehensive interrupt mask
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int setup_interrupt_mask(nic_context_t *ctx);

/**
 * @brief Configure bus master DMA settings
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int configure_bus_master_dma(nic_context_t *ctx);

/**
 * @brief Enable hardware statistics collection (Window 6)
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int enable_hardware_statistics(nic_context_t *ctx);

/**
 * @brief Setup link status monitoring
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int setup_link_monitoring(nic_context_t *ctx);

/**
 * @brief Validate complete hardware configuration
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int validate_hardware_configuration(nic_context_t *ctx);

/**
 * @brief Reset NIC hardware to known state
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int reset_nic_hardware(nic_context_t *ctx);

// --- Configuration Constants ---

/* Duplex mode constants */
#define DUPLEX_HALF               0
#define DUPLEX_FULL               1
#define DUPLEX_AUTO               2

/* Link speed constants */
#define SPEED_10MBPS              10
#define SPEED_100MBPS             100
#define SPEED_AUTO                0

/* Hardware configuration timing constants */
#define RESET_TIMEOUT_MS           1000    /* Maximum reset time */
#define CONFIG_STABILIZATION_MS    100     /* Configuration stabilization delay */
#define LINK_CHECK_INTERVAL_MS     500     /* Link status check interval */
#define STATS_UPDATE_INTERVAL_MS   1000    /* Statistics update interval */
#define CONFIG_VALIDATION_INTERVAL_MS 5000 /* Configuration validation interval */

#endif /* _3C515_H_ */
