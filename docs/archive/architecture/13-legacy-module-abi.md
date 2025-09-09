# Legacy: Module ABI Specification

> **⚠️ LEGACY DOCUMENT**: This document describes the abandoned .MOD module ABI specification.
> It is preserved for historical reference only. The current implementation uses a unified
> .EXE driver with vtable-based HAL. See `06-vtable-hal.md` for the current HAL design.

**Original Version**: v0.9 (DRAFT)

**Version**: 0.9 (DRAFT)  
**Date**: 2025-08-22  
**Status**: DRAFT - For review and validation by all agents

## Executive Summary

This document (archived) defines the 64-byte Module Application Binary Interface (ABI) for a prior modular architecture using .MOD files. It is superseded by the unified driver architecture and vtable-based HAL; retained for historical reference.

**Key Features:**
- Exactly 64-byte module header for all module types
- Hot/cold section separation for memory efficiency
- O(log N) symbol resolution with binary search
- Atomic relocation processing with interrupt safety
- Support for all CPU types from 80286 through Pentium

## Module Header Format

### 64-Byte Header Layout

The module header is exactly 64 bytes and uses little-endian byte ordering:

```c
typedef struct {
    /* 0x00: Module Identification (8 bytes) */
    char     signature[4];        /* "MD64" - Module Driver 64-byte header */
    uint8_t  abi_version;         /* ABI version (1 = v1.0) */
    uint8_t  module_type;         /* Module type (NIC/SERVICE/FEATURE/DIAG) */
    uint16_t flags;               /* Module capability flags */
    
    /* 0x08: Memory Layout (8 bytes) */
    uint16_t total_size_para;     /* Total module size in paragraphs */
    uint16_t resident_size_para;  /* Resident size after cold discard */
    uint16_t cold_size_para;      /* Cold section size to discard */
    uint16_t alignment_para;      /* Required paragraph alignment */
    
    /* 0x10: Entry Points (8 bytes) */
    uint16_t init_offset;         /* Module initialization entry point */
    uint16_t api_offset;          /* Hot API entry point offset */
    uint16_t isr_offset;          /* ISR entry point (0 if no ISR) */
    uint16_t unload_offset;       /* Module cleanup entry point */
    
    /* 0x18: Symbol Resolution (8 bytes) */
    uint16_t export_table_offset; /* Offset to export directory */
    uint16_t export_count;        /* Number of exported symbols */
    uint16_t reloc_table_offset;  /* Offset to relocation table */
    uint16_t reloc_count;         /* Number of relocation entries */
    
    /* 0x20: BSS and Requirements (8 bytes) */
    uint16_t bss_size_para;       /* Uninitialized data size */
    uint16_t required_cpu;        /* Required CPU (80286-Pentium) */
    uint16_t required_features;   /* Required features (FPU/MMX/CPUID) */
    uint16_t module_id;           /* Unique module ID */
    
    /* 0x28: Module Name (12 bytes) */
    char     module_name[11];     /* 8.3 format uppercase, null-padded */
    uint8_t  name_padding;        /* Align to even boundary */
    
    /* 0x34: Integrity and Reserved (16 bytes) */
    uint16_t header_checksum;     /* Header checksum (excluding this field) */
    uint16_t image_checksum;      /* Image checksum */
    uint32_t vendor_id;           /* Vendor identifier */
    uint32_t build_timestamp;     /* Build timestamp */
    uint32_t reserved[2];         /* Reserved for future use - must be 0 */
} __attribute__((packed)) module_header_t;
```

### Field Descriptions

#### Module Identification (0x00-0x07)
- **signature**: Fixed "MD64" signature for validation
- **abi_version**: Must be 1 for this specification  
- **module_type**: MODULE_TYPE_NIC (0x01), SERVICE (0x02), FEATURE (0x03), DIAGNOSTIC (0x04)
- **flags**: Capability flags (cold discard, ISR, DMA requirements, etc.)

