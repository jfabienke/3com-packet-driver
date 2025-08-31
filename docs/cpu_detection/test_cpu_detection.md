# Phase 1 CPU Detection Enhancement Test Plan

## Summary of Implemented Changes

### 1. ASM Module Enhancements (`src/asm/cpu_detect.asm`)

#### New CPU Type Constants
- Added `CPU_PENTIUM_PRO` (5) for P6 family processors
- Added `CPU_PENTIUM_4` (6) for NetBurst architecture
- Added `CPU_CPUID_CAPABLE` (7) for CPUs with CPUID but type not yet determined

#### Safety Features Added
- **V86 Mode Detection**: `detect_v86_mode()` checks VM flag in EFLAGS
- **CPUID Max Level Check**: `get_cpuid_max_level()` ensures safe CPUID usage
- **CLFLUSH Detection**: `detect_clflush_support()` checks bit 19 and gets cache line size
- **WBINVD Safety Check**: `check_wbinvd_safety()` prevents V86 mode traps

#### Export Functions for C Module
- `asm_get_cpu_family()` - Returns CPU family ID from CPUID
- `asm_get_cpuid_max_level()` - Returns maximum CPUID function
- `asm_is_v86_mode()` - Returns V86 mode status

#### Critical Fix
- Changed line 420 from returning `CPU_80486` to `CPU_CPUID_CAPABLE` for CPUID-enabled CPUs

### 2. Cache Operations Enhancements (`src/asm/cache_ops.asm`)

#### Safe Wrapper Functions
- `cache_clflush_safe()` - Checks CLFLUSH availability before use
- `cache_wbinvd_safe()` - Checks V86 mode before WBINVD
- `memory_fence_after_clflush()` - Critical memory fence using CPUID serialization

#### Memory Fence Implementation
- Added proper memory fence after CLFLUSH operations
- Uses CPUID as serializing instruction (guaranteed on all x86)
- Prepared for MFENCE on P4+ systems

### 3. C Module Updates (`src/c/main.c`)

#### CPU Type Refinement Logic
```c
if (g_cpu_info.type == CPU_TYPE_CPUID_CAPABLE) {
    uint8_t family = asm_get_cpu_family();
    switch (family) {
        case 4:  g_cpu_info.type = CPU_TYPE_80486; break;
        case 5:  g_cpu_info.type = CPU_TYPE_PENTIUM; break;
        case 6:  g_cpu_info.type = CPU_TYPE_PENTIUM_PRO; break;
        case 15: g_cpu_info.type = CPU_TYPE_PENTIUM_4; break;
    }
}
```

#### V86 Mode Warning
- Logs warning if running in V86 mode
- Sets CPU_FEATURE_V86_MODE flag for cache tier selection

### 4. Header Updates (`include/cpu_detect.h`)

#### New Type Definitions
- Added `CPU_TYPE_PENTIUM_4` and `CPU_TYPE_CPUID_CAPABLE`
- Added `CPU_FEATURE_WBINVD_SAFE` flag

## Test Scenarios

### Scenario 1: 486 CPU Detection
**Input**: 486 CPU with CPUID
**Expected**:
1. ASM returns `CPU_CPUID_CAPABLE`
2. C module refines to `CPU_TYPE_80486` (family 4)
3. WBINVD available, CLFLUSH not available
4. Cache tier 2 selected

### Scenario 2: Pentium Detection
**Input**: Pentium CPU (family 5)
**Expected**:
1. ASM returns `CPU_CPUID_CAPABLE`
2. C module refines to `CPU_TYPE_PENTIUM`
3. BSWAP, TSC features detected
4. Cache tier 2 selected

### Scenario 3: P6 Detection
**Input**: Pentium Pro/II/III (family 6)
**Expected**:
1. ASM returns `CPU_CPUID_CAPABLE`
2. C module refines to `CPU_TYPE_PENTIUM_PRO`
3. Enhanced features detected
4. Cache tier 2 selected

### Scenario 4: Pentium 4 Detection
**Input**: Pentium 4 (family 15)
**Expected**:
1. ASM returns `CPU_CPUID_CAPABLE`
2. C module refines to `CPU_TYPE_PENTIUM_4`
3. CLFLUSH detected with cache line size
4. Cache tier 1 selected (CLFLUSH available)

### Scenario 5: V86 Mode Detection
**Input**: Any CPU in V86 mode (Windows DOS box)
**Expected**:
1. V86 mode detected via VM flag
2. Warning logged
3. WBINVD marked unsafe
4. Fallback to tier 3 (software barriers)

### Scenario 6: 386 Without CPUID
**Input**: 386 CPU
**Expected**:
1. ASM returns `CPU_80386`
2. No CPUID refinement needed
3. Software barriers only
4. Cache tier 3 selected

## Safety Improvements Validated

1. **CPUID Max Level Check**: Prevents invalid CPUID calls
2. **V86 Mode Detection**: Prevents WBINVD traps
3. **Memory Fence After CLFLUSH**: Ensures DMA safety
4. **Feature Detection Before Use**: All privileged instructions checked
5. **Proper CPU Type Resolution**: CPUID family used for accurate detection

## Cache Tier Selection Logic

Based on detected features:
- **Tier 1 (CLFLUSH)**: P4+ with CLFLUSH support
- **Tier 2 (WBINVD)**: 486+ NOT in V86 mode
- **Tier 3 (Software)**: 386 or V86 mode
- **Tier 4 (None)**: 286 (no cache management)

## Integration Points

### DMA Buffer Management
- Uses cache tier for proper flushing before DMA
- CLFLUSH for specific cache lines (Tier 1)
- WBINVD for complete flush (Tier 2)
- Software barriers (Tier 3)

### SMC Patching
- Uses CPU type for instruction selection
- BSWAP optimization on 486+
- Enhanced instructions on P6+
- SSE2 optimizations on P4+

## Risk Mitigation

1. **V86 Trap Prevention**: Check before privileged instructions
2. **Feature Verification**: Don't assume features by CPU type
3. **Safe Defaults**: Fall back to lower tier on detection failure
4. **Logging**: Clear warnings for unusual configurations

## Next Steps

1. Test on real hardware (286, 386, 486, Pentium systems)
2. Verify V86 mode detection in DOS boxes
3. Benchmark cache operation performance
4. Validate DMA safety with enhanced detection