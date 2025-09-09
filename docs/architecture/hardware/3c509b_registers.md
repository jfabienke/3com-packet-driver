# 3Com 3C509B (EtherLink III) Register Map

## Overview
The 3C509B uses a windowed register architecture with 8 windows (0-7). All registers are accessed relative to the card's I/O base address (typically 0x300). The Command/Status register at offset 0x0E is always accessible regardless of the current window.

## Base I/O Address
- **Default**: 0x300 (configurable via EEPROM)
- **Range**: 0x200-0x3E0 in steps of 0x10
- **Size**: 16 bytes (0x00-0x0F)

## Global Registers (Always Accessible)

### Command/Status Register (0x0E)
**Write**: Send commands to NIC  
**Read**: Get NIC status

#### Command Format (Write to 0x0E)
```
Bits 15-11: Command Code
Bits 10-0:  Command Parameter (if applicable)
```

#### Commands
| Command | Code (Bits 15-11) | Parameter | Description |
|---------|-------------------|-----------|-------------|
| TOTAL_RESET | 0x00 | None | Complete hardware reset |
| SELECT_WINDOW | 0x01 | Window# (0-7) | Change active register window |
| START_COAX | 0x02 | None | Start BNC transceiver |
| RX_DISABLE | 0x03 | None | Disable receiver |
| RX_ENABLE | 0x04 | None | Enable receiver |
| RX_RESET | 0x05 | None | Reset receive logic |
| RX_DISCARD | 0x08 | None | Discard top RX packet |
| TX_ENABLE | 0x09 | None | Enable transmitter |
| TX_DISABLE | 0x0A | None | Disable transmitter |
| TX_RESET | 0x0B | None | Reset transmit logic |
| ACK_INTR | 0x0D | Status bits | Acknowledge interrupts |
| SET_INTR_ENB | 0x0E | Mask bits | Set interrupt enable mask |
| SET_STATUS_ENB | 0x0F | Mask bits | Set status enable mask |
| SET_RX_FILTER | 0x10 | Filter bits | Configure RX filter |
| SET_TX_THRESHOLD | 0x12 | Threshold | TX available threshold |
| SET_TX_START | 0x13 | Threshold | TX start threshold |
| STATS_ENABLE | 0x15 | None | Enable statistics |
| STATS_DISABLE | 0x16 | None | Disable statistics |
| STOP_COAX | 0x17 | None | Stop BNC transceiver |

#### Status Bits (Read from 0x0E)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | INT_LATCH | Interrupt occurred |
| 1 | ADAPTER_FAILURE | Hardware failure detected |
| 2 | TX_COMPLETE | Transmission completed |
| 3 | TX_AVAILABLE | TX FIFO has space |
| 4 | RX_COMPLETE | Packet received |
| 5 | RX_EARLY | Early RX (unused) |
| 6 | INT_REQ | Interrupt requested |
| 7 | STATS_FULL | Statistics counters updated |
| 12 | CMD_BUSY | Command in progress |

## Window 0: Configuration and EEPROM

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x03 | Reserved | - | Not used |
| 0x04-0x05 | CONFIG_CTRL | R/W | Configuration control |
| 0x06-0x07 | ADDR_CONFIG | R/W | Address configuration (I/O base) |
| 0x08-0x09 | IRQ_CONFIG | R/W | IRQ setting (bits 12-15) |
| 0x0A-0x0B | EEPROM_CMD | W | EEPROM command register |
| 0x0C-0x0D | EEPROM_DATA | R | EEPROM data register |

### EEPROM Commands (0x0A)
```
Bit 7: Read command (1)
Bit 6: Write command (1) - not used
Bits 5-0: EEPROM address (0-63)
```

### EEPROM Layout
| Address | Contents |
|---------|----------|
| 0x00 | Node Address 0-1 (MAC bytes 0-1) |
| 0x01 | Node Address 2-3 (MAC bytes 2-3) |
| 0x02 | Node Address 4-5 (MAC bytes 4-5) |
| 0x03 | Product ID (0x6D50 for 3C509B) |
| 0x08 | Manufacturing Date |
| 0x0A-0x0C | OEM Node Address |
| 0x0D | Software Configuration |
| 0x14 | Software Information |

