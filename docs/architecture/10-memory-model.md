# Memory Model Architecture

The 3Com Packet Driver implements a sophisticated three-tier memory management system designed to optimize performance and minimize conventional memory usage in DOS environments. This document provides comprehensive technical details of the memory architecture, allocation strategies, and optimization techniques.

## Overview

### Design Philosophy

The memory model is built around these core principles:
- **Minimize Conventional Memory Usage**: Keep critical 640KB space free for applications
- **Intelligent Tier Selection**: Automatically use the best available memory type
- **Unified Driver Architecture**: Single .EXE with hot/cold separation
- **Resident Budget**: Hot path ~7KB target for ISA PIO, ~10-12KB for PCI DMA
- **Bus-Aware Allocation**: Different strategies for ISA, PCI, and CardBus devices

### Memory Architecture Summary

```
┌──────────────────────────────────────────────────────────────┐
│                   Conventional Memory (640KB)                │
├──────────────────────────────────────────────────────────────┤
│  Resident Components (~7KB)   │  Initialization (Discarded)  │
│  └── Packet API Handler       │  └── Hardware Detection      │
│      └── ISR & HAL vtable     │      └── CPU Detection       │
│      └── Ring Descriptors     │      └── PnP Setup           │
├──────────────────────────────────────────────────────────────┤
│                Upper Memory Blocks (UMB) - Optional          │
│  Copy-only buffers and optional data structures              │
│  └── Statistics, logs, non-DMA buffers                       │
├──────────────────────────────────────────────────────────────┤
│          Extended Memory (XMS) - Enterprise Features         │
│  Large buffers and performance optimization structures       │
│  └── Packet pools, cache, diagnostic databases               │
└──────────────────────────────────────────────────────────────┘
```

### Detailed Memory Structure Relationships