#### Memory Layout (0x08-0x0F)
- **total_size_para**: Complete module size in 16-byte paragraphs
- **resident_size_para**: Memory that remains after cold section discard
- **cold_size_para**: Memory that can be freed after initialization
- **alignment_para**: Required memory alignment (1 = 16-byte boundary)

#### Entry Points (0x10-0x17)
- **init_offset**: One-time initialization function (mandatory)
- **api_offset**: Main API entry point for module calls (mandatory)
- **isr_offset**: Interrupt service routine (0 if no ISR)
- **unload_offset**: Cleanup function called before module removal

#### Symbol Resolution (0x18-0x1F)
- **export_table_offset**: Location of export directory in module
- **export_count**: Number of symbols exported by this module
- **reloc_table_offset**: Location of relocation entries
- **reloc_count**: Number of relocations to process

#### BSS and Requirements (0x20-0x27)
- **bss_size_para**: Uninitialized data section size in paragraphs
- **required_cpu**: Minimum CPU (0x0286-0x0586)
- **required_features**: Feature requirements (FPU, MMX, CPUID)
- **module_id**: Unique 16-bit module identifier

#### Module Name (0x28-0x33)
- **module_name**: DOS 8.3 filename format, uppercase, null-padded
- **name_padding**: Reserved byte for alignment

#### Integrity and Reserved (0x34-0x3F)
- **header_checksum**: Two's complement checksum of header (excluding this field)
- **image_checksum**: Two's complement checksum of entire module
- **vendor_id**: 32-bit vendor identification code
- **build_timestamp**: Unix timestamp of module compilation
- **reserved**: Must be zero, reserved for future ABI extensions

## Module File Format

### File Structure
```
┌─────────────────────────────────┐
│ Module Header (64 bytes)        │  <- module_header_t
├─────────────────────────────────┤
│ Hot Code Section                │  <- Remains resident
├─────────────────────────────────┤
│ Hot Data Section                │  <- Remains resident  
├─────────────────────────────────┤
│ Cold Code Section               │  <- Discarded after init
├─────────────────────────────────┤
│ Cold Data Section               │  <- Discarded after init
├─────────────────────────────────┤
│ Export Table                    │  <- Sorted symbol directory
├─────────────────────────────────┤
│ Relocation Table                │  <- Relocation entries
└─────────────────────────────────┘
```

### Memory Layout After Loading
After loading and initialization, the memory layout becomes:
```
┌─────────────────────────────────┐  <- Module Base (segment boundary)
│ Module Header (64 bytes)        │
├─────────────────────────────────┤
│ Hot Code Section                │  <- Remains resident
├─────────────────────────────────┤
│ Hot Data Section                │  <- Remains resident
├─────────────────────────────────┤
│ BSS Section (zeroed)            │  <- Uninitialized data
└─────────────────────────────────┘
```

## Symbol Resolution

### Export Directory Format

Each export entry is exactly 12 bytes:
```c
typedef struct {
    char     symbol_name[8];      /* Symbol name, null-padded */
    uint16_t symbol_offset;       /* Offset from module base */
    uint16_t symbol_flags;        /* Symbol attributes */
} __attribute__((packed)) export_entry_t;
```

### Symbol Resolution Algorithm

The export directory is sorted alphabetically by symbol name to enable O(log N) binary search:

```c
void far *resolve_symbol(const char *symbol_name) {
    // Binary search in global sorted symbol table
    int left = 0, right = g_symbol_count - 1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        int cmp = strcmp(symbol_name, g_symbol_table[mid].symbol_name);
        
        if (cmp == 0) {
            return g_symbol_table[mid].symbol_address;
        } else if (cmp < 0) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    
    return NULL; // Symbol not found
}
```

## Relocation Processing

### Relocation Entry Format

Each relocation entry is exactly 4 bytes:
```c
typedef struct {
    uint8_t  reloc_type;          /* Relocation type */
    uint8_t  reserved;            /* Reserved, must be 0 */
    uint16_t reloc_offset;        /* Offset from module base to patch */
} __attribute__((packed)) reloc_entry_t;
```

### Supported Relocation Types

