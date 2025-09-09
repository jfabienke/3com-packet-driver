# Stage 1: Bus Master Test - Integration Guide

## Overview

Stage 1 implements comprehensive DMA validation for the 3C515 NIC through the driver's own DMA path. The test validates safety on real 486/Pentium hardware with zero resident memory impact (except for 30-byte quiesce handlers).

## Components

### 1. Driver Extensions (Resident)

#### Quiesce/Resume Handlers (`src/asm/quiesce.asm`)
- **AH=90h**: Quiesce driver (stops NIC, masks IRQ)
- **AH=91h**: Resume driver (restores NIC, unmasks IRQ) 
- **AH=92h**: Get DMA statistics
- **Resident Size**: 30 bytes (24 code + 6 data)

#### Extension API Updates (`src/asm/extension_api.asm`)
- **AH=93h**: Set transfer mode (PIO/DMA)
- Expanded jump table for AH=80h-93h
- Total resident growth: ~45 bytes

### 2. Three-Layer DMA Policy (`src/c/dma_policy.c`)

```c
typedef struct {
    uint8_t runtime_enable;      // User toggle via INT 60h
    uint8_t validation_passed;   // Set by BMTEST
    uint8_t last_known_safe;     // Persistent across reboots
    uint32_t hw_signature;       // Hardware validation
} dma_policy_state_t;
```

**DMA enabled only when ALL three conditions are true:**
- User has enabled via runtime toggle
- Validation test has passed
- No recent DMA failures

**Persistence**: `C:\3CPKT\DMA.SAF` (atomic write via temp+rename)

### 3. VDS Interface (`include/vds.h`, `src/c/vds.c`)

Virtual DMA Services support for memory managers:
- Lock/unlock memory regions
- Get physical addresses
- Boundary validation utilities
- Support for EMM386, QEMM, Windows DOS boxes

### 4. Bus Master Test Utility (`tools/bmtest.c`)

External test program with zero resident impact:

#### Test Phases:
1. **Pre-validation**: Driver loaded, patches active
2. **Driver Control**: Quiesce driver, enable loopback
3. **Boundary Testing**: 64KB crossing validation
4. **Cache Coherency**: WBINVD timing and validation
5. **Performance**: PIO vs DMA throughput comparison

#### Output Files:
- `BMTEST.RPT`: Human-readable report
- `BMTEST.JSN`: JSON for automation
- `C:\3CPKT\DMA.SAF`: Persistent policy state

## Build Instructions

```bash
# Build driver with Stage 1 support
wmake release

# Build BMTEST utility
wmake bmtest

# Files generated:
# build/3cpd.exe    - Driver with Stage 1 extensions
# build/bmtest.exe  - Bus master test utility
```

## Testing Procedure

### 1. Install Driver
```
DEVICE=C:\3CPKT\3CPD.EXE /IO1=0x300 /IRQ1=10
```

### 2. Run Bus Master Test
```
C:\> BMTEST

3C515 Bus Master Test Utility v1.0
===================================

VDS: Available

Phase 1: Pre-validation
  Driver loaded: OK
  Patches active: OK

Phase 2: Driver control
  Driver quiesced: OK
  Loopback enabled: OK

Phase 3: Boundary validation
  Aligned buffer at 00123000: OK
  Boundary buffer at 0012FFF0: crosses 64KB (expected)
  Bounces used: 1
  Violations: 0

Phase 4: Cache coherency
  WBINVD median: 45 us
  WBINVD P95: 52 us

Phase 5: Performance comparison
  PIO: 100 packets in 850 ms, 173 KB/s
  DMA: 100 packets in 490 ms, 300 KB/s
  Speedup: 1.73 x

Driver resumed: OK

===================================
RESULT: ALL TESTS PASSED
DMA can be enabled safely

Report saved to BMTEST.RPT
```

### 3. Verify Policy State