```
══════════════════════════════════════════════════════════════════════════════
                     3COM PACKET DRIVER MEMORY ARCHITECTURE
══════════════════════════════════════════════════════════════════════════════

   CONVENTIONAL MEMORY (<1MB)                         EXTENDED MEMORY (XMS)
┌──────────────────────────────┐                   ┌─────────────────────────┐
│   DOS & DRIVERS (0-640KB)    │                   │   STAGING BUFFERS       │
├──────────────────────────────┤                   ├─────────────────────────┤
│                              │                   │ ┌─────────────────────┐ │
│    RESIDENT TSR (~5-12KB)    │                   │ │ Large Packet Pool   │ │
│ ┌──────────────────────────┐ │     copy-only     │ │  (64KB chunks)      │ │
│ │ INT 60h Handler    2KB   │◄├───────────────────┤►│                     │ │
│ │                          │ │                   │ └─────────────────────┘ │
│ │ ISR Dispatch Table       │ │                   │ ┌─────────────────────┐ │
│ │  ├─ ISA ISR ───────┐     │ │                   │ │ Statistics Cache    │ │
│ │  ├─ PCI ISR ──────┐│     │ │                   │ │  (16KB)             │ │
│ │  └─ CardBus ISR ─┐││     │ │                   │ └─────────────────────┘ │
│ └──────────────────┼┼┼─────┘ │                   │ ┌─────────────────────┐ │
│                    │││       │                   │ │ Diagnostic Logs     │ │
│ ┌──────────────────▼▼▼─────┐ │                   │ │  (8KB circular)     │ │
│ │  HAL VTable Array  2KB   │ │                   │ └─────────────────────┘ │
│ │  ┌───────────────────┐   │ │                   └─────────────────────────┘
│ │  │ISA: 3C509B (PIO)  │   │ │
│ │  │    3C515-TX (DMA) │   │ │                    UPPER MEMORY (640KB-1MB)
│ │  ├───────────────────┤   │ │                   ┌─────────────────────────┐
│ │  │PCI: Vortex (PIO)  │   │ │                   │   OPTIONAL BUFFERS      │
│ │  │    Boomerang (DMA)│   │ │     copy-only     ├─────────────────────────┤
│ │  │    Cyclone (DMA)  │   │◄├───────────────────┤ Performance Counters    │
│ │  │    Tornado (DMA)  │   │ │                   │ Debug Structures        │
│ │  ├───────────────────┤   │ │                   │ Non-Critical Tables     │
│ │  │CardBus: 3C575     │   │ │                   └─────────────────────────┘
│ │  └───────────────────┘   │ │
│ └──────────────────────────┘ │                   DMA TRANSFER PATHS
│                              │                   ═══════════════════
│         DMA BUFFERS          │
│ ┌──────────────────────────┐ │    ┌───────────┐  ISA DMA
│ │ ISA DMA Buffers          │◄├────┤   3C515   │  • 64KB boundary check
│ │  • 64KB aligned          │ │    └───────────┘  • 16MB limit
│ │  • <16MB address         │ │                   • VDS if paging
│ │  • VDS locked if needed  │ │    ┌───────────┐
│ ├──────────────────────────┤ │    │ Boomerang │  PCI DMA
│ │ PCI DMA Descriptors      │ │    │  Cyclone  │  • No 64KB restriction
│ │  • TX Ring [16]          │◄├────┤  Tornado  │  • 32-bit addressing
│ │  • RX Ring [16/32]       │ │    └───────────┘  • Cache-line aligned
│ │  • 16-byte aligned       │ │
│ ├──────────────────────────┤ │    ┌───────────┐  CardBus DMA
│ │ CardBus DMA Descriptors  │◄├────┤  3C575    │  • Same as PCI
│ │  • Same as PCI           │ │    └───────────┘  • 32-bit addressing
│ └──────────────────────────┘ │
│                              │    ┌───────────┐  PIO Only
│ ┌──────────────────────────┐ │    │  3C509B   │  • No DMA buffers
│ │ PIO Packet Buffers       │◄├────┤  Vortex   │  • Direct I/O port
│ │  • Immediate transfer    │ │    └───────────┘  • Small footprint
│ └──────────────────────────┘ │
│                              │
│ ┌──────────────────────────┐ │    COPY OPERATIONS
│ │ Routing Tables (256)     │ │    ════════════════
│ │  • MAC → NIC mapping     │ │
│ │  • Static routes         │ │    XMS ──copy──> Conv ──DMA──> NIC
│ │  • Flow cache            │ │    NIC ──DMA──> Conv ──copy──> XMS
│ └──────────────────────────┘ │
│                              │    UMB ──copy──> Conv ──DMA──> NIC
│ ┌──────────────────────────┐ │    NIC ──DMA──> Conv ──copy──> UMB
│ │ Per-NIC Context          │ │
│ │  • State & config        │ │    Conv ←─direct─→ NIC (PIO only)
│ │  • Statistics            │ │
│ └──────────────────────────┘ │
└──────────────────────────────┘

MEMORY TIER SELECTION LOGIC
════════════════════════════
                                      ┌─────────────┐
                                      │ Allocate    │
                                      │ Request     │
                                      └──────┬──────┘
                                             │
                                    ┌────────▼────────┐
                                    │ DMA Required?   │
                                    └────────┬────────┘
                                             │
                            ┌────────────────┼────────────────┐
                            │ YES                          NO │
                            ▼                                 ▼
                    ┌───────────────┐                ┌────────────────┐
                    │ Conventional  │                │ XMS Available? │
                    │ Memory Only   │                └────────┬───────┘
                    │ (<1MB)        │                         │
                    └───────┬───────┘             ┌───────────┼───────────┐
                            │                     │ YES                NO │
                    ┌───────▼────────┐            ▼                       ▼
                    │ ISA DMA?       │      ┌───────────┐        ┌──────────────┐
                    └───────┬────────┘      │ Use XMS   │        │UMB Available?│
                            │               │ (copy ops)│        └──────┬───────┘
                  ┌─────────┼─────────┐     └───────────┘               │
                  │ YES            NO │                          ┌──────┼──────┐
                  ▼                   ▼                          │ YES      NO │
            ┌────────────┐     ┌─────────────┐                   ▼             ▼
            │ 64KB align │     │ Cache line  │               ┌─────────┐  ┌──────────┐
            │ 16MB limit │     │ align only  │               │ Use UMB │  │Use Conv. │
            │ VDS lock   │     │ Direct phys │               │ (copy)  │  │(fallback)│
            └────────────┘     └─────────────┘               └─────────┘  └──────────┘
```

