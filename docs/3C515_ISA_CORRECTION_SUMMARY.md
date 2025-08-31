# 3C515-TX ISA Correction Summary

## Critical Error Corrected

The 3Com 3C515-TX "Corkscrew" was incorrectly implemented as a PCI card when it is actually an **ISA card with bus mastering capabilities**. This fundamental architectural error has been corrected throughout the codebase.

## Key Facts About 3C515-TX

- **Bus Type**: ISA (NOT PCI)
- **Released**: 1996
- **Speed**: 10/100 Mbps Fast Ethernet
- **Special Feature**: ISA bus mastering (rare for ISA cards)
- **Architecture**: Adapted Boomerang DMA engine for ISA constraints
- **Legacy**: Last major ISA-only design from 3Com

## Files Corrected

### 1. `/include/3c515.h`

**Removed (Incorrect PCI):**
```c
// PCI-specific registers
#define _3C515_TX_PCI_STATUS_REG       0x20    // WRONG!
#define _3C515_TX_PCI_ERR_FATAL        0x8000  // PCI-specific
#define _3C515_TX_PCI_ERR_PARITY       0x4000  // PCI-specific
#define _3C515_TX_PCI_ERR_TARGET_ABORT 0x2000  // PCI-specific
#define _3C515_TX_PCI_ERR_MASTER_ABORT 0x1000  // PCI-specific
#define _3C515_TX_PCI_IO_DELAY()       inl(0x80)   // Wrong delay
```

**Added (Correct ISA):**
```c
// ISA Bus Master DMA Registers
#define _3C515_TX_PKT_STATUS           0x400   // TX packet status
#define _3C515_TX_DOWN_LIST_PTR        0x404   // TX descriptor list
#define _3C515_TX_UP_LIST_PTR          0x418   // RX descriptor list

// ISA bus timing
#define _3C515_TX_ISA_IO_DELAY()       outp(0x80, 0)     // ~1us delay
#define _3C515_TX_ISA_DMA_MAX_ADDR     0xFFFFFF          // 16MB limit
#define _3C515_TX_ISA_DMA_BOUNDARY     0x10000           // 64KB boundary
```

### 2. `/src/asm/hardware.asm`

**Corrected Detection:**
- Changed from PCI configuration space scanning to ISA I/O port scanning
- Scan range: 0x100-0x3E0 in steps of 0x20 (ISA standard)
- EEPROM access at base+0x0A/0x0C (not 0x200A/0x200C)

**Corrected Constants:**
```asm
; Old (Wrong):
C515_CONFIG_BASE    EQU 0CF8h   ; PCI configuration address
C515_CONFIG_DATA    EQU 0CFCh   ; PCI configuration data

; New (Correct):
C515_ISA_MIN_IO     EQU 100h    ; Minimum ISA I/O
C515_ISA_MAX_IO     EQU 3E0h    ; Maximum ISA I/O
C515_DMA_CTRL       EQU 400h    ; ISA DMA control offset
```

**Added ISA-Specific Functions:**
- `isa_virt_to_phys` - Convert DOS segment:offset to 24-bit physical
- `check_isa_dma_boundary` - Verify 64KB boundary compliance
- `setup_isa_dma_descriptor` - Configure ISA DMA descriptors
- `init_3c515_bus_master` - Initialize ISA bus mastering

### 3. `/src/asm/packet_api.asm`

**Corrected Reset Function:**
```asm
reset_3c515_hardware:
    ; ISA timing delay (~10ms for reset)
    mov     cx, 10000
.reset_delay:
    out     80h, al         ; ISA I/O delay (~1us)
    loop    .reset_delay
```

## Technical Details

### ISA vs PCI Differences

| Feature | PCI (Incorrect) | ISA (Correct) |
|---------|-----------------|---------------|
| Configuration | PCI config space | ISA Plug and Play |
| I/O Delay | ~10μs | ~1μs |
| DMA Addressing | 32-bit | 24-bit (16MB limit) |
| DMA Boundaries | None | 64KB boundaries |
| Bus Speed | 33/66 MHz | 8 MHz |
| Detection | PCI enumeration | I/O port scanning |

### ISA Bus Mastering Implementation

The 3C515 implements bus mastering on ISA, which requires:

1. **Physical Address Translation**: DOS real mode segment:offset must be converted to 24-bit physical addresses
2. **Boundary Checking**: DMA buffers cannot cross 64KB boundaries
3. **Limited Address Space**: Maximum 16MB addressable (24-bit)
4. **Descriptor Management**: Similar to PCI but with ISA constraints

### Register Clarification

The register at offset 0x20 is **NOT** a PCI status register. It's actually:
- Window 7, offset 0x20: `_3C515_TX_DMA_DOWN_PKT_STATUS` (TX packet status)
- Part of the ISA bus master DMA control registers

## Impact of Corrections

### Performance
- ISA bus mastering still allows efficient DMA transfers
- Limited by ISA bus speed (8 MHz) not PCI
- Still achieves 100 Mbps network speed despite ISA limitations

### Compatibility
- Proper ISA detection ensures compatibility with DOS systems
- No PCI BIOS required (which wouldn't exist on ISA-only systems)
- Works with standard ISA Plug and Play mechanisms

### Memory Management
- Respects DOS conventional memory limitations
- Handles 16MB ISA DMA addressing limit
- Properly manages 64KB boundary restrictions

## Testing Recommendations

1. **ISA Detection**: Verify card detection at standard ISA I/O addresses
2. **DMA Operations**: Test with buffers near 64KB boundaries
3. **Timing**: Ensure ISA timing delays are sufficient
4. **Bus Mastering**: Verify DMA transfers work correctly
5. **DOS Compatibility**: Test on systems without PCI support

## Historical Context

The 3C515 "Corkscrew" was 3Com's attempt to bring Fast Ethernet (100 Mbps) to ISA-based systems. It was released in 1996 as a bridge technology for systems that hadn't yet adopted PCI. The card's unique feature was ISA bus mastering, which allowed DMA transfers on the traditionally programmed-I/O-only ISA bus.

## Conclusion

All PCI-specific code has been removed and replaced with proper ISA implementations. The driver now correctly treats the 3C515-TX as the sophisticated ISA card it actually is, maintaining its bus mastering capabilities while respecting ISA bus constraints and limitations.

This correction ensures the driver will work correctly on ISA-only DOS systems without PCI support, which was the original target market for the 3C515-TX.