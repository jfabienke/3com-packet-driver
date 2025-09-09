# TSR Survival Architecture

Last Updated: 2025-09-04
Status: supplemental
Purpose: Define precise, production-safe TSR defensive patterns and constraints for the unified driver.

## Overview

This document defines the architectural integration of defensive programming patterns into our modular 3Com packet driver. Based on the Packet Driver (Crynwr) specification and proven DOS TSR survival techniques, this architecture ensures reliable operation in the hostile DOS environment while aligning with our current codebase.

Scope: Defensive patterns across API/ISR, vector management, PIC/ELCR, DMA safety, and residency.  
Dependencies: Packet Driver Spec v1.11; DRIVER_BOOT_SEQUENCE.md; UNIFIED_DRIVER_ARCHITECTURE.md

## Core Constraints and Acceptance Criteria

- Crynwr compliance: INT 60h–7Fh; signature at vector+3 ("PKT DRVR"); AH=function, BX=handle; DS:SI/ES:DI for params; AX=result; CF set on error, clear on success.
- Hot code is 8086/286-safe: 16‑bit instructions only; no 32‑bit regs or complex opcodes; no DOS/BIOS calls from ISR; bounded ISR work.
- Vector management: Install/uninstall via INT 21h AH=35h/25h with AL=vector; mask interrupts across get/set; restore original ES:BX exactly; verify ownership at runtime.
- PIC/EOI discipline: Save both PIC masks (0x21 and 0xA1), unmask only the NIC IRQ (and cascade for IRQ≥8); issue correct EOIs; handle IRQ2↔9 aliasing.
- ELCR policy: Do not touch ELCR by default. Only program the device IRQ’s trigger type when strictly required (e.g., PCI/EISA level‑triggered). Never modify system IRQs (0,1,2,8).
- DMA/XMS policy: No ISA 8237 DMA. Use NIC bus mastering only (3C515/PCI) with VDS gating and strict alignment/non‑crossing rules. XMS is copy‑only, never used directly for NIC DMA.
- Resident budget: ≤ ~6.9 KB hot section target. Enforce via map‑file parsing; block merges that exceed budget without justification.

---

## The DOS TSR Ecosystem Challenge

### Environmental Hostility Analysis

Our packet driver operates in an environment where:

1. Multiple NIC families (ISA + PCI) must coexist safely
2. Unified driver (no runtime .MOD modules)  
3. Resident footprint is tightly budgeted (≈6.9 KB hot)
4. Multiple applications can access the driver concurrently
5. Other TSRs compete for vectors and memory

This requires disciplined ISR/TSR patterns and strict residency control.

### Threat Model

```
┌─────────────────────────────────────────────────────────┐
│                    DOS System Memory                    │
│  ┌─────────────────────────────────────────────────┐   │
│  │              Other TSRs/Drivers                 │   │
│  │  SMARTDRV • MOUSE • DOSKEY • PRINT • etc.      │   │
│  └─────────────────────────────────────────────────┘   │
│              ▲  memory/vectors contention  ▲           │
│              │  reentrancy and timing      │           │
│              ▼                              ▼           │
│  ┌─────────────────────────────────────────────────┐   │
│  │         OUR PACKET DRIVER (≈6.9 KB hot)        │   │
│  │  ┌─────────────────────────────────────────┐   │   │
│  │  │ 3CPD.EXE (unified; no runtime modules) │   │   │
│  │  │  • ISR/API (resident/hot)              │   │   │
│  │  │  • Cold init (discarded or copied down)│   │   │
│  │  └─────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────┘   │
│              ▲  vector validation & PIC/EOI  ▲          │
│              │  DMA/XMS safety & VDS policy  │          │
│              ▼  stack & IF discipline        ▼          │
└─────────────────────────────────────────────────────────┘
```

---

## Architectural Defense Layers

### Layer 1: Foundation Defense (Core Loader)

The 3CPD.COM core loader implements the fundamental survival mechanisms:

#### Entry Point Hardening
```asm
; Every entry point follows this pattern
driver_entry_point:
    SAFE_STACK_SWITCH          ; Switch to private stack
    
    pusha                      ; Save all registers  
    push ds
    push es
    
    ; Validate our environment
    call validate_driver_integrity
    jc .corrupted
    
    ; Set up known-good segments
    mov ax, cs
    mov ds, ax
    mov es, ax
    
    ; Process the request
    call handle_request
    
    pop es
    pop ds
    popa
    
    RESTORE_CALLER_STACK
    iret
    
.corrupted:
    call emergency_recovery
    ; ... error handling
```