## Memory Configuration Scenarios

### Minimalist Configuration (~7KB Resident)

**Use Case**: Single NIC, basic networking only

```
Conventional Memory (Resident):  ~7KB
├── Packet API Handler:           2KB
├── ISR & HAL vtable:             2KB
├── Ring Descriptors:             1KB
├── Core Data Structures:         2KB
└── Total After Init:            ~7KB

Discarded After Init:           ~23KB
├── Hardware Detection:           8KB
├── CPU Detection:                2KB
├── PnP Configuration:            5KB
├── Initialization Code:          8KB
└── (Released via INT 21h/4Ah)
```

**Memory Layout**:
- Unified driver loads as single .EXE
- Initialization code discarded after TSR installation
- HAL vtable provides hardware abstraction
- Optimized for maximum application memory

### Standard Configuration (~12KB Resident)

**Use Case**: Multi-NIC support with advanced features

```
Conventional Memory (Resident): ~12KB
├── Packet API Handler:           2KB
├── ISR & HAL vtables:            3KB
├── Ring Descriptors (2 NICs):    2KB
├── Packet Buffers:               3KB
├── Statistics & Counters:        1KB
├── Routing Tables:               1KB
└── Total After Init:           ~12KB
```

### Enterprise Configuration (~18KB Resident + XMS)

**Use Case**: Full feature set with performance optimization

```
Conventional Memory (Resident): ~18KB
├── Core Components:             12KB (as Standard)
├── Enhanced Buffers:             4KB
├── Performance Counters:         2KB
└── XMS Management:               1KB

Extended Memory (XMS):           Variable
├── Large Packet Buffers:        64KB
├── Statistics Database:         16KB
├── Diagnostic Logs:              8KB
└── Cache Structures:            32KB
```

### Mixed Bus Configuration (~15KB Resident)

**Use Case**: Combined ISA and PCI NICs in single system

```
Conventional Memory (Resident): ~15KB
├── Packet API Handler:           2KB
├── ISR & HAL vtables:            3KB  (ISA + PCI vtables)
├── ISA Ring Descriptors:         1KB  (3C515-TX: 16+16 entries)
├── PCI Ring Descriptors:         2KB  (Boomerang: 16+32 entries)
├── PCI DMA Buffers:              4KB  (16-byte aligned)
├── Routing Tables:               2KB  (multi-NIC support)
└── Statistics & Counters:        1KB
```

## Three-Tier Memory System

### Tier 1: Conventional Memory (0-640KB)

**Purpose**: Critical runtime components that must remain resident
**Allocation Strategy**: Minimize usage, essential services only

#### Core Resident Components (~7KB)

```c
/* Resident memory layout after initialization */
typedef struct {
    packet_interface_t  packet_api;        /*  2KB - Packet driver API */
    isr_handler_t       isr_handler;       /*  1KB - Interrupt handler */
    hal_vtable_t        hal_vtable;        /*  1KB - Hardware abstraction */
    ring_descriptors_t  ring_desc;         /*  1KB - DMA ring management */
    nic_context_t       nic_context;       /*  2KB - NIC state & config */
} resident_memory_t;  /* Total: ~7KB */
```

#### Memory Pressure Handling

```c
/* Conventional memory allocation with fallback */
void* allocate_conventional(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL && size > EMERGENCY_THRESHOLD) {
        /* Attempt emergency compaction */
        compact_buffers();
        ptr = malloc(size);
    }

    if (ptr == NULL) {
        /* Fall back to UMB if available */
        ptr = allocate_umb(size);
    }

    log_memory_allocation(ptr, size, "CONVENTIONAL");
    return ptr;
}
```

### Tier 2: Upper Memory Blocks (640KB-1MB)

**Purpose**: Optional buffers and non-critical data structures
**Allocation Strategy**: Use for copy-only operations, never for DMA

#### UMB Allocation Strategy

```c
/* UMB allocation policy for non-DMA buffers */
/* UMB memory (640KB-1MB) cannot be used for DMA operations */
/* Policy enforcement: */
/* - UMB buffers are marked as non-DMA capable */
/* - Copy operations required for any DMA transfers */
/* - Used only for statistics, logs, and non-critical data */
/* - Provides memory relief without DMA complications */
```

