# PTASK.MOD - Parallel Tasking Module Specification

## Overview

PTASK.MOD implements support for all 3Com 3C509 "Parallel Tasking" chip variants across ISA and PCMCIA form factors. This module represents the entry-level networking solution optimized for minimal memory usage while providing reliable 10 Mbps Ethernet connectivity.

## Module Classification

```
Module Type:        Hot Path (Resident)
Size Target:        5KB resident
Chip Family:        3C509 Parallel Tasking ASIC
Bus Support:        ISA, PCMCIA
Performance Class:  10 Mbps Ethernet
```

## Supported Hardware

### ISA Variants
```
3C509 Series Support:
├─ 3C509 - Original Parallel Tasking (1990)
├─ 3C509B - Enhanced revision (1992)
├─ 3C509C - Latest revision (1994)
├─ 3C509-TP - Twisted Pair variant
├─ 3C509-TPO - TP with link integrity
├─ 3C509-TPC - TP/Coax combo
├─ 3C509-COMBO - All media types
└─ OEM variants with 3C509 chip
```

### PCMCIA Variants  
```
3C589 Series Support:
├─ 3C589 - PCMCIA Parallel Tasking (1993)
├─ 3C589B - Enhanced PCMCIA (1994)
├─ 3C589C - Latest PCMCIA (1995)
└─ OEM PCMCIA variants
```

## Technical Specifications

### Core Features
```
Network Capabilities:
├─ 10BASE-T (twisted pair)
├─ 10BASE2 (thin coax/BNC)
├─ 10BASE5 (thick coax/AUI)
├─ Media auto-detection
├─ Link integrity checking
├─ Full/half duplex (10BASE-T)
└─ Collision detection

Transfer Method:
├─ Programmed I/O (PIO)
├─ 16-byte FIFO buffering
├─ Interrupt-driven operation
└─ No DMA (bus mastering)
```

### 3Com Window Architecture
```
Register Windows (common to all 3Com NICs):
├─ Window 0: Configuration/EEPROM
├─ Window 1: Operating registers
├─ Window 2: Station address
├─ Window 3: Media/link status
├─ Window 4: Diagnostics
├─ Window 5: TX/RX statistics
├─ Window 6: Media selection
└─ Window 7: Bus master (N/A for 3C509)
```

### EEPROM Configuration
```
EEPROM Layout (16 words):
├─ Word 0: Configuration options
├─ Word 1-3: Station address (MAC)
├─ Word 4: Media type settings
├─ Word 5: Connector type
├─ Word 6: IRQ/DMA settings
├─ Word 7: I/O base address
├─ Words 8-15: Manufacturer data
└─ Checksum validation
```

## Module Architecture

### Hot/Cold Separation

#### Hot Section (3KB resident)
```
Performance-Critical Code:
├─ Interrupt service routine
├─ Packet transmission
├─ Packet reception
├─ FIFO management
├─ Media status checking
└─ Statistics updates

CPU Optimization Points:
├─ Packet copy routines (286/386/486)
├─ FIFO I/O operations
├─ Interrupt acknowledge
└─ Status checking loops
```

#### Cold Section (2KB, discarded after init)
```
Initialization-Only Code:
├─ ISA Plug and Play detection
├─ EEPROM reading and parsing
├─ Media type configuration
├─ IRQ and I/O setup
├─ Self-test routines
├─ Configuration parsing
└─ Hardware validation

PCMCIA Extensions:
├─ Card Services integration
├─ CIS (Card Information Structure) parsing
├─ PCMCIA configuration
├─ Socket setup
└─ Hot-plug initialization
```

## ISA Implementation

### Detection Strategy
```
ISA Detection Sequence:
1. Scan for ISA Plug and Play signature
2. If PnP: Use LFSR sequence for activation
3. If non-PnP: Scan I/O ports 0x200-0x3E0
4. Validate by reading station address
5. Check EEPROM integrity (checksum)
6. Verify chip functionality (loopback test)
```