#### Memory Layout Protection
```
┌─────────────────────────────────────────────────┐
│                Core Loader (30KB)              │
├─────────────────────────────────────────────────┤
│ Signature: 'PKT_DRVR_V2' (validation)         │ ← Protected
│ Vector Table: Saved interrupt vectors          │ ← Monitored  
│ InDOS Cache: Cached DOS safety addresses       │ ← Critical
│ Module Registry: Loaded module tracking        │ ← Validated
│ Private Stack: 2KB isolated stack space        │ ← Switched
│ Critical Data: Driver state with checksums     │ ← Protected
│ Recovery Code: Emergency procedures            │ ← Armored
└─────────────────────────────────────────────────┘
```

### Layer 2: Module Defense (Dynamic Loading)

Each of the 14 enterprise modules implements standardized defensive interfaces:

#### Module Validation Protocol
```asm
; Module header structure (standardized across all modules)
module_header:
    signature       db 'MOD_V1',0     ; Module signature
    module_name     db 12 dup(?)      ; 8.3 DOS name
    version         dw ?              ; Version number
    size            dw ?              ; Module size
    checksum        dw ?              ; Integrity checksum
    dependencies    dw 16 dup(?)      ; Required modules
    entry_points    dw 32 dup(?)      ; Function table
    
; Module loading with defense
load_module_safely:
    ; Validate module signature
    VALIDATE_STRUCTURE bx
    jc .invalid_module
    
    ; Check dependencies  
    call verify_module_dependencies
    jc .dependency_error
    
    ; Allocate protected memory
    mov cx, [bx + module_header.size]
    call allocate_protected_memory   ; Adds canaries
    jc .memory_error
    
    ; Load with integrity checking
    call load_and_verify_module
    
    ; Initialize with timeout protection
    mov cx, TIMEOUT_LONG
    call initialize_module_with_timeout
    
    ret
```

#### Inter-Module Communication Security
```asm
; Safe module-to-module calls
call_module_function:
    ; Validate source module
    call validate_calling_module
    jc .unauthorized_call
    
    ; Check target module state  
    cmp byte [target_module + MODULE.state], MODULE_ACTIVE
    jne .module_unavailable
    
    ; Validate function number
    cmp ax, [target_module + MODULE.function_count]
    jae .invalid_function
    
    ; Switch to protected calling environment
    ENTER_CRITICAL_REENTRANT
    
    ; Set up canary for return detection
    mov [cs:call_canary], CANARY_PATTERN
    
    ; Make the call with error handling
    call far [target_module + MODULE.function_table + bx]
    
    ; Verify call completed normally
    cmp [cs:call_canary], CANARY_PATTERN  
    jne .module_corrupted_stack
    
    EXIT_CRITICAL_REENTRANT
    ret
```

### Layer 3: Hardware Defense (NIC Drivers)

Hardware interactions are the most vulnerable points requiring maximum protection:

#### Hardware Access Wrapper
```asm
; All hardware I/O goes through this wrapper
safe_hardware_io:
    ; Validate hardware state
    call check_hardware_responsiveness
    jc .hardware_failed
    
    ; Critical section for hardware access
    ENTER_CRITICAL
    
    ; Perform I/O with timeout
    mov cx, TIMEOUT_MEDIUM
    call perform_io_with_timeout
    
    EXIT_CRITICAL
    
    ; Verify operation completed successfully
    call verify_hardware_operation
    ret
    
.hardware_failed:
    call initiate_hardware_recovery
    stc
    ret
```

#### Bus Mastering Safety (3C515-TX)
```asm
; Enhanced safety for bus mastering operations
setup_bus_master_dma:
    ; Validate DMA buffer alignment and boundaries
    call validate_dma_buffer
    jc .invalid_buffer
    
    ; Ensure cache coherency before DMA
    call flush_cpu_caches
    
    ; Program DMA with timeout monitoring
    ENTER_CRITICAL
    
    ; Set up DMA descriptors with validation
    call setup_dma_descriptors
    
    ; Start DMA with timeout
    call start_dma_with_timeout
    
    EXIT_CRITICAL
    
    ; Monitor completion with timeout
    mov cx, TIMEOUT_LONG
    call wait_for_dma_completion
    
    ; Ensure cache coherency after DMA  
    call invalidate_cpu_caches
    
    ret
```

### Layer 4: API Defense (Application Interface)

The packet driver API is the primary attack surface from applications:

