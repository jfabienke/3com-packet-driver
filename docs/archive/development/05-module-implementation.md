# Module Implementation Guide

## Overview

This guide provides comprehensive instructions for implementing modules in the 3Com packet driver's enhanced modular architecture. It covers module structure, CPU optimization techniques, hot/cold separation, and testing requirements.

## Module Categories

### Hot Path Modules (Resident)

Performance-critical code that remains resident after initialization:
- Interrupt handlers
- Packet processing routines
- DMA operations
- Must be optimized for zero branching
- Subject to self-modification for CPU optimization

### Cold Path Modules (Discardable)

Initialization-only code that is discarded after setup:
- Hardware detection
- Configuration parsing
- EEPROM reading
- Resource allocation
- Freed immediately after use to maximize available memory

### Feature Modules (Optional)

User-selectable functionality:
- Routing capabilities
- Statistics collection
- Flow control
- Promiscuous mode support

## Module Header Format

### Standard Module Header

```c
typedef struct {
    // Identification (8 bytes)
    uint16_t magic;           // 0x4D44 ('MD')
    uint16_t version;         // BCD format (0x0100 = v1.00)
    uint16_t header_size;     // Size of this header
    uint16_t module_size;     // Total module size in paragraphs
    
    // Classification (8 bytes)
    uint16_t module_class;    // HOT_PATH, COLD_PATH, FEATURE
    uint16_t family_id;       // NIC family (PTASK, CORKSCREW, etc.)
    uint16_t feature_flags;   // Capability flags
    uint16_t reserved1;
    
    // Entry Points (6 bytes)
    uint16_t init_offset;     // Initialization function
    uint16_t vtable_offset;   // Function table offset
    uint16_t cleanup_offset;  // Cleanup function
    
    // CPU Optimization (6 bytes)
    uint16_t patch_count;     // Number of patch points
    uint16_t patch_table;     // Offset to patch table
    uint16_t patch_size;      // Total size of patch data
    
    // Hot/Cold Sections (8 bytes)
    uint16_t hot_offset;      // Start of hot code
    uint16_t hot_size;        // Size of hot code
    uint16_t cold_offset;     // Start of cold code
    uint16_t cold_size;       // Size of cold code
    
    // Dependencies (4 bytes)
    uint16_t deps_count;      // Number of dependencies
    uint16_t deps_offset;     // Offset to dependency list
    
    // Metadata (44 bytes)
    char     name[8];         // Module name (8.3 format)
    char     extension[4];    // Always ".MOD"
    char     description[32]; // Human-readable description
    
    // Integrity (2 bytes)
    uint16_t checksum;        // Module checksum
} module_header_t;
```

### Patch Table Format

```c
typedef struct {
    uint16_t offset;          // Offset from module base
    uint8_t  type;            // Patch type
    uint8_t  area_size;       // Total patch area size
    
    // CPU-specific code
    struct {
        uint8_t size;         // Instruction size
        uint8_t code[7];      // Instruction bytes
    } cpu_286, cpu_386, cpu_486, cpu_pentium;
} patch_entry_t;

// Patch types
enum {
    PATCH_TYPE_COPY = 1,      // Memory copy routine
    PATCH_TYPE_ZERO = 2,      // Memory clear routine
    PATCH_TYPE_CSUM = 3,      // Checksum calculation
    PATCH_TYPE_IO   = 4,      // I/O operations
    PATCH_TYPE_JUMP = 5       // Conditional jumps
};
```

## CPU Optimization Implementation

### Defining Patch Points