### PnP Activation Sequence
```asm
; ISA PnP activation for 3C509
activate_3c509_pnp:
    mov     dx, 0x279       ; PnP write port
    mov     al, 0xFF        ; Initiation key start
    out     dx, al
    
    ; Send 32-bit LFSR sequence
    mov     cx, 32
    mov     bx, 0xFF        ; LFSR seed
.lfsr_loop:
    mov     al, bl
    out     dx, al
    ; LFSR calculation
    mov     al, bl
    xor     al, bh
    and     al, 1
    shl     bx, 1
    or      bl, al
    loop    .lfsr_loop
    
    ; Card now isolated, ready for config
```

### I/O Port Assignment
```
Standard I/O Ranges:
├─ Primary: 0x300-0x30F (16 ports)
├─ Alternate: 0x310-0x31F
├─ Range: 0x200-0x3E0 (step 0x10)
└─ Ports used: 16 consecutive

Register Map (relative to base):
├─ +0x0: Command register
├─ +0x2: Status register  
├─ +0x4: Window selection
├─ +0x6: Data register
├─ +0x8: TX FIFO
├─ +0xA: RX FIFO
├─ +0xC: TX status
├─ +0xE: RX status
```

## PCMCIA Implementation

### Card Services Integration
```c
// PCMCIA support requires DOS Card Services
typedef struct {
    socket_t socket;
    client_handle_t client;
    config_req_t config;
    io_req_t io_req;
    irq_req_t irq_req;
} ptask_pcmcia_t;

int ptask_pcmcia_init(socket_t socket) {
    // Register with Card Services
    if (!card_services_available()) {
        return -1;  // No CS support
    }
    
    // Parse Card Information Structure
    if (parse_cis(socket) != CARD_3C589) {
        return -1;  // Wrong card type
    }
    
    // Request resources
    request_io(socket, &io_req);
    request_irq(socket, &irq_req);
    request_configuration(socket, &config);
    
    // Initialize as normal 3C509
    return init_3c509_common(io_req.BasePort, irq_req.IRQ);
}
```

### CIS Parsing
```c
// Card Information Structure parsing for 3C589
int parse_3c589_cis(socket_t socket) {
    tuple_t tuple;
    
    get_first_tuple(socket, &tuple);
    while (tuple.code != CISTPL_END) {
        switch(tuple.code) {
            case CISTPL_VERS_1:
                // Check for "3Com 3C589"
                if (strstr(tuple.data, "3C589")) {
                    return CARD_3C589;
                }
                break;
                
            case CISTPL_CONFIG:
                parse_config_options(&tuple);
                break;
                
            case CISTPL_CFTABLE_ENTRY:
                parse_io_irq_requirements(&tuple);
                break;
        }
        get_next_tuple(socket, &tuple);
    }
    return CARD_UNKNOWN;
}
```

### Hot-Plug Support
```c
// Hot-plug event handlers
void ptask_card_inserted(socket_t socket) {
    ptask_pcmcia_t *pcmcia;
    
    disable_interrupts();
    
    // Initialize PCMCIA variant
    if (ptask_pcmcia_init(socket) == 0) {
        pcmcia = &pcmcia_contexts[socket];
        
        // Register new interface
        register_packet_interface(socket, &ptask_ops);
        
        log_info("3C589 inserted in socket %d", socket);
    }
    
    enable_interrupts();
}

void ptask_card_removed(socket_t socket) {
    disable_interrupts();
    
    // Graceful shutdown
    unregister_packet_interface(socket);
    
    // Release Card Services resources
    release_configuration(socket);
    release_io(socket);
    release_irq(socket);
    
    log_info("3C589 removed from socket %d", socket);
    
    enable_interrupts();
}
```

## Performance Optimizations

### CPU-Specific Code Patches
```asm
; Packet copy optimization points
packet_copy_ptask:
patch_copy_286:
    ; Default 8086 code
    rep movsb           ; 1 byte per cycle
    nop
    nop
    ; Patched for 286+:
    ; rep movsw         ; 2 bytes per cycle
    
patch_copy_386:
    ; Default code as above
    ; Patched for 386+:
    ; db 66h            ; 32-bit prefix
    ; rep movsd         ; 4 bytes per cycle
```

