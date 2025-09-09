# Vendor Extension API Specification

Last Updated: 2025-09-04
Status: supplemental
Purpose: Define INT 60h vendor-specific extensions for diagnostics and observability.

## Overview
The 3Com Packet Driver implements vendor-specific extensions in the AH=80h-9Fh range for introspection and diagnostics. These extensions are ISR-safe, non-blocking, and have guaranteed bounded execution time (<2μs on 486).

## Design Principles
- **Zero ISR Impact**: All handlers read precomputed snapshots
- **Minimal Resident**: ≤41 bytes of hot code
- **Constant Time**: No loops, no dynamic allocation
- **Register Safe**: Preserves DS, ES, BP per Packet Driver spec
- **Error Signaling**: CF=1 on error, CF=0 on success

## API Functions

### AH=80h: Vendor Discovery
**Purpose**: Identify vendor extensions and negotiate capabilities  
**Input**: None  
**Output**:
- CF=0: Success
- AX: Signature ('3C' = 0x3343)
- BX: Version in BCD (0x0100 = version 1.00)
- CX: Maximum supported function (0x0084)
- DX: Capability flags
  - Bit 0: Basic introspection
  - Bit 1: Safety state queries
  - Bit 2: SMC patch info
  - Bit 3: Memory layout info

**Example**:
```assembly
mov ah, 80h
int 60h
jc  error
cmp ax, '3C'
jne not_3com
; BX = version, CX = max function, DX = capabilities
```

### AH=81h: Get Safety State
**Purpose**: Query runtime safety configuration  
**Input**: None  
**Output**:
- CF=0: Success
- AX: Safety flags
  - Bit 0: PIO mode forced
  - Bit 1: Patches verified OK
  - Bit 2: DMA boundary checking active
  - Bit 3: Cache operations enabled
  - Bit 4: ISR stack guard active
  - Bit 15: Kill switch activated
- BX: ISR stack free bytes
- CX: Number of active patches
- DX: Reserved

**Example**:
```c
union REGS r;
r.h.ah = 0x81;
int86(0x60, &r, &r);
if (!r.x.cflag) {
    if (r.x.ax & 0x0001) printf("PIO mode forced\n");
    printf("Stack free: %u bytes\n", r.x.bx);
}
```

### AH=82h: Get Patch Statistics
**Purpose**: Query SMC patch metrics  
**Input**: None  
**Output**:
- CF=0: Success
- AX: Total patches applied
- BX: Maximum CLI duration (PIT ticks)
- CX: Number of modules patched
- DX: Health code
  - 0x0A11: All systems good
  - 0x0BAD: Running degraded
  - 0xDEAD: Critical failure

### AH=83h: Get Memory Map
**Purpose**: Query resident memory layout  
**Input**: ES:DI = pointer to 8-byte buffer (or NULL)  
**Output**:
- CF=0: Success (buffer filled)
- CF=1: Error (no buffer provided)
- AX: Bytes written (8) or required size if no buffer

**Buffer Format** (8 bytes):
```
Offset  Size  Description
0       2     Hot code size (bytes)
2       2     Hot data size (bytes)
4       2     ISR stack size (bytes)
6       2     Total resident (bytes)
```

**Example**:
```c
uint16_t buffer[4];
union REGS r;
struct SREGS sr;

r.h.ah = 0x83;
r.x.di = FP_OFF(buffer);
sr.es = FP_SEG(buffer);
int86x(0x60, &r, &r, &sr);

if (!r.x.cflag) {
    printf("Resident: %u bytes\n", buffer[3]);
}
```

### AH=84h: Get Version Info
**Purpose**: Query version and build configuration  
**Input**: None  
**Output**:
- CF=0: Success
- AX: Version in BCD (0x0100 = 1.00)
- BX: Build flags
  - Bit 15: Production build
  - Bit 0: PIO mode active
  - Bit 1: DMA mode active
  - Bit 2: Debug features
  - Bit 3: Logging enabled
- CX: NIC type (0x0509 = 3C509B, 0x0515 = 3C515)
- DX: Reserved

## Error Handling

All functions return CF=1 on error with error codes in AX:

| Code | Meaning |
|------|---------|
| 0xFFFF | Invalid function (not implemented) |
| 0xFFFE | Buffer required but not provided |
| 0xFFFD | Buffer too small |
| 0xFFFC | Feature not ready |

## Register Contract

### Preserved Registers
Per Packet Driver specification, these registers are preserved:
- DS (Data Segment)
- ES (Extra Segment)  
- BP (Base Pointer)
- SI (Source Index) - except when used for output
- DI (Destination Index) - except when used for input

### Modified Registers
These registers may be modified:
- AX, BX, CX, DX (return values)
- Flags (CF for error signaling)

### Segment Assumptions
- No assumptions about segment registers
- Works in any memory model
- Caller's stack used (no stack switch)

## Timing Guarantees

| Function | Maximum Time | Typical Time |
|----------|-------------|--------------|
| AH=80h | 1.5μs | 0.8μs |
| AH=81h | 1.2μs | 0.7μs |
| AH=82h | 1.2μs | 0.7μs |
| AH=83h | 2.0μs | 1.2μs |
| AH=84h | 1.2μs | 0.7μs |

*Measured on 486DX2/66MHz*

## Implementation Details

### Memory Layout
```
Hot Code (41 bytes):
  +0: Extension check (3 bytes) - in main ISR
  +3: Unified handler (38 bytes)

Hot Data (40 bytes):
  +0: Discovery snapshot (8 bytes)
  +8: Safety snapshot (8 bytes)
  +16: Patch snapshot (8 bytes)
  +24: Memory snapshot (8 bytes)
  +32: Version snapshot (8 bytes)
```

### Snapshot Updates
- Snapshots computed once during driver initialization
- Never modified after TSR installation
- Read-only at runtime for safety
- No pointer indirection for speed

### Concurrency Safety
- All reads are atomic (16-bit aligned)
- No multi-word tearing possible
- ISR-safe without CLI/STI
- No race conditions

## Testing

### Test Utility
Use `EXTTEST.EXE` to validate:
```
C:\> EXTTEST

3Com Packet Driver Extension API Test
======================================
[PASS] CF clear on success
[PASS] Signature = '3C'
[PASS] Version BCD format
...
```

### Acceptance Criteria
- All register preservation tests pass
- Timing <2μs per call on 486
- No errors in 1000-call stress test
- Correct error codes for invalid functions
- Buffer overflow handled safely

## Future Extensions

Reserved ranges for future use:
- AH=85h-8Fh: Performance counters (Stage 2)
- AH=90h-9Fh: 3Com hardware-specific

## Stability Notice

**Current Status**: STABLE  
**API Version**: 1.0  
**Compatibility**: Will maintain backward compatibility

The AH=80h discovery call allows tools to detect capabilities and adapt to future versions without breaking.

---
**Implementation**: `src/asm/extension_api_opt.asm`  
**Header**: `include/extension_api.h`  
**Test**: `test/exttest.c`  
**Size**: 41 bytes hot code + 40 bytes data