1. **RELOC_TYPE_SEG_OFS (0x01)**: Segment:offset far pointer
2. **RELOC_TYPE_SEGMENT (0x02)**: Segment word only
3. **RELOC_TYPE_OFFSET (0x03)**: Offset word only
4. **RELOC_TYPE_REL_NEAR (0x04)**: Near relative jump/call
5. **RELOC_TYPE_REL_FAR (0x05)**: Far relative call

### Relocation Processing Algorithm

```c
int apply_single_relocation(module_instance_t *instance, const reloc_entry_t *reloc) {
    uint8_t *patch_location = (uint8_t*)instance->module_base + reloc->reloc_offset;
    uint16_t base_segment = instance->module_segment;
    
    switch (reloc->reloc_type) {
        case RELOC_TYPE_SEG_OFS: {
            // Patch segment:offset far pointer
            uint16_t *seg_ptr = (uint16_t*)(patch_location + 2);
            *seg_ptr = base_segment;
            break;
        }
        
        case RELOC_TYPE_SEGMENT: {
            // Patch segment word only
            uint16_t *seg_ptr = (uint16_t*)patch_location;
            *seg_ptr = base_segment;
            break;
        }
        
        case RELOC_TYPE_OFFSET: {
            // Offset relocations remain unchanged (module-relative)
            break;
        }
        
        default:
            return 0; // Unknown relocation type
    }
    
    return 1;
}
```

## Module Loading Lifecycle

### 1. Module Discovery
- Search for *.MOD files in current directory, executable directory, and PATH
- Validate file accessibility and basic header structure

### 2. Header Validation
- Verify "MD64" signature and ABI version
- Check module type and compatibility flags
- Validate file size matches header specifications
- Verify header and image checksums

### 3. Memory Allocation
- Allocate aligned memory based on total_size_para and alignment_para
- Ensure allocation respects paragraph boundaries for real-mode addressing

### 4. Image Loading
- Load entire module file to allocated memory
- Zero BSS section based on bss_size_para

### 5. Relocation Processing
- Apply all relocations in reloc_table
- Update segment references for loaded memory location

### 6. Symbol Registration
- Build global symbol table from export directory
- Sort symbols for O(log N) lookup performance
- Register module symbols in global namespace

### 7. Module Initialization
- Call module init_offset entry point
- Module performs hardware detection, resource allocation
- Module returns success/error code

### 8. Cold Section Discard
- Free memory regions marked as cold after successful initialization
- Update memory tracking to resident_size_para

## Entry Point Conventions

### Initialization Entry Point
```asm
module_init:
    push    bp
    mov     bp, sp
    
    ; Perform module-specific initialization
    ; - Hardware detection
    ; - Resource allocation  
    ; - Configuration parsing
    
    ; Return success (CF clear, AX=0) or error (CF set, AX=error_code)
    clc
    xor     ax, ax
    pop     bp
    retf                    ; Far return to loader
```

### API Entry Point
```asm
module_api:
    push    bp
    mov     bp, sp
    
    ; AX = function number
    ; ES:DI = parameter structure (if applicable)
    ; Implement module-specific API
    ; All registers preserved except AX (return value)
    
    clc                     ; Success
    xor     ax, ax
    pop     bp
    retf
```

### ISR Entry Point (if applicable)
```asm
module_isr:
    ; Save all registers (see calling-conventions.md)
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    bp
    push    ds
    push    es
    
    ; Set up DS for module data access
    mov     ax, cs
    mov     ds, ax
    
    ; Handle interrupt (must complete within 60μs for receive path)
    ; ...
    
    ; Send EOI to PIC before restoring registers
    mov     al, 20h
    out     20h, al
    
    ; Restore all registers in reverse order
    pop     es
    pop     ds
    pop     bp
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    iret
```

### Cleanup Entry Point
```asm
module_cleanup:
    push    bp
    mov     bp, sp
    
    ; Free allocated resources
    ; Disable interrupts if ISR was registered
    ; Restore any modified system state
    
    clc                     ; Success
    xor     ax, ax
    pop     bp
    retf
```

