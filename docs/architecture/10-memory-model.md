# Memory Model Architecture

The 3Com Packet Driver implements a sophisticated three-tier memory management system designed to optimize performance and minimize conventional memory usage in DOS environments. This document provides comprehensive technical details of the memory architecture, allocation strategies, and optimization techniques.

## Overview

### Design Philosophy
The memory model is built around these core principles:
- **Minimize Conventional Memory Usage**: Keep critical 640KB space free for applications
- **Intelligent Tier Selection**: Automatically use the best available memory type
- **Modular Allocation**: Per-module memory pools with isolation
- **Dynamic Scaling**: Memory usage scales with loaded features (43KB-88KB)

### Memory Architecture Summary
```
┌─────────────────────────────────────────────────────────────┐
│                     Conventional Memory (640KB)             │
├─────────────────────────────────────────────────────────────┤
│ Core Loader (30KB)        │ Essential Runtime Components    │
│ └── Base Services         │ └── Packet Interface           │
│     └── Module Manager    │     └── Hardware Abstraction   │
├─────────────────────────────────────────────────────────────┤
│                Upper Memory Blocks (UMB) - Optional         │
│ Hardware Modules (15-25KB each)                            │
│ └── PTASK.MOD (EtherLink III)                              │
│ └── BOOMTEX.MOD (Vortex/Boomerang/Hurricane)              │
├─────────────────────────────────────────────────────────────┤
│              Extended Memory (XMS) - Enterprise Features    │
│ Feature Modules (1-8KB each)                              │
│ └── WOL, VLAN, HWSTATS, MII, DIAGUTIL, etc.               │
│ Large Buffers & Statistics Collections                    │
└─────────────────────────────────────────────────────────────┘
```

## Three-Tier Memory System

### Tier 1: Conventional Memory (0-640KB)
**Purpose**: Critical runtime components that must remain resident
**Allocation Strategy**: Minimize usage, essential services only

#### Core Loader (30KB Fixed)
```c
/* Core loader memory layout */
typedef struct {
    packet_interface_t  packet_api;        /*  4KB - Packet driver API */
    module_manager_t    module_mgr;        /*  6KB - Module loading system */
    hardware_db_t       hardware_db;       /*  8KB - NIC detection database */
    config_system_t     config_mgr;        /*  4KB - Configuration management */
    core_services_t     core_services;     /*  4KB - API for modules */
    interrupt_handler_t int_handlers;      /*  2KB - Interrupt management */
    debug_system_t      debug_mgr;         /*  2KB - Logging and diagnostics */
} core_loader_t;  /* Total: ~30KB */
```

#### Essential Runtime Components (13KB Variable)
- **Ring Buffers**: 8KB (16 descriptors × 2 rings × 256 bytes)
- **Packet Buffers**: 4KB (Emergency packet handling)
- **Statistics**: 1KB (Core performance counters)

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
**Purpose**: Hardware-specific modules and large static structures
**Allocation Strategy**: Optimize for code modules and hardware databases

#### Hardware Modules
**PTASK.MOD - EtherLink III Family (~15KB)**
```c
/* EtherLink III module memory structure */
typedef struct {
    nic_driver_t    drivers[23];           /*  8KB - Per-NIC drivers */
    media_control_t media_handlers[5];     /*  2KB - Media type handling */
    eeprom_parser_t eeprom_logic;          /*  3KB - EEPROM interpretation */
    isa_services_t  isa_bus_services;      /*  2KB - ISA bus abstraction */
} etl3_module_t;  /* Total: ~15KB */
```

**BOOMTEX.MOD - Vortex/Boomerang/Hurricane (~25KB)**
```c
/* Unified PCI module memory structure */
typedef struct {
    nic_driver_t        drivers[42];       /* 12KB - Per-NIC drivers */
    pci_services_t      pci_bus_services;  /*  4KB - PCI bus abstraction */
    dma_engine_t        dma_management;    /*  3KB - Bus master DMA */
    window_cache_t      register_cache;    /*  2KB - Window optimization */
    cyclone_features_t  cyclone_logic;     /*  2KB - Cyclone-specific */
    tornado_features_t  tornado_logic;     /*  2KB - Tornado-specific */
} boomtex_module_t;  /* Total: ~25KB */
```