## Window 1: Operating Status and TX/RX

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x01 | TX_FIFO/RX_FIFO | W/R | TX FIFO (write) / RX FIFO (read) |
| 0x02-0x03 | TX_FIFO/RX_FIFO | W/R | TX FIFO (write) / RX FIFO (read) |
| 0x04-0x05 | Reserved | - | Not used |
| 0x06-0x07 | Reserved | - | Not used |
| 0x08-0x09 | RX_STATUS | R | RX packet status |
| 0x0A | Timer | R/W | Latency timer |
| 0x0B | TX_STATUS | R | TX completion status |
| 0x0C-0x0D | TX_FREE | R | Free bytes in TX FIFO |

### RX Status Register (0x08)
```
Bit 15: Incomplete (packet not fully received)
Bit 14: Error (packet has error)
Bits 13-11: Error code (if bit 14 set)
Bits 10-0: Packet length in bytes
```

#### RX Error Codes (Bits 13-11 when Error bit 14 is set)
| Code | Error Type | Description |
|------|------------|-------------|
| 0x0000 | FIFO Overrun | Receive FIFO overflowed |
| 0x0800 | Oversize packet | Frame > 1518 bytes |
| 0x1000 | Framing/Dribble | Framing error or dribble bits |
| 0x1800 | Runt packet | Frame < 64 bytes |
| 0x2000 | Alignment error | Frame not byte-aligned |
| 0x2800 | CRC error | Failed CRC check |

### TX Status Register (0x0B)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | COMPLETE | Transmission finished |
| 1 | DEFERRED | Delayed due to medium busy |
| 2 | ABORTED | Transmission aborted |
| 3 | SINGLE_COLL | Single collision occurred |
| 4 | MULT_COLL | Multiple collisions |
| 5 | UNDERRUN | TX FIFO underrun |
| 6 | JABBER | Jabber timeout |
| 7 | MAX_COLL | Maximum collisions exceeded |

#### Driver-Specific TX Status Values
| Value | Meaning | Driver Action |
|-------|---------|---------------|
| 0x88 | Normal completion with 16 collisions | Often ignored by drivers |
| 0x82 | Duplex mismatch signature | Driver logs warning |
| 0x38 | Abort mask (bits 5-3) | Increment abort counter |
| 0x30 | Reset needed (bits 5-4) | Issue TxReset command |
| 0x3C | Any error (bits 5-2) | Issue TxEnable command |

## Window 2: Station Address Setup

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x01 | ADDR_0 | W | Station address bytes 0-1 |
| 0x02-0x03 | ADDR_2 | W | Station address bytes 2-3 |
| 0x04-0x05 | ADDR_4 | W | Station address bytes 4-5 |
| 0x06-0x0F | Reserved | - | Not used |

## Window 3: Multicast Setup
### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x07 | MULTICAST | W | Multicast filter bitmap |
| 0x08-0x0F | Reserved | - | Not used |

## Window 4: Diagnostics and Media

### Registers
| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| 0x00-0x01 | VCO_STATUS | R | VCO status |
| 0x02-0x03 | Reserved | - | Not used |
| 0x04-0x05 | FIFO_DIAG | R | FIFO diagnostic |
| 0x06-0x07 | NET_DIAG | R | Network diagnostic |
| 0x08-0x09 | Reserved | - | Not used |
| 0x0A-0x0B | MEDIA_STATUS | R/W | Media type and status |
| 0x0C-0x0F | Reserved | - | Not used |

### Media Status Register (0x0A)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | AUI_ENABLE | Enable AUI |
| 3 | SQE_ENABLE | Enable SQE for AUI |
| 6 | JABBER_ENABLE | Enable jabber detection |
| 7 | LINKBEAT_ENABLE | Enable link beat |
| 15 | FULL_DUPLEX | Enable full duplex |

