# Loader Contract v1.0 - 3Com Packet Driver Modular Architecture

**Version**: 1.0  
**Date**: 2025-08-22  
**Status**: MANDATORY - All modules must comply with this contract

## Overview

This document defines the exact contract between the core loader (3COMPD.COM) and loadable modules (*.MOD files). This contract specifies module loading sequence, relocation processing, symbol resolution, and cold section discard protocol.

## Module File Format

### File Structure
```
┌─────────────────────────────────┐
│ Module Header (64 bytes)        │  <- module_header_t
├─────────────────────────────────┤
│ Code Section                    │  <- Machine code
│ ├─ Hot Code (resident)          │
│ └─ Cold Code (discardable)      │
├─────────────────────────────────┤
│ Data Section                    │  <- Initialized data
│ ├─ Hot Data (resident)          │
│ └─ Cold Data (discardable)      │
├─────────────────────────────────┤
│ Export Table                    │  <- export_entry_t array
├─────────────────────────────────┤
│ Relocation Table                │  <- reloc_entry_t array
└─────────────────────────────────┘
```

### Memory Layout After Loading
```
┌─────────────────────────────────┐  <- Module Base (segment boundary)
│ Module Header (64 bytes)        │
├─────────────────────────────────┤
│ Hot Code Section                │  <- Remains resident
│ (resident after init)           │
├─────────────────────────────────┤
│ Hot Data Section                │  <- Remains resident
│ (resident after init)           │
├─────────────────────────────────┤
│ Cold Code Section               │  <- Discarded after init
│ (discarded after init)          │
├─────────────────────────────────┤
│ Cold Data Section               │  <- Discarded after init  
│ (discarded after init)          │
├─────────────────────────────────┤
│ BSS Section                     │  <- Zeroed, resident
│ (uninitialized data)            │
├─────────────────────────────────┤
│ Export Table                    │  <- Used by loader, may be discarded
├─────────────────────────────────┤
│ Relocation Table                │  <- Used by loader, then discarded
└─────────────────────────────────┘
```

## Loader Responsibilities

### 1. Module Discovery and Validation

#### File Discovery
```c
// Loader searches for modules in these locations:
// 1. Current directory
// 2. Directory containing 3COMPD.COM
// 3. PATH environment variable directories
// 4. Explicit paths from command line

int discover_modules(const char *module_path) {
    // Search for *.MOD files
    // Validate file size and accessibility
    // Return count of discovered modules
}
```

#### Header Validation
```c
int validate_module_file(const char *filename) {
    module_header_t header;
    
    // Read and validate header
    if (!read_header(filename, &header)) return 0;
    if (!validate_module_header(&header)) return 0;
    
    // Verify file size matches header
    if (get_file_size(filename) != header.total_size_para * 16) return 0;
    
    // Validate checksums
    if (!verify_header_checksum(&header)) return 0;
    if (!verify_image_checksum(filename, &header)) return 0;
    
    return 1;
}
```

### 2. Memory Allocation

#### Module Memory Allocation
```c
typedef struct {
    uint16_t module_segment;    // Base segment of loaded module
    uint16_t total_size_para;   // Total allocated size in paragraphs
    uint16_t resident_size_para; // Size after cold discard
    void far *module_base;      // Far pointer to module base
} module_instance_t;

int allocate_module_memory(const module_header_t *header, module_instance_t *instance) {
    // Calculate required memory
    uint16_t total_para = header->total_size_para;
    uint16_t align_para = header->alignment_para;
    
    // Allocate aligned memory block
    uint16_t segment = allocate_aligned_paragraphs(total_para, align_para);
    if (!segment) return ERROR_OUT_OF_MEMORY;
    
    // Initialize instance structure
    instance->module_segment = segment;
    instance->total_size_para = total_para;
    instance->resident_size_para = header->resident_size_para;
    instance->module_base = MK_FP(segment, 0);
    
    return SUCCESS;
}
```

### 3. Module Loading

