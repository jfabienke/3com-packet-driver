# 3Com NIC Initialization Sequences

## Overview
This document provides detailed, step-by-step initialization sequences for both 3C509B and 3C515-TX NICs. These sequences are critical for QEMU emulation to accurately reproduce hardware behavior.

## 3C509B Initialization Sequence

### Phase 1: Hardware Reset and Detection
```c
// Step 1: Issue Global Reset (1ms minimum delay required)
outw(base + 0x0E, 0x0000);  // TOTAL_RESET command
delay_ms(2);                 // Conservative 2ms delay

// Step 2: Verify card presence
outw(base + 0x0E, 0x0800);  // SELECT_WINDOW(0)
uint16_t product_id = read_eeprom(base, 0x03);
if (product_id != 0x6D50) {
    // Not a 3C509B
    return ERROR_NOT_FOUND;
}

// Step 3: Read hardware configuration from EEPROM
uint16_t config_ctrl = inw(base + 0x04);
uint16_t addr_config = inw(base + 0x06);
uint16_t irq_config = inw(base + 0x08);
```

### Phase 2: MAC Address Configuration
```c
// Step 4: Read MAC address from EEPROM
outw(base + 0x0E, 0x0800);  // SELECT_WINDOW(0)
uint16_t mac_word0 = read_eeprom(base, 0x00);  // Bytes 0-1
uint16_t mac_word1 = read_eeprom(base, 0x01);  // Bytes 2-3
uint16_t mac_word2 = read_eeprom(base, 0x02);  // Bytes 4-5

// Step 5: Write MAC address to station address registers
outw(base + 0x0E, 0x0802);  // SELECT_WINDOW(2)
outw(base + 0x00, mac_word0);
outw(base + 0x02, mac_word1);
outw(base + 0x04, mac_word2);
```

### Phase 3: Media Configuration
```c
// Step 6: Configure transceiver type
outw(base + 0x0E, 0x0804);  // SELECT_WINDOW(4)
uint16_t media_status = inw(base + 0x0A);

// Auto-detect media type
if (config_ctrl & 0x8000) {  // Auto-select enabled
    // Try 10BaseT first
    media_status = 0x00C0;  // Enable link beat
    outw(base + 0x0A, media_status);
    delay_ms(50);
    
    uint16_t net_diag = inw(base + 0x06);
    if (!(net_diag & 0x0080)) {  // No link
        // Try BNC
        media_status = 0x0001;  // Enable BNC
        outw(base + 0x0A, media_status);
        outw(base + 0x0E, 0x1000);  // START_COAX command
    }
} else {
    // Use configured media type from EEPROM
    uint8_t xcvr_type = (config_ctrl >> 14) & 0x03;
    switch (xcvr_type) {
        case 0: media_status = 0x0001; break;  // AUI
        case 1: media_status = 0x0002; break;  // BNC
        case 2: media_status = 0x00C0; break;  // 10BaseT
    }
    outw(base + 0x0A, media_status);
}
```

### Phase 4: Operational Configuration
```c
// Step 7: Configure RX filter
outw(base + 0x0E, 0x0801);  // SELECT_WINDOW(1)
outw(base + 0x0E, 0x8005);  // SET_RX_FILTER: Accept broadcast + individual

// Step 8: Set TX thresholds
outw(base + 0x0E, 0x9800);  // SET_TX_THRESHOLD: 1536 bytes
outw(base + 0x0E, 0x9000);  // SET_TX_START: 256 bytes

// Step 9: Enable statistics
outw(base + 0x0E, 0xA800);  // STATS_ENABLE

// Step 10: Clear all pending interrupts
outw(base + 0x0E, 0x6FFF);  // ACK_INTR: Clear all

// Step 11: Enable interrupts
outw(base + 0x0E, 0x7098);  // SET_INTR_ENB: TX_COMPLETE | RX_COMPLETE | ADAPTER_FAILURE

// Step 12: Enable receiver
outw(base + 0x0E, 0x2000);  // RX_ENABLE

// Step 13: Enable transmitter
outw(base + 0x0E, 0x4800);  // TX_ENABLE
```

