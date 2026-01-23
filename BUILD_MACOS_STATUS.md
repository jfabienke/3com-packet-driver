# macOS Cross-Compilation Status

**Last Updated:** 2026-01-23 20:05:00 CET

## Overview

This document tracks the progress of enabling macOS cross-compilation for the 3Com Packet Driver project using Open Watcom v2.

## Build System

### ✅ Completed

| Component | Status | Details |
|-----------|--------|---------|
| `build_macos.sh` | Working | Build script with dual assembler support |
| Open Watcom v2 | Configured | ARM64 macOS cross-compiler for DOS 16-bit |
| WASM integration | Working | Watcom Assembler for MASM-syntax files |
| NASM integration | Working | NASM for NASM-syntax files |
| Compiler flags | Set | C89 mode, small memory model, TSR settings |

### Build Script Features

```bash
./build_macos.sh          # Default release build
./build_macos.sh release  # Release build
./build_macos.sh debug    # Debug build with symbols
./build_macos.sh clean    # Clean build artifacts
./build_macos.sh info     # Show build configuration
```

## Source Code Issues

### C Source Files (75 files)

#### ✅ Fixed
| File | Issue | Fix Applied |
|------|-------|-------------|
| `include/common.h` | MK_FP/FP_SEG/FP_OFF macro conflicts | Added `#ifndef` guards |
| `include/common.h` | `static inline` (C99) | Changed to `static` |
| `include/common.h` | GNU statement expressions | Converted to static functions |
| `include/common.h` | NIC_TYPE_* macro conflicts | Added conditional compilation |
| `include/nic_defs.h` | C99 designated initializers | Converted to C89 positional init |

#### ⚠️ Remaining Issues
- Some headers may have additional C99 features
- Potential duplicate type definitions across headers
- Need full build test to find remaining issues

### Assembly Files (24 files)

#### File Syntax Classification

| Syntax | Files | Assembler |
|--------|-------|-----------|
| NASM | `cpudet.asm`, `tsrldr.asm` | nasm |
| MASM | 22 other files | wasm |

#### ❌ Critical Issues

**1. Mixed Assembler Syntax in Include Files**

`include/tsr_defensive.inc` uses NASM syntax but is included by WASM-assembled files:
- NASM `struc`/`endstruc` vs MASM `STRUC`/`ENDS`
- NASM `.field` labels vs MASM `field` labels
- NASM `resb` vs MASM `db ? dup(?)`
- NASM `%%label` local labels vs MASM `@@label`

**Affected files:**
- `src/asm/tsrcom.asm`
- `src/asm/tsrwrap.asm`
- Any other file including `tsr_defensive.inc`

**2. Missing CPU Directives**

Many WASM files use 386+ instructions without `.386` directive:
```
Error! E002: Invalid instruction with current CPU setting
```

**Affected files:**
- `src/asm/nicirq.asm` (lines 486, 933, 979, 1052, etc.)
- `src/asm/hwsmc.asm` (lines 185, 223)
- Others

**3. Local Label Conflicts**

WASM doesn't support NASM-style `.label` local labels:
```
Error! E299: '.done' is already defined
Error! E299: '.aligned' is already defined
```

**Affected files:**
- `src/asm/nicirq.asm`
- `src/asm/hwsmc.asm`
- `src/asm/pktops.asm`

**4. Segment Definition Issues**

```
Error! E277: Do not mix simplified and full segment definitions
Error! E206: Block nesting error
```

**Affected files:**
- `src/asm/pktapi.asm`
- `src/asm/hwsmc.asm`

**5. NASM-Specific Errors**

`tsrldr.asm`:
```
error: SEG applied to something which is already a segment base
```

## Required Work

### Phase 1: Assembly Compatibility (High Priority)

1. **Create WASM-compatible `tsr_defensive.inc`**
   - Convert NASM struct syntax to MASM
   - Convert local label syntax
   - Convert `resb`/`resw` to MASM equivalents
   - Estimated: ~200 lines to convert

2. **Add `.386` directives to WASM files**
   - Add CPU mode directive at start of each file
   - Or add before sections using 386+ instructions

3. **Fix local label conflicts**
   - Rename `.done` to unique labels like `done_nicirq_xyz`
   - Or use WASM `@@` local label syntax

4. **Fix segment definitions**
   - Ensure consistent use of simplified or full segment definitions
   - Not mixing `.model`/`segment` with `SEGMENT`/`ENDS`

### Phase 2: C Header Cleanup (Medium Priority)

1. Audit all headers for C99 features
2. Remove duplicate type definitions
3. Ensure proper include order

### Phase 3: Full Build Testing (Low Priority)

1. Complete clean build
2. Test on DOS emulator (DOSBox)
3. Verify packet driver functionality

## Build Output Summary (After Initial Fixes)

| Category | Total | Compiled | Failed | Notes |
|----------|-------|----------|--------|-------|
| Loader ASM | 1 | 0 | 1 | NASM SEG syntax issue |
| Hot ASM | 12 | 3 | 9 | pcmisr, flowrt, pktcopy OK |
| Cold ASM | 6 | 0 | 6 | Not attempted yet |
| Hot C | 13 | 0 | 0 | Not attempted yet |
| Cold C | 44 | 0 | 0 | Not attempted yet |

### Successfully Compiled Assembly Files
- `pcmisr.asm` - ✅ OK
- `flowrt.asm` - ✅ OK
- `pktcopy.asm` - ✅ OK

## Environment

```
Host:     macOS Apple Silicon (ARM64)
Target:   DOS 16-bit (8086+ compatible)
Compiler: Open Watcom v2 (wcc)
Assemblers: WASM (MASM-syntax), NASM (NASM-syntax)
```

## Files Modified

| File | Changes |
|------|---------|
| `build_macos.sh` | Created - macOS build script with dual assembler support |
| `include/common.h` | Fixed C99 features (inline), macro guards for MK_FP/FP_SEG/FP_OFF |
| `include/nic_defs.h` | Converted C99 designated initializers to C89 positional init |
| `include/tsr_defensive_wasm.inc` | Created - WASM/MASM compatible version of TSR macros |
| `include/asm_interfaces.inc` | Updated to use tsr_defensive_wasm.inc |
| `src/asm/tsrcom.asm` | Updated to use tsr_defensive_wasm.inc |
| `src/asm/tsrwrap.asm` | Fixed local labels for WASM compatibility, updated include |
| `BUILD_MACOS_STATUS.md` | Created - this status document |

## Next Steps

### Immediate (High Priority)
1. ~~Create MASM-compatible version of `tsr_defensive.inc`~~ ✅ Done
2. Fix remaining local label conflicts (`.done`, `.chain`, etc.) in all ASM files
3. Add `.386` directives where needed for 386+ instructions
4. Fix segment definition issues (simplified vs full segment definitions)

### Short-Term (Medium Priority)
5. Fix NASM SEG syntax in tsrldr.asm
6. Complete all assembly file compilation
7. Begin C compilation testing

### Long-Term
8. Full build and link testing
9. Runtime testing in DOSBox/86Box
10. Verification of packet driver functionality

## Estimated Scope

The assembly codebase requires significant refactoring for cross-platform compilation:
- **24 ASM files** total (22 MASM-style, 2 NASM-style)
- **~90K+ total lines of code**
- Local label naming conflicts across most files
- Mixed segment definition styles in some files

A systematic approach would be needed to convert all files to consistent syntax.