#### File Loading Process
```c
int load_module_image(const char *filename, module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    // Load entire module file to allocated memory
    if (!read_file_to_memory(filename, instance->module_base)) {
        return ERROR_MODULE_LOAD_FAILED;
    }
    
    // Zero BSS section
    if (header->bss_size_para > 0) {
        void far *bss_start = MK_FP(instance->module_segment, 
                                   header->total_size_para * 16 - header->bss_size_para * 16);
        _fmemset(bss_start, 0, header->bss_size_para * 16);
    }
    
    return SUCCESS;
}
```

### 4. Relocation Processing

#### Apply Relocations
```c
int apply_relocations(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    // Skip if no relocations
    if (header->reloc_count == 0) return SUCCESS;
    
    // Get relocation table
    reloc_entry_t *reloc_table = (reloc_entry_t*)
        ((uint8_t*)instance->module_base + header->reloc_table_offset);
    
    // Apply each relocation
    for (int i = 0; i < header->reloc_count; i++) {
        if (!apply_single_relocation(instance, &reloc_table[i])) {
            return ERROR_MODULE_RELOCATION;
        }
    }
    
    return SUCCESS;
}

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

### 5. Symbol Resolution

#### Export Table Processing
```c
typedef struct {
    char symbol_name[9];        // Null-terminated symbol name
    void far *symbol_address;   // Far pointer to symbol
    uint16_t symbol_flags;      // Symbol attributes
} resolved_symbol_t;

int build_symbol_table(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    if (header->export_count == 0) return SUCCESS;
    
    export_entry_t *exports = (export_entry_t*)
        ((uint8_t*)instance->module_base + header->export_table_offset);
    
    // Register each exported symbol
    for (int i = 0; i < header->export_count; i++) {
        resolved_symbol_t symbol;
        
        // Copy symbol name (ensure null termination)
        _fmemcpy(symbol.symbol_name, exports[i].symbol_name, 8);
        symbol.symbol_name[8] = '\0';
        
        // Calculate symbol address
        symbol.symbol_address = MK_FP(instance->module_segment, exports[i].symbol_offset);
        symbol.symbol_flags = exports[i].symbol_flags;
        
        // Add to global symbol table
        if (!register_symbol(&symbol)) {
            return ERROR_MODULE_SYMBOL;
        }
    }
    
    return SUCCESS;
}
```

#### Symbol Lookup API
```c
// O(log N) binary search in sorted symbol table
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

### 6. Module Initialization

#### Module Init Sequence
```c
typedef int (far *module_init_func_t)(void);

int initialize_module(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    // Get init function pointer
    module_init_func_t init_func = (module_init_func_t)
        MK_FP(instance->module_segment, header->init_offset);
    
    // Call module initialization
    int result = init_func();
    
    if (result != SUCCESS) {
        return ERROR_MODULE_INIT_FAILED;
    }
    
    return SUCCESS;
}
```

### 7. Cold Section Discard

#### Discard Process
```c
int discard_cold_section(module_instance_t *instance) {
    module_header_t *header = (module_header_t*)instance->module_base;
    
    // Skip if no cold section
    if (header->cold_size_para == 0) return SUCCESS;
    
    // Calculate cold section boundaries
    uint16_t resident_size_bytes = header->resident_size_para * 16;
    uint16_t cold_size_bytes = header->cold_size_para * 16;
    
    // Free cold section memory
    uint16_t cold_segment = instance->module_segment + 
                           (resident_size_bytes / 16);
    
    if (!free_memory_paragraphs(cold_segment, header->cold_size_para)) {
        return ERROR_MEMORY_FREE_FAILED;
    }
    
    // Update instance tracking
    instance->total_size_para = header->resident_size_para;
    
    // Mark cold section as discarded in header
    header->cold_size_para = 0;
    
    return SUCCESS;
}
```

## Module Responsibilities

### Module Entry Points