### EEPROM Read Sequence
```c
uint16_t read_eeprom(uint16_t base, uint8_t addr) {
    // Select Window 0
    outw(base + 0x0E, 0x0800);
    
    // Issue read command
    outw(base + 0x0A, 0x80 | (addr & 0x3F));
    
    // Wait for completion (162µs typical)
    uint16_t timeout = 1000;
    while (--timeout) {
        if (!(inw(base + 0x0A) & 0x8000)) {
            break;
        }
        delay_us(1);
    }
    
    // Read data
    return inw(base + 0x0C);
}
```

## 3C515-TX Initialization Sequence

### Phase 1: Hardware Reset and Detection
```c
// Step 1: Issue Global Reset (10ms minimum delay required)
outw(base + 0x0E, 0x0000);  // TOTAL_RESET command
delay_ms(15);                // Conservative 15ms delay

// Step 2: Verify card presence and type
outw(base + 0x0E, 0x0800);  // SELECT_WINDOW(0)
uint16_t model_id = read_eeprom_515(base, 0x03);
if (model_id != 0x5051) {  // 3C515-TX model ID
    return ERROR_NOT_FOUND;
}

// Step 3: Check for bus master support
uint16_t capabilities = inw(base + 0x08);
bool bus_master_capable = (capabilities & 0x0020) != 0;
```

### Phase 2: MAC Address and Basic Configuration
```c
// Step 4: Read MAC address from EEPROM (different offset for 515)
outw(base + 0x0E, 0x0800);  // SELECT_WINDOW(0)
uint16_t mac_word0 = read_eeprom_515(base, 0x00);
uint16_t mac_word1 = read_eeprom_515(base, 0x01);
uint16_t mac_word2 = read_eeprom_515(base, 0x02);

// Step 5: Write MAC address
outw(base + 0x0E, 0x0802);  // SELECT_WINDOW(2)
outw(base + 0x00, mac_word0);
outw(base + 0x02, mac_word1);
outw(base + 0x04, mac_word2);

// Step 6: Configure options
outw(base + 0x0E, 0x0803);  // SELECT_WINDOW(3)
uint32_t config = inl(base + 0x00);

// Set transceiver type (100baseTx)
config &= ~(0x0F << 20);  // Clear XCVR bits
config |= (0x04 << 20);   // Set to 100baseTx

// Enable auto-negotiation if supported
if (config & (1 << 24)) {  // AUTOSELECT bit
    config |= (1 << 24);
}

outl(base + 0x00, config);
```

### Phase 3: DMA Configuration (Bus Master Mode)
```c
if (bus_master_capable) {
    // Step 7: Allocate DMA descriptors (must be below 16MB for ISA)
    struct dma_descriptor *tx_ring = allocate_dma_memory(sizeof(struct dma_descriptor) * 16);
    struct dma_descriptor *rx_ring = allocate_dma_memory(sizeof(struct dma_descriptor) * 16);
    
    // Step 8: Initialize RX descriptors
    for (int i = 0; i < 16; i++) {
        rx_ring[i].next = physical_addr(&rx_ring[(i + 1) % 16]);
        rx_ring[i].status = 0;
        rx_ring[i].addr = physical_addr(rx_buffers[i]);
        rx_ring[i].length = 1536;  // Standard MTU + headers
    }
    
    // Step 9: Initialize TX descriptors
    for (int i = 0; i < 16; i++) {
        tx_ring[i].next = physical_addr(&tx_ring[(i + 1) % 16]);
        tx_ring[i].status = 0;
        tx_ring[i].addr = 0;  // Will be set when transmitting
        tx_ring[i].length = 0;
    }
    
    // Step 10: Configure DMA registers
    outw(base + 0x0E, 0x0807);  // SELECT_WINDOW(7)
    outl(base + 0x38, physical_addr(rx_ring));  // UP_LIST_PTR
    outl(base + 0x24, physical_addr(tx_ring));  // DOWN_LIST_PTR
    
    // Step 11: Set DMA thresholds
    outw(base + 0x40F, 0x0020);  // TX_FREE_THRESH: 32 bytes
}
```