### FIFO Management Optimization
```asm
; Optimized FIFO reading for 3C509
read_rx_fifo_optimized:
    mov     dx, [io_base]
    add     dx, RX_FIFO_PORT
    
    ; CPU-specific unrolled loop
    test    byte [cpu_flags], CPU_386_BIT
    jnz     .read_32bit
    
    ; 16-bit optimized
.read_16bit:
    mov     cx, [packet_len]
    shr     cx, 1               ; Word count
    rep     insw                ; Bulk read
    adc     cx, 0               ; Handle odd byte
    rep     insb
    jmp     .done
    
    ; 32-bit optimized  
.read_32bit:
    mov     cx, [packet_len]
    shr     cx, 2               ; Dword count
    db      66h
    rep     insd                ; Bulk 32-bit read
    
    mov     cx, [packet_len]
    and     cx, 3               ; Remaining bytes
    rep     insb
    
.done:
```

## Media Detection and Configuration

### Media Types Supported
```c
typedef enum {
    MEDIA_10BASE_T = 0,         // Twisted pair
    MEDIA_10BASE2,              // Thin coax (BNC)
    MEDIA_10BASE5,              // Thick coax (AUI)
    MEDIA_AUTO                  // Auto-detect
} ptask_media_t;

// Media detection sequence
ptask_media_t detect_media(uint16_t io_base) {
    uint16_t media_status;
    
    // Check twisted pair link
    select_window(io_base, 3);
    media_status = inw(io_base + MEDIA_STATUS);
    
    if (media_status & LINK_BEAT_DETECT) {
        return MEDIA_10BASE_T;
    }
    
    // Check coax connectivity
    if (media_status & COAX_AVAILABLE) {
        return MEDIA_10BASE2;
    }
    
    // Default to AUI
    return MEDIA_10BASE5;
}
```

### Auto-Negotiation (Link Integrity)
```c
// 10BASE-T link integrity checking
int check_link_integrity(uint16_t io_base) {
    uint16_t media_status;
    int stable_count = 0;
    
    select_window(io_base, 3);
    
    // Check link stability over time
    for (int i = 0; i < 10; i++) {
        media_status = inw(io_base + MEDIA_STATUS);
        
        if (media_status & LINK_BEAT_DETECT) {
            stable_count++;
        }
        
        delay_ms(10);
    }
    
    // Link is good if stable 80% of time
    return (stable_count >= 8);
}
```

## Error Handling and Recovery

### Common Error Conditions
```c
// Error types specific to 3C509
typedef enum {
    PTASK_ERROR_NONE = 0,
    PTASK_ERROR_TX_UNDERRUN,    // Transmit FIFO underrun
    PTASK_ERROR_TX_JABBER,      // Jabber error
    PTASK_ERROR_RX_OVERRUN,     // Receive FIFO overrun
    PTASK_ERROR_CRC,            // CRC error
    PTASK_ERROR_MEDIA,          // Media disconnect
    PTASK_ERROR_FIFO            // FIFO error
} ptask_error_t;

// Error recovery procedures
int ptask_recover_tx_underrun(uint16_t io_base) {
    // Reset transmitter
    select_window(io_base, 1);
    outw(io_base + COMMAND, TX_RESET);
    
    // Wait for reset completion
    for (int i = 0; i < 1000; i++) {
        if (!(inw(io_base + STATUS) & TX_IN_RESET)) {
            break;
        }
        delay_us(10);
    }
    
    // Re-enable transmitter
    outw(io_base + COMMAND, TX_ENABLE);
    
    return 0;
}
```

## Memory Usage Profile

