# PCI Control Path vs Data Path Architecture for 3Com NICs

Last Updated: 2025-09-04
Status: supplemental
Purpose: Explain control-path (PCI config) vs data-path (IO/MMIO) for PCI NICs in DOS.

## Executive Summary

This document maps how each 3Com PCI NIC family transitions from "invisible" (unconfigured) to "operational" (data flowing) state in DOS real mode. The key insight: **PCI configuration is the control path that enables the data path**, but once configured, all families revert to standard DOS-style I/O port operations.

## The Fundamental Problem and Solution

### Before PCI Configuration
- **NIC is invisible**: No valid I/O ports, no IRQ assigned
- **BARs unset**: Contains 0x00000000 or 0xFFFFFFFF
- **No data path**: Cannot send/receive packets
- **Driver blocked**: No way to access NIC registers

### After PCI Configuration  
- **NIC is accessible**: Valid I/O port base assigned
- **IRQ wired**: Interrupt Line register contains PIC IRQ
- **Data path open**: Standard IN/OUT instructions work
- **Driver operational**: Full register access via I/O ports

## Control Path: PCI BIOS Configuration (INT 1Ah)

All PCI NIC families use the same control path sequence:

```asm
; 1. Detection Phase
mov ax, 0xB101          ; PCI BIOS installation check
int 0x1A
jc no_pci

; 2. Enumeration Phase  
mov ax, 0xB102          ; Find device by ID
mov cx, [device_id]     ; e.g., 0x9000 for 3C900
mov dx, 0x10B7          ; 3Com vendor ID
int 0x1A
jc not_found
; Returns: BH=bus, BL=dev/func

; 3. Configuration Phase
mov ax, 0xB10A          ; Read BAR0 (dword)
mov di, 0x10            ; BAR0 offset
int 0x1A
; Returns: ECX=BAR value

test cl, 0x01           ; I/O space?
jz mmio_bar             ; Real mode problem!
and ecx, 0xFFFFFFFC     ; Mask flags
mov [io_base], cx       ; Save I/O base

; 4. Enable Device
mov ax, 0xB109          ; Read Command register
mov di, 0x04
int 0x1A
or cx, 0x0005           ; I/O space + bus master
mov ax, 0xB10C          ; Write Command register
int 0x1A

; 5. Get IRQ
mov ax, 0xB108          ; Read Interrupt Line
mov di, 0x3C
int 0x1A
mov [nic_irq], cl
```

## Data Path: Family-Specific I/O Operations

Once configured, each family uses different register layouts but all through I/O ports:

### Vortex Family (3C590/3C595) - Window Registers

```asm
; After PCI config, io_base is valid (e.g., 0xD000)
mov dx, [io_base]
add dx, 0x0E            ; Command register
mov ax, 0x0800          ; Select Window 0
out dx, ax

; Now access Window 0 registers
mov dx, [io_base]
add dx, 0x00            ; Window 0, offset 0
in ax, dx               ; Read manufacturer ID

; TX path - PIO FIFO
mov dx, [io_base]
add dx, 0x10            ; TX FIFO (Window 1)
mov cx, packet_len
mov si, packet_data
rep outsw               ; Send packet data

; RX path - PIO FIFO  
mov dx, [io_base]
add dx, 0x10            ; RX FIFO (Window 1)
mov cx, rx_len
mov di, rx_buffer
rep insw                ; Receive packet data
```

**Characteristics:**
- Pure I/O port access (real mode friendly)
- Window-based register banking
- Programmed I/O for data transfer
- No DMA setup required

### Boomerang Family (3C900/3C905) - DMA Descriptors

```asm
; After PCI config, set up DMA rings
mov dx, [io_base]
add dx, 0x24            ; DN_LIST_PTR register
mov eax, [tx_ring_phys] ; Physical address of TX ring
out dx, eax             ; 32-bit write (needs 386+)

; TX descriptor setup (in memory)
mov si, tx_desc
mov [si+0], dword 0     ; Next pointer (0=end)
mov [si+4], dword 0     ; Status
mov eax, [packet_phys]
mov [si+8], eax         ; Buffer physical address
mov [si+12], word packet_len ; Length

; Start TX DMA
mov dx, [io_base]
add dx, 0x20            ; DMA_CTRL register
mov al, 0x01            ; Start download
out dx, al

; RX path similar with UP_LIST_PTR
mov dx, [io_base]
add dx, 0x38            ; UP_LIST_PTR register
mov eax, [rx_ring_phys]
out dx, eax
```

**Characteristics:**
- I/O ports for control registers
- Memory-based descriptor rings
- Bus master DMA for data transfer
- Requires physical memory management

### Cyclone Family (3C905B) - Enhanced DMA

```asm
; After PCI config, enhanced features
mov dx, [io_base]
add dx, 0x0E            ; Command register

; Enable Wake-on-LAN
mov ax, 0x0800 | 7      ; Select Window 7
out dx, ax
mov dx, [io_base]
add dx, 0x0C            ; WOL register
mov al, 0x01            ; Enable magic packet
out dx, al

; TX with hardware assists
mov si, tx_desc
mov [si+4], word 0x8000 ; Request TX complete interrupt
or [si+4], word 0x0100  ; Add IP checksum request

; Power management
mov ax, 0xB10A          ; Read PCI PM register
mov di, 0x40            ; PM capability offset
int 0x1A
; Manipulate power states via PCI config
```

**Characteristics:**
- Same I/O + DMA as Boomerang
- Additional window registers for WOL
- Power management via PCI config space
- Hardware acceleration flags in descriptors

### Tornado Family (3C905C) - Hardware Offload

