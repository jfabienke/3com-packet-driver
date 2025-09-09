# 3Com NIC Hardware Emulation Notes

This document provides critical implementation details for accurate emulation of 3Com EtherLink III family NICs based on analysis of production drivers and hardware behavior.

## Overview

Accurate emulation requires matching not just documented hardware behavior, but also the quirks and undocumented features that production drivers depend on. This document captures these requirements based on extensive driver analysis.

## Core Emulation Principles

### 1. Bit-Perfect Status Registers
Production drivers check specific bit patterns. Any deviation can cause driver initialization failures or incorrect error handling.

### 2. Timing Accuracy
While cycle-accurate timing isn't required, relative timing relationships must be maintained (e.g., EEPROM access delays, reset completion times).

### 3. Side Effects
Many operations have side effects that drivers depend on (e.g., reading TX_STATUS clears it, writing commands changes state).

## 3C509B Emulation Requirements

### Register Implementation

#### RX_STATUS (Window 1, Offset 0x08)
```c
struct rx_status_register {
    uint16_t length:11;      // Bits 10-0: Packet length (0-2047)
    uint16_t error_code:3;   // Bits 13-11: Error type
    uint16_t error:1;        // Bit 14: Error flag
    uint16_t incomplete:1;   // Bit 15: Packet still arriving
};

// Error codes (when error bit is set)
#define RX_ERR_OVERRUN   0x0  // 0x0000
#define RX_ERR_OVERSIZE  0x1  // 0x0800
#define RX_ERR_DRIBBLE   0x2  // 0x1000
#define RX_ERR_RUNT      0x3  // 0x1800
#define RX_ERR_ALIGN     0x4  // 0x2000
#define RX_ERR_CRC       0x5  // 0x2800
```

**Emulation Requirements:**
- Set bit 15 while packet is being received
- Clear bit 15 when packet fully in FIFO
- Always set bit 14 when reporting any error
- Use exact error codes shown above
- Length field valid even when error bit is set

#### TX_STATUS (Window 1, Offset 0x0B)
```c
struct tx_status_register {
    uint8_t complete:1;      // Bit 0: TX finished
    uint8_t deferred:1;      // Bit 1: TX was deferred
    uint8_t aborted:1;       // Bit 2: TX aborted
    uint8_t single_coll:1;   // Bit 3: Single collision
    uint8_t mult_coll:1;     // Bit 4: Multiple collisions
    uint8_t underrun:1;      // Bit 5: FIFO underrun
    uint8_t jabber:1;        // Bit 6: Jabber condition
    uint8_t max_coll:1;      // Bit 7: 16 collisions
};
```

**Emulation Requirements:**
- Clear entire register on read (self-clearing)
- Return 0x88 for successful TX with 16 collisions
- Return 0x82 to indicate duplex mismatch
- Stack multiple status values if multiple packets complete

#### NET_DIAG (Window 4, Offset 0x06)
```c
struct net_diag_register {
    uint16_t revision:2;     // Bits 1-0: Chip revision
    uint16_t reserved1:4;    // Bits 5-2: Reserved
    uint16_t stats_en:1;     // Bit 6: Statistics enable
    uint16_t fifo_ok:1;      // Bit 7: FIFO test result
    uint16_t ext_loop:1;     // Bit 8: External loopback
    uint16_t sqe_or_int:1;   // Bit 9: SQE (AUI) or internal test
    uint16_t rx_ok:1;        // Bit 10: RX test result
    uint16_t link_or_stats:1; // Bit 11: Link beat or stats test
    uint16_t tx_ok:1;        // Bit 12: TX test result
    uint16_t upper_ok:1;     // Bit 13: Upper bytes test
    uint16_t reserved2:1;    // Bit 14: Reserved
    uint16_t full_dup:1;     // Bit 15: Full duplex enable
};
```

**Emulation Requirements:**
- Bit 11 reports link beat when 10BaseT active
- Bit 9 reports SQE test when AUI active
- Bits are read-only except 6 and 15
- Return consistent values across reads

### Command Processing

#### Window Selection
```c
void process_command(uint16_t cmd) {
    if ((cmd & 0xF800) == 0x0800) {  // SelectWindow command
        current_window = cmd & 0x07;
    }
}
```

**Requirements:**
- Window state persists until changed
- Some registers accessible from all windows
- Invalid window numbers wrap (window 8 = window 0)

#### TX Reset Sequence
```c
void tx_reset_sequence() {
    // 1. Set CmdInProgress bit
    status_reg |= 0x1000;
    
    // 2. Clear TX FIFO
    tx_fifo_clear();
    
    // 3. Reset TX state machine
    tx_state = TX_IDLE;
    
    // 4. Clear CmdInProgress after delay
    schedule_event(10, clear_cmd_in_progress);
}
```