#### UMB Allocation Strategy
```c
/* UMB allocation with optimal placement */
typedef struct {
    void*           base_address;
    size_t          total_size;
    size_t          used_size;
    umb_block_t*    free_blocks;
    module_info_t*  loaded_modules;
} umb_manager_t;

int allocate_module_in_umb(const char* module_name, size_t module_size) {
    /* Find best-fit UMB block */
    umb_block_t* block = find_best_fit_umb(module_size);
    if (block == NULL) return UMB_NO_SPACE;
    
    /* Load module into UMB */
    if (load_module_at_address(module_name, block->address) == SUCCESS) {
        block->used = 1;
        block->module = module_name;
        return UMB_SUCCESS;
    }
    
    return UMB_LOAD_FAILED;
}
```

### Tier 3: Extended Memory (XMS) - Enterprise Features
**Purpose**: Enterprise feature modules and large data structures
**Allocation Strategy**: Dynamic allocation for optional features

#### Enterprise Feature Modules (1-8KB each)
```c
/* Enterprise module memory allocation */
typedef struct {
    char            module_name[12];       /* DOS 8.3 + .MOD */
    void*           xms_handle;            /* XMS memory handle */
    size_t          module_size;           /* Actual module size */
    uint32_t        feature_flags;         /* Capability flags */
    service_api_t*  service_interface;     /* Module services */
} enterprise_module_t;

/* Feature module sizes */
static const module_size_info_t enterprise_modules[] = {
    { "WOL.MOD",      4096 },  /* Wake-on-LAN support */
    { "ANSIUI.MOD",   8192 },  /* Color terminal interface */
    { "VLAN.MOD",     3072 },  /* 802.1Q VLAN tagging */
    { "MCAST.MOD",    5120 },  /* Advanced multicast */
    { "JUMBO.MOD",    2048 },  /* Jumbo frame support */
    { "MII.MOD",      3072 },  /* Media Independent Interface */
    { "HWSTATS.MOD",  3072 },  /* Hardware statistics */
    { "PWRMGMT.MOD",  3072 },  /* Power management */
    { "NWAY.MOD",     2048 },  /* Auto-negotiation */
    { "DIAGUTIL.MOD", 6144 },  /* Diagnostic utilities */
    { "MODPARAM.MOD", 4096 },  /* Runtime configuration */
    { "MEDFAIL.MOD",2048 },  /* Media failover */
    { "DEFINT.MOD",   2048 },  /* Deferred interrupts */
    { "WINCACHE.MOD", 1024 }   /* Window caching */
};
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

## Memory Configuration Scenarios

### Minimalist Configuration (43KB Total)
**Use Case**: Single NIC, basic networking only
```
Conventional Memory:  43KB
├── Core Loader:      30KB
├── Hardware Module:  13KB (PTASK.MOD only)
└── UMB/XMS Usage:     0KB
```

**Memory Layout**:
- Core loader with essential services
- Single hardware module (auto-detected)
- No enterprise features loaded
- Optimized for maximum application memory

### Standard Enterprise (59KB Total)
**Use Case**: Typical business deployment with core enterprise features
```
Conventional Memory:  30KB (Core only)
Upper Memory:         15KB (Hardware module)
Extended Memory:      14KB (8 enterprise modules)
Total Footprint:      59KB
```

**Loaded Modules**:
- Core enterprise features: WOL, ANSIUI, VLAN, MCAST, JUMBO
- Enterprise critical: MII, HWSTATS, PWRMGMT
- Hardware: Auto-detected family module

### Advanced Enterprise (69KB Total)
**Use Case**: Full enterprise features with diagnostics
```
Conventional Memory:  30KB (Core only)
Upper Memory:         25KB (Both hardware modules)
Extended Memory:      14KB (11 enterprise modules)
Total Footprint:      69KB
```

**Additional Features**:
- Advanced modules: NWAY, DIAGUTIL, MODPARAM
- Support for all 65 NIC variants simultaneously
- Professional diagnostic capabilities

### Maximum Configuration (88KB Total)
**Use Case**: Complete functionality - all features enabled
```
Conventional Memory:  30KB (Core only)
Upper Memory:         40KB (Hardware + overflow)
Extended Memory:      18KB (All 14 enterprise modules)
Total Footprint:      88KB
```

**Complete Feature Set**:
- All hardware modules loaded
- All 14 enterprise feature modules
- Performance optimizations: MEDFAIL, DEFINT, WINCACHE
- Maximum diagnostic and monitoring capabilities

## Memory Optimization Techniques

### Dynamic Loading Strategy
```c
/* Memory-aware module loading */
typedef enum {
    LOAD_CONVENTIONAL_ONLY,     /* Minimal memory usage */
    LOAD_WITH_UMB,             /* Use UMB if available */
    LOAD_ENTERPRISE_XMS        /* Full XMS utilization */
} memory_strategy_t;

