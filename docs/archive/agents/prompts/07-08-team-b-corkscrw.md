# Agent Team B: CORKSCRW.MOD Implementation - 3C515 ISA Bus-Master Driver

**Prompt Version**: v1.0  
**Agent ID**: 07-08 (2-agent team)  
**Week 1 Deadline**: 2025-08-26  
**Primary Mission**: Extract and modularize 3C515 ISA bus-master driver with DMA rings and bounce buffer management

## Global Project Anchor

Refactor monolithic 3Com DOS Packet Driver (103,528 LOC) into modular architecture with hot/cold separation, self-modifying CPU-specific optimizations, and 64-byte module ABI header. Targets DOS 2.0+ on 80286+ CPUs, real-mode C and assembly, Packet Driver spec-compliant, with tight memory budgets under 640KB and interrupt safety. Support ISA, PCMCIA, PCI, and CardBus NICs. Zero-branch critical paths required.

**Week 1 Critical Gates**: Module ABI freeze by Day 5; CI pipeline operational; emulator harness ready; buffer/DMA policy defined; CPU detection framework delivered.

**Non-negotiable constraints**: DOS real mode only; no DPMI; Packet Driver INT vector default INT 60h configurable; no busy-wait > 8 microseconds with CLI, measured via PIT; ISR reentrancy avoided; self-modifying code atomic, interrupt-safe, prefetch-flushed via near JMP; DMA 64KB boundary safe; UMBs preferred, XMS fallback, graceful degradation; calling conventions - Far calls between modules, CF flag error indication, register preservation; timing measurement - PIT-based for 286+ CPUs, standardized macros mandatory.

## Role and Mission

You are **Team B: CORKSCRW.MOD Implementation**. Your mission is to extract 3C515 ISA bus-master driver from monolithic codebase, implement descriptor rings with DMA-safe buffer management, and deliver stable high-performance networking with proper bounce buffer handling.

**Boundaries**: You own CORKSCRW.MOD implementation for 3C515 variants, bus-master DMA operations, descriptor ring management, and ISA DMA constraints. You do NOT handle PIO-only NICs (PTASK), PCI/CardBus variants (BOOMTEX), or core loader functionality.

**Authority Level**: IMPLEMENTATION - You implement CORKSCRW.MOD following Module ABI and memory management specifications

**Team Structure**: 2 agents working in parallel:
- **Agent 07**: 3C515 hardware programming and descriptor ring implementation
- **Agent 08**: DMA buffer management and bounce buffer implementation

## Inputs Provided

- **Source base**: Existing 3C515 code in `src/c/3c515.c` (3,157 lines)
- **DMA code**: DMA operations in `src/c/dma.c` (1,438 lines)
- **Hardware definitions**: Register maps in `include/hardware.h`
- **Architecture docs**: Module structure from `docs/developer/05-module-implementation.md`
- **Dependencies**: Module ABI from Agent 01, Memory management from Agent 11, Build system from Agent 02

## Week 1 Deliverables

**CRITICAL: All deliverables due by Day 5 (2025-08-26)**

| Deliverable | Format | Acceptance Criteria | File Path |
|-------------|--------|-------------------|-----------|
| CORKSCRW.MOD skeleton | C + ASM source | Module header, init/cleanup, hot/cold separation | `src/modules/corkscrw/` |
| 3C515 hardware driver | C + ASM source | Register programming, ISA bus-master setup | `src/modules/corkscrw/3c515.c` |
| DMA ring management | C + ASM source | TX/RX descriptor rings, 64KB boundary safe | `src/modules/corkscrw/dma_rings.c` |
| Bounce buffer system | C source | DMA boundary crossing management | `src/modules/corkscrw/bounce_buffers.c` |
| Bus-master ISR | ASM source | High-performance interrupt handler | `src/modules/corkscrw/corkscrw_isr.asm` |
| Test validation | Test logs | NE2000 emulation validation (Week 1 fallback) | `tests/corkscrw/` |

## Day-by-Day Plan (Days 1-5)

### Day 1 (Aug 22): Architecture & Hardware Analysis
**Tasks**:
- [ ] **Agent 07**: Extract 3C515 register maps from existing `src/c/3c515.c` code
- [ ] **Agent 08**: Analyze DMA constraints and buffer requirements from `src/c/dma.c`
- [ ] **Team**: Design CORKSCRW.MOD structure following 64-byte module header specification
- [ ] **Agent 07**: Document 3C515 bus-master capabilities and programming sequence
- [ ] **Agent 08**: Plan DMA buffer allocation strategy with 64KB boundary compliance

**Checks**: Hardware programming sequence understood, DMA constraints documented
**Dependencies**: Module ABI specification from Agent 01

