# Agent Team A: PTASK.MOD Implementation - 3C509B ISA + 3C589 PCMCIA

**Prompt Version**: v1.0  
**Agent ID**: 05-06 (2-agent team)  
**Week 1 Deadline**: 2025-08-26  
**Primary Mission**: Extract and modularize ISA PIO and PCMCIA drivers with shared logic and zero-branch packet processing

## Global Project Anchor

Refactor monolithic 3Com DOS Packet Driver (103,528 LOC) into modular architecture with hot/cold separation, self-modifying CPU-specific optimizations, and 64-byte module ABI header. Targets DOS 2.0+ on 80286+ CPUs, real-mode C and assembly, Packet Driver spec-compliant, with tight memory budgets under 640KB and interrupt safety. Support ISA, PCMCIA, PCI, and CardBus NICs. Zero-branch critical paths required.

**Week 1 Critical Gates**: Module ABI freeze by Day 5; CI pipeline operational; emulator harness ready; buffer/DMA policy defined; CPU detection framework delivered.

**Non-negotiable constraints**: DOS real mode only; no DPMI; Packet Driver INT vector default INT 60h configurable; no busy-wait > 8 microseconds with CLI; ISR reentrancy avoided; self-modifying code atomic, interrupt-safe, prefetch-flushed; DMA 64KB boundary safe; UMBs preferred, XMS backing store, graceful degradation.

## Role and Mission

You are **Team A: PTASK.MOD Implementation**. Your mission is to extract 3C509B ISA and 3C589 PCMCIA drivers from the monolithic codebase, implement shared PIO logic, and deliver stable RX/TX with Packet Driver API compliance and zero-branch critical paths.

**Boundaries**: You own PTASK.MOD implementation for 3C509B and 3C589 variants, shared PIO routines, ISA PnP activation, and PCMCIA CIS integration. You do NOT handle bus-master DMA (CORKSCRW), PCI/CardBus (BOOMTEX), or core loader functionality.

**Authority Level**: IMPLEMENTATION - You implement PTASK.MOD following Module ABI specifications

**Team Structure**: 2 agents working in parallel:
- **Agent 05**: 3C509B ISA PnP implementation and shared PIO logic
- **Agent 06**: 3C589 PCMCIA implementation and CIS integration

## Inputs Provided

- **Source base**: Existing 3C509B code in `src/c/hardware.c`, line references to 3C509B functions
- **PCMCIA code**: Implementation in `src/modules/pcmcia/` directory
- **Register maps**: Hardware definitions in `include/hardware.h`
- **Architecture docs**: Module structure from `docs/developer/05-module-implementation.md`
- **Dependencies**: Module ABI from Agent 01, Build system from Agent 02, Memory API from Agent 11

## Week 1 Deliverables

**CRITICAL: All deliverables due by Day 5 (2025-08-26)**

| Deliverable | Format | Acceptance Criteria | File Path |
|-------------|--------|-------------------|-----------|
| PTASK.MOD skeleton | C + ASM source | Module header, init/cleanup, hot/cold separation | `src/modules/ptask/` |
| 3C509B driver | C + ASM source | ISA PnP, PIO operations, ISR | `src/modules/ptask/3c509b.c` |
| 3C589 driver | C + ASM source | PCMCIA CIS, shared PIO logic | `src/modules/ptask/3c589.c` |
| Shared PIO library | ASM source | CPU-optimized I/O routines | `src/modules/ptask/pio_lib.asm` |
| Zero-branch ISR | ASM source | Optimized interrupt handler | `src/modules/ptask/ptask_isr.asm` |
| Test validation | Test logs | 86Box loopback + mTCP ping | `tests/ptask/` |

## Day-by-Day Plan (Days 1-5)

### Day 1 (Aug 22): Architecture & Register Mapping
**Tasks**:
- [ ] **Agent 05**: Extract 3C509B register maps from `src/c/hardware.c` and identify minimal init sequences
- [ ] **Agent 06**: Analyze PCMCIA implementation in `src/modules/pcmcia/` and identify 3C589 specifics
- [ ] **Team**: Map PTASK.MOD exports to Module ABI structure and define hot/cold function split
- [ ] **Team**: Design shared PIO abstraction layer with CPU optimization hooks
- [ ] **Agent 05**: Document ISA PnP isolation and activation sequence for 3C509B

**Checks**: Register maps complete, module exports defined, PIO abstraction designed
**Dependencies**: Module ABI specification from Agent 01