```asm
; In module assembly code
SECTION .text

; Define a patchable copy routine
packet_copy_routine:
patch_copy_start:
    ; Default 8086 code (must work on all CPUs)
    push    si
    push    di
    push    cx
    cld
    rep     movsb           ; 2 bytes
    nop                     ; Padding for larger instructions
    nop
    nop
    pop     cx
    pop     di
    pop     si
    ret
patch_copy_end:

; Patch table entry
SECTION .data
patch_table:
    dw      patch_copy_start - module_base  ; Offset
    db      PATCH_TYPE_COPY                 ; Type
    db      5                               ; Area size
    
    ; 286 optimization
    db      2                               ; Size
    db      0F3h, 0A5h                     ; REP MOVSW
    db      0, 0, 0, 0, 0                  ; Padding
    
    ; 386 optimization
    db      3                               ; Size
    db      066h, 0F3h, 0A5h               ; 66h prefix + REP MOVSD
    db      0, 0, 0, 0                     ; Padding
    
    ; 486 optimization (same as 386 but aligned)
    db      3                               ; Size
    db      066h, 0F3h, 0A5h               ; 66h prefix + REP MOVSD
    db      0, 0, 0, 0                     ; Padding
    
    ; Pentium optimization (paired instructions)
    db      5                               ; Size
    db      066h, 0F3h, 0A5h, 090h, 090h   ; MOVSD + NOPs for pairing
    db      0, 0                           ; Padding
```

### Loader Patching Process

```c
// In loader (3COMPD.COM)
void apply_cpu_patches(module_header_t *module) {
    uint8_t *module_base = (uint8_t*)module;
    patch_entry_t *patches = (patch_entry_t*)(module_base + module->patch_table);
    
    for (int i = 0; i < module->patch_count; i++) {
        uint8_t *patch_location = module_base + patches[i].offset;
        uint8_t *new_code;
        uint8_t code_size;
        
        // Select CPU-specific code
        switch (g_cpu_info.type) {
            case CPU_TYPE_PENTIUM:
                new_code = patches[i].cpu_pentium.code;
                code_size = patches[i].cpu_pentium.size;
                break;
            case CPU_TYPE_80486:
                new_code = patches[i].cpu_486.code;
                code_size = patches[i].cpu_486.size;
                break;
            case CPU_TYPE_80386:
                new_code = patches[i].cpu_386.code;
                code_size = patches[i].cpu_386.size;
                break;
            case CPU_TYPE_80286:
                new_code = patches[i].cpu_286.code;
                code_size = patches[i].cpu_286.size;
                break;
            default:
                continue; // Keep original 8086 code
        }
        
        // Apply patch with interrupts disabled
        _disable();
        memcpy(patch_location, new_code, code_size);
        
        // Fill remaining area with NOPs
        memset(patch_location + code_size, 0x90, 
               patches[i].area_size - code_size);
        _enable();
    }
    
    // Flush caches on 486+
    if (g_cpu_info.type >= CPU_TYPE_80486) {
        flush_instruction_cache();
    }
}
```

## Hot/Cold Code Separation

### Module Layout

```asm
; Module structure
SECTION .header
    ; Module header (64 bytes)
    module_header:
        dw  0x4D44              ; Magic
        dw  0x0100              ; Version
        ; ... rest of header

SECTION .hot    ; Resident code
hot_section_start:
    ; Interrupt handler
    packet_interrupt_handler:
        ; Performance-critical code
        ; No calls to cold section!
        
    ; Fast packet copy
    fast_packet_copy:
        ; Optimized copy routine
        
hot_section_end:

SECTION .cold   ; Discardable code
cold_section_start:
    ; Initialization
    module_init:
        ; Hardware detection
        ; EEPROM reading
        ; Configuration
        
    ; One-time setup
    configure_hardware:
        ; Register programming
        ; Resource allocation
        
cold_section_end:

SECTION .data   ; Resident data
    ; Vtable for hot functions
    module_vtable:
        dw  packet_interrupt_handler
        dw  fast_packet_copy
        ; ... other hot functions
```

### Discarding Cold Code

```c
// After module initialization
void discard_cold_section(module_header_t *module) {
    uint8_t *module_base = (uint8_t*)module;
    uint8_t *cold_start = module_base + module->cold_offset;
    size_t cold_size = module->cold_size;
    
    // Mark memory as free
    if (cold_size > 0) {
        // DOS memory management
        _dos_freemem(FP_SEG(cold_start));
        
        // Update module size
        module->module_size -= (cold_size / 16); // Convert to paragraphs
        
        // Clear cold section pointers
        module->cold_offset = 0;
        module->cold_size = 0;
    }
}
```

## Critical Path Inlining

### Handler Matrix Generation