### FIFO Behavior

#### RX FIFO
- 2KB total size
- Stores complete packets only
- Packet available when bit 15 of RX_STATUS is clear
- RxDiscard command removes current packet

#### TX FIFO  
- 2KB total size
- Threshold for TxAvailable interrupt (typically 1536 bytes)
- Drains to network at line speed
- TxReset clears entirely

### Timing Requirements

| Operation | Minimum | Typical | Maximum |
|-----------|---------|---------|---------|
| EEPROM Read | 150µs | 162µs | 200µs |
| Global Reset | 5ms | 10ms | 20ms |
| TX Reset | 100µs | 500µs | 1ms |
| RX Discard | 10µs | 50µs | 100µs |
| Window Select | 0 | 1µs | 5µs |

## 3C515 (Corkscrew) Additional Requirements

### Bus Master DMA

#### Descriptor Format
```c
struct dma_descriptor {
    uint32_t next;      // Physical address of next descriptor
    uint32_t status;    // Status/control field
    uint32_t addr;      // Buffer physical address
    uint32_t length;    // Buffer length and flags
};
```

**Status Field Bits:**
- Bit 31: Complete
- Bit 30: Error
- Bit 16: Download complete (TX only)
- Bits 12-0: Actual length

### ISA DMA Considerations
- 24-bit address limit (16MB)
- Must handle 64KB boundary crossing
- VDS (Virtual DMA Services) support required for EMM386

## 3C59x (Vortex/Boomerang) Requirements

### Chip Family Detection
```c
enum chip_family {
    VORTEX,     // 3C590, programmed I/O primary
    BOOMERANG,  // 3C900, full bus master
    CYCLONE,    // 3C905, enhanced bus master
    TORNADO     // 3C905B, gigabit ready
};
```

### Window 7 Bus Master Registers
| Offset | Register | Purpose |
|--------|----------|---------|
| 0x24 | DownListPtr | TX descriptor list |
| 0x38 | UpListPtr | RX descriptor list |
| 0x00 | MasterAddr | DMA address |
| 0x06 | MasterLen | DMA length |
| 0x0C | MasterStatus | DMA status |

## Common Emulation Pitfalls

### 1. Incorrect Error Codes
Using wrong bit patterns in error codes causes drivers to miscount error types.

### 2. Missing Side Effects
Forgetting that reading TX_STATUS clears it causes drivers to miss status updates.

### 3. Window State Loss
Not maintaining window selection across operations breaks multi-step sequences.

### 4. Timing Too Fast
Completing operations instantly can cause drivers to miss expected delays.

### 5. Missing Status Stacking
Not queuing multiple TX status values causes drivers to lose packet completion events.

## Validation Test Suite

### Basic Tests
1. EEPROM read of MAC address
2. Window selection and register access
3. Packet transmission and status check
4. Packet reception and error injection
5. Reset and recovery sequences

### Driver Compatibility Tests
1. Linux 3c509.c driver probe and init
2. DOS packet driver installation
3. Windows 95 PnP detection
4. mTCP DHCP and ping
5. Large packet transfer (FTP)

### Stress Tests
1. Rapid packet transmission
2. RX FIFO overflow handling
3. Collision and error recovery
4. Hot reset during operation
5. Maximum packet size handling

## Implementation Checklist

- [ ] RX_STATUS register with correct error codes
- [ ] TX_STATUS register with special values (0x88, 0x82)
- [ ] Self-clearing behavior for TX_STATUS
- [ ] NET_DIAG link detection bits
- [ ] Window selection persistence
- [ ] FIFO size enforcement
- [ ] Command completion delays
- [ ] Interrupt latch behavior
- [ ] EEPROM read timing
- [ ] Reset sequences
- [ ] Status register stacking
- [ ] Error injection for testing
- [ ] Statistics counter overflow
- [ ] Media type detection
- [ ] Full duplex operation

## Debugging Tips

### Driver Not Detecting Card
- Check EEPROM ID values (0x6D50 for 3C509B)
- Verify I/O port decode
- Ensure proper PnP response

### Packet Loss
- Verify FIFO thresholds
- Check interrupt generation
- Validate RX_STATUS bits

### Performance Issues
- Check for proper interrupt mitigation
- Verify DMA completion timing
- Monitor FIFO levels

## References

1. 3Com EtherLink III Technical Reference Manual
2. Linux kernel drivers/net/ethernet/3com/
3. DOS Packet Driver Specification v1.09
4. PCI Local Bus Specification 2.2 (for 3C59x)
5. ISA Bus Master DMA Controller Programming Guide