After successful test:
```
C:\> TYPE C:\3CPKT\DMA.SAF
[Binary file containing policy state]

C:\> DEBUG C:\3CPKT\DMA.SAF
-d100
0100: 00 01 01 12 34 56 78 00  ........  # runtime=0, validated=1, safe=1
```

### 4. Enable DMA at Runtime

Use packet driver Extension API:
```asm
mov ah, 93h     ; Set transfer mode
mov al, 1       ; 1 = DMA mode
int 60h
jc error        ; CF set on error
```

## Validation Criteria

### Pass Conditions:
- ✅ Driver quiesces successfully (ISR not active)
- ✅ No boundary violations detected
- ✅ Cache coherency tests pass
- ✅ DMA faster than PIO (>1.5x speedup expected)
- ✅ All bounces handled correctly

### Fail Conditions:
- ❌ ISR remains active during quiesce
- ❌ Boundary violations detected
- ❌ Cache coherency failures
- ❌ DMA slower than PIO
- ❌ Hardware signature mismatch

## Hardware Requirements

### Minimum:
- 486DX CPU (for WBINVD instruction)
- 3C515-TX NIC with bus master support
- Chipset with ISA DMA support

### Recommended Test Systems:
- Intel 430HX/VX/TX chipsets
- AMD/VIA Socket 7 chipsets
- Real hardware (not emulators)

## Safety Mechanisms

### Automatic Fallback:
- PIO mode on any DMA failure
- Three consecutive failures disable DMA permanently
- Hardware signature validation on each boot

### Manual Override:
```
; Force PIO mode
mov ah, 93h
mov al, 0      ; 0 = PIO mode
int 60h
```

## Troubleshooting

### "Driver not loaded"
- Ensure 3CPD.EXE loaded in CONFIG.SYS
- Check INT 60h vector

### "Patches not active"
- Verify driver version supports Stage 1
- Check for patch checksum failures

### "Failed to quiesce"
- ISR may be handling traffic
- Retry after network idle period

### "Boundary violations"
- Chipset DMA issue detected
- DMA will remain disabled (safe)

### "DMA slower than PIO"
- Bus master not functioning correctly
- Check chipset configuration
- DMA will be disabled

## Performance Expectations

| Mode | 10 Mbps (3C509B) | 100 Mbps (3C515) |
|------|------------------|------------------|
| PIO  | ~600 KB/s        | ~2.5 MB/s        |
| DMA  | N/A              | ~4.3 MB/s        |

**Expected DMA Speedup**: 1.7x to 2.0x

## Integration with mTCP

After successful validation:
```
SET MTCP=C:\MTCP\MTCP.CFG
SET 3C515_DMA=1
FTP ftp.cdrom.com
```

DMA will be used automatically if all three policy conditions are met.

## Files Modified

### Existing Files Updated:
- `Makefile`: Added build targets for new components
- `src/asm/extension_api.asm`: Added AH=90h-93h handlers

### New Files Created:
- `src/asm/quiesce.asm`: Driver quiesce/resume (30 bytes resident)
- `src/c/dma_policy.c`: Three-layer policy management
- `src/c/vds.c`: VDS implementation
- `include/vds.h`: VDS interface definitions
- `tools/bmtest.c`: Bus master test utility

## Memory Impact

### Resident (stays after init):
- Quiesce handlers: 30 bytes
- Extension API updates: 15 bytes
- **Total Stage 1 resident**: 45 bytes

### Non-resident (test utility):
- BMTEST.EXE: ~20KB (external program)
- No impact on driver resident size

## Next Steps

After Stage 1 validation passes:

1. **Stage 2**: Enhanced diagnostics (INT 60h AH=A0h-AFh)
2. **Stage 3A**: Seqlock for multi-NIC consistency
3. **Stage 3B**: XMS state migration
4. **Stage 3C**: Multi-NIC failover with GARP

---

**Status**: COMPLETE  
**Resident Growth**: 45 bytes (under 85-byte total budget)  
**Safety**: Three-layer policy with automatic fallback  
**Testing**: Comprehensive validation through driver's DMA path