#### API Call Validation
```asm
; Packet Driver API entry (INT 60h)
packet_driver_api:
    ; Immediate stack switch for safety
    SAFE_STACK_SWITCH
    
    ; Save complete processor state
    pusha
    push ds
    push es
    
    ; Validate API call parameters
    call validate_api_parameters
    jc .invalid_parameters
    
    ; Check if DOS is safe for API processing
    CHECK_DOS_COMPLETELY_SAFE
    jnz .dos_busy
    
    ; Dispatch to function handler with timeout
    call dispatch_api_function_safely
    
    pop es
    pop ds  
    popa
    
    RESTORE_CALLER_STACK
    retf 2                     ; Return with flags
    
.invalid_parameters:
.dos_busy:
    ; Error handling with safe return
    PACKET_ERROR BAD_COMMAND
    pop es
    pop ds
    popa
    RESTORE_CALLER_STACK  
    retf 2
```

#### Multi-Application Isolation
```asm
; Access type registration with isolation
register_access_type:
    ; Find free access type slot
    mov bx, offset access_type_table
    mov cx, MAX_ACCESS_TYPES
    
.find_slot:
    VALIDATE_STRUCTURE bx       ; Check table integrity
    jc .table_corrupted
    
    cmp byte [bx + ACCESS_TYPE.in_use], 0
    je .found_slot
    
    add bx, size ACCESS_TYPE
    loop .find_slot
    
    PACKET_ERROR NO_SPACE
    
.found_slot:
    ; Validate application handler
    call validate_handler_address
    jc .invalid_handler
    
    ; Initialize access type with protection
    ENTER_CRITICAL
    
    mov byte [bx + ACCESS_TYPE.in_use], 1
    mov word [bx + ACCESS_TYPE.packet_type], dx
    mov word [bx + ACCESS_TYPE.handler_offset], si
    mov word [bx + ACCESS_TYPE.handler_segment], ds
    
    ; Update structure checksum
    UPDATE_STRUCTURE_CHECKSUM bx
    
    EXIT_CRITICAL
    
    PACKET_SUCCESS
```

---

## Component Integration Strategy (Unified Driver)

Unified .EXE driver with vtable-based HAL and no runtime .MOD modules:
- AMIS compliance and vector monitoring in the unified core.
- HAL enforces per-NIC safety (timeouts, error recovery) through `nic_ops_t`.
- DMA safety and cache coherency are applied in cold patching; hot paths are minimal.

### Phase 6 Integration: Critical Fixes

#### TODO Resolution with Defense
When fixing the 665 TODOs, each fix must include:

1. **Input Validation**: All parameters validated before use
2. **State Checking**: System state verified before operations
3. **Error Handling**: Graceful failure with recovery attempts
4. **Resource Cleanup**: Proper resource release on all paths
5. **Integrity Verification**: Post-operation validation

#### Hardware Driver Hardening
```asm
; Example: Fixing hardware detection TODO with defense
detect_nic_with_defense:
    ; Validate I/O port range
    cmp dx, MIN_IO_PORT
    jb .invalid_port
    cmp dx, MAX_IO_PORT  
    ja .invalid_port
    
    ; Test port responsiveness with timeout
    WAIT_FOR_CONDITION dx, PORT_READY_BIT, TIMEOUT_SHORT
    jc .port_unresponsive
    
    ; Attempt detection with error handling
    RETRY_ON_ERROR detect_nic_hardware, 3
    jc .detection_failed
    
    ; Validate detection results
    call validate_detection_results
    jc .invalid_results
    
    PACKET_SUCCESS
    
.invalid_port:
.port_unresponsive:
.detection_failed:
.invalid_results:
    PACKET_ERROR CANT_RESET
```

---

## Memory Architecture Defense

### UMB/Resident Strategy
```
Upper Memory Blocks (non-resident copy-only buffers):
┌─────────────────────────────────────────────┐
│ Segment 1 (64KB - Maximum single segment)  │
├─────────────────────────────────────────────┤
│ Protection Header (CANARY_PATTERN)         │ ← Memory guard
│ Core Loader (30KB)                         │ ← Entry point hardening
│ (Legacy module examples removed)          │
│ Critical Modules (9KB)                     │ ← Essential features
│ Protection Footer (CANARY_PATTERN)         │ ← Memory guard
├─────────────────────────────────────────────┤
│ Segment 2 (24KB - Overflow if needed)     │
│ Additional Modules                         │ ← Non-critical features
└─────────────────────────────────────────────┘
```