```c
// Generate specialized handlers at compile time
#define GENERATE_RX_HANDLER(nic, cpu, mode) \
void __fastcall rx_handler_##nic##_##cpu##_##mode(void) { \
    __asm { \
        push    ax \
        push    dx \
        push    di \
        push    es \
    } \
    \
    /* NIC-specific packet read */ \
    if (nic == NIC_3C509) { \
        __asm { \
            mov     dx, 0x300 \
            add     dx, RX_STATUS \
            in      ax, dx \
        } \
    } else if (nic == NIC_3C515) { \
        __asm { \
            mov     dx, 0x300 \
            add     dx, DMA_STATUS \
            in      ax, dx \
        } \
    } \
    \
    /* CPU-specific copy */ \
    if (cpu >= CPU_386) { \
        __asm { \
            db      0x66 \
            rep     movsd \
        } \
    } else if (cpu >= CPU_286) { \
        __asm { \
            rep     movsw \
        } \
    } else { \
        __asm { \
            rep     movsb \
        } \
    } \
    \
    /* Mode-specific filtering */ \
    if (mode != MODE_PROMISCUOUS) { \
        __asm { \
            call    check_packet_filter \
            jnz     drop_packet \
        } \
    } \
    \
    __asm { \
        call    deliver_packet \
        pop     es \
        pop     di \
        pop     dx \
        pop     ax \
        iret \
    } \
}

// Generate all combinations
GENERATE_RX_HANDLER(NIC_3C509, CPU_286, MODE_NORMAL)
GENERATE_RX_HANDLER(NIC_3C509, CPU_286, MODE_PROMISCUOUS)
GENERATE_RX_HANDLER(NIC_3C509, CPU_386, MODE_NORMAL)
GENERATE_RX_HANDLER(NIC_3C509, CPU_386, MODE_PROMISCUOUS)
// ... etc
```

## Module Development Workflow

### 1. Module Planning

```makefile
# Module specification
MODULE_NAME = PTASK
MODULE_DESC = 3C509 Parallel Tasking Driver
HOT_SIZE = 4096      # 4KB resident
COLD_SIZE = 8192     # 8KB init (discarded)
PATCH_COUNT = 12     # CPU optimization points
```

### 2. Implementation Structure

```
PTASK/
├── Makefile
├── ptask.asm        # Main module code
├── ptask_hot.asm    # Hot path functions
├── ptask_cold.asm   # Cold path functions
├── ptask_patches.inc # CPU patch definitions
└── ptask_test.c     # Module tests
```

### 3. Build Process

```makefile
# Build hot and cold sections separately
ptask_hot.obj: ptask_hot.asm
    nasm -f obj -DHOT_SECTION $< -o $@

ptask_cold.obj: ptask_cold.asm
    nasm -f obj -DCOLD_SECTION $< -o $@

# Link with section ordering
PTASK.MOD: ptask_hot.obj ptask_cold.obj
    wlink @ptask.lnk
    $(PATCH_GEN) $@ ptask_patches.inc
    $(CHECKSUM) $@
```

### 4. Testing Requirements

```c
// Module test suite
typedef struct {
    bool (*test_load)(void);
    bool (*test_init)(void);
    bool (*test_hot_path)(void);
    bool (*test_cpu_patches)(void);
    bool (*test_cold_discard)(void);
    bool (*test_unload)(void);
} module_test_t;

bool test_module(const char *module_name) {
    // Load module
    module_header_t *mod = load_module(module_name);
    assert(mod != NULL);
    
    // Verify header
    assert(mod->magic == 0x4D44);
    assert(verify_checksum(mod));
    
    // Test CPU patches
    apply_cpu_patches(mod);
    assert(verify_patches(mod));
    
    // Initialize module
    assert(init_module(mod) == 0);
    
    // Test hot path performance
    uint32_t cycles = benchmark_hot_path(mod);
    assert(cycles < MAX_CYCLES_PER_PACKET);
    
    // Discard cold section
    size_t before = get_free_memory();
    discard_cold_section(mod);
    size_t after = get_free_memory();
    assert(after > before);
    
    // Cleanup
    unload_module(mod);
    return true;
}
```

## Performance Guidelines

### Hot Path Rules

1. **No Branches**: Use inlining and handler matrices
2. **No Far Calls**: Keep everything in one segment
3. **Register Preservation**: Minimize stack operations
4. **Cache Alignment**: Align critical data on cache lines
5. **Instruction Pairing**: Optimize for dual pipelines (Pentium)