### Runtime Memory Map
```
PTASK.MOD Memory Layout (5KB total):
┌─────────────────────────────────────┐
│ Hot Code Section (3KB)              │
│ ├─ Interrupt handlers    800 bytes  │
│ ├─ Packet I/O routines  1200 bytes  │
│ ├─ FIFO management      600 bytes   │
│ └─ Status/statistics    400 bytes   │
├─────────────────────────────────────┤
│ Data Section (1.5KB)               │
│ ├─ NIC context          256 bytes  │
│ ├─ Statistics counters   128 bytes  │
│ ├─ EEPROM cache         32 bytes   │
│ ├─ Media state          64 bytes   │
│ └─ Buffers/work area    1048 bytes │
├─────────────────────────────────────┤
│ Patch Table (0.5KB)               │
│ ├─ CPU patch points     200 bytes  │
│ ├─ Alternative code     200 bytes  │
│ └─ Patch metadata       112 bytes  │
└─────────────────────────────────────┘

Cold Section (2KB, discarded):
├─ Hardware detection    800 bytes
├─ EEPROM reading       400 bytes  
├─ Media configuration  400 bytes
├─ Self-test routines   300 bytes
└─ PCMCIA init          100 bytes
```

## Configuration Examples

### ISA Configuration
```batch
REM Auto-detect 3C509 ISA
3COMPD.COM /MODULE=PTASK

REM Force specific I/O and IRQ
3COMPD.COM /MODULE=PTASK /IO=0x300 /IRQ=10

REM Specify media type
3COMPD.COM /MODULE=PTASK /MEDIA=10BASET

REM Enable link integrity checking
3COMPD.COM /MODULE=PTASK /LINKCHECK=ON
```

### PCMCIA Configuration
```batch
REM Card Services must be loaded first
DEVICE=C:\DOS\SS365SL.SYS
DEVICE=C:\DOS\CS.EXE

REM Auto-detect PCMCIA cards
3COMPD.COM /MODULE=PTASK /PCMCIA=ON

REM Force PCMCIA socket
3COMPD.COM /MODULE=PTASK /SOCKET=0
```

## Testing and Validation

### Test Requirements
```
Hardware Testing:
├─ 3C509B on ISA bus (primary)
├─ 3C589 PCMCIA card (secondary)
├─ Multiple media types
├─ Various I/O base addresses
├─ Different IRQ configurations
└─ Hot-plug scenarios (PCMCIA)

Software Testing:  
├─ DOS 3.3 through 6.22
├─ Memory managers (EMM386, QEMM)
├─ TCP/IP stacks (mTCP, Trumpet)
├─ Packet applications
└─ Stress testing
```

### Performance Benchmarks
```
Target Performance (3C509B):
├─ Throughput: 9.5+ Mbps (95% of 10Mbps)
├─ Latency: <50 microseconds
├─ CPU usage: <15% on 80386-25
├─ Memory efficiency: 5KB resident
└─ Boot time: <1 second
```

## Integration with Core System

### Module Interface
```c
// PTASK module exports
typedef struct {
    int (*init)(nic_context_t *ctx);
    int (*transmit)(nic_context_t *ctx, packet_t *pkt);
    int (*receive)(nic_context_t *ctx);
    int (*set_media)(nic_context_t *ctx, media_type_t media);
    int (*get_statistics)(nic_context_t *ctx, stats_t *stats);
    void (*cleanup)(nic_context_t *ctx);
} ptask_ops_t;

// Registration with core loader
int ptask_register(void) {
    module_info_t info = {
        .name = "PTASK",
        .version = 0x0100,
        .size = 5120,          // 5KB
        .type = MODULE_HOT_PATH,
        .family = FAMILY_3C509,
        .ops = &ptask_operations
    };
    
    return register_module(&info);
}
```

## Future Enhancements

### Potential Improvements
```
Phase 6+ Enhancements:
├─ 100BASE-TX support (3C509D variant)
├─ Advanced power management
├─ Enhanced PCMCIA features
├─ Wake-on-LAN support
├─ SNMP MIB support
└─ IPv6 support preparation
```

This specification provides the complete technical foundation for implementing PTASK.MOD as the optimal 3C509 family driver for DOS environments.