### XMS Protection Strategy (Copy‑Only)
XMS is used strictly as a copy‑only staging and spill area. NIC DMA descriptors, rings, and device‑visible buffers reside in conventional memory, aligned and sized to avoid 64KB crossings. When VDS is present and policy allows, it validates and translates conventional buffers for bus mastering. Never point NIC DMA at XMS memory.
```
Extended Memory (XMS - Variable size):
┌─────────────────────────────────────────────┐
│ Ring Buffers (51KB)                        │
├─────────────────────────────────────────────┤
│ Protection: DMA-safe boundaries            │ ← Cache-aligned
│ TX Descriptors: 16 × 1600B                 │ ← Canary-protected
│ RX Descriptors: 16 × 1600B                 │ ← Timeout-monitored
│ Protection: End-of-buffer markers          │ ← Overflow detection
├─────────────────────────────────────────────┤
│ Burst Buffers (13KB)                       │
│ Emergency packet storage                   │ ← Overflow handling
├─────────────────────────────────────────────┤
│ Statistics (3KB)                           │
│ Protected counters and diagnostics         │ ← Integrity-checked
└─────────────────────────────────────────────┘
```

---

## Performance vs Reliability Trade-offs

### Overhead Analysis

| Defense Layer | CPU Overhead | Memory Overhead | Reliability Gain |
|---------------|--------------|-----------------|------------------|
| Entry Point Hardening | 15-25 cycles | 2KB stack | Critical |
| Structure Validation | 10-20 cycles | 6 bytes/struct | High |
| Vector Monitoring | 30 cycles/check | 64 bytes | High |  
| Critical Sections | 8 cycles/section | 0 bytes | Medium |
| Memory Canaries | 4 cycles/access | 8 bytes/alloc | Medium |
| Hardware Timeouts | 5-1000 cycles | 0 bytes | Critical |
| **Total Overhead** | **~5-10%** | **~3-5KB** | **Production Ready** |

### Optimization Strategies

1. **Selective Validation**: Validate critical structures frequently, others periodically
2. **Cached Safety Checks**: Cache InDOS address rather than calling INT 21h repeatedly  
3. **Fast Path Optimization**: Minimal overhead for common operations
4. **Lazy Initialization**: Initialize defensive structures only when needed
5. **Conditional Compilation**: Debug builds include additional checks

---

## TSR Residency Strategies (Copy‑Down and Shrink‑In‑Place)

To guarantee a compact, stable resident footprint, we use one of two production‑safe residency strategies:

### Shrink‑In‑Place (Simple, Preferred When Contiguous)
1. Compute resident paragraphs from the map file (hot code/data + stack).  
2. INT 21h AH=4Ah: resize the program’s MCB to the resident size.  
3. Install final interrupt vectors (INT 60h and any hardware ISR) to point to the resident addresses.  
4. INT 21h AH=31h: terminate and stay resident.

### Explicit Copy‑Down (For Precise Placement/Non‑Contiguous Layouts)
1. Determine resident footprint and build a relocation list of far pointers/vtables/vectors that reference code/data addresses.  
2. INT 21h AH=48h: allocate a new block for the resident image at the desired segment.  
3. Far‑copy hot code and data into the new block.  
4. Rebase: update all far pointers, vtables, and interrupt vectors to the new segment; switch CS via a far jump/call if required.  
5. INT 21h AH=49h: free the original program block (and any discardable MCBs).  
6. INT 21h AH=31h: TSR with the size of the new resident block.

Safety notes:
- Mask interrupts during vector swaps and relocation; install vectors only after the resident image is committed.  
- Keep the relocation list minimal by making hot code position‑independent; avoid absolute segment constants.  
- Verify that DMA buffers and PIC/ELCR state are correct after relocation and before enabling interrupts.

Acceptance:
- Post‑relocation, rerun ownership checks, vector validation, and a minimal self‑test before activation.  
- Resident size must match configured budget within tolerance; record in build logs.

## Testing and Validation Architecture

### Defensive Pattern Testing

#### Unit Testing Framework
```asm
; Test framework for defensive patterns
test_defensive_patterns:
    ; Test 1: Memory protection
    call test_memory_canaries
    jc .test_failed
    
    ; Test 2: Vector validation  
    call test_vector_ownership
    jc .test_failed
    
    ; Test 3: Structure integrity
    call test_structure_validation  
    jc .test_failed
    
    ; Test 4: Hardware timeouts
    call test_hardware_timeouts
    jc .test_failed
    
    ; Test 5: Module safety
    call test_module_isolation
    jc .test_failed
    
    ; All tests passed
    mov ax, 'OK'
    clc
    ret
    
.test_failed:
    mov ax, 'ER'
    stc
    ret
```