### Tier 3: Extended Memory (XMS)

**Purpose**: Large buffers and enterprise features
**Allocation Strategy**: Copy-only operations, performance optimization

#### Critical Policy: No Direct DMA to XMS

**XMS memory is NEVER used directly for NIC DMA operations.** This is a fundamental design decision for reliability:

**Why XMS Cannot Be Used for DMA:**
1. **Physical Address Issues**: XMS handles don't guarantee contiguous physical memory
2. **Addressing Limits**: Many NICs have 24-bit (ISA) or 32-bit (PCI) addressing constraints
3. **Platform Compatibility**: 286/8086 systems and diverse chipsets cannot reliably DMA to XMS
4. **Boundary Violations**: XMS allocations may cross 64KB boundaries unpredictably

**Implementation Strategy:**
- All DMA descriptors and buffers allocated in conventional memory (<1MB)
- XMS used only for staging buffers with explicit copy operations
- Copy from XMS to DMA-safe conventional buffers before transmission
- Copy from DMA buffers to XMS after reception for processing

```c
/* XMS is explicitly marked as non-DMA capable */
#define XMS_DMA_CAPABLE  0  /* Never use XMS for direct DMA */

/* Policy: XMS requires copy operations for DMA transfers */
/* Actual implementation uses conventional buffers as intermediary */
/* 1. Allocate DMA-safe buffer in conventional memory */
/* 2. Copy from XMS to conventional buffer */
/* 3. Perform DMA from conventional buffer */
/* 4. Copy results back to XMS if needed */
```

#### XMS Memory Management

```c
/* XMS allocation with error handling */
typedef struct {
    uint16_t    handle;
    uint32_t    size;
    void*       linear_address;
    uint8_t     locked;
} xms_block_t;

xms_block_t* allocate_xms_block(size_t size) {
    xms_block_t* block = malloc(sizeof(xms_block_t));
    if (block == NULL) return NULL;

    /* Request XMS memory from driver */
    if (xms_allocate_memory(size, &block->handle) != XMS_SUCCESS) {
        free(block);
        return NULL;
    }

    /* Lock for access */
    if (xms_lock_memory(block->handle, &block->linear_address) != XMS_SUCCESS) {
        xms_free_memory(block->handle);
        free(block);
        return NULL;
    }

    block->size = size;
    block->locked = 1;
    return block;
}
```

## DMA Buffer Management

### Bus-Specific Allocation Strategies

Different bus architectures have different DMA requirements:

#### ISA Bus DMA (3C509B PIO, 3C515-TX Bus Master)
- **64KB Boundary**: Strict enforcement to prevent ISA DMA controller wraparound
- **16MB Limit**: 24-bit addressing limitation of ISA DMA
- **Alignment**: 16-byte alignment for efficiency
- **Allocation**: Uses `allocate_constrained_dma_buffer()` with retry logic
- **Memory Source**: Conventional memory only (<1MB)

#### PCI Bus DMA (Vortex, Boomerang, Cyclone, Tornado)
- **No 64KB Restriction**: PCI uses 32-bit addressing
- **Cache Line Alignment**: 16-byte minimum, 32-byte preferred
- **Direct Physical Access**: No VDS required in real mode
- **Allocation**: Uses `mem_alloc_aligned()` for descriptor rings
- **Memory Source**: Conventional memory only (<1MB)

#### CardBus DMA (3C575 series)
- **Treated as PCI**: Inherits PCI memory model
- **32-bit Addressing**: Full 4GB address space
- **Same as PCI**: Uses PCI allocation path in code
- **Memory Source**: Conventional memory only (<1MB)

#### Universal DMA Memory Policy
**All bus types allocate DMA buffers from conventional memory (<1MB) only.**
- UMB is never used for DMA (requires copy operations)
- XMS is never used for DMA (requires copy operations)
- This ensures compatibility across all DOS configurations

### Ring Buffer Sizes by Generation

