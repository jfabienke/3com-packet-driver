# CORKSCRW.MOD - Corkscrew Module Specification

## Overview

CORKSCRW.MOD implements support for the 3Com 3C515-TX "Corkscrew" Fast Ethernet adapter, a unique ISA card that bridges the gap between 10 Mbps ISA networking and 100 Mbps PCI technology. This module represents the most sophisticated ISA networking solution, featuring bus mastering DMA on the ISA bus.

## Module Classification

```
Module Type:        Hot Path (Resident)
Size Target:        6KB resident  
Chip Family:        3C515 Corkscrew ASIC
Bus Support:        ISA only
Performance Class:  100 Mbps Fast Ethernet
Unique Feature:     ISA bus mastering
```

## Supported Hardware

### Primary Support
```
3C515 Series:
└─ 3C515-TX - ISA Fast Ethernet (100BASE-TX)
   ├─ Released: 1995
   ├─ Bus: 16-bit ISA
   ├─ Speed: 100 Mbps
   ├─ Connector: RJ-45
   └─ Unique: Bus mastering on ISA
```

### Technical Uniqueness
```
Why Corkscrew is Special:
├─ Only ISA card with 100 Mbps capability
├─ Bus mastering DMA on ISA bus
├─ Bridge between ISA and PCI eras
├─ Advanced features in ISA form factor
├─ Requires 386+ CPU for bus mastering
└─ VDS integration for V86 compatibility
```

## Technical Specifications

### Core Features
```
Network Capabilities:
├─ 100BASE-TX (Fast Ethernet)
├─ Auto-negotiation (100/10, full/half)
├─ MII transceiver interface
├─ Hardware flow control
├─ Full/half duplex operation
└─ Advanced error detection

Transfer Method:
├─ Bus mastering DMA
├─ Ring buffer architecture
├─ 16 transmit descriptors
├─ 16 receive descriptors  
├─ Scatter-gather DMA
└─ Hardware checksum assist
```

### Bus Mastering on ISA
```
ISA Bus Master Challenges:
├─ 24-bit address limitation (16MB)
├─ No cache coherency support
├─ VDS required for V86 mode
├─ DMA conflicts with other devices
├─ Timing constraints on ISA bus
└─ Power consumption concerns

Corkscrew Solutions:
├─ Smart descriptor management
├─ VDS integration
├─ DMA buffer allocation below 16MB
├─ Bus arbitration protocols
└─ Advanced error recovery
```

### Ring Buffer Architecture
```
Descriptor Ring Layout:
┌─────────────────────────────────────┐
│ TX Ring (16 descriptors × 8 bytes) │
│ ├─ Descriptor 0: Address, Length   │
│ ├─ Descriptor 1: Address, Length   │
│ │   ...                             │
│ └─ Descriptor 15: Address, Length  │
├─────────────────────────────────────┤
│ RX Ring (16 descriptors × 8 bytes) │
│ ├─ Descriptor 0: Address, Length   │
│ ├─ Descriptor 1: Address, Length   │
│ │   ...                             │
│ └─ Descriptor 15: Address, Length  │
└─────────────────────────────────────┘

Descriptor Format (8 bytes):
├─ Bytes 0-3: Buffer physical address
├─ Bytes 4-5: Buffer length/status
├─ Bytes 6-7: Next descriptor pointer
```

## Module Architecture

### Hot/Cold Separation

#### Hot Section (4KB resident)
```
Performance-Critical Code:
├─ Interrupt service routine
├─ DMA descriptor management
├─ Ring buffer operations
├─ Packet transmission
├─ Packet reception
├─ MII transceiver control
├─ Flow control handling
└─ Statistics updates

CPU Optimization Points:
├─ Descriptor setup (386+ specific)
├─ Cache management (486+)
├─ Memory barriers
└─ Interrupt coalescing
```

#### Cold Section (2KB, discarded after init)
```
Initialization-Only Code:
├─ ISA bus master detection
├─ DMA capability testing
├─ VDS (Virtual DMA Services) setup
├─ Ring buffer allocation
├─ MII PHY initialization
├─ Auto-negotiation setup
├─ Hardware self-test
└─ Configuration parsing
```

## ISA Bus Mastering Implementation