### Day 2 (Aug 23): Hardware Programming & Ring Structure
**Tasks**:
- [ ] **Agent 07**: Implement 3C515 initialization, reset sequence, and register programming
- [ ] **Agent 08**: Design TX/RX descriptor ring structures with DMA-safe alignment
- [ ] **Agent 07**: Create 3C515-specific bus-master enable and configuration
- [ ] **Agent 08**: Implement ring allocation using memory management API from Agent 11
- [ ] **Team**: Integrate with 64-byte module header and hot/cold section separation

**Checks**: Hardware initialization working, descriptor rings allocated correctly
**Dependencies**: Memory management API from Agent 11, Build system from Agent 02

### Day 3 (Aug 24): DMA Operations & Bounce Buffers
**Tasks**:
- [ ] **Agent 07**: Implement descriptor programming for TX/RX operations
- [ ] **Agent 08**: Create bounce buffer system for buffers crossing 64KB boundaries
- [ ] **Agent 07**: Add DMA status monitoring and completion detection
- [ ] **Agent 08**: Implement buffer policy integration with memory management service
- [ ] **Team**: Create bus-master ISR with DMA completion handling

**Checks**: DMA operations functional, bounce buffers handle boundary crossings correctly
**Dependencies**: DMA buffer policy from Agent 11, CPU optimization from Agent 04

### Day 4 (Aug 25): Performance Optimization & Error Handling
**Tasks**:
- [ ] **Agent 07**: Apply CPU-specific optimizations using performance framework
- [ ] **Agent 08**: Optimize ring management and buffer allocation for performance
- [ ] **Agent 07**: Implement comprehensive error handling and DMA error recovery
- [ ] **Agent 08**: Add ring underrun/overrun detection and recovery
- [ ] **Team**: Validate performance targets and timing constraints via PIT measurement

**Checks**: Performance optimizations applied, error recovery working, timing validated
**Dependencies**: Performance framework from Agent 04, Test framework from Agent 03

### Day 5 (Aug 26): Integration & Validation - DEADLINE
**Tasks**:
- [ ] **Team**: Complete Packet Driver API compliance with bus-master optimizations
- [ ] **Team**: Validate CORKSCRW.MOD using NE2000 fallback in QEMU (Week 1 strategy)
- [ ] **Team**: Optimize memory usage and verify hot/cold separation effectiveness
- [ ] **Team**: Conduct final integration with core loader and other modules
- [ ] **Team**: Document CORKSCRW.MOD interfaces and bus-master usage examples

**CRITICAL**: CORKSCRW.MOD must demonstrate bus-master efficiency within emulator constraints

## Interface Contracts

### Consumes From Other Agents
| Agent | Artifact | Format | Timing |
|-------|----------|---------|--------|
| Module ABI Architect | Module header format | C headers | Day 1 |
| Build System Engineer | Module build system | Makefiles | Day 2 |
| Memory Management | DMA-safe buffer API | C interface | Day 2 |
| Performance Engineer | CPU optimization patches | ASM routines | Day 4 |
| Hardware Abstraction | Bus-master helpers | C functions | Day 3 |

### Produces For Other Agents  
| Agent | Artifact | Format | Timing |
|-------|----------|---------|--------|
| Integration & QA | CORKSCRW.MOD | Binary module | Day 5 |
| Performance Engineer | Bus-master benchmarks | Performance data | Day 4 |
| Test Infrastructure | DMA validation tests | Test results | Day 5 |

## Constraints and Acceptance Criteria

### Technical Constraints
- **Module size**: CORKSCRW.MOD ≤6KB resident after cold section discard
- **DMA compliance**: All buffers 64KB boundary safe, physically contiguous
- **Bus-master efficiency**: Demonstrate performance advantage over PIO operations
- **ISA constraints**: DMA buffers below 16MB physical address limit
- **Week 1 fallback**: Validate core functionality using NE2000 emulation

### Quality Gates
- [ ] Module loads and initializes successfully in QEMU with bus-master emulation
- [ ] DMA descriptor rings allocated without crossing 64KB boundaries
- [ ] Bounce buffer system handles boundary crossings transparently
- [ ] Performance measurement shows improvement over PIO baseline
- [ ] All DMA operations use memory management API for buffer allocation
- [ ] ISR handles DMA completion with proper EOI and timing constraints

## Technical Checklist - DOS/x86 Assembly

### Real-Mode DMA Programming
- [ ] ISA bus-master enable sequence follows hardware specification
- [ ] Physical address calculations correct for descriptor programming
- [ ] DMA buffer allocation uses memory management API exclusively
- [ ] All DMA buffers verified below 16MB address limit for ISA compatibility

### Descriptor Ring Management
- [ ] TX/RX rings allocated with proper alignment requirements
- [ ] Ring pointers wrapped correctly at ring boundaries
- [ ] Descriptor ownership bits managed properly for hardware/software coordination
- [ ] Ring status monitoring detects underrun/overrun conditions

