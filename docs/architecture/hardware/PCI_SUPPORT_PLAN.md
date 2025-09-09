# PCI Support Implementation Plan for 3Com Packet Driver

Last Updated: 2025-09-04
Status: supplemental
Purpose: Plan and scope for adding 3Com PCI NIC families using INT 1Ah.

## Executive Summary

This document outlines the complete technical approach for adding PCI NIC support to the 3Com DOS packet driver using real-mode INT 1Ah BIOS services. This approach avoids the complexity of protected mode switching while maintaining full DOS compatibility.

## Current Architecture Status

### Production Ready (ISA)
- **3C509B**: 10 Mbps ISA with PnP, PIO mode
- **3C515-TX**: 100 Mbps ISA with bus mastering DMA

### Partially Implemented (PCI)
- **3C900-TPO**: BOOMTEX module exists but uses direct I/O (needs refactoring)
- Device capabilities defined for 3C590/3C595
- No integration with main driver

## Technical Approach: INT 1Ah BIOS Services

### Why INT 1Ah Instead of Direct I/O

The PCI configuration space requires 32-bit I/O operations on ports 0xCF8/0xCFC:
```asm
; This DOESN'T work in real mode:
mov eax, 0x80001800  ; 32-bit config address
mov dx, 0xCF8
out dx, eax          ; No 32-bit OUT in real mode!
```

INT 1Ah BIOS services solve this by providing real-mode safe access:
```asm
; This WORKS in real mode:
mov ax, 0xB109       ; Read config word
mov bh, 0            ; Bus number
mov bl, 0x18         ; Device/Function
mov di, 0x00         ; Register offset
int 0x1A             ; BIOS handles 32-bit access
```

## Implementation Steps

### Step 1: PCI BIOS Detection

```asm
detect_pci_bios:
    push    es
    push    di
    
    ; Clear EDI for PM entry point (optional)
    xor     di, di
    xor     edi, edi
    
    ; PCI BIOS Installation Check
    mov     ax, 0xB101
    int     0x1A
    jc      .no_pci_bios
    
    ; Check signature
    cmp     edx, ' ICP'     ; 'PCI ' in little-endian
    jne     .no_pci_bios
    
    ; Save BIOS info
    mov     [pci_version], bx      ; BH.BL = version
    mov     [last_pci_bus], cl     ; Last bus number
    mov     [pci_hw_mech], al      ; Hardware characteristics
    
    ; AL bits:
    ; Bit 0: Config Mechanism #1 supported
    ; Bit 1: Config Mechanism #2 supported
    ; Bit 4: Special Cycle via Mechanism #1
    ; Bit 5: Special Cycle via Mechanism #2
    
    clc                     ; Success
    jmp     .done
    
.no_pci_bios:
    stc                     ; Failure
    
.done:
    pop     di
    pop     es
    ret
```

### Step 2: Device Enumeration

#### Method A: Find Specific 3Com Devices
```asm
find_3com_nics:
    xor     si, si          ; Device index
    
.find_loop:
    ; Find by Vendor/Device ID
    mov     ax, 0xB102      ; Find PCI device
    mov     cx, [device_id] ; Device ID (e.g., 0x9000 for 3C900)
    mov     dx, 0x10B7      ; 3Com vendor ID
    int     0x1A
    jc      .not_found
    
    ; BH = Bus, BL = Device/Function
    mov     [nic_bus], bh
    mov     [nic_devfn], bl
    
    ; Found a device, save and look for more
    call    save_device_info
    inc     si
    cmp     si, MAX_NICS
    jb      .find_loop
    
.not_found:
    ret
```

#### Method B: Exhaustive Bus Scan
```asm
scan_all_buses:
    xor     bh, bh          ; Start at bus 0
    
.bus_loop:
    xor     bl, bl          ; Device 0, Function 0
    
.device_loop:
    ; Read Vendor ID
    mov     ax, 0xB109      ; Read config word
    mov     di, 0x00        ; Offset 0 = Vendor ID
    int     0x1A
    jc      .next_device
    
    cmp     cx, 0xFFFF      ; No device?
    je      .next_device
    
    cmp     cx, 0x10B7      ; 3Com vendor?
    jne     .check_multifunction
    
    ; Found 3Com device, read Device ID
    mov     di, 0x02
    mov     ax, 0xB109
    int     0x1A
    
    ; Check if it's a NIC we support
    call    is_supported_nic
    jc      .check_multifunction
    
    call    save_device_info
    
.check_multifunction:
    ; Read Header Type at offset 0x0E
    mov     ax, 0xB108      ; Read byte
    mov     di, 0x0E
    int     0x1A
    
    test    cl, 0x80        ; Multi-function?
    jz      .single_function
    
    ; Check remaining functions
    and     bl, 0xF8        ; Keep device, clear function
    add     bl, 7           ; Jump to function 7
    jmp     .next_function
    
.single_function:
    and     bl, 0xF8        ; Next device
    add     bl, 8
    
.next_function:
    inc     bl
    test    bl, 0x07        ; Wrapped to next device?
    jnz     .device_loop
    
.next_device:
    add     bl, 8           ; Next device
    cmp     bl, 0            ; Wrapped to 0?
    jne     .device_loop
    
    inc     bh              ; Next bus
    cmp     bh, [last_pci_bus]
    jbe     .bus_loop
    
    ret
```