### DMA Buffer Management
```c
// DMA buffer requirements for ISA
typedef struct {
    void *virt_addr;        // Virtual address
    uint32_t phys_addr;     // Physical address (< 16MB)
    uint16_t size;          // Buffer size
    uint16_t flags;         // VDS flags
} dma_buffer_t;

// Allocate DMA-capable buffers
int alloc_dma_buffers(corkscrw_context_t *ctx) {
    // Must be below 16MB for ISA DMA
    for (int i = 0; i < NUM_RX_DESC; i++) {
        ctx->rx_buffers[i] = alloc_dma_buffer(1536, DMA_BELOW_16MB);
        if (!ctx->rx_buffers[i]) {
            return -ENOMEM;
        }
    }
    
    // TX buffers can be allocated on demand
    return 0;
}
```

### VDS Integration
```c
// Virtual DMA Services for EMM386/QEMM compatibility
typedef struct {
    uint16_t version;
    uint16_t flags;
    uint16_t max_region_size;
    uint16_t max_regions;
    void (*lock_region)(void *addr, uint32_t size);
    void (*unlock_region)(void *addr, uint32_t size);
    uint32_t (*get_phys_addr)(void *virt_addr);
} vds_interface_t;

int setup_vds_buffers(corkscrw_context_t *ctx) {
    vds_interface_t *vds = get_vds_interface();
    
    if (!vds) {
        // No VDS - direct physical addressing
        return setup_direct_dma(ctx);
    }
    
    // Lock and translate all DMA buffers
    for (int i = 0; i < NUM_RX_DESC; i++) {
        vds->lock_region(ctx->rx_buffers[i].virt_addr, 1536);
        ctx->rx_buffers[i].phys_addr = 
            vds->get_phys_addr(ctx->rx_buffers[i].virt_addr);
    }
    
    return 0;
}
```

### Descriptor Ring Management
```c
// TX descriptor setup
void setup_tx_descriptor(corkscrw_context_t *ctx, int desc, 
                        void *buffer, uint16_t length) {
    tx_desc_t *txd = &ctx->tx_ring[desc];
    
    // Physical address for DMA
    txd->addr = get_physical_address(buffer);
    txd->length = length;
    txd->status = 0;
    
    // Link to next descriptor
    txd->next = (desc + 1) % NUM_TX_DESC;
    
    // Ensure descriptor is written before starting DMA
    memory_barrier();
}

// RX descriptor setup
void setup_rx_descriptor(corkscrw_context_t *ctx, int desc) {
    rx_desc_t *rxd = &ctx->rx_ring[desc];
    
    rxd->addr = ctx->rx_buffers[desc].phys_addr;
    rxd->length = 1536;     // Maximum ethernet frame
    rxd->status = DESC_OWNED_BY_NIC;
    rxd->next = (desc + 1) % NUM_RX_DESC;
    
    memory_barrier();
}
```

## MII Transceiver Interface

### PHY Management
```c
// MII register definitions
#define MII_BMCR        0x00    // Basic Mode Control
#define MII_BMSR        0x01    // Basic Mode Status  
#define MII_ANAR        0x04    // Auto-negotiation Advertisement
#define MII_ANLPAR      0x05    // Auto-neg Link Partner Ability

// MII bit-bang operations
uint16_t mii_read(uint16_t io_base, uint8_t phy_addr, uint8_t reg) {
    uint32_t command = 0x60000000;      // Start + Read
    command |= (phy_addr << 23);
    command |= (reg << 18);
    
    select_window(io_base, 4);
    
    // Send command via bit-banging
    for (int i = 31; i >= 0; i--) {
        uint16_t bit = (command >> i) & 1;
        
        outw(io_base + MII_DATA, bit ? MII_DATA_BIT : 0);
        delay_us(1);
        outw(io_base + MII_DATA, bit ? MII_DATA_BIT | MII_CLK : MII_CLK);
        delay_us(1);
    }
    
    // Read response
    uint16_t result = 0;
    for (int i = 15; i >= 0; i--) {
        outw(io_base + MII_DATA, MII_CLK);
        delay_us(1);
        
        if (inw(io_base + MII_DATA) & MII_DATA_BIT) {
            result |= (1 << i);
        }
        
        outw(io_base + MII_DATA, 0);
        delay_us(1);
    }
    
    return result;
}

// Auto-negotiation setup
int setup_autoneg(uint16_t io_base) {
    uint16_t anar = mii_read(io_base, PHY_ADDR, MII_ANAR);
    
    // Advertise 100TX full/half and 10T full/half
    anar |= ANAR_100TXFD | ANAR_100TXHD | ANAR_10TFD | ANAR_10THD;
    mii_write(io_base, PHY_ADDR, MII_ANAR, anar);
    
    // Start auto-negotiation
    uint16_t bmcr = mii_read(io_base, PHY_ADDR, MII_BMCR);
    bmcr |= BMCR_ANENABLE | BMCR_ANRESTART;
    mii_write(io_base, PHY_ADDR, MII_BMCR, bmcr);
    
    return 0;
}
```