```c
/* ISA Cards */
#define ISA_3C515_TX_RING_SIZE    16    /* 3C515-TX transmit descriptors */
#define ISA_3C515_RX_RING_SIZE    16    /* 3C515-TX receive descriptors */

/* PCI Vortex (PIO-only) */
/* No ring buffers - uses immediate PIO transfers */

/* PCI Boomerang/Cyclone/Tornado (DMA) */
#define PCI_BOOM_TX_RING_SIZE     16    /* Boomerang TX descriptors */
#define PCI_BOOM_RX_RING_SIZE     16    /* Boomerang RX descriptors */
#define PCI_STANDARD_TX_RING      16    /* Standard PCI TX ring */
#define PCI_STANDARD_RX_RING      32    /* Standard PCI RX ring */

/* Memory footprint per NIC type */
ISA 3C515-TX:  ~1KB ring descriptors + 4KB buffers = ~5KB
PCI Vortex:    ~2KB packet buffers (no rings) = ~2KB
PCI Boomerang: ~2KB ring descriptors + 8KB buffers = ~10KB
```

### 64KB Boundary Restrictions (ISA Only)

ISA DMA buffers must comply with 64KB boundary limitations to prevent buffer corruption:

```c
/* 64KB boundary validation (from memory.c) */
static bool crosses_64k_boundary(void *addr, uint32_t size) {
    uint32_t linear = ((uint32_t)FP_SEG(addr) << 4) + FP_OFF(addr);
    uint32_t start_64k = linear & 0xFFFF0000;
    uint32_t end_64k = (linear + size - 1) & 0xFFFF0000;
    return start_64k != end_64k;
}

/* DMA-safe buffer allocation (from memory.c) */
static void* allocate_constrained_dma_buffer(uint32_t size, uint32_t alignment, 
                                            bool use_isa_dma, int retry_count) {
    /* Calculate allocation size with room for alignment */
    alloc_size = size + alignment - 1;
    
    /* Add extra if we need to avoid 64KB boundary */
    if (use_isa_dma) {
        alloc_size += 0x10000;  /* Extra 64KB to ensure we can avoid boundary */
    }
    
    /* Allocate base buffer */
    base_buffer = memory_alloc(alloc_size, MEM_TYPE_DMA);
    
    /* Apply ISA DMA constraints if needed */
    if (use_isa_dma) {
        /* Check 16MB boundary (ISA DMA limit - 24-bit address) */
        if (aligned_addr + size > 0x1000000) {
            /* Retry allocation */
        }
        
        /* Check 64KB boundary crossing */
        if (crosses_64k_boundary(aligned_buffer, size)) {
            /* Adjust within allocation */
        }
    }
    
    return aligned_buffer;
}
```

### ISR Safety Requirements

**Critical Timing Constraints:**
- Maximum ISR execution: ≤60 microseconds
- Maximum CLI duration: ≤8 microseconds
- Memory operations in ISR: Try-lock only, no blocking

```c
/* ISR-safe memory operations */
void* isr_safe_alloc(size_t size) {
    return try_alloc_from_pool(size, 0); /* No wait, fail if unavailable */
}

/* Forbidden in ISR context */
void* isr_unsafe_alloc(size_t size) {
    return alloc_with_gc(size); /* May trigger garbage collection */
}
```

### CPU-Specific Alignment Requirements

```c
typedef struct {
    cpu_type_t cpu;
    size_t min_alignment;
    size_t dma_alignment;
    size_t cache_line;
} cpu_alignment_t;

static const cpu_alignment_t cpu_alignments[] = {
    { CPU_8086,    2,  16, 0  },  /* Word alignment, no cache */
    { CPU_80286,   2,  16, 0  },  /* Word alignment, no cache */
    { CPU_80386,   4,  16, 16 },  /* DWORD alignment, 16B cache */
    { CPU_80486,   4,  16, 16 },  /* DWORD alignment, 16B cache */
    { CPU_PENTIUM, 4,  32, 32 }, /* DWORD alignment, 32B cache */
};
```

### VDS Integration for Bus Mastering

For ISA bus-master capable devices (3C515-TX), VDS (Virtual DMA Services) provides physical address translation when memory managers are present. PCI/CardBus devices do not require VDS in real mode:

