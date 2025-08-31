# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a DOS packet driver supporting 3Com 3C515-TX (100 Mbps) and 3C509B (10 Mbps) network interface cards. The driver is written in C and Assembly, targeting DOS 2.0+ on Intel 80286+ systems, and implements the Packet Driver Specification for compatibility with DOS networking applications like mTCP.

## Build System

The project uses Open Watcom C/C++ and NASM with a custom Makefile:

### Build Commands
- `wmake` or `wmake release` - Build optimized release version
- `wmake debug` - Build debug version with symbols
- `wmake clean` - Clean build directory

### Requirements
- Open Watcom C/C++ 1.9 or later
- NASM (Netwide Assembler)
- GNU Make compatible make utility

## Architecture

The driver is structured as a modular TSR (Terminate and Stay Resident) program with the following key layers:

### Core Modules
- **Main/Initialization** (`src/c/main.c`, `src/c/init.c`) - Entry point, TSR setup, hardware detection
- **Hardware Abstraction** (`src/c/hardware.c`, `src/asm/hardware.asm`) - Low-level NIC interaction for 3C515-TX and 3C509B
- **Memory Management** (`src/c/memory.c`, `src/c/xms_detect.c`, `src/c/buffer_alloc.c`) - XMS and conventional memory handling
- **Packet Driver API** (`src/c/api.c`, `src/asm/packet_api.asm`) - Implements Packet Driver Specification
- **Routing** (`src/c/routing.c`, `src/c/static_routing.c`, `src/asm/flow_routing.asm`) - Multi-homing with static and flow-aware routing
- **Network Operations** (`src/c/packet_ops.c`, `src/asm/packet_ops.asm`) - Packet transmission/reception
- **Configuration** (`src/c/config.c`) - Command-line parameter parsing from CONFIG.SYS
- **Diagnostics** (`src/c/diagnostics.c`, `src/c/logging.c`, `src/c/stats.c`) - Logging and statistics

### Hardware Support
- **3C515-TX**: 100 Mbps Fast Ethernet with bus mastering support (80286+ CPUs with chipset support)
- **3C509B**: 10 Mbps Ethernet with programmed I/O
- Both cards support Plug and Play auto-detection

### Key Features
- Multi-homing support for multiple NICs under single interrupt
- Internal multiplexing for multiple applications without external tools
- XMS memory utilization with conventional memory fallback
- Flow-aware routing to maintain connection symmetry
- Compact TSR design (<6KB resident size)

## File Structure

```
include/           - Header files for all modules
src/c/            - C source files
src/asm/          - Assembly source files
docs/             - Architecture and implementation documentation
refs/             - Technical references and legacy code
build/            - Build output directory
tools/            - Development utilities
```

## Configuration

The driver loads as a device driver in CONFIG.SYS with optional parameters:

- `/IO1=`, `/IO2=` - I/O base addresses for NICs
- `/IRQ1=`, `/IRQ2=` - IRQ assignments
- `/SPEED=` - Network speed (10/100, limited by NIC capability)
- `/BUSMASTER=` - Enable/disable bus mastering (3C515-TX only)
- `/LOG=ON` - Enable diagnostic logging
- `/ROUTE=` - Static routing rules for multi-homing

## Development Notes

- Performance-critical code is implemented in Assembly
- C code provides higher-level logic and maintainability
- Code targets DOS real mode exclusively
- Strict adherence to Packet Driver Specification for application compatibility
- Memory efficiency is critical due to DOS 640KB conventional memory limit

---

## Implementation Guidance

### Project Planning Documents

The repository contains comprehensive planning documentation that must be consulted for all development work:

#### **`docs/IMPLEMENTATION_PLAN.md`** - Master Implementation Strategy
**Purpose**: Defines the complete 4-phase implementation approach with parallel sub-agent development
**When to Use**:
- Before starting any implementation work
- When planning task assignments and timelines
- When coordinating parallel development efforts
- When assessing project scope and dependencies

**Key Sections**:
- Phase-based development strategy (8 weeks, 4 phases)
- Sub-agent specialization (ASM, Hardware, Memory, API, Networking, Diagnostics)
- Parallel task group coordination
- Risk mitigation strategies
- Success metrics and deliverables