## Performance Optimizations

### CPU-Specific Optimizations
```asm
; Descriptor setup optimization
setup_tx_desc_optimized:
    ; Check if 386+ for 32-bit operations
    cmp     byte [cpu_type], CPU_386
    jb      .use_16bit
    
    ; 32-bit descriptor write (386+)
    mov     eax, [buffer_phys_addr]
    mov     [edi], eax              ; Write address
    mov     ax, [buffer_length]
    mov     [edi+4], ax             ; Write length
    jmp     .memory_barrier
    
.use_16bit:
    ; 16-bit operations for 286
    mov     ax, word ptr [buffer_phys_addr]
    mov     [edi], ax
    mov     ax, word ptr [buffer_phys_addr+2]
    mov     [edi+2], ax
    mov     ax, [buffer_length]
    mov     [edi+4], ax
    
.memory_barrier:
    ; Ensure write ordering
    test    byte [cpu_flags], CPU_486_BIT
    jz      .no_barrier
    
    ; 486+ memory barrier
    lock    or      byte [edi], 0   ; Serialize writes
    
.no_barrier:
```

### Cache Optimization (486+)
```c
// Cache-aligned buffer allocation for 486+
void* alloc_cache_aligned_buffer(size_t size) {
    if (g_cpu_info.type >= CPU_TYPE_80486) {
        // Align to 16-byte cache line boundary
        void *buffer = malloc(size + 15);
        return (void*)(((uintptr_t)buffer + 15) & ~15);
    } else {
        // No cache alignment needed
        return malloc(size);
    }
}

// Cache-friendly descriptor ring layout
int setup_descriptor_rings(corkscrw_context_t *ctx) {
    if (g_cpu_info.type >= CPU_TYPE_80486) {
        // Align rings to cache boundaries
        ctx->tx_ring = alloc_cache_aligned_buffer(sizeof(tx_desc_t) * NUM_TX_DESC);
        ctx->rx_ring = alloc_cache_aligned_buffer(sizeof(rx_desc_t) * NUM_RX_DESC);
        
        // Prefetch descriptors
        for (int i = 0; i < NUM_TX_DESC; i++) {
            prefetch_cache_line(&ctx->tx_ring[i]);
        }
    } else {
        // Standard allocation
        ctx->tx_ring = malloc(sizeof(tx_desc_t) * NUM_TX_DESC);
        ctx->rx_ring = malloc(sizeof(rx_desc_t) * NUM_RX_DESC);
    }
    
    return 0;
}
```

## Interrupt Handling

### High-Performance ISR
```asm
; Optimized interrupt service routine
corkscrw_interrupt:
    push    ax
    push    dx
    push    si
    push    di
    
    ; Get interrupt status
    mov     dx, [io_base]
    add     dx, INT_STATUS
    in      ax, dx
    
    ; Check if our interrupt
    test    ax, INT_MASK
    jz      .not_ours
    
    ; Handle TX completion
    test    ax, INT_TX_COMPLETE
    jz      .check_rx
    
    call    handle_tx_complete
    
.check_rx:
    ; Handle RX available
    test    ax, INT_RX_COMPLETE
    jz      .check_errors
    
    call    handle_rx_complete
    
.check_errors:
    ; Handle error conditions
    test    ax, INT_ERROR_MASK
    jz      .acknowledge
    
    call    handle_errors
    
.acknowledge:
    ; Acknowledge interrupt
    mov     dx, [io_base]
    add     dx, COMMAND
    mov     ax, CMD_INT_ACK
    out     dx, ax
    
.not_ours:
    pop     di
    pop     si
    pop     dx
    pop     ax
    iret
```

