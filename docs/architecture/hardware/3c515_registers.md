# 3Com 3C515-TX (Corkscrew) Register Map

## Overview
The 3C515-TX is a 100 Mbps Fast Ethernet ISA NIC with bus mastering capabilities. It uses a windowed register architecture similar to the 3C509B but adds DMA support and 100 Mbps features. The card supports 8 register windows with the Command/Status register at 0x0E always accessible.

## Key Features
- **ISA Bus Master DMA**: Rare ISA bus mastering capability
- **100 Mbps Support**: Fast Ethernet on ISA bus
- **Dual Operation Modes**: PIO and DMA modes
- **Extended I/O Range**: 32 bytes (vs 16 for 3C509B)

## Base I/O Address
- **Default**: 0x300 (configurable)
- **Range**: 0x200-0x3E0 in steps of 0x20
- **Size**: 32 bytes (0x00-0x1F) + DMA registers at 0x400 offset

## Global Registers (Always Accessible)

### Command/Status Register (0x0E)
**Write**: Send commands  
**Read**: Get status

#### Commands (Write to 0x0E)
| Command | Code (Bits 15-11) | Parameter | Description |
|---------|-------------------|-----------|-------------|
| TOTAL_RESET | 0x00 | None | Complete hardware reset |
| SELECT_WINDOW | 0x01 | Window# | Change active window (0-7) |
| START_COAX | 0x02 | None | Start coax transceiver |
| RX_DISABLE | 0x03 | None | Disable receiver |
| RX_ENABLE | 0x04 | None | Enable receiver |
| RX_RESET | 0x05 | None | Reset RX logic |
| UP_STALL | 0x06 | 0 | Stall RX DMA |
| UP_UNSTALL | 0x06 | 1 | Unstall RX DMA |
| DOWN_STALL | 0x06 | 2 | Stall TX DMA |
| DOWN_UNSTALL | 0x06 | 3 | Unstall TX DMA |
| RX_DISCARD | 0x08 | None | Discard top RX packet |
| TX_ENABLE | 0x09 | None | Enable transmitter |
| TX_DISABLE | 0x0A | None | Disable transmitter |
| TX_RESET | 0x0B | None | Reset TX logic |
| ACK_INTR | 0x0D | Status bits | Acknowledge interrupts |
| SET_INTR_ENB | 0x0E | Mask bits | Set interrupt enable |
| SET_STATUS_ENB | 0x0F | Mask bits | Set status enable |
| SET_RX_FILTER | 0x10 | Filter bits | Configure RX filter |
| SET_TX_THRESHOLD | 0x12 | Threshold | TX FIFO threshold |
| SET_TX_START | 0x13 | Threshold | TX start threshold |
| START_DMA_UP | 0x14 | 0 | Start RX DMA |
| START_DMA_DOWN | 0x14 | 1 | Start TX DMA |
| STATS_ENABLE | 0x15 | None | Enable statistics |
| STATS_DISABLE | 0x16 | None | Disable statistics |
| STOP_COAX | 0x17 | None | Stop coax transceiver |

#### Status Bits (Read from 0x0E)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | INT_LATCH | Interrupt occurred |
| 1 | ADAPTER_FAILURE | Hardware failure |
| 2 | TX_COMPLETE | Transmission complete |
| 3 | TX_AVAILABLE | TX FIFO has space |
| 4 | RX_COMPLETE | Packet received |
| 5 | RX_EARLY | Early RX (unused) |
| 6 | INT_REQ | Interrupt request |
| 7 | STATS_FULL | Statistics updated |
| 8 | DMA_DONE | DMA transfer complete |
| 9 | DOWN_COMPLETE | TX DMA complete |
| 10 | UP_COMPLETE | RX DMA complete |
| 11 | DMA_IN_PROGRESS | DMA active |
| 12 | CMD_IN_PROGRESS | Command busy |

## Window 0: EEPROM Access

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x08 | IRQ_CONFIG | R/W | IRQ configuration |
| 0x0A | EEPROM_CMD | W | EEPROM command (offset 0x200A for 3C515) |
| 0x0C | EEPROM_DATA | R | EEPROM data (offset 0x200C for 3C515) |

### EEPROM Commands
```
Bit 7: Read (1)
Bit 6: Write (1) - not used
Bits 5-0: Address (0-63)
```

### EEPROM Layout
| Address | Contents |
|---------|----------|
| 0x00 | MAC Address bytes 0-1 |
| 0x01 | MAC Address bytes 2-3 |
| 0x02 | MAC Address bytes 4-5 |
| 0x03 | Model ID (0x5051 for 3C515-TX) |
| 0x07 | 3Com ID (0x6D50) |

