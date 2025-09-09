# 3Com NIC Driver Compatibility Reference

This document details driver-specific behaviors and quirks discovered through analysis of production drivers. This information is critical for accurate hardware emulation.

## 3C509B Driver Behaviors

### Linux 3c509.c Driver
**Source**: Linux kernel drivers/net/ethernet/3com/3c509.c

#### RX Status Handling
```c
rx_status = inw(ioaddr + RX_STATUS);  // 16-bit read at offset 0x08
if (rx_status & 0x4000) {             // Bit 14: Error flag
    error = rx_status & 0x3800;       // Bits 13-11: Error code
    switch(error) {
        case 0x0000: rx_over_errors++;     // FIFO overrun
        case 0x0800: rx_length_errors++;   // Oversize packet
        case 0x1000: rx_frame_errors++;    // Framing error
        case 0x1800: rx_length_errors++;   // Runt packet
        case 0x2000: rx_frame_errors++;    // Alignment error
        case 0x2800: rx_crc_errors++;      // CRC error
    }
}
pkt_len = rx_status & 0x7ff;          // Bits 10-0: Packet length
```

#### TX Status Handling
```c
tx_status = inb(ioaddr + TX_STATUS);  // 8-bit read at offset 0x0B
if (tx_status & 0x38) tx_aborted_errors++;  // Bits 5-3: Abort conditions
if (tx_status & 0x30) outw(TxReset, ioaddr + EL3_CMD);  // Reset needed
if (tx_status & 0x3C) outw(TxEnable, ioaddr + EL3_CMD); // Re-enable TX
```

#### Special TX Status Values
- **0x88**: Normal completion with 16 collisions - drivers often ignore this
- **0x82**: Duplex mismatch signature - drivers log warning but continue

#### Link Detection
```c
// Window 4, NET_DIAG register at offset 0x06
tmp = inw(ioaddr + WN4_NETDIAG);
if (tmp & 0x0800)  // Bit 11: Link beat detect for 10BaseT
    link_up = true;
if (tmp & 0x0200)  // Bit 9: SQE test for AUI
    sqe_ok = true;
```

### DOS Packet Driver Behaviors
**Source**: Legacy 3C509.ASM drivers

#### Early RX Detection
DOS drivers check bit 9 (0x200) of RX_STATUS for early receive indication:
```assembly
in ax, dx           ; Read RX_STATUS
test ax, 0x200      ; Test bit 9
jnz rx_early        ; Handle early RX
```

#### Window 4 Quirk
DOS drivers clear bit 3 in Window 4 configuration (purpose unknown):
```assembly
mov ax, 0x804       ; Select Window 4
out dx, ax
add dx, byte -0x4   ; Point to W4 config
in ax, dx
and ax, 0xfff7      ; Clear bit 3
out dx, ax
```

## 3C515 (Corkscrew) Driver Behaviors

### Linux 3c515.c Driver

#### TX Status Handling
Similar to 3C509B but with enhanced error recovery:
```c
tx_status = inb(ioaddr + TxStatus);
if (tx_status & 0x3C) {  // Any TX-disabling error
    if (tx_status & 0x04) tx_fifo_errors++;
    if (tx_status & 0x38) tx_aborted_errors++;
    if (tx_status & 0x30) {
        outw(TxReset, ioaddr + EL3_CMD);
        // Wait for reset completion
        for (i = 20; i >= 0; i--)
            if (!(inw(ioaddr + EL3_STATUS) & CmdInProgress))
                break;
    }
}
```

#### Link Detection (100Mbps Support)
```c
media_status = inw(ioaddr + Wn4_Media);
if (media_status & Media_LnkBeat) {  // 0x0800
    // Link detected (10BaseT or 100BaseTX)
}
```

## 3C59x (Vortex/Boomerang) Driver Behaviors

### Linux 3c59x.c Driver

#### Vortex vs Boomerang Detection
Driver differentiates between chip families:
```c
if (vp->capabilities & BOOMERANG_BUS_MASTER) {
    // Use descriptor-based DMA
} else {
    // Use programmed I/O
}
```

#### TX Status Special Handling
```c
tx_status = ioread8(ioaddr + TxStatus);
if (tx_status != 0x88 && vortex_debug > 0) {
    if (tx_status == 0x82) {
        pr_err("Probably a duplex mismatch");
    }
}
```

#### Descriptor Status Checking
For bus-master mode:
```c
// RX Descriptor
if (rx_status & RxDComplete) {       // 0x00008000
    if (rx_status & RxDError) {      // 0x00004000
        rx_error = rx_status >> 16;  // Error in upper 16 bits
    }
    pkt_len = rx_status & 0x1fff;    // Bits 12-0
}

// TX Descriptor  
if (vp->tx_ring[entry].status & DN_COMPLETE) {  // 0x00010000
    // Transmission complete
}
```

## Common Driver Workarounds

### 1. Multiple Status Reads
Most drivers read status registers multiple times to ensure all conditions are handled:
```c
int i = 4;
while (--i > 0 && (tx_status = inb(ioaddr + TX_STATUS)) > 0) {
    // Handle each status update
    outb(0x00, ioaddr + TX_STATUS);  // Clear status
}
```

### 2. Reset Sequencing
Drivers follow specific reset sequences:
1. Issue reset command
2. Wait for command completion (check CmdInProgress bit)
3. Re-enable interface
4. Restore interrupt mask

### 3. FIFO Draining
Before reset, drivers ensure FIFOs are empty:
```c
while (inw(ioaddr + EL3_STATUS) & 0x1000) {
    // Wait for RX discard to complete
}
```

### 4. Timing Considerations
- EEPROM reads: 162µs delay between operations
- Reset completion: Up to 10ms wait
- Link detection: 3-second timeout for auto-negotiation

## Emulation Requirements

### Critical Behaviors to Emulate

1. **Status Register Clearing**
   - TX_STATUS clears on read
   - RX_STATUS requires explicit RxDiscard command

2. **Error Bit Patterns**
   - Always set bit 14 (0x4000) for RX errors
   - Use exact error codes in bits 13-11
   - Return 0x88 for normal TX completion with collisions

3. **Window Selection**
   - Window state must persist across operations
   - Some registers accessible regardless of window

4. **Interrupt Behavior**
   - IntLatch bit must be set when any enabled interrupt occurs
   - AckIntr command must clear specific interrupt bits

5. **Link Detection Timing**
   - Link beat detection should stabilize within 100ms
   - Report consistent status across multiple reads

### Compatibility Testing

To verify emulation accuracy, test against:
1. Linux 3c509.c driver initialization sequence
2. DOS packet driver PKTSEND/PKTRECV utilities
3. Windows 95/98 built-in 3C509 driver
4. mTCP stack for DOS
5. FreeBSD if_ep driver (if available)

## References

- Linux kernel source: drivers/net/ethernet/3com/
- Donald Becker's original drivers: http://www.scyld.com/network/
- 3Com Technical References (archived)
- DOS Packet Driver Specification v1.09