## Error Handling

### Module Error Codes
- **0x0000**: SUCCESS
- **0x0020**: FILE_NOT_FOUND
- **0x0021**: INVALID_MODULE  
- **0x0022**: INCOMPATIBLE
- **0x0023**: LOAD_FAILED
- **0x0024**: INIT_FAILED
- **0x0025**: ALREADY_LOADED
- **0x0026**: DEPENDENCY
- **0x0027**: ABI_MISMATCH
- **0x0028**: CHECKSUM
- **0x0029**: RELOCATION
- **0x002A**: SYMBOL
- **0x002B**: OUT_OF_MEMORY

### Error Return Convention
All module functions use the DOS convention:
- **Success**: Carry flag clear (CF=0), AX=0
- **Error**: Carry flag set (CF=1), AX=error_code

## Performance Requirements

### Loading Performance Targets
- **Module discovery**: <50ms per directory
- **File loading**: <100ms for 8KB module
- **Relocation processing**: <10ms for 100 relocations
- **Symbol registration**: <5ms for 20 exports
- **Cold section discard**: <1ms

### Memory Efficiency
- **Loader overhead**: <2KB resident
- **Symbol table**: <256 bytes per 20 symbols
- **Instance tracking**: <32 bytes per loaded module

### Symbol Resolution Performance
- **Binary search**: O(log N) worst case
- **Target**: <10 cycles for ordinal lookup
- **Maximum symbols**: 1024 per system

## Calling Conventions

All inter-module calls follow the standardized calling conventions defined in `calling-conventions.md`:

### Register Preservation
- **Caller-saved**: AX, BX, CX, DX, Flags (except CF)
- **Callee-saved**: DS, ES, BP, SI, DI, SS:SP

### Parameter Passing
- **Small parameters**: Registers (AX, BX, CX, DX)
- **Large parameters**: DS:SI (source) or ES:DI (destination)
- **Far pointers**: ES:DI (segment:offset)

### ISR Requirements
- **Register save**: Save ALL registers and segments
- **Interrupt disable**: CLI duration ≤8μs
- **EOI timing**: Send before register restoration
- **ISR-safe functions**: Check SYMBOL_FLAG_ISR_SAFE

## Validation and Testing

### Header Validation
```c
int validate_module_header(const module_header_t *hdr) {
    // Check signature, ABI version, field consistency
    // Validate entry points within module bounds
    // Verify alignment and size requirements
}
```

### Checksum Validation
```c
uint16_t calculate_header_checksum(const module_header_t *hdr) {
    // Two's complement checksum of all header bytes
    // Excludes the checksum field itself
}
```

### Runtime Validation
- Module loading sequence validation
- Symbol resolution correctness
- Memory alignment verification
- Entry point accessibility checking

## Implementation Notes

### DOS Real-Mode Constraints
- All pointers must use segment:offset format
- Memory allocation must respect paragraph (16-byte) boundaries
- No assumptions about flat memory addressing
- Interrupt handlers must be position-independent

### Timing Constraints
- Use PIT-based timing measurement from `timing-measurement.h`
- CLI sections must complete within 8 microseconds
- ISR execution must complete within 60 microseconds for receive path
- Module initialization must complete within 100 milliseconds

### Memory Management
- Hot sections remain resident for performance
- Cold sections are discarded after initialization
- BSS sections are zeroed during loading
- DMA buffers must be 64KB boundary safe

## Future Considerations

### ABI Version Management
- Reserved fields allow for future extensions
- ABI version checking prevents incompatibility
- Vendor ID field enables vendor-specific extensions

### Performance Enhancements
- CPU-specific optimization through self-modifying code
- Branch prediction hints for critical paths
- Cache-aware data structure alignment

---

**APPROVAL REQUIRED**: This draft requires validation by Build System Engineer, Test Infrastructure, Performance Engineer, and all NIC teams before finalizing as ABI v1.0.

**FREEZE DEADLINE**: Day 5 (2025-08-26) - No changes allowed after ABI v1.0 publication.