### Step 3: Device Configuration

```asm
configure_pci_nic:
    ; Read BAR0 (I/O or Memory)
    mov     ax, 0xB10A      ; Read dword
    mov     bh, [nic_bus]
    mov     bl, [nic_devfn]
    mov     di, 0x10        ; BAR0
    int     0x1A
    
    ; ECX contains BAR value
    test    cl, 0x01        ; I/O space?
    jz      .memory_bar
    
    ; I/O BAR - mask off flags
    and     ecx, 0xFFFFFFFC
    mov     [io_base], cx
    jmp     .enable_device
    
.memory_bar:
    ; Memory BAR - need to handle carefully in real mode
    ; For now, fail if not I/O mapped
    stc
    ret
    
.enable_device:
    ; Read Command Register
    mov     ax, 0xB109      ; Read word
    mov     di, 0x04        ; Command register
    int     0x1A
    
    ; Enable I/O Space and Bus Master
    or      cx, 0x0005      ; Bit 0 = I/O, Bit 2 = Bus Master
    
    ; Write back Command Register
    mov     ax, 0xB10C      ; Write word
    mov     di, 0x04
    int     0x1A
    
    ; Read Interrupt Line
    mov     ax, 0xB108      ; Read byte
    mov     di, 0x3C        ; Interrupt Line
    int     0x1A
    
    mov     [nic_irq], cl
    
    clc                     ; Success
    ret
```

### Step 4: Fallback Mechanisms

If BIOS is broken, check hardware characteristics from installation check:

```asm
direct_config_fallback:
    ; Check if Mechanism #1 is supported
    test    byte [pci_hw_mech], 0x01
    jz      .try_mechanism_2
    
    ; For Mechanism #1, we'd need 32-bit I/O
    ; This requires protected mode or unreal mode
    ; Skip for now
    jmp     .fail
    
.try_mechanism_2:
    ; Check if Mechanism #2 is supported
    test    byte [pci_hw_mech], 0x02
    jz      .fail
    
    ; Mechanism #2 uses 8/16-bit I/O (real mode safe!)
    ; Enable configuration space
    mov     al, 0x80        ; Enable bit
    or      al, [func_num] ; Function number
    mov     dx, 0xCF8
    out     dx, al
    
    ; Select bus
    mov     al, [bus_num]
    mov     dx, 0xCFA
    out     dx, al
    
    ; Access config registers at 0xC000 + (device << 8) + register
    mov     dx, 0xC000
    mov     al, [device_num]
    shl     ax, 8
    add     dx, ax
    add     dx, [register]
    
    ; Now can use normal IN/OUT
    in      ax, dx          ; Read config word
    
.fail:
    stc
    ret
```

## Supported PCI NICs

### Phase 1: Vortex Family (1995-1996)
- **3C590**: 10 Mbps PCI, PIO mode, first PCI NIC
- **3C592**: 10 Mbps EISA variant
- **3C595**: 100 Mbps Fast Ethernet, media auto-select
- **3C597**: 100 Mbps EISA variant

**Architecture**: Window registers, programmed I/O FIFOs

### Phase 2: Boomerang Family (1997-1998)
- **3C900**: 10 Mbps with bus mastering DMA
- **3C900B**: Enhanced with power management
- **3C905**: 100 Mbps with scatter-gather DMA
- **3C905A**: Cost-reduced version

**Architecture**: Descriptor-based DMA, zero-copy networking

### Phase 3: Cyclone Family (1998-1999)
- **3C905B**: Advanced features, Wake-on-LAN, PXE boot ROM
- **3C905B-TX**: Most common variant, RJ-45 only
- **3C905B-FX**: Fiber optic variant
- **3C905B-Combo**: Multiple media types

**Architecture**: Enhanced DMA engine, hardware flow control

### Phase 4: Tornado Family (1999-2001)
- **3C905C**: Hardware checksums, VLAN tagging
- **3C905CX**: Cost-optimized variant
- **3C920**: Embedded/integrated version
- **3C980**: Server-oriented with larger buffers

**Architecture**: TCP/UDP checksum offload, 802.1Q VLAN hardware

