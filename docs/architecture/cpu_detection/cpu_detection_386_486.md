# 386 vs 486 CPU Detection Implementation

## Overview
This document explains the implementation of proper 386 vs 486 detection using the AC (Alignment Check) flag test, which correctly distinguishes between these processors even when CPUID is not available.

## The Problem
Many early 486 processors (486DX, 486SX, early 486DX2) do not support the CPUID instruction, which was introduced late in the 486 lifecycle. The previous implementation incorrectly assumed:
- No CPUID = 386 ❌
- CPUID available = 486+ ✓

This misidentified early 486 CPUs as 386, preventing use of 486-specific optimizations.

## The Solution: AC Flag Test
The 486 introduced the AC (Alignment Check) flag as bit 18 in the EFLAGS register. This flag:
- **Does not exist on 386** - Bit 18 cannot be modified
- **Exists on all 486+** - Bit 18 can be toggled

## Implementation Details

### Detection Flow
```
1. Test 8086 vs 286+: Shift wrap behavior
2. Test 286 vs 386+: FLAGS bits 12-15 behavior  
3. Test 386 vs 486: AC flag (bit 18) toggle test ← NEW
4. Test CPUID availability: Late 486 vs early 486
5. Use CPUID if available: Get exact CPU family
```

### AC Flag Test Code
```assembly
test_alignment_check_flag:
    pushfd              ; Save EFLAGS
    pop eax            
    mov ecx, eax       ; Save original
    
    xor eax, 0x40000   ; Toggle AC flag (bit 18)
    push eax
    popfd              ; Try to set modified EFLAGS
    
    pushfd             ; Read back
    pop eax
    
    push ecx           ; Restore original
    popfd
    
    xor eax, ecx       ; What changed?
    and eax, 0x40000   ; Isolate AC bit
    jz is_386          ; No change = 386
    ; Changed = 486+
```

## CPU Models Affected

### 486 CPUs WITHOUT CPUID (now correctly detected)
- Intel 486DX (all speeds)
- Intel 486SX (all speeds) 
- Intel 486DX2-50, 486DX2-66 (early steppings)
- Intel 486SL series
- AMD Am486DX, Am486SX
- Cyrix Cx486DX, Cx486SX

### 486 CPUs WITH CPUID (already worked)
- Intel 486DX4 (all models)
- Intel 486DX2-66, 486DX2-80 (late steppings)
- AMD Am486DX4
- Some late Cyrix 486 models

### 386 CPUs (correctly identified as 386)
- Intel 386DX, 386SX
- AMD Am386DX, Am386SX
- All 386 clones

## Features Enabled for 486

When a 486 is correctly detected (with or without CPUID), these features are enabled:

### Instructions
- **BSWAP** - Byte swap (network byte order conversion)
- **CMPXCHG** - Compare and exchange (atomic operations)
- **INVLPG** - Invalidate page in TLB
- **XADD** - Exchange and add

### Cache Management  
- **WBINVD** - Write-back and invalidate cache
- **INVD** - Invalidate cache (dangerous)
- Internal 8KB unified cache

### Performance
- Pipelined execution
- On-chip FPU (DX models)
- Faster string operations

## Testing the Implementation

### Test Matrix

| CPU | AC Flag | CPUID | Detection | Result |
|-----|---------|-------|-----------|---------|
| 386DX | No | No | Correct | CPU_80386 ✓ |
| 486DX | Yes | No | **Fixed** | CPU_80486 ✓ |
| 486SX | Yes | No | **Fixed** | CPU_80486 ✓ |
| 486DX2 | Yes | No | **Fixed** | CPU_80486 ✓ |
| 486DX4 | Yes | Yes | Correct | CPU_80486 ✓ |
| Pentium | Yes | Yes | Correct | CPU_CPUID_CAPABLE ✓ |

### Verification Commands

In DOS, use these utilities to verify detection:
```
CHKCPU.EXE   - Should show correct 486 model
MSD.EXE      - Microsoft Diagnostics
CPUID.EXE    - Shows CPUID availability
```

## Benefits

1. **Correct 486 Identification** - All 486 CPUs properly detected
2. **486 Optimizations Enabled** - BSWAP, cache management available
3. **Better Performance** - Can use 486-specific code paths
4. **Accurate System Information** - Proper CPU reporting

## Edge Cases Handled

### NexGen Nx586
This CPU appears as a 386 in our test because it doesn't properly support the AC flag, despite being a 5th generation processor. This is correct behavior as it maintains compatibility.

### Cyrix 486DLC/486SLC  
These are 486-class cores in 386 pinout. They:
- Support some 486 instructions
- May or may not support AC flag
- Detection depends on specific model

### IBM 486SLC2
Special IBM variant that:
- Has 16KB cache
- Supports most 486 instructions
- Should be detected as 486 if AC flag works

## Implementation Files

- `src/asm/cpu_detect.asm` - Main detection logic
  - `detect_cpu_type` - Main detection flow
  - `test_alignment_check_flag` - AC flag test

## References

- Intel 486 Processor Family Programmer's Reference Manual
- Intel Application Note AP-485: Intel Processor Identification
- Ralf Brown's Interrupt List - CPU Detection
- Linux kernel arch/x86/kernel/cpu/common.c