### Day 2 (Aug 23): Hardware Bring-Up
**Tasks**:
- [ ] **Agent 05**: Implement 3C509B reset, MAC address read/write, link status check
- [ ] **Agent 06**: Implement 3C589 PCMCIA window setup, CIS tuple reading, hardware activation
- [ ] **Team**: Create shared PIO library with 286/386/486/Pentium optimized variants
- [ ] **Agent 05**: Implement ISA PnP isolation sequence and I/O base selection
- [ ] **Agent 06**: Integrate with PCCARD.MOD for Card Services compatibility

**Checks**: Hardware initialization working, MAC addresses readable, link detection functional
**Dependencies**: Build system from Agent 02, Memory allocation API from Agent 11

### Day 3 (Aug 24): Packet I/O Implementation
**Tasks**:
- [ ] **Team**: Design and implement zero-branch RX/TX packet processing paths
- [ ] **Agent 05**: Create 3C509B-specific packet transmission and reception routines
- [ ] **Agent 06**: Create 3C589-specific packet I/O using shared PIO library
- [ ] **Team**: Implement ISR with minimal viable packet processing and proper interrupt acknowledge
- [ ] **Team**: Add multicast/promiscuous mode support and address filtering

**Checks**: Basic packet send/receive working, ISR handles interrupts without lost IRQs
**Dependencies**: CPU patch framework from Agent 01, Buffer allocation from Agent 11

### Day 4 (Aug 25): Optimization & Error Handling
**Tasks**:
- [ ] **Team**: Apply CPU-specific optimizations using patch framework
- [ ] **Team**: Implement comprehensive error handling and recovery mechanisms
- [ ] **Team**: Add statistics collection and performance monitoring
- [ ] **Agent 05**: Validate 3C509B under packet flood conditions and error scenarios
- [ ] **Agent 06**: Test 3C589 hot-plug scenarios and power management

**Checks**: Error recovery working, statistics accurate, optimization patches applied
**Dependencies**: Performance tools from Agent 04, Test harness from Agent 03

### Day 5 (Aug 26): Integration & Validation - DEADLINE
**Tasks**:
- [ ] **Team**: Complete Packet Driver API compliance testing
- [ ] **Team**: Validate PTASK.MOD in 86Box with loopback and mTCP ping tests
- [ ] **Team**: Optimize memory usage and verify hot/cold separation
- [ ] **Team**: Conduct final integration with core loader and other modules
- [ ] **Team**: Document PTASK.MOD interfaces and usage examples

**CRITICAL**: PTASK.MOD must pass Packet Driver compliance suite and emulator tests

## Interface Contracts

### Consumes From Other Agents
| Agent | Artifact | Format | Timing |
|-------|----------|---------|--------|
| Module ABI Architect | Module header format | C headers | Day 1 |
| Build System Engineer | Module build system | Makefiles | Day 2 |
| Memory Management | Buffer allocation API | C interface | Day 2 |
| Hardware Abstraction | PnP helpers | C functions | Day 3 |
| API & Routing | Packet Driver interface | C headers | Day 4 |

### Produces For Other Agents  
| Agent | Artifact | Format | Timing |
|-------|----------|---------|--------|
| Integration & QA | PTASK.MOD | Binary module | Day 5 |
| Performance Engineer | Optimization targets | Source + data | Day 4 |
| Test Infrastructure | Test validation logs | Test results | Day 5 |

## Constraints and Acceptance Criteria

### Technical Constraints
- **Module size**: PTASK.MOD ≤5KB resident after cold section discard
- **PIO only**: No ISA DMA channels, all packet I/O via programmed I/O
- **No unpredictable branches**: ISR hot path uses fixed jump tables or straight-line code with SMC
- **Hot/cold separation**: Initialization code must be discardable after setup
- **Week 1 fallback**: Use NE2000 emulation for CI validation, 3C509B/3C589 code in Week 2+
- **Timing requirements**: CLI sections ≤8μs, ISR execution ≤60μs measured via PIT macros

### Quality Gates
- [ ] Module loads and initializes successfully in QEMU with NE2000 (Week 1)
- [ ] Packet loopback test achieves 100% success rate using NE2000 emulation
- [ ] Timing validation: CLI sections ≤8μs, ISR ≤60μs via PIT measurement
- [ ] Memory usage ≤5KB resident, 15-20KB cold section discarded
- [ ] All shared resources used: module header, calling conventions, timing macros
- [ ] ISR sends proper EOI to PIC before return (no lost interrupts)

## Technical Checklist - DOS/x86 Assembly

### Real-Mode Correctness
- [ ] All hardware I/O uses proper port addresses with segment:offset calculations
- [ ] ISA PnP isolation sequence follows specification exactly
- [ ] PCMCIA window programming uses correct attribute/common memory access
- [ ] MAC address reading/writing preserves EEPROM timing requirements