### Device ID Mapping
```c
#define VENDOR_3COM             0x10B7

/* Vortex Family */
#define DEVICE_3C590            0x5900  /* 10 Mbps */
#define DEVICE_3C595_1          0x5950  /* 100baseTX */
#define DEVICE_3C595_2          0x5951  /* 100baseT4 */
#define DEVICE_3C595_3          0x5952  /* 100base-MII */

/* Boomerang Family */
#define DEVICE_3C900_TPO        0x9000  /* 10baseT */
#define DEVICE_3C900_COMBO      0x9001  /* 10base Combo */
#define DEVICE_3C900_TPC        0x9004  /* 10baseT TPC */
#define DEVICE_3C900B_TPO       0x9005  /* 10baseT TPO */
#define DEVICE_3C905_TX         0x9050  /* 100baseTX */
#define DEVICE_3C905_T4         0x9051  /* 100baseT4 */

/* Cyclone Family */
#define DEVICE_3C905B_TX        0x9055  /* 100baseTX */
#define DEVICE_3C905B_FX        0x905A  /* 100baseFX */
#define DEVICE_3C905B_COMBO     0x9058  /* 10/100 Combo */

/* Tornado Family */
#define DEVICE_3C905C_TX        0x9200  /* 100baseTX */
#define DEVICE_3C920B_EMB       0x9201  /* Embedded */
#define DEVICE_3C920B_EMB_WNM   0x9202  /* Embedded WNM */
#define DEVICE_3C980_TX         0x9800  /* Server NIC */
#define DEVICE_3C980C_TX        0x9805  /* Server Cyclone */
```

### Architecture Evolution

#### Vortex → Boomerang
- Added bus mastering DMA
- Descriptor rings replace PIO FIFOs
- Reduced CPU overhead from 40% to 10%

#### Boomerang → Cyclone  
- Improved DMA engine
- Wake-on-LAN support
- PXE boot ROM capability
- Power management (ACPI)

#### Cyclone → Tornado
- Hardware checksum offload
- VLAN tag insertion/extraction
- Jumbo frame support (some variants)
- Interrupt mitigation

## Integration with Existing Architecture

### Module Structure
```
3CPKT.EXE
├── ISA Detection (existing)
│   ├── PnP BIOS
│   ├── ISAPnP
│   └── Legacy ID Port
├── PCI Detection (new)
│   ├── INT 1Ah BIOS
│   ├── Device enumeration
│   └── BAR configuration
└── Unified Driver Core
    ├── Packet API
    ├── Buffer management
    └── Statistics
```

### Memory Footprint Impact
- PCI detection code: +2KB in cold section (discarded)
- PCI runtime support: +1KB in hot section
- Total resident increase: ~1KB

### JIT Patch Points for PCI
```asm
PATCH_pci_config_read:
    ; Default: INT 1Ah BIOS
    mov ax, 0xB109
    int 0x1A
    
    ; Can be patched to:
    ; - Direct Mechanism #1 (if in PM)
    ; - Direct Mechanism #2 (if supported)
```

## Testing Strategy

### Virtual Environment
1. QEMU with PCI NIC emulation
2. VirtualBox/VMware with DOS
3. PCem with period-correct BIOS

### Real Hardware
1. 486 with PCI slots
2. Pentium systems
3. Industrial PCs with both ISA and PCI

## Error Handling

### BIOS Not Present
- Fall back to ISA-only mode
- Log diagnostic message
- Return appropriate error code

### Device Not Found
- Continue with ISA detection
- No error if ISA NICs found

### BAR Configuration Failed
- Skip PCI device
- Try next device
- Fall back to ISA

## Compatibility Notes

### Known BIOS Issues
- **Award 4.51PG**: Function 0 read bug (workaround included)
- **Early AMI**: May not support all functions
- **Phoenix**: Generally reliable

### CPU Requirements
- Minimum: 386 for PCI support
- Recommended: 486+ for bus mastering
- Optimal: Pentium for full performance

## Implementation Timeline

### Phase 1: Foundation (2 weeks)
- [ ] INT 1Ah detection and enumeration
- [ ] Basic 3C590 support (PIO mode)
- [ ] Integration with existing driver

### Phase 2: Enhancement (2 weeks)  
- [ ] Bus mastering support
- [ ] 3C900/3C905 implementation
- [ ] Performance optimization

### Phase 3: Polish (1 week)
- [ ] Fallback mechanisms
- [ ] Error handling
- [ ] Documentation

## Conclusion

Using INT 1Ah BIOS services provides a clean, real-mode compatible path to PCI support without the complexity of protected mode switching. This approach maintains the driver's philosophy of simplicity while extending support to PCI-era hardware.

The existing BOOMTEX module provides a foundation but needs refactoring from direct I/O to BIOS services. With this approach, the driver can support both ISA and PCI NICs in a unified, maintainable codebase.
