# Architecture Documentation

This directory contains comprehensive architecture documentation for the 3Com Enterprise DOS Packet Driver, covering design principles, technical specifications, and implementation details.

## Document Organization

### Core Architecture (00-09)
- **01-requirements.md** - System requirements and capabilities
- **02-design.md** - High-level design principles and philosophy
- **03-overview.md** - Current unified driver architecture overview
- **04-roadmap.md** - Implementation roadmap and future enhancements
- **05-unified-architecture.md** - Detailed unified driver architecture
- **06-vtable-hal.md** - Hardware abstraction layer vtable specification
- **07-boot-sequence.md** - Driver boot and initialization sequence

### Memory & Performance (10-19)
- **10-memory-model.md** - Three-tier memory management system
- **11-performance-optimizations.md** - Self-modifying code and optimization techniques

### Cache Management (20-29)
- **20-cache-coherency.md** - Four-tier cache coherency management
- **21-cache-management-design.md** - Cache management implementation details
- **22-runtime-coherency-testing.md** - Runtime hardware coherency validation

### Implementation Details (30-39)
- **30-performance-analysis.md** - Performance characteristics and benchmarks
- **31-cpu-detection.md** - CPU detection and optimization strategies
- **32-chipset-database.md** - Chipset compatibility and workarounds
- **33-rx-copybreak.md** - RX copybreak memory optimization

### DOS Environment (40-49)
- **40-dos-environment.md** - DOS-specific programming complexities
- **41-tsr-implementation.md** - TSR survival and defensive programming

### Reference (90-99)
- **90-references.md** - Technical references and specifications

## Current Architecture Status (Canonical)

The driver uses a **unified .EXE architecture** with:
- Single executable with hot/cold code separation
- ≈6.9 KB resident memory footprint after initialization (map-enforced)
- HAL vtable-based hardware abstraction
- Support for 3C509B (ISA) and 3C515-TX (PCI) NICs
- Three-tier memory management (Conventional/UMB/XMS)

## Quick Reference

For understanding the current implementation:
1. Start with **03-overview.md** for the big picture
2. Review **05-unified-architecture.md** for detailed design
3. Check **06-vtable-hal.md** for hardware abstraction
4. Consult **10-memory-model.md** for memory management

For specific topics:
- Performance: See **11-performance-optimizations.md** and **30-performance-analysis.md**
- Hardware support: See **31-cpu-detection.md** and **32-chipset-database.md**
- DOS specifics: See **40-dos-environment.md** and **41-tsr-implementation.md**