### Network Diagnostic Register (0x06)
| Bit | Name | Description |
|-----|------|-------------|
| 15 | FD_ENABLE | Full-duplex enable (R/W) |
| 13 | UPPER_BYTES_OK | Upper bytes test OK |
| 12 | TX_OK | TX test OK |
| 11 | LINK_BEAT/STATS_OK | Link beat detect (10BaseT) OR Statistics test OK |
| 10 | RX_OK | RX test OK |
| 9 | SQE_TEST/INTERNAL_OK | SQE test (AUI) OR Internal test OK |
| 8 | EXTERNAL_WRAP | External loopback |
| 7 | FIFO_OK | FIFO test passed |
| 6 | STATS_ENABLE | Enable extended statistics (R/W) |
| 1-0 | REVISION | Hardware revision code |

**Note**: Bits 11 and 9 are overloaded - their meaning depends on the active media type. DOS drivers may check bit 9 (0x200) for early RX indication.

## Window 5: Command Results
Reserved for future use - not implemented in 3C509B

## Window 6: Statistics

### Registers (All Read-Only)
| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | TX_CARRIER_ERRORS | Carrier sense errors |
| 0x01 | TX_HEARTBEAT_ERRORS | SQE test errors |
| 0x02 | TX_MULT_COLLISIONS | Multiple collision count |
| 0x03 | TX_SINGLE_COLLISIONS | Single collision count |
| 0x04 | TX_LATE_COLLISIONS | Late collision count |
| 0x05 | RX_OVERRUNS | RX overrun count |
| 0x06 | TX_FRAMES_OK | Good TX frames |
| 0x07 | RX_FRAMES_OK | Good RX frames |
| 0x08 | TX_DEFERRALS | TX deferrals |
| 0x09 | Reserved | Not used |
| 0x0A-0x0B | RX_BYTES_OK | Total RX bytes |
| 0x0C-0x0D | TX_BYTES_OK | Total TX bytes |

## Window 7: Reserved
Not used in 3C509B

## Programming Sequences

### Initialization Sequence
```c
1. Global Reset: outw(base + 0x0E, 0x0000)
2. Wait 1ms minimum
3. Select Window 0: outw(base + 0x0E, 0x0800)
4. Read MAC from EEPROM addresses 0-2
5. Select Window 2: outw(base + 0x0E, 0x0802)
6. Write MAC address to registers 0x00-0x05
7. Select Window 1: outw(base + 0x0E, 0x0801)
8. Enable RX: outw(base + 0x0E, 0x2000)
9. Enable TX: outw(base + 0x0E, 0x4800)
10. Set RX filter: outw(base + 0x0E, 0x8005)
11. Enable interrupts: outw(base + 0x0E, 0x7098)
```

### Packet Transmission
```c
1. Select Window 1
2. Check TX_FREE (0x0C) >= packet_size
3. Write length to TX FIFO: outw(base + 0x00, length)
4. Write packet data to TX FIFO (0x00)
5. Issue TX start if needed
6. Wait for TX_COMPLETE interrupt
7. Read TX_STATUS (0x0B)
8. Acknowledge interrupt
```

### Packet Reception
```c
1. RX_COMPLETE interrupt occurs
2. Select Window 1
3. Read RX_STATUS (0x08)
4. Check for errors (bit 14)
5. Get packet length (bits 10-0)
6. Read packet from RX_FIFO (0x00)
7. Issue RX_DISCARD: outw(base + 0x0E, 0x4000)
8. Acknowledge interrupt
```

## Important Notes

1. **Window Selection**: Always select the appropriate window before accessing window-specific registers
2. **Command Busy**: Check CMD_BUSY bit before issuing new commands
3. **EEPROM Access**: Wait for EEPROM_BUSY bit to clear after commands
4. **Interrupt Handling**: Must acknowledge interrupts to clear them
5. **TX Threshold**: Setting TX threshold too low can cause underruns
6. **RX Discard**: Must discard packet to free RX FIFO for next packet

## Timing Requirements

- **Reset Delay**: 1ms minimum after TOTAL_RESET
- **EEPROM Read**: 162µs typical per word
- **ISA I/O Delay**: ~3.3µs per I/O operation
- **Command Completion**: Check CMD_BUSY, typically <10µs

## Media Types

| Type | Code | Description |
|------|------|-------------|
| AUI | 0x0 | Attachment Unit Interface |
| BNC | 0x1 | 10Base2 (Coax) |
| TP | 0x2 | 10BaseT (Twisted Pair) |
| AUTO | 0x3 | Auto-select (combo cards) |