```c
/* VDS DDS structure (from vds.h) */
typedef struct {
    uint32_t    size;           /* Region size in bytes */
    uint32_t    offset;         /* Linear offset */
    uint16_t    segment;        /* Segment (or selector) */
    uint16_t    buffer_id;      /* Buffer ID (0 if not allocated) */
    uint32_t    physical;       /* Physical address */
    uint16_t    flags;          /* Returned flags indicating cache handling */
} VDS_DDS;

/* VDS buffer locking (from dma_operations.c) */
int lock_dma_buffer_vds(void* buffer, size_t size) {
    VDS_DDS dds;
    int result;
    
    /* Check if VDS is available and should be used */
    if (vds_available()) {
        /* Lock region with VDS */
        result = vds_lock_region(buffer, size, &dds);
        if (result != VDS_SUCCESS) {
            LOG_ERROR("DMA: VDS lock failed with code %d", result);
            return ERROR_DMA_LOCK;
        }
        
        /* Physical address now available in dds.physical */
        return dds.physical;
    }
    
    /* No VDS - use direct physical address */
    return virt_to_phys(buffer);
}
```

## Memory Optimization Techniques

### Hot/Cold Separation

```c
/* Driver sections for hot/cold separation */
#pragma code_seg("INIT_TEXT")    /* Initialization - discarded */
#pragma data_seg("INIT_DATA")    /* Init data - discarded */

void init_hardware(void) {
    /* This code is discarded after initialization */
    detect_nics();
    configure_pnp();
    setup_eeprom();
}

#pragma code_seg("_TEXT")        /* Resident code */
#pragma data_seg("_DATA")        /* Resident data */

void packet_handler(void) {
    /* This code remains resident */
    process_packet();
}

/* TSR installation with memory release */
void install_tsr(void) {
    /* Calculate resident size */
    uint16_t resident_paras = (_end_resident - _start) >> 4;

    /* Release initialization memory */
    _dos_keep(0, resident_paras);
}
```

### Buffer Pool Management

```c
/* Per-NIC buffer allocation - sizes vary by bus type */
typedef struct {
    nic_id_t        nic_id;
    bus_type_t      bus_type;              /* ISA, PCI, CardBus */
    memory_tier_t   preferred_tier;
    void*           tx_buffers[32];        /* Max TX ring (PCI can use 32) */
    void*           rx_buffers[32];        /* Max RX ring (PCI standard) */
    size_t          buffer_size;           /* Per-buffer size */
    uint32_t        allocation_flags;      /* DMA, alignment, etc. */
    uint16_t        actual_tx_ring_size;   /* 16 for most, varies by NIC */
    uint16_t        actual_rx_ring_size;   /* 16 for ISA, 32 for PCI */
} nic_buffer_context_t;

int allocate_nic_buffers(nic_buffer_context_t* ctx) {
    /* Determine optimal buffer placement */
    if (ctx->preferred_tier == TIER_XMS && xms_available()) {
        return allocate_buffers_xms(ctx);
    } else if (ctx->preferred_tier == TIER_UMB && umb_available()) {
        return allocate_buffers_umb(ctx);
    } else {
        /* Fall back to conventional with smaller buffers */
        ctx->buffer_size = min(ctx->buffer_size, 256);
        return allocate_buffers_conventional(ctx);
    }
}
```

## Performance Characteristics

### Memory Access Patterns

| Memory Type | Access Time | ISA DMA | PCI DMA | Fragmentation |
|-------------|-------------|---------|---------|---------------|
| Conventional | 0 wait states | Yes | Yes | High |
| UMB | 0-1 wait states | No (copy required) | No (copy required) | Medium |
| XMS | 1-2 wait states | No (copy required) | No (copy required) | Low |

### Bus-Specific Memory Requirements

| NIC Type | Bus | Ring Buffers | DMA Buffers | Total Resident |
|----------|-----|--------------|-------------|----------------|
| 3C509B | ISA | None (PIO) | None | ~5KB |
| 3C515-TX | ISA | 1KB (16+16) | 4KB | ~8KB |
| 3C59x Vortex | PCI | None (PIO) | None | ~5KB |
| 3C90x Boomerang | PCI | 2KB (16+16) | 8KB | ~12KB |
| 3C905B Cyclone | PCI | 2KB (16+32) | 8KB | ~12KB |
| 3C905C Tornado | PCI | 2KB (16+32) | 8KB | ~12KB |
| 3C575 CardBus | CardBus | 2KB (16+32) | 8KB | ~12KB |

