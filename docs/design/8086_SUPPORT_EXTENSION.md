# 3Com Packet Driver - 8086/8088 CPU Support Extension

## Design Document

This document describes the architectural changes required to extend the 3Com packet driver to support Intel 8086/8088 processors, in addition to the existing 80286+ support.

---

## Executive Summary

The current driver targets 80286+ CPUs but uses a **runtime dispatch architecture** that can be extended to support 8086/8088. The key insight is that 32-bit code exists in the binary but is gated by CPU detection - the same pattern can accommodate 8086-specific paths.

**Scope:**
- Support 8086/8088 and compatible processors (NEC V20/V30)
- 3C509B NIC only (PIO mode, 10 Mbps)
- Simplified boot path (no V86/VDS/XMS/bus mastering)
- Estimated 5-6 branch points for 8086-safe code

**Out of Scope:**
- 3C515-TX support on 8086 (requires 16-bit ISA slot, bus mastering)
- PCI NIC support (PCI didn't exist in 8086 era)

---

## Current Architecture Overview

### Runtime CPU Dispatch Model

The driver already supports multiple CPU tiers via runtime detection:

```
┌─────────────────────────────────────────────────────────┐
│                    Driver Binary                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │
│  │ 8-bit path  │  │ 16-bit path │  │ 32-bit path │      │
│  │ (8086) NEW  │  │ (286)       │  │ (386+)      │      │
│  └─────────────┘  └─────────────┘  └─────────────┘      │
│         ▲               ▲                ▲               │
│         └───────────────┴────────────────┘               │
│              Runtime CPU Detection (Phase 1)             │
└─────────────────────────────────────────────────────────┘
```

### Existing Optimization Flags

```c
// packet_ops.asm:237-246
OPT_NONE   EQU 0    // 8086 - to be implemented
OPT_16BIT  EQU 1    // 286+
OPT_32BIT  EQU 2    // 386+
```

### SMC (Self-Modifying Code) Behavior by CPU

| CPU | SMC Status | Code Path |
|-----|------------|-----------|
| 8086/8088 | Disabled | Static 8086-safe fallbacks |
| 80286 | Disabled | Static 16-bit fallbacks |
| 80386+ | Enabled | Patched optimized paths |

---

## 8086 Hardware Constraints

### CPU Limitations

| Feature | 8086 Status | Impact |
|---------|-------------|--------|
| Registers | 16-bit only (AX, BX, CX, DX, SI, DI, BP, SP) | No EAX/EBX/etc. |
| Address space | 20-bit (1 MB max) | Segment:Offset only |
| Protected mode | Not available | Always real mode |
| V86 mode | Not available | No memory managers |
| CPUID | Not available | Must use alternate detection |

### Instruction Restrictions (8086 vs 80186+)

| Instruction | 80186+ | 8086 Alternative |
|-------------|--------|------------------|
| `PUSHA/POPA` | 1 byte | 7× `PUSH`/`POP` (14 bytes) |
| `PUSH imm` | 3 bytes | `MOV AX, imm` + `PUSH AX` (4 bytes) |
| `SHR AX, 4` | 3 bytes | `MOV CL, 4` + `SHR AX, CL` (4 bytes) |
| `IMUL AX, BX, 10` | 4 bytes | Setup + `MUL` |
| `INS/OUTS` | 1-2 bytes | Loop with `IN`/`OUT` + `STOS`/`LODS` |
| `ENTER/LEAVE` | 4/1 bytes | Manual stack frame |

### Typical 8086 System Configuration

| Component | Typical | Impact on Driver |
|-----------|---------|------------------|
| CPU | 8086/8088 @ 4.77-10 MHz | Slower execution |
| Bus | 8-bit or 16-bit ISA | 3C509B compatible |
| RAM | 256KB - 640KB | Conventional memory only |
| DMA | 8237 controller | Not used (PIO mode) |
| PIC | 8259 | Standard interrupt handling |

---

## Boot Process Modifications for 8086

### Phase Relevance Summary

| Phase | Name | 8086 Status | Notes |
|-------|------|-------------|-------|
| 0 | Entry Validation | ✅ Keep | Unchanged |
| 1 | CPU Detection | ⚠️ Modify | Accept 8086, set OPT_8086 flag |
| 2 | Platform Probe (V86/VDS) | ❌ Skip | V86 requires 386+ |
| 3 | Config Parsing | ✅ Keep | Unchanged |
| 4 | Chipset/Bus Detection | ⚠️ Simplify | ISA only, skip PCI |
| 4.5 | VDS/DMA Refinement | ❌ Skip | VDS requires V86 |
| 5 | Memory Init (Core) | ⚠️ Simplify | Conventional only, skip XMS |
| 6 | 3C509B Detection | ✅ Keep | Primary 8086 NIC |
| 7 | 3C515-TX Detection | ❌ Skip | Requires bus mastering |
| 8 | PCI NIC Detection | ❌ Skip | PCI didn't exist |
| 9 | Memory Init (DMA) | ⚠️ Simplify | Conventional buffers only |
| 9.5 | Bottom-Half Init | ✅ Keep | Unchanged |
| 10 | TSR Relocation | ⚠️ Simplify | Skip UMB attempt |
| 11 | API Installation | ✅ Keep | Unchanged |
| 11.5 | NIC IRQ Binding | ✅ Keep | Unchanged |
| 12 | Enable Interrupts | ✅ Keep | Unchanged |
| 13 | API Activation | ✅ Keep | Unchanged |
| 14 | Complete Boot | ✅ Keep | Unchanged |

### 8086 Boot Flow

```
Phase 0:  Entry Validation         [unchanged]
Phase 1:  CPU Detection            [detect 8086, set flags]
          ├─ if 8086 → set OPT_8086, DMA_POLICY_DIRECT
          └─ skip Phases 2, 4.5, 7, 8
Phase 3:  Config Parsing           [unchanged]
Phase 4:  Bus Detection            [ISA only]
Phase 5:  Memory Init              [conventional only]
Phase 6:  3C509B Detection         [primary NIC for 8086]
Phase 9:  Buffer Allocation        [conventional memory]
Phase 9.5: Bottom-Half Init        [unchanged]
Phase 10: TSR Relocation           [no UMB]
Phases 11-14: API/IRQ/Complete     [unchanged]
```

---

## Code Modifications Required

### 1. Remove 286+ Minimum CPU Check

**File:** `src/loader/cpu_detect.c:328-332`

```c
// CURRENT - rejects 8086
if (g_cpu_info.cpu_type < CPU_TYPE_80286) {
    LOG_ERROR("CPU below minimum requirement (80286+)");
    return ERROR_CPU_UNKNOWN;
}

// MODIFIED - accept 8086
if (g_cpu_info.cpu_type < CPU_TYPE_8086) {
    LOG_ERROR("CPU detection failed");
    return ERROR_CPU_UNKNOWN;
}
```

### 2. Add OPT_8086 Flag and Detection

**File:** `include/cpu_detect.h` (add constant)

```asm
OPT_8086   EQU 0    ; 8086/8088 - no 186+ instructions
OPT_16BIT  EQU 1    ; 286+ - PUSHA/POPA, shifts, INS/OUTS
OPT_32BIT  EQU 2    ; 386+ - 32-bit registers
```

**File:** `src/c/smc_patches.c` (extend CPU handling)

```c
if (cpu_info.cpu_type >= CPU_TYPE_80386) {
    // Enable SMC, 32-bit paths
} else if (cpu_info.cpu_type >= CPU_TYPE_80286) {
    // Disable SMC, use 16-bit static paths
} else {
    // 8086: Disable SMC, use 8086-safe static paths
    g_cpu_opt_level = OPT_8086;
}
```

### 3. Branch Points for 8086-Safe Code

#### Branch Point 1: Register Save/Restore (5 locations)

**Files:** `tsr_common.asm`, `packet_ops.asm`, `cache_ops.asm`

```asm
; CURRENT (286+)
pusha
; ... code ...
popa

; 8086-SAFE ALTERNATIVE
push ax
push bx
push cx
push dx
push si
push di
push bp
; ... code ...
pop bp
pop di
pop si
pop dx
pop cx
pop bx
pop ax
```

**Locations:**
| File | Lines | Function | Path |
|------|-------|----------|------|
| `tsr_common.asm` | 266-277 | `int28_handler` | HOT |
| `tsr_common.asm` | 581-615 | deferred handler | HOT |
| `packet_ops.asm` | 1465-1471 | `copy_286` | HOT |
| `cache_ops.asm` | 162-214 | `cache_flush_range` | COLD |
| `promisc.asm` | 424-461 | ISR (PUSHAD→PUSHA→explicit) | HOT |

#### Branch Point 2: String I/O (REP INS/OUTS)

**Files:** `direct_pio.asm`, `hardware_smc.asm`, `nic_irq_smc.asm`

```asm
; CURRENT (186+)
rep insw

; 8086-SAFE ALTERNATIVE
.pio_loop:
    in ax, dx
    stosw
    loop .pio_loop
```

**Locations:**
| File | Lines | Function | Path |
|------|-------|----------|------|
| `direct_pio.asm` | 129, 192, 247, 266, 361, 440 | packet I/O | HOT |
| `hardware_smc.asm` | 123, 159 | block read/write | HOT |
| `nic_irq_smc.asm` | 335-342 | ISR packet read | HOT |

#### Branch Point 3: Shift with Immediate > 1

**Files:** Various

```asm
; CURRENT (186+)
shr ax, 4

; 8086-SAFE ALTERNATIVE
mov cl, 4
shr ax, cl
```

**Locations:** `packet_ops.asm`, `main.asm`, `flow_routing.asm`, `nic_irq_smc.asm` (~15 instances)

#### Branch Point 4: PUSH Immediate

**Files:** `smc_patches.asm`

```asm
; CURRENT (186+)
push 2

; 8086-SAFE ALTERNATIVE
mov ax, 2
push ax
```

**Locations:** `smc_patches.asm:339,397` (COLD path - init only)

### 4. Conditional Boot Phase Execution

**File:** `src/c/main.c`

```c
// After Phase 1 CPU detection
if (g_cpu_info.cpu_type < CPU_TYPE_80286) {
    // 8086 path - skip V86/VDS/XMS phases
    g_boot_flags |= BOOT_FLAG_8086_MODE;
    driver_state.dma_policy = DMA_POLICY_DIRECT;
    // Skip to Phase 3
}

// In Phase 4
if (!(g_boot_flags & BOOT_FLAG_8086_MODE)) {
    detect_pci_bus();  // Skip on 8086
}

// In Phases 6-8
if (g_boot_flags & BOOT_FLAG_8086_MODE) {
    // Only detect 3C509B, skip 3C515-TX and PCI NICs
    detect_3c509b_only();
}
```

---

## Implementation Strategy

### Approach: Conditional Assembly with Runtime Dispatch

Use `%ifdef` for build-time 8086 inclusion, combined with runtime CPU checks:

```asm
; Example: packet_ops.asm
packet_copy_entry:
    mov bl, [current_cpu_opt]
    cmp bl, OPT_8086
    je .copy_8086
    cmp bl, OPT_16BIT
    je .copy_16bit
    jmp .copy_32bit

.copy_8086:
    ; 8086-safe code (no INS/OUTS, no PUSHA)
    call packet_copy_8086
    ret

.copy_16bit:
    ; 286 code (PUSHA, REP INSW)
    call packet_copy_286
    ret

.copy_32bit:
    ; 386+ code (32-bit registers)
    call packet_copy_386
    ret
```

### File Modification Summary

| File | Changes | Effort |
|------|---------|--------|
| `src/loader/cpu_detect.c` | Accept 8086, add OPT_8086 | Low |
| `include/cpu_detect.h` | Add CPU type constants | Low |
| `src/c/main.c` | Conditional phase execution | Medium |
| `src/c/smc_patches.c` | Handle 8086 in SMC init | Low |
| `src/asm/tsr_common.asm` | Add 8086 register save paths | Medium |
| `src/asm/packet_ops.asm` | Add 8086 copy routines | Medium |
| `src/asm/direct_pio.asm` | Add loop-based I/O | Medium |
| `src/asm/hardware_smc.asm` | Add 8086 block I/O | Medium |
| `src/asm/nic_irq_smc.asm` | Add 8086 ISR path | Medium |
| `src/asm/promisc.asm` | Fix PUSHAD→explicit push | Low |
| `Makefile` | Add 8086 build target (optional) | Low |

---

## Hardware Support Matrix

### NIC Support by CPU

| CPU | 3C509B (PIO) | 3C515-TX (DMA) | PCI NICs |
|-----|--------------|----------------|----------|
| 8086/8088 | ✅ Yes | ❌ No | ❌ No |
| 80286 | ✅ Yes | ⚠️ With bus master | ❌ No |
| 80386+ | ✅ Yes | ✅ Yes | ✅ Yes |

### 3C515-TX on 8086 - Why Not Supported

| Issue | Detail |
|-------|--------|
| Slot type | 3C515-TX is 16-bit ISA only |
| Bus mastering | Requires DMA controller support |
| Register layout | ISA-aliased registers at +0x400 |
| Performance | PIO mode would yield ~5-10 Mbps (defeats purpose) |
| Code complexity | No existing PIO path, would need full implementation |

**Recommendation:** Use 3C509B for 8086 systems. It's the appropriate NIC for the era and performance level.

---

## Estimated Impact

### TSR Size

| Build | Estimated Size | Notes |
|-------|---------------|-------|
| Full (386+ optimized) | <6 KB | Current driver |
| 286 fallback | <5 KB | No SMC patches |
| 8086 minimal | <4 KB | No XMS, VDS, PCI, 3C515 |

### Performance Expectations

| CPU | NIC | Throughput | CPU Usage |
|-----|-----|------------|-----------|
| 8086 @ 4.77 MHz | 3C509B | ~200-400 Kbps | High |
| 8086 @ 10 MHz | 3C509B | ~500-800 Kbps | High |
| 286 @ 12 MHz | 3C509B | ~1-2 Mbps | Moderate |
| 386 @ 33 MHz | 3C509B | ~5-8 Mbps | Low |
| 386 @ 33 MHz | 3C515-TX | ~40-60 Mbps | Low (DMA) |

---

## Testing Requirements

### Hardware Test Matrix

| System | CPU | Bus | NIC | Priority |
|--------|-----|-----|-----|----------|
| IBM PC/XT | 8088 | 8-bit ISA | 3C509B | High |
| IBM PC/AT clone | 286 | 16-bit ISA | 3C509B | High |
| 386 system | 386 | ISA | 3C509B | Medium |
| 386 system | 386 | ISA | 3C515-TX | Medium |

### Emulator Testing

| Emulator | 8086 Support | Recommended |
|----------|--------------|-------------|
| 86Box | ✅ Excellent | Primary test platform |
| PCem | ✅ Good | Secondary |
| DOSBox-X | ⚠️ Limited | Basic testing only |
| QEMU | ❌ 386+ only | Not suitable |

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| 8086 instruction incompatibility | Medium | High | Thorough code audit complete |
| Performance too slow for practical use | Medium | Medium | Document minimum requirements |
| Interrupt latency issues on 8086 | Low | Medium | Optimize ISR paths |
| Memory constraints on 640KB systems | Low | Low | Minimal TSR footprint |

---

## Summary

Adding 8086/8088 support is **feasible** with the following scope:

1. **~5-6 branch points** for 8086-safe code alternatives
2. **Simplified boot path** skipping V86/VDS/XMS/PCI phases
3. **3C509B only** - appropriate NIC for 8086-era systems
4. **Minimal code duplication** - leverages existing dispatch architecture

The existing runtime dispatch model (`OPT_16BIT`/`OPT_32BIT`) naturally extends to include `OPT_8086`, making this a clean architectural addition rather than a major rewrite.

---
---

# Appendix: Original Codebase Analysis

## Project Overview

This is a **DOS packet driver** for 3Com NICs (3C515-TX 100Mbps and 3C509B 10Mbps), written in C and Assembly targeting DOS 2.0+ on Intel 80286+ systems. It implements a TSR (Terminate and Stay Resident) architecture.

## Boot Process Stages (14 Phases)

The driver has a sophisticated multi-phase initialization sequence with full error recovery:

### Phase 0: Entry Validation
- **Location:** `src/c/main.c:628-638`
- Checks for existing driver installation (INT 0x60)
- Verifies DOS compatibility and available memory
- Validates command-line arguments

### Phase 1: CPU Detection
- **Location:** `src/c/main.c:641-651`
- Detects CPU type (requires 286+)
- Identifies vendor (Intel, AMD, Cyrix, etc.)
- Detects features: CPUID, FPU, 32-bit ops, V86 mode

### Phase 2: Early Platform Probe
- **Location:** `src/c/main.c:654-676`
- Detects real mode vs V86 mode
- Checks VDS (Virtual DMA Services) availability
- Determines DMA policy:
  - `DMA_POLICY_DIRECT` - Real mode, direct physical access
  - `DMA_POLICY_COMMONBUF` - V86 + VDS, use bounce buffers
  - `DMA_POLICY_FORBID` - V86 without VDS, no DMA

### Phase 3: Configuration Parsing
- **Location:** `src/c/main.c:252-260` (in `driver_init()`)
- Parses CONFIG.SYS device line parameters
- `/IO1=`, `/IO2=`, `/IRQ1=`, `/IRQ2=`, `/SPEED=`, `/BUSMASTER=`, `/LOG=`, `/ROUTE=`

### Phase 4: Chipset and Bus Detection
- **Location:** `src/c/main.c:267-311`
- Detects system chipset and bus type (MCA, EISA, PCI, ISA)
- Tests bus-master capability for 286 systems

### Phase 4.5: VDS Detection and DMA Policy Refinement
- **Location:** `src/c/main.c:318-362`
- Initializes VDS support
- Performs DMA cache coherency tests
- Refines DMA policy based on test results

### Phase 5: Memory Subsystem Init (Core)
- **Location:** `src/c/main.c:364-372`
- Initializes core memory management framework
- Sets up memory pool infrastructure

### Phases 6-8: Hardware Detection
- **Location:** `src/c/main.c:374-382`, `src/c/init.c:112-228`
- Phase 6: 3C509B detection (PIO-based, 10 Mbps)
- Phase 7: 3C515-TX detection (bus-master, 100 Mbps)
- Phase 8: PCI NIC detection (Vortex, Boomerang, Cyclone, Tornado)
- Validates NICs against configuration
- At least 1 NIC must be detected

### Phase 9: Memory Subsystem Init (DMA Buffers)
- **Location:** `src/c/main.c:397-408`
- Allocates DMA-safe buffers based on DMA policy
- Creates packet RX/TX buffer pools

### Phase 9.5: Bottom-Half Processing Init
- **Location:** `src/c/main.c:410-415`
- Initializes deferred packet processing

### Phase 10: TSR Relocation
- **Location:** `src/c/main.c:418-430`
- Relocates driver to final memory location (UMB if available)
- **Must occur BEFORE interrupt vector installation**

### Phase 11: Packet Driver API Installation
- **Location:** `src/c/main.c:433-445`
- Installs minimal API hooks (INT 0x60)
- PIC remains masked at this point

### Phase 11.5: NIC IRQ Binding
- **Location:** `src/c/main.c:447-455`
- Binds NIC IRQ to ISR handler
- Installs hardware interrupt vector

### Phase 12: Enable Interrupts
- **Location:** `src/c/main.c:461-469`
- Unmasks PIC for NIC interrupt
- Starts actual packet reception

### Phase 13: Final API Activation
- **Location:** `src/c/main.c:472-483`
- Activates full Packet Driver API
- Enables all driver functions

### Phase 14: Complete Boot
- **Location:** `src/c/main.c:494-515`
- Final validation and status reporting
- Prints boot summary

## Assembly Entry Point

**`src/asm/main.asm:116` - `driver_entry`**

The assembly layer handles:
1. Check if already loaded
2. Memory optimization init
3. UMB loading attempt
4. Defensive programming init
5. CPU detection (ASM routines)
6. Hardware init
7. PnP detection
8. Packet API init
9. NIC IRQ init
10. Install interrupt vectors (INT 60h, INT 28h, hardware IRQs)
11. TSR installation via DOS INT 21h/31h

## Critical Ordering Dependencies

1. Entry Validation → CPU Detection (need valid environment)
2. CPU Detection → V86 Check (need 386+ for EFLAGS.VM)
3. Platform Probe → Memory Init (DMA policy determines allocation)
4. Core Memory → Hardware (buffer infrastructure needed)
5. Hardware → DMA Buffers (need NIC capabilities)
6. TSR Relocation → Vector Install (vectors must point to final location)
7. Vector Install → Interrupt Enable (handlers must be ready)
8. Interrupt Enable → API Activation (ISR must be operational)

## Error Recovery (Unwind System)

- **Header:** `include/unwind.h`
- Each phase has a corresponding unwind phase for cleanup
- `unwind_execute(error_code, msg)` cleans up in reverse order
- Ensures safe rollback on initialization failure

## Key Files

| File | Purpose |
|------|---------|
| `src/c/main.c` | Main entry point, phase orchestration |
| `src/c/init.c` | Hardware detection logic |
| `src/asm/main.asm` | Assembly entry, TSR installation |
| `include/unwind.h` | Error recovery definitions |
| `src/c/config.c` | CONFIG.SYS parameter parsing |
| `src/c/hardware.c` | Hardware abstraction |

## Exit Codes

- 0: Success (TSR installed)
- 1: General error
- 2: Driver already loaded
- 3: CPU not supported
- 4: Hardware init failed
- 5: Memory allocation failed