int load_modules_optimized(memory_strategy_t strategy) {
    memory_info_t mem_info;
    get_memory_info(&mem_info);
    
    /* Always load core in conventional memory */
    if (load_core_loader() != SUCCESS) return LOAD_FAILED;
    
    switch (strategy) {
        case LOAD_CONVENTIONAL_ONLY:
            return load_minimal_hardware_module();
            
        case LOAD_WITH_UMB:
            if (mem_info.umb_available > 20*1024) {
                return load_hardware_modules_umb();
            }
            /* Fall through to conventional if UMB insufficient */
            return load_minimal_hardware_module();
            
        case LOAD_ENTERPRISE_XMS:
            if (mem_info.xms_available > 32*1024) {
                load_hardware_modules_umb();
                return load_enterprise_modules_xms();
            }
            /* Fall back to standard configuration */
            return load_with_umb_only();
    }
}
```

### Buffer Pool Management
```c
/* Per-NIC buffer allocation */
typedef struct {
    nic_id_t        nic_id;
    memory_tier_t   preferred_tier;
    void*           tx_buffers[16];        /* Transmit ring */
    void*           rx_buffers[16];        /* Receive ring */
    size_t          buffer_size;           /* Per-buffer size */
    uint32_t        allocation_flags;      /* DMA, alignment, etc. */
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

### Memory Compaction
```c
/* Defragmentation for long-running systems */
typedef struct {
    void*       old_address;
    void*       new_address;
    size_t      size;
    uint8_t     moved;
} relocation_entry_t;

int compact_conventional_memory(void) {
    relocation_entry_t relocations[MAX_RELOCATIONS];
    int relocation_count = 0;
    
    /* Phase 1: Identify moveable blocks */
    for (int i = 0; i < allocated_block_count; i++) {
        if (blocks[i].moveable && blocks[i].tier == TIER_CONVENTIONAL) {
            relocations[relocation_count++] = create_relocation(&blocks[i]);
        }
    }
    
    /* Phase 2: Compact by moving blocks */
    for (int i = 0; i < relocation_count; i++) {
        if (move_memory_block(&relocations[i]) == SUCCESS) {
            update_pointers(&relocations[i]);
            relocations[i].moved = 1;
        }
    }
    
    /* Phase 3: Update free block list */
    rebuild_free_block_list();
    
    return calculate_memory_savings(relocations, relocation_count);
}
```

## Performance Characteristics

### Memory Access Patterns
| Memory Type | Access Time | DMA Compatible | Fragmentation |
|-------------|-------------|----------------|---------------|
| Conventional | 0 wait states | Yes | High |
| UMB | 0-1 wait states | Yes | Medium |
| XMS | 1-2 wait states | No* | Low |

*XMS requires copying to conventional memory for DMA operations

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
        move_module_headers_to_umb();
    }
    
    /* Use XMS for large data structures */
    if (xms_available()) {
        move_eeprom_database_to_xms();
        move_configuration_cache_to_xms();
        move_diagnostic_buffers_to_xms();
    }
    
    /* Compact remaining conventional memory */
    compact_conventional_memory();
    
    log_info("Conventional memory optimization complete: %uKB free",
             get_conventional_free_memory() / 1024);
}
```

This memory model enables the 3Com Packet Driver to achieve maximum functionality while maintaining compatibility with DOS memory constraints. The three-tier approach ensures optimal performance across diverse hardware configurations while providing enterprise-grade capabilities through intelligent memory management.