### Memory Optimization

1. **Discard Aggressively**: Free cold code immediately
2. **Share Common Code**: Use shared modules for common functions
3. **Compact Data**: Pack structures to minimize size
4. **Reuse Buffers**: Implement buffer pooling
5. **Lazy Loading**: Load features only when needed

### CPU Optimization Priority

```c
// Optimization impact by CPU
typedef struct {
    cpu_type_t cpu;
    const char *optimization;
    int speedup_percent;
} optimization_impact_t;

optimization_impact_t impacts[] = {
    { CPU_286, "REP MOVSW vs MOVSB", 100 },
    { CPU_386, "REP MOVSD vs MOVSW", 100 },
    { CPU_386, "32-bit registers", 30 },
    { CPU_486, "Cache alignment", 25 },
    { CPU_486, "Instruction ordering", 15 },
    { CPU_PENTIUM, "Dual pipeline pairing", 40 },
    { CPU_PENTIUM, "Branch prediction", 20 }
};
```

## Module Certification

### Required Benchmarks

1. **Load Time**: < 50ms
2. **Init Time**: < 100ms
3. **Hot Path Latency**: < 200 cycles/packet
4. **Memory Usage**: Within specification
5. **CPU Patch Verification**: All patches apply correctly

### Compliance Checklist

- [ ] Module header complete and valid
- [ ] Hot/cold sections properly separated
- [ ] All patch points documented
- [ ] CPU optimizations tested on 286/386/486/Pentium
- [ ] Memory freed after cold section discard
- [ ] No branches in critical path
- [ ] Checksum verification passes
- [ ] Compatible with all DOS memory managers
- [ ] Tested with minimum 512KB RAM
- [ ] Documentation complete

## Example: PTASK.MOD Implementation

```asm
; PTASK.MOD - 3C509 Parallel Tasking Driver
; Hot section: 4KB, Cold section: 8KB (discarded)

BITS 16
ORG 0

; Module header
module_header:
    dw  0x4D44          ; Magic
    dw  0x0100          ; Version 1.0
    dw  64              ; Header size
    dw  768             ; Total size (12KB in paragraphs)
    dw  HOT_PATH        ; Module class
    dw  FAMILY_PTASK    ; 3C509 family
    dw  0               ; Feature flags
    dw  0               ; Reserved
    dw  init_module     ; Init offset
    dw  vtable          ; Vtable offset
    dw  cleanup_module  ; Cleanup offset
    dw  8               ; Patch count
    dw  patch_table     ; Patch table offset
    dw  256             ; Patch data size
    dw  hot_start       ; Hot section start
    dw  4096            ; Hot section size
    dw  cold_start      ; Cold section start
    dw  8192            ; Cold section size
    dw  0               ; No dependencies
    dw  0               ; Dependency offset
    db  'PTASK   '      ; Name
    db  '.MOD'          ; Extension
    db  '3C509 Parallel Tasking Driver      ' ; Description
    dw  0               ; Checksum (filled by build)

; Hot section - Performance critical
hot_start:
    ; Interrupt handler (branch-free)
    rx_interrupt:
        push    ax
        push    dx
        mov     dx, 0x30C   ; Status register
        in      ax, dx
        test    ax, 0x4000  ; RX complete?
        jz      .not_ours
        ; ... packet processing
        
    ; Vtable for external access
    vtable:
        dw  rx_interrupt
        dw  tx_packet
        dw  get_stats
        
hot_end:

; Cold section - Initialization only
cold_start:
    init_module:
        ; Detect hardware
        call    detect_3c509
        ; Read EEPROM
        call    read_eeprom
        ; Configure NIC
        call    setup_hardware
        ret
        
cold_end:

; Patch table
patch_table:
    ; Entry 1: Packet copy
    dw  copy_routine - module_header
    db  PATCH_TYPE_COPY
    ; ... patch data
```

## Conclusion

This guide provides the foundation for implementing high-performance modules in the 3Com packet driver architecture. By following these guidelines, modules will achieve optimal performance while maintaining minimal memory footprint, making the driver suitable for even the most memory-constrained DOS systems.