### DMA Completion Handling
```c
// TX completion processing
void handle_tx_complete(corkscrw_context_t *ctx) {
    while (ctx->tx_dirty != ctx->tx_current) {
        int desc = ctx->tx_dirty;
        tx_desc_t *txd = &ctx->tx_ring[desc];
        
        // Check if descriptor completed
        if (txd->status & DESC_OWNED_BY_NIC) {
            break;  // Still owned by hardware
        }
        
        // Update statistics
        if (txd->status & TX_ERROR_MASK) {
            ctx->stats.tx_errors++;
            
            // Specific error handling
            if (txd->status & TX_UNDERRUN) {
                handle_tx_underrun(ctx);
            }
        } else {
            ctx->stats.tx_packets++;
            ctx->stats.tx_bytes += txd->length;
        }
        
        // Free the buffer if dynamically allocated
        if (ctx->tx_buffers[desc]) {
            free_tx_buffer(ctx->tx_buffers[desc]);
            ctx->tx_buffers[desc] = NULL;
        }
        
        ctx->tx_dirty = (desc + 1) % NUM_TX_DESC;
    }
}
```

## Error Handling and Recovery

### DMA Error Recovery
```c
// Comprehensive error handling
int handle_adapter_error(corkscrw_context_t *ctx, uint16_t error_status) {
    if (error_status & ERR_DMA_ABORT) {
        // DMA was aborted - restart rings
        log_error("DMA abort detected - reinitializing rings");
        
        // Stop DMA
        outw(ctx->io_base + COMMAND, CMD_STOP_DMA);
        
        // Reset descriptor rings
        init_tx_ring(ctx);
        init_rx_ring(ctx);
        
        // Restart DMA
        outw(ctx->io_base + COMMAND, CMD_START_DMA);
        
        return 0;
    }
    
    if (error_status & ERR_FIFO_UNDERRUN) {
        // Increase TX threshold
        if (ctx->tx_threshold < MAX_TX_THRESHOLD) {
            ctx->tx_threshold += 32;
            set_tx_threshold(ctx->io_base, ctx->tx_threshold);
            log_info("Increased TX threshold to %d", ctx->tx_threshold);
        }
    }
    
    if (error_status & ERR_LINK_FAILURE) {
        // Media disconnect - attempt recovery
        return recover_link(ctx);
    }
    
    return -1;  // Unrecoverable error
}

// Link recovery procedure
int recover_link(corkscrw_context_t *ctx) {
    // Reset PHY
    mii_write(ctx->io_base, PHY_ADDR, MII_BMCR, BMCR_RESET);
    
    // Wait for reset completion
    for (int i = 0; i < 1000; i++) {
        if (!(mii_read(ctx->io_base, PHY_ADDR, MII_BMCR) & BMCR_RESET)) {
            break;
        }
        delay_ms(1);
    }
    
    // Restart auto-negotiation
    return setup_autoneg(ctx->io_base);
}
```

## Flow Control Implementation

### IEEE 802.3x Flow Control
```c
// PAUSE frame handling
void handle_pause_frame(corkscrw_context_t *ctx, packet_t *pkt) {
    if (pkt->length >= 64 && 
        is_pause_frame(pkt->data)) {
        
        uint16_t pause_time = get_pause_time(pkt->data);
        
        if (pause_time > 0) {
            // Pause transmission
            ctx->tx_paused = true;
            ctx->pause_time = pause_time;
            
            // Set pause timer
            start_pause_timer(ctx, pause_time);
        } else {
            // Resume transmission
            ctx->tx_paused = false;
            restart_tx_queue(ctx);
        }
    }
}

// Generate PAUSE frame
int send_pause_frame(corkscrw_context_t *ctx, uint16_t pause_time) {
    static uint8_t pause_frame[] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x01,  // Dest MAC
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Src MAC (filled)
        0x88, 0x08,                           // Ethertype
        0x00, 0x01,                           // Opcode
        0x00, 0x00                            // Pause time (filled)
    };
    
    // Fill source MAC
    memcpy(&pause_frame[6], ctx->mac_addr, 6);
    
    // Fill pause time (network byte order)
    pause_frame[16] = pause_time >> 8;
    pause_frame[17] = pause_time & 0xFF;
    
    // Pad to minimum frame size
    packet_t pkt = {
        .data = pause_frame,
        .length = 64,
        .flags = PKT_FLAG_PAUSE
    };
    
    return transmit_packet(ctx, &pkt);
}
```

## Memory Usage Profile