#### **`docs/IMPLEMENTATION_TRACKER.md`** - Real-Time Progress Management
**Purpose**: Active tracking of implementation progress, task assignments, and quality metrics
**When to Use**:
- At the start of each development session (check current phase status)
- When assigning tasks to sub-agents
- When reporting progress or updating stakeholders
- When identifying blockers or dependencies
- When conducting phase gate reviews

**Key Sections**:
- Phase status dashboard with real-time progress
- Task group assignments and sub-agent allocations
- Quality metrics tracking and performance targets
- Risk assessment and mitigation status
- Communication protocols and standups

#### **`docs/TESTING_STRATEGY.md`** - Comprehensive Quality Assurance
**Purpose**: Defines testing approach for each phase with specific test cases and success criteria
**When to Use**:
- Before implementing any feature (understand testing requirements)
- When validating phase completion (run phase-specific test suites)
- When encountering bugs or compatibility issues
- When preparing for production deployment
- When establishing quality gates

**Key Sections**:
- 160+ specific test cases organized by phase
- Hardware compatibility testing procedures
- Performance benchmarking requirements
- Automated vs manual testing strategies
- Production readiness criteria

#### **`docs/REFERENCES.md`** - Technical Implementation Foundation
**Purpose**: Comprehensive technical documentation and specifications needed for implementation
**When to Use**:
- When implementing hardware-specific code (register programming, DMA)
- When working with protocols (Ethernet, ARP, Packet Driver API)
- When programming DOS-specific features (TSR, memory management)
- When troubleshooting hardware or compatibility issues

**Key Sections**:
- Complete hardware specifications (3C509B, 3C515-TX)
- Protocol specifications (Ethernet, ARP)
- DOS programming references (TSR, XMS, Packet Driver API)
- Linux driver references for hardware programming details

### Development Workflow

#### **Phase Management**
1. **Check Current Phase**: Always consult `IMPLEMENTATION_TRACKER.md` to understand current phase and task assignments
2. **Review Phase Plan**: Refer to `IMPLEMENTATION_PLAN.md` for detailed phase objectives and deliverables
3. **Understand Dependencies**: Check task group dependencies before starting work
4. **Update Progress**: Update tracker after completing tasks or milestones

#### **Sub-Agent Coordination**
1. **Specialization Assignment**: Use tracker to determine which sub-agent should handle specific tasks
2. **Parallel Execution**: Coordinate multiple sub-agents working on independent modules
3. **Integration Points**: Plan synchronization between dependent task groups
4. **Quality Reviews**: Cross-sub-agent reviews for interface compliance

#### **Quality Assurance**
1. **Test-First Development**: Review testing strategy before implementing features
2. **Phase Gate Validation**: Run phase-specific test suites before proceeding
3. **Performance Monitoring**: Track metrics against targets in tracker
4. **Regression Prevention**: Execute regression tests after changes

#### **Technical Implementation**
1. **Consult References**: Use `REFERENCES.md` for all technical specifications
2. **Follow Architecture**: Maintain modular TSR design with proper memory layout
3. **Hardware Programming**: Use comprehensive register definitions in `include/` headers
4. **Protocol Compliance**: Ensure Packet Driver Specification adherence

### Critical Success Factors

1. **Always Use the Tracker**: The implementation tracker is the single source of truth for project status
2. **Respect Phase Gates**: Do not proceed to next phase until current phase exit criteria are met
3. **Coordinate Sub-Agents**: Use parallel development strategy to maximize efficiency
4. **Maintain Quality**: Run appropriate test suites before declaring tasks complete
5. **Document Progress**: Update tracker and communicate status regularly

### Emergency Procedures

#### **When Blocked**
1. Update tracker with blocker status and impact assessment
2. Consult implementation plan for alternative approaches
3. Review dependencies to identify critical path impacts
4. Escalate if blocker affects phase completion timeline

#### **When Tests Fail**
1. Consult testing strategy for specific test requirements
2. Review technical references for correct implementation approach
3. Check compatibility requirements and target system configurations
4. Update tracker with test failure analysis and remediation plan

These planning documents form the operational foundation for the entire project and must be actively used throughout the implementation process to ensure successful delivery of the production-ready packet driver.