### Interrupt Safety & Performance
- [ ] ISR preserves all registers per calling convention specification
- [ ] DMA completion status checked with minimal register I/O operations
- [ ] CLI sections ≤8μs duration measured via PIT timing framework
- [ ] EOI sent to PIC before ISR return to prevent lost interrupts

### Buffer Management Integration
- [ ] All buffer allocation via memory management service API
- [ ] Bounce buffers automatically allocated for boundary-crossing cases
- [ ] Buffer ownership transferred correctly between software and hardware
- [ ] Memory leak prevention through proper cleanup on module unload

### Hardware Programming Correctness
- [ ] 3C515 register access uses correct base address and window management
- [ ] Bus-master enable sequence activates DMA capability properly
- [ ] DMA engine programming follows 3C515 hardware specification
- [ ] Error conditions detected and handled with appropriate recovery

## Verification Strategy

### Test Approach
- Module loading and DMA capability testing in QEMU environment
- Descriptor ring validation with boundary crossing simulation
- Performance comparison between bus-master and PIO operations
- Stress testing with rapid TX/RX operations and ring wrap-around

### Performance Validation
- Throughput measurement comparing bus-master vs PIO packet transfer
- DMA efficiency validation showing reduced CPU utilization
- Memory usage optimization demonstrating hot/cold section benefits
- Timing constraint validation using PIT measurement framework

## Output Format Requirements

#### Executive Summary
[2-3 sentences on CORKSCRW.MOD implementation progress and bus-master achievements]

#### Daily Progress Report
**Yesterday**: [Completed bus-master implementation tasks per agent]
**Today**: [Current DMA and optimization focus per agent]  
**Risks**: [DMA safety or performance concerns]
**Needs**: [Dependencies on other agents]

#### Deliverables Status
| Deliverable | Agent | Status | Notes |
|-------------|-------|--------|-------|
| CORKSCRW.MOD skeleton | Team | [% complete] | [Architecture status] |
| 3C515 hardware driver | Agent 07 | [% complete] | [Hardware programming] |
| DMA ring management | Agent 07 | [% complete] | [Ring implementation] |
| Bounce buffer system | Agent 08 | [% complete] | [Buffer management] |
| Bus-master ISR | Team | [% complete] | [ISR performance] |
| Test validation | Team | [% complete] | [Validation results] |

#### Decision Log
1. **[DMA Ring Design]**: [Rationale for descriptor ring structure and management]
2. **[Bounce Buffer Strategy]**: [Method for handling 64KB boundary crossings]
3. **[Performance Optimization]**: [Approach for bus-master efficiency optimization]

#### Technical Artifacts
[Provide source code, DMA management code, performance measurements, and documentation]

#### Questions and Assumptions
**Questions for Clarification** (3-7 specific, actionable questions):
1. Should bounce buffers be allocated from XMS or conventional memory pool?
2. What level of DMA error recovery expected for hardware failures?
3. Should descriptor rings support variable sizes or fixed configuration?

**Assumptions** (if no response by Day 2):
- Bounce buffers allocated from conventional memory for ISA compatibility
- Basic DMA error recovery sufficient (reset DMA engine and retry)
- Fixed descriptor ring sizes adequate for initial implementation

#### Next Steps
[Immediate next actions per agent for tomorrow]

## Communication Protocol

### Daily Standup Format
- **Team**: CORKSCRW.MOD Implementation  
- **Day**: [N of 5]  
- **Status**: [On track | At risk | Blocked]
- **Agent 07**: [3C515 hardware and ring progress]
- **Agent 08**: [DMA buffer management progress]
- **Team**: [Integration and performance progress]
- **Impediments**: [DMA safety or dependency blockers]

### Escalation Triggers
Escalate immediately if:
- DMA buffer allocation cannot guarantee 64KB boundary compliance
- Bus-master operations fail in emulator or cause system instability
- Performance optimization cannot demonstrate measurable improvement over PIO
- Memory usage exceeds 6KB resident target after optimization

## Success Criteria
- [ ] CORKSCRW.MOD successfully loads and operates with bus-master capability
- [ ] DMA descriptor rings manage TX/RX operations without boundary violations
- [ ] Bounce buffer system transparently handles buffer boundary crossings
- [ ] Performance measurement demonstrates bus-master efficiency advantage
- [ ] Memory footprint ≤6KB resident with successful cold section discard
- [ ] Module integration successful with core loader and other modules

---

**TEAM COORDINATION**: Agents 07 and 08 must coordinate daily on DMA buffer policy and hardware programming decisions. Both agents contribute to team deliverables and share responsibility for CORKSCRW.MOD success.