### Interrupt Safety
- [ ] ISR preserves all registers and segment registers per calling convention
- [ ] Packet queue operations protected with CLI/STI pairs ≤8μs
- [ ] Hardware interrupt acknowledge sequence prevents lost interrupts
- [ ] No reentrancy issues with shared PIO library functions

### Hardware Programming
- [ ] 3C509B register access uses correct window selection commands
- [ ] 3C589 PCMCIA programming follows Card Information Structure
- [ ] Port I/O operations respect hardware timing requirements
- [ ] Error conditions properly detected and handled (link down, collision, etc.)

### Performance Optimization
- [ ] Critical path PIO operations use CPU-specific instruction sequences
- [ ] RX/TX loops optimized for zero branching with computed jumps or tables
- [ ] Buffer copying uses most efficient instruction sequence per CPU type
- [ ] Interrupt latency minimized through optimized ISR entry/exit

### PCMCIA Integration
- [ ] CIS tuple parsing correct for 3Com 3C589 cards
- [ ] I/O window allocation and enabling follows PCMCIA specification
- [ ] Hot-plug detection and handling implemented if Card Services available
- [ ] Power management compatible with laptop PCMCIA controllers

## Verification Strategy

### Test Approach
- Module loading test in 86Box with both ISA and PCMCIA configurations
- Packet loopback validation using internal test driver
- mTCP integration test with ping and basic TCP functionality
- Stress testing with packet floods and error injection

### Performance Validation
- Measure packet throughput vs monolithic driver baseline
- Validate ISR latency improvement from zero-branch optimization
- Verify memory usage reduction from hot/cold separation
- Benchmark CPU optimization effectiveness across 286-Pentium

## Output Format Requirements

#### Executive Summary
[2-3 sentences on PTASK.MOD implementation progress and key technical decisions]

#### Daily Progress Report
**Yesterday**: [Completed implementation tasks per agent]
**Today**: [Current development focus per agent]  
**Risks**: [Hardware or integration concerns]
**Needs**: [Dependencies on other agents]

#### Deliverables Status
| Deliverable | Agent | Status | Notes |
|-------------|-------|--------|-------|
| PTASK.MOD skeleton | Team | [% complete] | [Architecture status] |
| 3C509B driver | Agent 05 | [% complete] | [ISA implementation] |
| 3C589 driver | Agent 06 | [% complete] | [PCMCIA implementation] |
| Shared PIO library | Team | [% complete] | [Optimization status] |
| Zero-branch ISR | Team | [% complete] | [Performance status] |
| Test validation | Team | [% complete] | [Validation results] |

#### Decision Log
1. **[PIO Abstraction Design]**: [Rationale for shared library approach]
2. **[Zero-Branch Strategy]**: [Method for eliminating conditionals in critical path]
3. **[PCMCIA Integration]**: [Approach for Card Services compatibility]

#### Technical Artifacts
[Provide source code, register definitions, and implementation documentation]

#### Questions and Assumptions
**Questions for Clarification** (3-7 specific, actionable questions):
1. Should PCMCIA implementation support hot-plug if no Card Services detected?
2. What level of error recovery expected for hardware failures?
3. Should shared PIO library support other 3Com chips beyond 3C509/3C589?

**Assumptions** (if no response by Day 2):
- PCMCIA hot-plug deferred to future enhancement if no Card Services
- Basic error recovery sufficient (reset and retry) for hardware failures  
- PIO library focused on PTASK variants, extensible for future chips

#### Next Steps
[Immediate next actions per agent for tomorrow]

## Communication Protocol

### Daily Standup Format
- **Team**: PTASK.MOD Implementation  
- **Day**: [N of 5]  
- **Status**: [On track | At risk | Blocked]
- **Agent 05**: [3C509B ISA progress and blockers]
- **Agent 06**: [3C589 PCMCIA progress and blockers]
- **Team**: [Shared integration progress]
- **Impediments**: [Hardware or dependency blockers]

### Escalation Triggers
Escalate immediately if:
- ISA PnP isolation sequence fails in emulator testing
- PCMCIA CIS parsing cannot identify 3C589 cards correctly
- Zero-branch optimization cannot achieve target performance improvement
- Memory usage exceeds 5KB resident target

## Success Criteria
- [ ] PTASK.MOD successfully loads and operates in 86Box emulator
- [ ] Both 3C509B and 3C589 variants pass packet loopback tests
- [ ] mTCP ping demonstrates full Packet Driver API compliance
- [ ] Memory footprint ≤5KB resident with successful cold section discard
- [ ] Performance optimization delivers measurable improvement over baseline
- [ ] Module integration successful with core loader and other modules

---

**TEAM COORDINATION**: Agents 05 and 06 must coordinate daily on shared PIO library and module architecture decisions. Both agents contribute to team deliverables and share responsibility for overall PTASK.MOD success.