## Window 1: Operating Status

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x10 | TX_FIFO/RX_FIFO | W/R | TX FIFO (write) / RX FIFO (read) |
| 0x14 | RX_ERRORS | R | RX error counters |
| 0x18 | RX_STATUS | R | RX packet status |
| 0x1A | TIMER | R/W | Timer register |
| 0x1B | TX_STATUS | R | TX completion status |
| 0x1C-0x1D | TX_FREE | R | Free bytes in TX FIFO |

### RX Status (0x18)
```
Bit 15: Incomplete
Bit 14: Error
Bits 12-0: Packet length (up to 8191 bytes)
```

### TX Status (0x1B)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | COMPLETE | TX finished |
| 1 | DEFERRED | Delayed |
| 2 | ABORTED | TX aborted |
| 3 | SINGLE_COLL | Single collision |
| 4 | MULT_COLL | Multiple collisions |
| 5 | UNDERRUN | FIFO underrun |
| 6 | JABBER | Jabber error |
| 7 | MAX_COLL | Maximum collisions |

## Window 2: Station Address

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x01 | ADDR_0 | W | MAC bytes 0-1 |
| 0x02-0x03 | ADDR_2 | W | MAC bytes 2-3 |
| 0x04-0x05 | ADDR_4 | W | MAC bytes 4-5 |

## Window 3: Configuration

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x03 | CONFIG | R/W | Configuration register |
| 0x06-0x07 | MAC_CTRL | R/W | MAC control |
| 0x08-0x09 | OPTIONS | R/W | Options register |

### Configuration Register Bits
| Bits | Name | Description |
|------|------|-------------|
| 2-0 | RAM_SIZE | RAM size configuration |
| 3 | RAM_WIDTH | RAM width (8/16 bit) |
| 5-4 | RAM_SPEED | RAM access speed |
| 7-6 | ROM_SIZE | Boot ROM size |
| 19-16 | RAM_SPLIT | TX/RX buffer split |
| 23-20 | XCVR | Transceiver type |
| 24 | AUTOSELECT | Auto-select media |

### Transceiver Types
| Value | Type | Description |
|-------|------|-------------|
| 0 | 10baseT | 10 Mbps twisted pair |
| 1 | AUI | Attachment Unit Interface |
| 2 | 10baseTOnly | 10baseT only |
| 3 | 10base2 | Coax/BNC |
| 4 | 100baseTx | 100 Mbps twisted pair |
| 5 | 100baseFx | 100 Mbps fiber |
| 6 | MII | Media Independent Interface |
| 8 | Default | Use EEPROM default |

## Window 4: Diagnostics and Media

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x06 | NET_DIAG | R | Network diagnostics |
| 0x0A | MEDIA | R/W | Media control |
| 0x08 | MII_READ | R/W | MII read (offset 0x0800) |
| 0x0A | MII_WRITE | W | MII write (offset 0x0A00) |

### Media Control Bits (0x0A)
| Bit | Name | Description |
|-----|------|-------------|
| 3 | SQE_ENABLE | Enable SQE for AUI |
| 7-6 | TP_SELECT | 10baseT selection |
| 11 | LINK_BEAT | Link beat detection |

## Window 5: Reserved
Not used in 3C515

## Window 6: Statistics

### Registers (All Read-Only, Clear on Read)
| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | TX_CARRIER_ERRORS | Carrier errors |
| 0x01 | TX_HEARTBEAT_ERRORS | SQE errors |
| 0x02 | TX_MULT_COLLISIONS | Multiple collisions |
| 0x03 | TX_SINGLE_COLLISIONS | Single collisions |
| 0x04 | TX_LATE_COLLISIONS | Late collisions |
| 0x05 | RX_OVERRUNS | RX overruns |
| 0x06 | TX_FRAMES_OK | Good TX frames |
| 0x07 | RX_FRAMES_OK | Good RX frames |
| 0x08 | TX_DEFERRALS | TX deferrals |
| 0x0C | BAD_SSD | Bad Start of Stream Delimiter |

## Window 7: Bus Master Control

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x03 | MASTER_ADDR | R/W | DMA physical address |
| 0x06-0x07 | MASTER_LEN | R/W | DMA transfer length |
| 0x0C | MASTER_STATUS | R | DMA status |
| 0x20 | DOWN_PKT_STATUS | R | TX DMA packet status |
| 0x24-0x27 | DOWN_LIST_PTR | R/W | TX descriptor list pointer |
| 0x30 | UP_PKT_STATUS | R | RX DMA packet status |
| 0x38-0x3B | UP_LIST_PTR | R/W | RX descriptor list pointer |

## DMA Registers (Base + 0x400)