### Runtime Memory Map
```
CORKSCRW.MOD Memory Layout (6KB total):
┌─────────────────────────────────────┐
│ Hot Code Section (4KB)              │
│ ├─ Interrupt handlers   1000 bytes  │
│ ├─ DMA management       1200 bytes  │
│ ├─ Ring operations      800 bytes   │
│ ├─ MII interface        600 bytes   │
│ └─ Flow control         400 bytes   │
├─────────────────────────────────────┤
│ Data Section (1.5KB)               │
│ ├─ NIC context          512 bytes  │
│ ├─ TX descriptors       128 bytes  │
│ ├─ RX descriptors       128 bytes  │
│ ├─ Statistics           256 bytes  │
│ ├─ MII state            128 bytes  │
│ └─ Buffers/work area    384 bytes  │
├─────────────────────────────────────┤
│ Patch Table (0.5KB)               │
│ ├─ CPU patch points     256 bytes  │
│ ├─ Alternative code     128 bytes  │
│ └─ Patch metadata       128 bytes  │
└─────────────────────────────────────┘

Cold Section (2KB, discarded):
├─ Hardware detection    600 bytes
├─ VDS setup            400 bytes
├─ Ring initialization   500 bytes
├─ PHY setup            300 bytes
└─ Self-test routines   200 bytes

DMA Buffers (allocated separately):
├─ RX buffers: 16 × 1536 = 24KB
├─ TX buffers: As needed
└─ Descriptor rings: 256 bytes
```

## Configuration Examples

### Standard Configuration
```batch
REM Auto-detect 3C515
3COMPD.COM /MODULE=CORKSCRW

REM Force specific settings
3COMPD.COM /MODULE=CORKSCRW /IO=0x300 /IRQ=10 /DMA=5

REM Enable flow control
3COMPD.COM /MODULE=CORKSCRW /FLOWCTRL=ON

REM Optimize for server use
3COMPD.COM /MODULE=CORKSCRW /TXTHRESH=512 /PERFORMANCE=HIGH
```

### Memory Manager Considerations
```batch
REM Ensure DMA buffers below 16MB
DEVICE=EMM386.EXE RAM

REM Load driver high but keep DMA buffers low
DEVICEHIGH=C:\NET\3COMPD.COM /MODULE=CORKSCRW
```

## Testing and Validation

### Hardware Requirements
```
Test Platform Requirements:
├─ 80386DX-25 or higher (bus mastering)
├─ 16-bit ISA slot
├─ 4MB+ RAM (for DMA buffers)
├─ DOS 5.0+ (for VDS support)
├─ 100BASE-TX network infrastructure
└─ Compatible hub/switch
```

### Performance Benchmarks
```
Target Performance (3C515-TX):
├─ Throughput: 95+ Mbps (95% of 100Mbps)
├─ Latency: <20 microseconds
├─ CPU usage: <10% on 80486-33
├─ Memory efficiency: 6KB resident + 25KB DMA
├─ Boot time: <2 seconds
└─ Auto-negotiation: <3 seconds
```

## Integration Points

### VDS Service Requirements
```
VDS Version Requirements:
├─ Minimum: VDS 1.0
├─ Preferred: VDS 2.0+
├─ Features needed:
│   ├─ Physical address translation
│   ├─ Memory locking
│   ├─ Region size limits
│   └─ Scatter-gather support
└─ Fallback: Direct DMA (no EMM386)
```

### Module Interface
```c
// CORKSCRW module exports
typedef struct {
    int (*init)(nic_context_t *ctx);
    int (*transmit)(nic_context_t *ctx, packet_t *pkt);
    int (*receive)(nic_context_t *ctx);
    int (*set_speed)(nic_context_t *ctx, uint32_t speed);
    int (*set_duplex)(nic_context_t *ctx, bool full_duplex);
    int (*get_statistics)(nic_context_t *ctx, stats_t *stats);
    void (*cleanup)(nic_context_t *ctx);
} corkscrw_ops_t;
```

## Conclusion

CORKSCRW.MOD represents the pinnacle of ISA networking technology, bringing 100 Mbps performance to ISA-based systems through sophisticated bus mastering and DMA techniques. Despite the complexity of implementing DMA on ISA, the module achieves excellent performance while maintaining compatibility with DOS memory managers and providing robust error recovery.

The Corkscrew serves as a critical bridge between the ISA and PCI eras, enabling legacy systems to participate in Fast Ethernet networks while maintaining the reliability and compatibility expected in DOS environments.