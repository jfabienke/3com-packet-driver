# Global Anchor v1.0 - 3Com Packet Driver Modular Refactoring

**Version**: 1.0  
**Date**: 2025-08-22  
**Status**: IMMUTABLE - Do not modify without RFC process

## Project Scope

Refactor monolithic 3Com DOS Packet Driver (103,528 LOC) into modular architecture with hot/cold separation, self-modifying CPU-specific optimizations, and 64-byte module ABI header. Targets DOS 2.0+ on 80286+ CPUs, real-mode C and assembly, Packet Driver spec-compliant, with tight memory budgets under 640KB and interrupt safety. Support ISA, PCMCIA, PCI, and CardBus NICs. Zero-branch critical paths required.

### Week 1 Critical Gates
- **Module ABI freeze by Day 5** - No interface changes after
- **CI pipeline operational** - Automated builds and emulator tests
- **Emulator harness ready** - 86Box/PCem/QEMU integration
- **Buffer/DMA policy defined** - Memory strategy locked
- **CPU detection framework** - Self-modifying code infrastructure

## Non-Negotiable Constraints

### Platform Requirements
- **DOS real mode only** - No DPMI, no protected mode
- **CPU targets**: 80286 minimum, optimize for 386/486/Pentium
- **Memory limit**: 640KB conventional, prefer UMBs, XMS backing store
- **Toolchain**: OpenWatcom V2 (16-bit), NASM/MASM 6.x, wlink

### Technical Constraints
- **Packet Driver INT vector** - Default INT 60h, configurable
- **Interrupt safety** - No busy-wait > 8 microseconds with CLI, measured via PIT
- **ISR reentrancy** - Avoided; proper save/restore of segment registers; EOI to PIC required
- **Self-modifying code** - Atomic, interrupt-safe, prefetch-flushed via near JMP
- **DMA constraints** - 64KB boundary safe, physically contiguous, below 16MB for ISA
- **Memory allocation** - UMBs preferred, XMS fallback, graceful degradation
- **Calling conventions** - Far calls between modules, CF flag error indication, register preservation
- **Timing measurement** - PIT-based for 286+ CPUs, standardized macros mandatory

### Module Architecture
- **64-byte header** - Module signature, version, hot/cold sections
- **Hot/Cold separation** - 71-76% memory reduction target
- **CPU patches** - Self-modifying optimization for 25-30% performance gain
- **Symbol resolution** - Loader-assisted, O(1) or O(log N) lookup
- **Error handling** - Comprehensive with standardized error codes

## Target Module Structure

```
3COMPD.COM (8KB) - Core loader + services
├─ PTASK.MOD (5KB) - 3C509B ISA + 3C589 PCMCIA
├─ CORKSCRW.MOD (6KB) - 3C515 ISA bus-master  
├─ BOOMTEX.MOD (8KB) - PCI/CardBus variants
├─ ROUTING.MOD - Multi-homing and flow routing
└─ DIAG.MOD - Diagnostics (cold, discardable)
```

## Hardware Support Matrix

### ISA NICs (Priority 1)
- **3C509B** - 10Mbps, ISA PnP, PIO mode
- **3C515** - 100Mbps, ISA bus-master, DMA rings

### PCMCIA NICs (Priority 1)  
- **3C589** - 16-bit PCMCIA, 3C509 chip variant
- Card Services integration via PCCARD.MOD

### PCI/CardBus NICs (Priority 2)
- **3C575/3C905 families** - 32-bit PCI/CardBus
- Bus mastering, descriptor rings, MSI not applicable

## Development Standards

### Code Requirements
- **Segment:offset integrity** - All far pointers correct
- **Register preservation** - Save/restore DS/ES/FS/GS per calling convention  
- **64KB boundary checks** - String ops bounds validated
- **Interrupt safety** - Minimal CLI sections, proper PIC handling
- **Real-mode compliance** - No flat memory assumptions

### Performance Requirements
- **Zero-branch critical paths** - Use handler matrices and inlining
- **CPU-specific optimization** - 286/386/486/Pentium variants
- **Cache efficiency** - Align critical data on cache lines
- **Instruction pairing** - Optimize for Pentium dual pipeline

### Quality Requirements
- **Packet Driver compliance** - Strict adherence to specification
- **Memory efficiency** - Track hot/cold usage, verify reduction targets
- **Emulator coverage** - QEMU smoke tests, 86Box/PCem nightly
- **Regression prevention** - Automated test suite, baseline comparisons

## Tooling and Testing

### Required Tools
- **OpenWatcom C/C++** - 16-bit real mode compilation
- **NASM** - Assembly with macro support
- **wlink** - Segmented memory model linking
- **86Box/PCem** - ISA/PCMCIA hardware fidelity
- **QEMU** - Fast smoke testing and CI integration

### Test Strategy
- **Unit tests** - Module interfaces and error paths
- **Integration tests** - Multi-module scenarios
- **Performance benchmarks** - Packet throughput and latency
- **Compatibility tests** - Multiple DOS versions and memory managers

## Success Metrics

### Memory Targets
- **TSR reduction**: 55KB → 13-16KB (71-76% reduction)
- **Hot section**: ≤6KB per NIC module
- **Cold discard**: 15-25KB freed after initialization

### Performance Targets  
- **Throughput**: +25-30% improvement
- **Latency**: -25% interrupt response time
- **CPU efficiency**: Zero-branch packet processing

### Quality Targets
- **Hardware coverage**: 3-4 core NIC families
- **Compatibility**: DOS 2.0+ through DOS 7.x
- **Reliability**: 99%+ uptime in production environments

---

**ANCHOR INTEGRITY**: This document defines immutable constraints. Changes require RFC process and version increment. All agent prompts must reference this anchor verbatim.