#### Initialization Entry Point
```asm
; Module must provide initialization function
; Called once during module loading
; DS points to module data segment
; Must return SUCCESS (CF clear, AX=0) or error code (CF set, AX=error)

module_init:
    push    bp
    mov     bp, sp
    
    ; Perform module-specific initialization
    ; - Hardware detection
    ; - Resource allocation
    ; - Configuration parsing
    
    ; Return success
    clc
    xor     ax, ax
    pop     bp
    retf                    ; Far return to loader
```

#### API Entry Point  
```asm
; Module's main API function
; Called by other modules or core loader
; Parameters passed according to calling conventions

module_api:
    push    bp
    mov     bp, sp
    
    ; Implement module-specific API
    ; All registers preserved except AX (return value)
    
    clc                     ; Success
    xor     ax, ax
    pop     bp
    retf
```

#### ISR Entry Point (if applicable)
```asm
; Interrupt service routine
; Must follow ISR conventions exactly
; Send EOI before return

module_isr:
    ; Save all registers (see calling-conventions.md)
    push    ax
    push    bx
    ; ... save all registers
    
    ; Set up DS for module data access
    push    ds
    mov     ax, cs
    mov     ds, ax
    
    ; Handle interrupt
    ; Must complete within 60μs for receive path
    
    ; Send EOI to PIC
    mov     al, 20h
    out     20h, al
    
    ; Restore all registers
    pop     ds
    ; ... restore all registers
    pop     ax
    iret
```

#### Cleanup Entry Point
```asm
; Called before module unload
; Must free all allocated resources

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

### Module Data Organization

#### Hot Data Section (Resident)
```c
// Data that must remain resident
typedef struct {
    uint16_t nic_base_port;     // Hardware I/O base
    uint8_t  mac_address[6];    // Network address
    uint16_t interrupt_count;   // Statistics counter
    // ... other resident data
} hot_data_t;
```

#### Cold Data Section (Discardable)  
```c
// Data only needed during initialization
typedef struct {
    char     config_string[128]; // Configuration parameters
    uint16_t detection_ports[16]; // Hardware detection data
    uint8_t  eeprom_buffer[256];  // EEPROM contents
    // ... other init-only data
} cold_data_t;
```

## Error Handling

### Loader Error Codes
```c
#define LOADER_SUCCESS                  0x0000
#define LOADER_ERROR_FILE_NOT_FOUND    0x0020
#define LOADER_ERROR_INVALID_MODULE    0x0021
#define LOADER_ERROR_INCOMPATIBLE      0x0022
#define LOADER_ERROR_LOAD_FAILED       0x0023
#define LOADER_ERROR_INIT_FAILED       0x0024
#define LOADER_ERROR_ALREADY_LOADED    0x0025
#define LOADER_ERROR_DEPENDENCY        0x0026
#define LOADER_ERROR_ABI_MISMATCH      0x0027
#define LOADER_ERROR_CHECKSUM          0x0028
#define LOADER_ERROR_RELOCATION        0x0029
#define LOADER_ERROR_SYMBOL            0x002A
```

### Error Recovery
```c
int load_module_with_recovery(const char *filename) {
    module_instance_t instance;
    
    // Attempt loading
    int result = load_module(filename, &instance);
    
    if (result != SUCCESS) {
        // Clean up partially loaded module
        cleanup_failed_load(&instance);
        
        // Log error with specific details
        log_loader_error(filename, result);
    }
    
    return result;
}
```

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

## Validation and Testing

### Loader Validation
```c
// Test harness validates:
// 1. Correct module loading sequence
// 2. Proper relocation application  
// 3. Symbol resolution accuracy
// 4. Cold section discard effectiveness
// 5. Error handling and cleanup

int test_loader_contract(void) {
    // Load test module
    // Verify all contract requirements
    // Test error conditions
    // Validate cleanup
}
```

### Module Validation
```c  
// Module validation checklist:
// 1. Header format compliance
// 2. Entry point implementations
// 3. Error code usage
// 4. Memory usage within limits
// 5. Timing requirements met
```

---

**CONTRACT COMPLIANCE**: All modules must implement this contract exactly. The loader validates compliance and rejects non-conforming modules. Any deviations require contract version update and compatibility testing.