```asm
; After PCI config, advanced offload
mov dx, [io_base]
add dx, 0x0E
mov ax, 0x0800 | 7      ; Window 7
out dx, ax

; Enable hardware checksums
mov dx, [io_base]  
add dx, 0x30            ; Checksum control
mov ax, 0x0003          ; IP + TCP/UDP checksums
out dx, ax

; TX with full offload
mov si, tx_desc
mov [si+4], word 0x8100 ; TX complete + checksum
mov [si+14], word vlan_tag ; VLAN tag insertion

; VLAN configuration
mov dx, [io_base]
add dx, 0x34            ; VLAN control register
mov ax, 0x8100          ; Enable VLAN processing
out dx, ax
```

**Characteristics:**
- Extended descriptor format
- Hardware checksum offload
- VLAN tag insertion/extraction
- Some features need MMIO (not real-mode friendly)

## BAR Strategy for Real Mode

### I/O BAR Requirements

All families MUST use I/O BARs for real-mode DOS:

```asm
check_bar_type:
    ; Read BAR0
    mov ax, 0xB10A
    mov di, 0x10
    int 0x1A
    
    test cl, 0x01       ; Bit 0 = I/O space
    jnz .io_bar_good
    
    ; Memory BAR - check if below 1MB
    cmp ecx, 0x100000
    jae .fail           ; Above 1MB = inaccessible
    
    ; Could map if below 1MB but risky
    jmp .fail
    
.io_bar_good:
    and ecx, 0xFFFFFFFC ; Clear flags
    mov [io_base], cx   ; Save base
    clc
    ret
    
.fail:
    ; Try BAR1 or fail gracefully
    stc
    ret
```

### Family BAR Layouts

| Family | BAR0 | BAR1 | BAR2 | Real Mode |
|--------|------|------|------|-----------|
| Vortex | I/O (128 bytes) | Memory (optional) | - | ✅ Full support |
| Boomerang | I/O (128 bytes) | Memory (optional) | - | ✅ Full support |
| Cyclone | I/O (128 bytes) | Memory (128 bytes) | - | ✅ I/O only |
| Tornado | I/O (256 bytes) | Memory (128 bytes) | - | ⚠️ Limited features |

## Interrupt Path Configuration

All families use the same interrupt wiring:

```asm
setup_irq:
    ; Read Interrupt Line from PCI config
    mov ax, 0xB108      ; Read byte
    mov di, 0x3C        ; Interrupt Line register
    int 0x1A
    mov [nic_irq], cl   ; CL = IRQ number
    
    ; Install DOS ISR
    mov al, [nic_irq]
    add al, 8           ; IRQ to INT vector (IRQ0=INT8)
    cmp al, 16
    jb .master_pic
    add al, 0x70-16     ; Slave PIC vectors
    
.master_pic:
    mov ah, 0x25        ; Set interrupt vector
    mov dx, isr_handler
    int 0x21
    
    ; Unmask IRQ in 8259 PIC
    mov cl, [nic_irq]
    mov ax, 1
    shl ax, cl          ; Create IRQ mask
    not ax              ; Invert for unmasking
    
    cmp cl, 8
    jb .unmask_master
    
    ; Slave PIC
    sub cl, 8
    in al, 0xA1         ; Read slave mask
    and al, ah          ; Clear bit
    out 0xA1, al        ; Write back
    jmp .done
    
.unmask_master:
    in al, 0x21         ; Read master mask
    and al, ah          ; Clear bit
    out 0x21, al        ; Write back
    
.done:
    ret
```

## Complete Flow: From Invisible to Operational

### Phase 1: Discovery (Control Path)
```asm
; NIC doesn't exist to DOS yet
call detect_pci_bios
call find_3com_devices  ; INT 1Ah, AX=B102h
call read_device_ids    ; Identify family
```

### Phase 2: Configuration (Control Path)
```asm
; Make NIC visible
call read_bars          ; Get I/O base
call enable_device      ; Set Command bits
call read_irq          ; Get IRQ assignment
```

### Phase 3: Initialization (Data Path Begins)
```asm
; NIC now accessible via I/O ports
mov dx, [io_base]       ; Valid port from BAR
call reset_nic          ; Direct I/O operations
call setup_rings        ; Configure DMA if applicable
call read_mac_address   ; Access EEPROM via I/O
```

### Phase 4: Operation (Pure Data Path)
```asm
; Normal packet operations
call transmit_packet    ; Write to I/O ports
call receive_packet     ; Read from I/O ports
call handle_interrupt   ; Service IRQ
```

## Key Insights

1. **PCI config is one-time setup**: Once BARs are configured, you never touch PCI config space again during normal operation

2. **All families become ISA-like**: After configuration, they behave like enhanced ISA cards with I/O ports and IRQs

3. **Real mode constraint**: Must use I/O BARs; MMIO BARs above 1MB are inaccessible without protected mode tricks

4. **DMA is DOS-compatible**: Bus master DMA works fine in real mode as long as buffers are in conventional memory

5. **Feature degradation**: Later families (Tornado) lose some features in real mode if those features require MMIO access

## Implementation Priority

1. **Start with Vortex**: Simplest, PIO-only, most ISA-like
2. **Add Boomerang**: Introduces DMA but still I/O based
3. **Extend to Cyclone**: Same architecture, more features
4. **Consider Tornado**: Evaluate which features work in real mode

This architecture ensures that despite being PCI devices, all NIC families can operate in DOS real mode using the familiar I/O port programming model, with INT 1Ah BIOS services acting as the one-time "gate opener" to make the devices visible.
