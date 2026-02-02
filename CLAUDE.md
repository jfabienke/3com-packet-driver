# CLAUDE.md

> Last Updated: 2026-02-01 10:31 UTC

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a DOS packet driver supporting 3Com 3C515-TX (100 Mbps) and 3C509B (10 Mbps) network interface cards. The driver is written in C and Assembly, targeting DOS 2.0+ on Intel 80286+ systems, and implements the Packet Driver Specification for compatibility with DOS networking applications like mTCP.

## Build System

The project uses Open Watcom C/C++ and NASM with two makefile options:

### GNU Make (Cross-Compilation on Linux/macOS/Windows with GNU Make)
```bash
make release        # Build optimized release version
make debug          # Build debug version with symbols
make production     # Size-optimized build (excludes debug modules)
make config-8086    # Build for 8086/8088 CPUs
make clean          # Clean build directory
```

### Open Watcom wmake (Native DOS/Windows Build)
```bash
wmake -f Makefile.wat              # Release build (default)
wmake -f Makefile.wat debug        # Debug build with symbols
wmake -f Makefile.wat production   # Size-optimized production build
wmake -f Makefile.wat config-8086  # Build for 8086/8088 CPUs
wmake -f Makefile.wat pci-utils    # Build PCI diagnostic utilities
wmake -f Makefile.wat clean        # Clean build directory
wmake -f Makefile.wat info         # Show available targets
```

### macOS ARM64 Build (Cross-compilation with Open Watcom v2)

The project is cross-compiled on macOS ARM64 using **native Mach-O ARM64 Open Watcom v2 binaries**.

#### Native macOS ARM64 Toolchain

The Open Watcom v2 toolchain has been compiled natively for Apple Silicon. The binaries live in:

```
/Users/johnfabienke/Development/macos-open-watcom/open-watcom-v2/rel/armo64/
```

Key tools (all Mach-O 64-bit arm64 executables):
| Tool | Purpose |
|------|---------|
| `wcc` | C compiler (16-bit target) |
| `wcc386` | C compiler (32-bit target) |
| `wlink` | Linker |
| `wmake` | Make utility |
| `wasm` | Watcom assembler (MASM syntax) |
| `wlib` | Library manager |
| `wdis` | Disassembler |
| `dmpobj` | Object file dumper |
| `wcl` | Compiler/linker wrapper |

These binaries depend only on system libc (`/usr/lib/libSystem.B.dylib`) — no emulation or virtualization needed.

#### Build via Shell Script (Recommended)

```bash
./build_macos.sh              # Release build (default)
./build_macos.sh debug        # Debug build with symbols
./build_macos.sh production   # Size-optimized production
./build_macos.sh clean        # Clean artifacts
./build_macos.sh info         # Show configuration
```

#### Build via wmake

```bash
# Set up environment
export WATCOM=/Users/johnfabienke/Development/macos-open-watcom/open-watcom-v2/rel
export PATH="$WATCOM/armo64:$PATH"

# Build
wmake -f Makefile.wat clean
wmake -f Makefile.wat

# Verify map file (release mode - stricter DGROUP limits)
python3 tools/verify_map.py build/3cpd.map --release

# Verify map file (debug mode - allows red zone)
python3 tools/verify_map.py build/3cpd.map --debug
```

#### Assembly

The project uses a dual assembler approach:
- **NASM** (`/opt/homebrew/bin/nasm`) — for NASM-syntax `.asm` files (installed via Homebrew)
- **WASM** (`$WATCOM/armo64/wasm`) — for MASM-syntax `.asm` files

Both output OMF object format compatible with `wlink`.

#### Libraries

- DOS 16-bit runtime libraries: `$WATCOM/lib286/dos/` (clibl.lib, wovl.lib, etc.)
- Watcom C headers: `$WATCOM/h/`

### Requirements
- Open Watcom C/C++ v2 (macOS ARM64 native binaries in `armo64/`)
- NASM (Netwide Assembler) — `brew install nasm`
- GNU Make (for cross-compilation) OR wmake (for native builds)

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