### Phase 4: Media Selection and MII Configuration
```c
// Step 12: Check for MII PHY
outw(base + 0x0E, 0x0804);  // SELECT_WINDOW(4)
uint16_t phy_id = mii_read(base, 0, 2);  // Read PHY ID

if (phy_id != 0xFFFF) {
    // Step 13: Configure PHY for auto-negotiation
    uint16_t bmcr = mii_read(base, 0, 0);  // Read control register
    bmcr |= 0x1200;  // Enable auto-neg and restart
    mii_write(base, 0, 0, bmcr);
    
    // Step 14: Wait for auto-negotiation (up to 3 seconds)
    uint16_t timeout = 3000;
    while (timeout--) {
        uint16_t bmsr = mii_read(base, 0, 1);
        if (bmsr & 0x0020) {  // Auto-neg complete
            break;
        }
        delay_ms(1);
    }
    
    // Step 15: Read negotiated capabilities
    uint16_t lpar = mii_read(base, 0, 5);  // Link partner ability
    bool full_duplex = (lpar & 0x0140) != 0;
    bool speed_100 = (lpar & 0x0180) != 0;
    
    // Configure MAC for negotiated mode
    outw(base + 0x0E, 0x0803);  // SELECT_WINDOW(3)
    uint16_t mac_ctrl = inw(base + 0x06);
    if (full_duplex) {
        mac_ctrl |= 0x0020;  // Enable full duplex
    }
    outw(base + 0x06, mac_ctrl);
}
```

### Phase 5: Operational Enable
```c
// Step 16: Configure interrupts and filters
outw(base + 0x0E, 0x0801);  // SELECT_WINDOW(1)

// Set RX filter
outw(base + 0x0E, 0x8005);  // SET_RX_FILTER: Broadcast + Individual

// Set TX configuration
outw(base + 0x0E, 0x9800);  // SET_TX_THRESHOLD: 1536 bytes

// Clear pending interrupts
outw(base + 0x0E, 0x6FFF);  // ACK_INTR: All

// Enable interrupts
if (bus_master_capable) {
    // Include DMA interrupts
    outw(base + 0x0E, 0x7698);  // SET_INTR_ENB: Standard + DMA
} else {
    // PIO mode only
    outw(base + 0x0E, 0x7098);  // SET_INTR_ENB: Standard only
}

// Step 17: Start operations
outw(base + 0x0E, 0x2000);  // RX_ENABLE
outw(base + 0x0E, 0x4800);  // TX_ENABLE

if (bus_master_capable) {
    // Start DMA engines
    outw(base + 0x0E, 0x3001);  // UP_UNSTALL (RX DMA)
    outw(base + 0x0E, 0x3003);  // DOWN_UNSTALL (TX DMA)
}
```

### EEPROM Read Sequence (3C515)
```c
uint16_t read_eeprom_515(uint16_t base, uint8_t addr) {
    // 3C515 uses different EEPROM offsets
    outw(base + 0x0E, 0x0800);  // SELECT_WINDOW(0)
    
    // Issue read command at offset 0x200A
    outw(base + 0x200A, 0x80 | (addr & 0x3F));
    
    // Wait for completion (200µs typical for 3C515)
    uint16_t timeout = 1000;
    while (--timeout) {
        if (!(inw(base + 0x200A) & 0x8000)) {
            break;
        }
        delay_us(1);
    }
    
    // Read data from offset 0x200C
    return inw(base + 0x200C);
}
```

### MII PHY Access
```c
uint16_t mii_read(uint16_t base, uint8_t phy_addr, uint8_t reg_addr) {
    outw(base + 0x0E, 0x0804);  // SELECT_WINDOW(4)
    
    // Construct MII read command
    uint32_t cmd = 0x60000000;  // Start + Read
    cmd |= (phy_addr & 0x1F) << 23;
    cmd |= (reg_addr & 0x1F) << 18;
    
    // Write command to MII_READ register
    outl(base + 0x0800, cmd);
    
    // Wait for completion
    uint16_t timeout = 1000;
    while (timeout--) {
        uint32_t status = inl(base + 0x0800);
        if (!(status & 0x10000000)) {  // Not busy
            return status & 0xFFFF;
        }
        delay_us(1);
    }
    
    return 0xFFFF;  // Timeout
}

void mii_write(uint16_t base, uint8_t phy_addr, uint8_t reg_addr, uint16_t data) {
    outw(base + 0x0E, 0x0804);  // SELECT_WINDOW(4)
    
    // Construct MII write command
    uint32_t cmd = 0x50000000;  // Start + Write
    cmd |= (phy_addr & 0x1F) << 23;
    cmd |= (reg_addr & 0x1F) << 18;
    cmd |= data & 0xFFFF;
    
    // Write command to MII_WRITE register
    outl(base + 0x0A00, cmd);
    
    // Wait for completion
    uint16_t timeout = 1000;
    while (timeout--) {
        uint32_t status = inl(base + 0x0A00);
        if (!(status & 0x10000000)) {  // Not busy
            break;
        }
        delay_us(1);
    }
}
```