### DMA Control Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x400 | PKT_STATUS | R | Packet status |
| 0x404 | DOWN_LIST_PTR | R/W | TX descriptor list |
| 0x408 | FRAG_ADDR | R/W | Fragment address |
| 0x40C | FRAG_LEN | R/W | Fragment length |
| 0x40F | TX_FREE_THRESH | R/W | TX threshold |
| 0x410 | UP_PKT_STATUS | R | RX packet status |
| 0x418 | UP_LIST_PTR | R/W | RX descriptor list |
| 0x41C | UP_POLL | W | RX poll demand |
| 0x408 | DOWN_POLL | W | TX poll demand |

## DMA Descriptor Format

### Descriptor Structure (16 bytes)
```c
struct dma_descriptor {
    uint32_t next;      // Physical address of next descriptor
    uint32_t status;    // Status and control bits
    uint32_t addr;      // Physical buffer address
    uint32_t length;    // Buffer length and flags
};
```

### Descriptor Status Bits
| Bit | Name | Description |
|-----|------|-------------|
| 15 | COMPLETE | Descriptor complete |
| 14 | ERROR | Error occurred |
| 13 | LAST | Last descriptor |
| 12 | FIRST | First descriptor |
| 16 | DN_COMPLETE | Download complete |
| 17 | UP_COMPLETE | Upload complete |

### DMA Constraints (ISA)
- **24-bit Address Limit**: 16MB (0xFFFFFF)
- **64KB Boundary**: Cannot cross 64KB boundaries
- **Bounce Buffers**: Required for boundary violations

## Programming Sequences

### Initialization with DMA
```c
1. Global Reset: outw(base + 0x0E, 0x0000)
2. Wait 10ms
3. Select Window 0: outw(base + 0x0E, 0x0800)
4. Read MAC from EEPROM
5. Select Window 2: outw(base + 0x0E, 0x0802)
6. Write MAC address
7. Select Window 3: outw(base + 0x0E, 0x0803)
8. Configure transceiver and options
9. Select Window 7: outw(base + 0x0E, 0x0807)
10. Setup DMA descriptor lists
11. Write UP_LIST_PTR and DOWN_LIST_PTR
12. Select Window 1: outw(base + 0x0E, 0x0801)
13. Enable RX/TX
14. Enable DMA: UP_UNSTALL, DOWN_UNSTALL
15. Enable interrupts
```

### DMA Transmission
```c
1. Build TX descriptor in memory
2. Set descriptor addr = packet physical address
3. Set descriptor length = packet size | LAST | FIRST
4. Write descriptor address to DOWN_LIST_PTR
5. Issue DOWN_UNSTALL command
6. Wait for DOWN_COMPLETE interrupt
7. Check descriptor status
```

### DMA Reception
```c
1. Build RX descriptor ring
2. Allocate buffers for each descriptor
3. Set descriptor addr = buffer physical address
4. Set descriptor length = buffer size
5. Write first descriptor to UP_LIST_PTR
6. Issue UP_UNSTALL command
7. On UP_COMPLETE interrupt:
   - Check descriptor status
   - Process packet
   - Reset descriptor
   - Issue UP_UNSTALL
```

## ISA Bus Master Specifics

### VDS (Virtual DMA Services) Support
- Required for EMM386/QEMM compatibility
- Translates virtual to physical addresses
- Handles DMA buffer locking

### Bus Master Testing
```c
1. Check chipset compatibility
2. Test small DMA transfer
3. Verify data integrity
4. Test 64KB boundary handling
5. Validate VDS if present
```

## Media Selection

### Auto-Negotiation (100baseTx)
1. Check MII PHY presence
2. Read PHY capabilities
3. Advertise capabilities
4. Wait for link partner
5. Select highest common mode

### Manual Selection
1. Select Window 4
2. Write media type to MEDIA register
3. Check NET_DIAG for link status
4. Monitor for link changes

## Important Notes

1. **Bus Mastering**: Not all ISA chipsets support bus mastering
2. **24-bit Limit**: ISA DMA limited to 16MB address space
3. **64KB Boundaries**: Use bounce buffers for crossing
4. **Timing**: ISA bus slower than PCI, adjust timeouts
5. **Interrupts**: May share IRQ with other ISA devices
6. **Reset Delay**: 10ms minimum after TOTAL_RESET
7. **EEPROM Delay**: 200µs typical for 3C515

## Differences from 3C509B

| Feature | 3C509B | 3C515-TX |
|---------|--------|----------|
| Speed | 10 Mbps | 10/100 Mbps |
| DMA | No | Yes (Bus Master) |
| I/O Range | 16 bytes | 32 bytes + DMA |
| Windows | 8 (5 used) | 8 (7 used) |
| FIFO Size | 2KB | 8KB |
| MII Support | No | Yes |
| Auto-Neg | No | Yes |