### Allocation Performance

```c
/* Memory allocation benchmarks */
typedef struct {
    memory_tier_t   tier;
    uint32_t        avg_alloc_cycles;    /* CPU cycles */
    uint32_t        avg_free_cycles;     /* CPU cycles */
    uint8_t         fragmentation_pct;   /* Fragmentation percentage */
} memory_perf_t;

static const memory_perf_t memory_performance[] = {
    { TIER_CONVENTIONAL, 150, 50,  25 },  /* Fast but fragments */
    { TIER_UMB,         200, 75,  15 },   /* Good balance */
    { TIER_XMS,         300, 100,  5 }    /* Slower but clean */
};
```

### Memory Usage Monitoring

```c
/* Runtime memory monitoring */
typedef struct {
    uint32_t    total_allocated;
    uint32_t    peak_usage;
    uint32_t    allocation_count;
    uint32_t    free_count;
    uint32_t    compaction_count;
    uint8_t     fragmentation_level;     /* 0-100% */
} memory_stats_t;

void update_memory_statistics(void) {
    memory_stats_t* stats = get_memory_stats();

    /* Update peak usage */
    uint32_t current_usage = get_current_memory_usage();
    if (current_usage > stats->peak_usage) {
        stats->peak_usage = current_usage;
    }

    /* Calculate fragmentation */
    stats->fragmentation_level = calculate_fragmentation_percentage();

    /* Trigger compaction if needed */
    if (stats->fragmentation_level > COMPACTION_THRESHOLD) {
        schedule_memory_compaction();
        stats->compaction_count++;
    }
}
```

## Integration with DOS Memory Management

### DOS Memory Managers Compatibility

```c
/* Support for various DOS memory managers */
typedef enum {
    DOS_MEMORY_MANAGER_NONE,
    DOS_MEMORY_MANAGER_HIMEM,     /* Microsoft HIMEM.SYS */
    DOS_MEMORY_MANAGER_QEMM,      /* Quarterdeck QEMM */
    DOS_MEMORY_MANAGER_EMM386,    /* Microsoft EMM386 */
    DOS_MEMORY_MANAGER_386MAX     /* Qualitas 386MAX */
} dos_memory_manager_t;

int detect_memory_manager(void) {
    /* Check for XMS driver (HIMEM.SYS) */
    if (is_xms_driver_present()) {
        log_info("XMS driver detected, extended memory available");
        return DOS_MEMORY_MANAGER_HIMEM;
    }

    /* Check for EMM386 or QEMM */
    if (is_umb_provider_present()) {
        log_info("UMB provider detected");
        return detect_specific_umb_provider();
    }

    log_warning("No memory manager detected, conventional memory only");
    return DOS_MEMORY_MANAGER_NONE;
}
```

### Conventional Memory Optimization

```c
/* Minimize conventional memory footprint */
void optimize_conventional_usage(void) {
    /* Move non-critical structures to UMB */
    if (umb_available()) {
        move_debug_buffers_to_umb();
        move_statistics_to_umb();
        move_large_tables_to_umb();
    }

    /* Use XMS for large data structures */
    if (xms_available()) {
        move_packet_pools_to_xms();
        move_cache_to_xms();
        move_diagnostic_buffers_to_xms();
    }

    /* Compact remaining conventional memory */
    compact_conventional_memory();

    log_info("Conventional memory optimization complete: %uKB free",
             get_conventional_free_memory() / 1024);
}
```

This memory model enables the 3Com Packet Driver to achieve maximum functionality while maintaining compatibility with DOS memory constraints. The unified driver architecture with hot/cold separation ensures minimal resident memory usage (~5KB for PIO NICs, ~12KB for DMA-capable NICs) while providing enterprise-grade capabilities through intelligent, bus-aware memory management.

Key distinctions by bus architecture:
- **ISA**: Strict 64KB boundary and 16MB limit constraints, VDS required with memory managers
- **PCI**: Relaxed alignment requirements, no 64KB restrictions, direct physical addressing
- **CardBus**: Treated as PCI with 32-bit addressing capabilities