## Critical Timing Requirements

### 3C509B Timing
- **Reset Recovery**: 1ms minimum after TOTAL_RESET
- **EEPROM Access**: 162µs typical per word read
- **Command Execution**: <10µs typical (check CMD_BUSY)
- **Media Detection**: 50ms delay for link beat detection
- **ISA I/O Cycle**: ~3.3µs per operation

### 3C515-TX Timing
- **Reset Recovery**: 10ms minimum after TOTAL_RESET
- **EEPROM Access**: 200µs typical per word read
- **Auto-Negotiation**: Up to 3 seconds
- **MII Access**: ~25µs per transaction
- **DMA Setup**: Must complete before enabling DMA engines

## Error Recovery Sequences

### Link Loss Recovery
```c
void handle_link_loss() {
    // 1. Disable RX/TX
    outw(base + 0x0E, 0x1800);  // RX_DISABLE
    outw(base + 0x0E, 0x5000);  // TX_DISABLE
    
    // 2. Reset RX/TX logic
    outw(base + 0x0E, 0x2800);  // RX_RESET
    outw(base + 0x0E, 0x5800);  // TX_RESET
    
    // 3. Re-detect media
    // ... (media detection sequence)
    
    // 4. Re-enable RX/TX
    outw(base + 0x0E, 0x2000);  // RX_ENABLE
    outw(base + 0x0E, 0x4800);  // TX_ENABLE
}
```

### DMA Error Recovery (3C515)
```c
void handle_dma_error() {
    // 1. Stall DMA engines
    outw(base + 0x0E, 0x3000);  // UP_STALL
    outw(base + 0x0E, 0x3002);  // DOWN_STALL
    
    // 2. Reset descriptor pointers
    outw(base + 0x0E, 0x0807);  // SELECT_WINDOW(7)
    outl(base + 0x38, physical_addr(rx_ring));
    outl(base + 0x24, physical_addr(tx_ring));
    
    // 3. Clear error status
    uint32_t status = inl(base + 0x0C);
    
    // 4. Restart DMA engines
    outw(base + 0x0E, 0x3001);  // UP_UNSTALL
    outw(base + 0x0E, 0x3003);  // DOWN_UNSTALL
}
```

## State Machine Considerations

### Initialization State Machine
```
RESET -> DETECT -> CONFIG_MAC -> CONFIG_MEDIA -> CONFIG_DMA -> OPERATIONAL
   |        |          |             |              |
   v        v          v             v              v
 ERROR    ERROR      ERROR         ERROR          ERROR
```

### Operational State Transitions
```
IDLE <-> TRANSMITTING
  ^           |
  |           v
  +---- TX_COMPLETE
  
IDLE <-> RECEIVING  
  ^           |
  |           v
  +---- RX_COMPLETE
```

## QEMU Implementation Notes

1. **Register Access Patterns**: The driver expects immediate register updates after writes
2. **EEPROM Emulation**: Must return valid data with proper timing delays
3. **Interrupt Generation**: Must match hardware timing for TX/RX completion
4. **DMA Simulation**: For 3C515, must handle 24-bit addressing and 64KB boundaries
5. **Window State**: Must maintain current window selection across accesses
6. **Command Processing**: Some commands have delayed effects (e.g., RESET)
7. **Status Bits**: Must update status register based on internal state
8. **Media Detection**: Should simulate link state for configured media type

## Validation Checklist

- [ ] EEPROM returns correct MAC address and configuration
- [ ] Window selection persists between accesses
- [ ] Commands complete with appropriate delays
- [ ] Status bits reflect accurate hardware state
- [ ] Interrupts generated at correct times
- [ ] DMA descriptors processed in order (3C515)
- [ ] Media detection returns expected results
- [ ] Error conditions handled gracefully
- [ ] Reset sequence completes successfully
- [ ] Packet transmission/reception works end-to-end