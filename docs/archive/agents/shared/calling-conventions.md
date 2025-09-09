# Calling Conventions v1.0 - 3Com Packet Driver Modular Architecture

**Version**: 1.0  
**Date**: 2025-08-22  
**Status**: MANDATORY - All modules must follow these conventions exactly

## Overview

This document defines the exact calling conventions for all inter-module communication in the 3Com packet driver modular architecture. These conventions ensure proper register preservation, error handling, and real-mode compatibility across all 14-16 agent implementations.

## Register Usage Convention

### Caller-Saved Registers (Scratch Registers)
**Caller must save these if needed across calls:**
- **AX** - Primary return value, error code
- **BX** - Secondary parameter/return value  
- **CX** - Count parameter, loop counter
- **DX** - I/O port address, secondary parameter
- **Flags** - Condition codes (except CF for error indication)

### Callee-Saved Registers (Preserved by Called Function)
**Called function must preserve these:**
- **DS** - Data segment (restored before return)
- **ES** - Extra segment (restored before return) 
- **BP** - Base pointer (stack frame)
- **SI** - Source index
- **DI** - Destination index
- **SS:SP** - Stack pointer (obviously)

### Segment Register Usage
- **CS** - Code segment, managed by far call/return
- **DS** - Points to caller's data segment on entry, must be preserved
- **ES** - Used for far pointer returns, caller must set if needed
- **SS** - Stack segment, never modified by modules

## Function Call Types

### Near Calls (Within Module)
```asm
; Used for internal module functions
; Standard 16-bit near call
call    internal_function
```

**Convention:**
- Uses module's own code segment
- 16-bit return address on stack
- Register preservation as above
- No segment register changes

### Far Calls (Between Modules)
```asm
; Used for inter-module communication
; Always use far call for module boundaries
call    far ptr external_function
```

**Convention:**
- Uses far call with segment:offset
- 32-bit return address (segment:offset) on stack  
- Full register preservation required
- DS must point to original caller's data on return

## Error Handling Convention

### Error Return Method
```asm
; Success case
clc                     ; Clear carry flag
mov     ax, 0           ; Success code
ret                     ; Or retf for far calls

; Error case  
stc                     ; Set carry flag
mov     ax, error_code  ; Specific error from error-codes.h
; Optionally set ES:DI to point to error detail structure
ret
```

### Error Checking by Caller
```asm
call    some_function
jc      handle_error    ; Jump if carry set (error)
; Success path continues here

handle_error:
; AX contains error code
; ES:DI may point to additional error information
```

### Error Code Values
Use standardized error codes from `error-codes.h`:
- **0x0000** - SUCCESS
- **0x0001** - ERROR_INVALID_PARAM
- **0x0002** - ERROR_OUT_OF_MEMORY
- (See complete list in error-codes.h)

## Parameter Passing

### Small Parameters (≤16 bits)
```asm
; Pass in registers
mov     ax, param1      ; First parameter
mov     bx, param2      ; Second parameter
mov     cx, param3      ; Third parameter
call    function
```

### Large Parameters or Structures
```asm
; Pass via DS:SI (source) or ES:DI (destination)
mov     si, offset parameter_struct
call    function
```

### Far Pointers
```asm
; Pass segment in ES, offset in DI
mov     es, buffer_segment
mov     di, buffer_offset
call    function
```

## ISR (Interrupt Service Routine) Conventions

### ISR Entry Requirements
```asm
my_isr:
    ; Save ALL registers and segments
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    bp
    push    ds
    push    es
    
    ; Set up our data segment
    mov     ax, cs
    mov     ds, ax          ; DS = CS for access to module data
    
    ; ISR code here
    ; ...
    
    ; Send EOI to PIC before restoring registers
    mov     al, 20h         ; Non-specific EOI
    out     20h, al         ; Send to primary PIC
    
    ; Restore registers in reverse order
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

### ISR Constraints
- **No DOS calls** - INT 21h forbidden in ISR context
- **No floating point** - FPU state not preserved
- **CLI duration ≤8μs** - Measured via PIT timing macros
- **Always send EOI** - Before register restoration
- **ISR-safe functions only** - Check SYMBOL_FLAG_ISR_SAFE

## Memory Access Conventions

### Module Data Access
```asm
; Access module's own data
mov     ax, ds:[variable]   ; DS points to module data segment

; Access external data via far pointer
mov     es, buffer_segment
mov     ax, es:[di]         ; Access via ES:DI
```

### DMA Buffer Access
```asm
; All DMA buffers obtained via memory management API
; Guaranteed to be physically contiguous and 64KB boundary safe
call    alloc_dma_buffer
jc      allocation_failed
; ES:DI now points to DMA-safe buffer
```

## Stack Usage

### Stack Frame for Complex Functions
```asm
function:
    push    bp
    mov     bp, sp          ; Establish stack frame
    sub     sp, 10          ; Allocate 10 bytes local storage
    
    ; Local variables at [bp-2], [bp-4], etc.
    ; Parameters at [bp+4], [bp+6], etc. (after near call)
    ; Parameters at [bp+6], [bp+8], etc. (after far call)
    
    mov     sp, bp          ; Restore stack pointer
    pop     bp
    ret                     ; Or retf for far calls
```

### Stack Requirements
- **Minimum 2KB** - Required stack space per module
- **No stack overflow** - Check available stack before deep recursion
- **Balanced calls** - Every push must have matching pop

## Performance Considerations

### Register Allocation Optimization
```asm
; Preferred register usage for hot paths:
; AX - Primary accumulator, return values
; BX - Base register, frequently used data
; CX - Loop counters, shift counts
; DX - I/O ports, multiplication/division
; SI - Source pointers, frequently accessed
; DI - Destination pointers, string operations
```

### Calling Sequence Optimization
```asm
; Efficient parameter passing for hot paths
mov     ax, port_address
mov     dx, data_value
call    fast_port_write    ; Optimized for speed
```

## Module API Standards

### Standard Module Entry Points
Every module must provide these standard entry points:

```c
// C function prototypes (assembly implementation)
int module_init(void);                    // Initialize module
int module_cleanup(void);                 // Cleanup before unload  
int module_get_info(module_info_t *info); // Get module information
```

### NIC Module API
NIC modules must implement standard network interface:

```c
int nic_detect(nic_info_t *info);        // Hardware detection
int nic_init(nic_info_t *nic);           // Initialize NIC
int nic_send(nic_info_t *nic, packet_t *pkt);  // Send packet
// Additional functions as defined in module specification
```

## Validation and Testing

### Calling Convention Validation
```asm
; Test harness checks:
; 1. All callee-saved registers preserved
; 2. Error codes correctly returned via CF/AX
; 3. Stack balanced after calls
; 4. Segment registers properly preserved
```

### Performance Validation
- **Near calls**: <10 cycles overhead
- **Far calls**: <25 cycles overhead  
- **ISR entry/exit**: <50 cycles total
- **Parameter marshalling**: <5 cycles per parameter

## Compliance Requirements

### Mandatory Checks
- [ ] All inter-module calls use far calling convention
- [ ] Error handling uses CF flag + AX error code
- [ ] Register preservation follows specification exactly
- [ ] ISR functions send EOI before return
- [ ] No DOS calls from ISR context
- [ ] Stack usage balanced (push/pop pairs)

### Quality Assurance
- Static analysis tools validate calling sequences
- Runtime checks verify register preservation
- Performance tests measure calling overhead
- Emulator testing validates real-mode behavior

---

**COMPLIANCE**: All agents must validate their code against these conventions. Any deviations require explicit approval and documentation of compatibility impact.