#### Stress Testing Scenarios
1. **Memory Corruption**: Deliberately corrupt random memory locations
2. **Vector Stealing**: Simulate other TSRs stealing interrupt vectors
3. **Stack Exhaustion**: Test behavior with minimal stack space
4. **Hardware Failure**: Simulate non-responsive hardware
5. **Module Corruption**: Test inter-module communication with corrupted modules

### Integration Testing Strategy

#### Multi-TSR Environment Testing
```bash
# Test configuration with multiple hostile TSRs
SMARTDRV.EXE 2048 1024      # Disk cache with memory pressure
MOUSE.COM                   # Mouse driver (vector competition)  
DOSKEY.COM /BUFSIZE=512     # Command enhancement (memory pressure)
PRINT.COM /B:4096           # Print spooler (DOS calls)
3CPD.COM /ADVANCED          # Our packet driver
MTCP-PKT-TEST.EXE          # Packet driver stress test
```

#### Production Validation Criteria
1. **24+ Hour Stability**: No crashes or corruption in extended testing
2. **Multi-Application Support**: 5+ concurrent network applications  
3. **Memory Efficiency**: <5% overhead from defensive patterns
4. **Error Recovery**: Automatic recovery from 95%+ of error conditions
5. **Compatibility**: Works with 10+ common TSR combinations

---

## Emergency Recovery Procedures

### Corruption Detection and Recovery

#### Automated Recovery System
```asm
; Multi-level recovery system
emergency_recovery:
    ; Level 1: Soft recovery (repair data structures)
    call repair_data_structures
    jnc .recovery_successful
    
    ; Level 2: Module restart (reload corrupted modules)
    call restart_corrupted_modules
    jnc .recovery_successful
    
    ; Level 3: Hardware reset (reset all NICs)
    call reset_all_hardware
    jnc .recovery_successful
    
    ; Level 4: Emergency shutdown (disable driver safely)
    call emergency_shutdown
    
.recovery_successful:
    ; Log recovery event
    call log_recovery_event
    ret
```

#### Graceful Degradation Strategy
1. **Module Isolation**: Disable corrupted modules while maintaining core functionality
2. **Feature Reduction**: Disable enterprise features to preserve basic networking
3. **Safe Mode**: Minimal functionality mode with maximum defensive measures
4. **Clean Shutdown**: Safe driver removal when recovery is impossible

---

## Historical Validation

### Comparison with Original 3C5X9PD

| Aspect | Original 3C5X9PD | Our Enhanced Driver |
|--------|------------------|---------------------|
| **Resident (hot)** | ≈6.9 KB target | map-enforced |
| **NICs Supported** | 1 (3C509 only) | 65 (4 generations) |
| **Defensive Patterns** | Basic (stack switching, CLI/STI) | Comprehensive (10+ patterns) |
| **Module System** | None | 14 enterprise modules |
| **Cache Management** | None (relied on WT cache) | 4-tier coherency system |
| **Error Recovery** | Minimal | Multi-level automated recovery |
| **Memory Protection** | None | Canaries, signatures, checksums |
| **API Validation** | Basic | Complete parameter validation |

### Lessons Learned Integration

1. **Stack Discipline**: Original's paranoid stack switching was correct - we've enhanced it
2. **Vector Ownership**: Original's vector validation was survival technique - we've systematized it  
3. **Critical Sections**: Original's liberal CLI/STI was defensive - we've optimized it
4. **Hardware Timeouts**: Original's timeout loops were necessary - we've standardized them
5. **Segment Hygiene**: Original's segment resets were critical - we've automated them

---

## Conclusion

The TSR Survival Architecture represents the evolution of DOS networking from the original 3C5X9PD's battle-tested defensive patterns to a modern, enterprise-capable packet driver. By systematically applying 30+ years of accumulated TSR wisdom across our modular architecture, we achieve:

1. **65x Hardware Support** with maintained reliability
2. **14 Enterprise Modules** with safe dynamic loading  
3. **Multi-Application Support** with proper isolation
4. **Automated Recovery** from the majority of error conditions
5. **Production Reliability** suitable for mission-critical DOS networking

The defensive patterns are not academic exercises but proven survival techniques that enabled the original driver's success. Our enhanced implementation scales these patterns across a far more complex architecture while maintaining the same reliability principles.

**The fundamental truth remains unchanged**: In the DOS TSR environment, paranoia is not a bug - it's a feature. The systems that survive are those that assume the worst and defend accordingly.
