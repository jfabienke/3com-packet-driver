# Memory Impact Analysis - Foundation Stabilization

## Executive Summary
The foundation stabilization adds approximately 2.1KB to resident memory, primarily for ISR stack safety. This overhead is justified by the critical safety improvements.

## Detailed Memory Impact

### ISR Stack Safety (Stage -1.C)
**Component**: Private ISR Stack  
**Location**: `src/asm/nic_irq_smc.asm`  
**Impact**: 2052 bytes resident

```assembly
isr_private_stack   db  2048 DUP(0)    ; 2048 bytes
saved_ss            dw  0               ; 2 bytes  
saved_sp            dw  0               ; 2 bytes
; Total: 2052 bytes
```

**Justification**: Prevents stack corruption when multiple TSRs are loaded

### SMC Patching Overhead (Stage -1.B)
**Component**: Extended patch targets  
**Location**: Multiple patch sites  
**Impact**: ~50 bytes resident

- DMA boundary check patch: 5 bytes
- Cache flush pre patch: 5 bytes  
- Cache flush post patch: 5 bytes
- 3C515 transfer patch: 5 bytes
- ISR stack switch code: ~30 bytes

**Justification**: Enables CPU-specific optimizations and safety checks

### Build System Additions (Stage -1.A)
**Component**: TSR C wrappers  
**Location**: `tsr_c_wrappers.obj`  
**Impact**: ~200 bytes resident (estimated)

**Justification**: Required for proper C/ASM interfacing in TSR

### DMA Safety Gates (Stage -1.E)
**Component**: Runtime checks  
**Location**: Various modules  
**Impact**: ~100 bytes resident

- PIO enforcement checks: ~20 bytes
- Patch verification code: ~40 bytes
- Safety flag storage: ~40 bytes

**Justification**: Prevents unsafe DMA operations

## Memory Map (Projected)

```
Segment     Size    Description
-------     ----    -----------
_TEXT       ~3500   Hot code (resident)
  +2052             ISR private stack (NEW)
  +50               SMC patches (NEW)
  +200              TSR wrappers (NEW)
  +100              DMA safety (NEW)
_DATA       ~1000   Hot data (resident)
COLD_TEXT   ~8000   Init code (discarded)
```

## Total Resident Impact

**Before Foundation Fixes**: ~4.5KB resident  
**After Foundation Fixes**: ~6.6KB resident  
**Net Increase**: ~2.1KB

## Memory Optimization Opportunities

### Production Build Flags
The production build uses aggressive optimization:
```makefile
-DPRODUCTION    # Disables debug code
-DNO_LOGGING    # Removes logging strings
-DNO_STATS      # Removes statistics collection
-os             # Optimize for size
```

### DMA Module Optimization
Special build rules for DMA modules:
```makefile
$(BUILDDIR)/dma_mapping.obj: -DPRODUCTION -DNO_LOGGING
$(BUILDDIR)/dma_boundary.obj: -DPRODUCTION -DNO_LOGGING  
$(BUILDDIR)/dma_safety.obj: -DPRODUCTION -DNO_LOGGING
```

Expected savings: ~300 bytes per module

## Comparison with Original Goals

**Original TSR Goal**: <4KB resident  
**Current Status**: ~6.6KB resident  
**Over Budget**: 2.6KB

### Justification for Exceeding Goal:
1. **ISR Stack Safety** (2KB): Critical for DOS TSR stability
2. **DMA Safety** (0.1KB): Prevents data corruption
3. **SMC Infrastructure** (0.3KB): Enables CPU optimizations
4. **TSR Wrappers** (0.2KB): Required for C support

## Future Optimization Plan

### Phase 1: Code Size Reduction
- Convert critical C functions to assembly
- Inline small functions
- Potential savings: ~500 bytes

### Phase 2: Stack Size Tuning  
- Profile actual ISR stack usage
- Potentially reduce from 2KB to 1KB
- Potential savings: 1024 bytes

### Phase 3: Feature Gating
- Make safety features optional via compile flags
- Allow PIO-only build without DMA code
- Potential savings: ~800 bytes

## Verification Commands

Once build environment is available:

```bash
# Generate memory map
make production
cat build/3cpd.map | grep "Segment"

# Check binary size
ls -l build/3cpd.exe

# Analyze hot sections
objdump -h build/3cpd.exe | grep TEXT

# Count resident bytes
size build/*.obj | grep -E "(hot|tsr)"
```

## Conclusion

The 2.1KB memory increase is a necessary investment in:
- **Reliability**: ISR stack protection
- **Safety**: DMA boundary checking  
- **Performance**: CPU-specific optimizations
- **Maintainability**: Proper C/ASM interfacing

For DOS TSRs operating in the hostile conventional memory environment, these safety features are essential for production deployment.