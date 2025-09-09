# 3Com Packet Driver Boot Sequence

**Last Updated:** September 8, 2025
**Version:** 1.2
**Status:** canonical
**Scope:** Complete initialization sequence from DOS entry to TSR installation

## Executive Summary

This document provides the authoritative reference for the 3Com Packet Driver's boot sequence, detailing all 9 stages from initial DOS entry through TSR installation. The driver supports ISA cards (3C509B, 3C515-TX), PCI cards (3C590/3C595 Vortex, 3C900/3C905 Boomerang, 3C905B Cyclone, 3C905C Tornado), and PC Card devices (3C589 PCMCIA, 3C575 CardBus), implementing the Packet Driver Specification.

## High-Level Boot Sequence

```
════════════════════════════════════════════════════════════════════════
                   3COM PACKET DRIVER BOOT SEQUENCE
════════════════════════════════════════════════════════════════════════

[DOS ENTRY] → main.c:main() → init.c:driver_init()
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 1: CPU Detection (cpu_detect.asm + cpu_detect.c)              │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ CPU Type: 8086/286/386/486/CPUID-capable                        │
│ ├─■ Vendor ID: CPUID function 0 → "GenuineIntel"/"AuthenticAMD"     │
│ ├─■ Model Lookup: 78+ CPUs with codenames (Pentium "P5", etc.)      │
│ ├─■ Features: TSC, MSR, CLFLUSH, cache sizes, FPU presence          │
│ └─■ Speed: RDTSC/PIT timing, 5 trials, confidence scoring           │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 2: Platform Detection (platform_probe_early.c)                │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ V86 mode and DPMI detection                                     │
│ ├─■ VDS presence check (INT 4Bh)                                    │
│ ├─■ Memory manager detection (EMM386/QEMM/HIMEM)                    │
│ ├─■ DMA policy determination (FORBID/VDS/DIRECT)                    │
│ └─■ Safe chipset identification (PCI only)                          │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 3: Memory Setup (memory.c + buffer_alloc.c)                   │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ Three-tier memory system init (XMS/UMB/Conventional)           │
│ ├─■ Per-NIC buffer pool allocation                                  │
│ ├─■ VDS common buffer setup (if VDS present)                        │
│ ├─■ DMA-safe buffer allocation (64KB-aligned)                       │
│ └─■ RX copybreak pool initialization                                │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 4: NIC Enumeration (init.c:166 + pci_integration.c)           │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ ISA PnP isolation sequence (3C509B/3C515-TX)                    │
│ ├─■ Legacy I/O probe with signature check                           │
│ ├─■ PCI/CardBus enumeration (47+ variants in database)              │
│ ├─■ Generation dispatch (Vortex/Boomerang/Cyclone/Tornado)          │
│ └─■ Resource assignment from pools (I/O, IRQ)                       │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 5: Configuration (config.c + routing.c)                       │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ Parse CONFIG.SYS parameters (/IO1, /IRQ1, /BUSMASTER)           │
│ ├─■ Execute bus master auto-test if /BUSMASTER=AUTO                 │
│ ├─■ Initialize routing tables (256 entries)                         │
│ ├─■ Setup bridge learning table with aging                          │
│ └─■ Configure buffer overrides and operation modes                  │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 6: NIC Initialization (nic_init.c)                            │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ NIC-specific init (3c509b/3c515 ops assignment)                 │
│ ├─■ Bus master capability testing and validation                    │
│ ├─■ DMA policy check (runtime/validated/safe)                       │
│ ├─■ MII/PHY configuration with MDIO lock                            │
│ └─■ Cache coherency system initialization                           │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 7: Safety Patch Application                                   │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ Apply runtime safety patches                                    │
│ ├─■ Install VDS buffer management                                   │
│ ├─■ Configure 64KB boundary detection                               │
│ └─■ Setup PIO fallback stubs                                        │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 8: TSR Installation                                           │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ Calculate resident memory requirements                          │
│ ├─■ Hook INT 60h for Packet Driver API                              │
│ ├─■ Register hardware interrupt handlers                            │
│ └─■ Mark hot sections as resident                                   │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Stage 9: Cold Section Discard                                       │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ Release initialization code                                     │
│ ├─■ Free temporary buffers                                          │
│ ├─■ Print success message                                           │
│ └─■ TSR exit (INT 21h, AH=31h)                                      │
└─────────────────────────────────────────────────────────────────────┘
  │
  ▼
[DRIVER ACTIVE] - Ready for packet operations
```

## Detailed Stage Breakdown

### Stage 1: CPU Detection

**Primary Files:**
- `src/asm/cpu_detect.asm` (3610 lines) - Low-level CPU detection
- `src/loader/cpu_detect.c` (547 lines) - High-level interface and model database
- `include/cpu_detect.h` - Type definitions and constants

**Detailed Implementation:**

```
═══════════════════════════════════════════════════════════════════════
Stage 1: CPU Detection (src/asm/cpu_detect.asm + src/loader/cpu_detect.c)
═══════════════════════════════════════════════════════════════════════
├─■ CPU Type Detection
│ ├─◇ PUSHF/POPF tests for 8086/286/386/486 differentiation
│ ├─◇ AC flag test (bit 18) for 386 vs 486
│ └─◇ ID flag test (bit 21) for CPUID availability
│
├─■ CPU Vendor Identification
│ ├─◇ CPUID available? (486+ with ID flag support)
│ │ ├─✓ Execute CPUID function 0 (get_cpu_vendor_id @ line 302)
│ │ │ ├─► EBX = First 4 chars → cpu_vendor_id[0-3]
│ │ │ ├─► EDX = Next 4 chars → cpu_vendor_id[4-7]
│ │ │ └─► ECX = Last 4 chars → cpu_vendor_id[8-11]
│ │ │
│ │ ├─✓ Vendor String Mapping (cpu_detect.c @ line 347)
│ │ │ ├─► "GenuineIntel" → VENDOR_INTEL
│ │ │ ├─► "AuthenticAMD" → VENDOR_AMD
│ │ │ ├─► "CyrixInstead" → VENDOR_CYRIX
│ │ │ ├─► "CentaurHauls" → VENDOR_VIA
│ │ │ ├─► "GenuineTMx86" → VENDOR_TRANSMETA
│ │ │ └─► "NexGenDriven" → VENDOR_NEXGEN
│ │ │
│ │ └─✓ Model Database Lookup (cpu_detect.c @ lines 79-189)
│ │   ├─► Intel: 36 models (Pentium to Ivy Bridge)
│ │   ├─► AMD: 24 models (K5 to Athlon 64 X2)
│ │   ├─► Cyrix: 5 models (5x86 to VIA Cyrix III)
│ │   ├─► VIA: 10 models (WinChip to Nano)
│ │   └─► Transmeta: 3 models (Crusoe to Efficeon)
│ │
│ └─✗ No CPUID (286/386/early 486)
│   ├─► Hardware probe tests (detect_cpu_vendor_no_cpuid)
│   ├─► Cyrix DIR0 register test (port 22h/23h)
│   ├─► AMD UMOV instruction behavior test
│   ├─► NexGen CPUID-without-ID-flag test
│   └─► Default to VENDOR_INTEL if unknown
│
├─■ CPU Signature Extraction (get_cpu_signature_info @ line 363)
│ ├─◇ CPUID function 1 → EAX signature
│ ├─► Stepping: bits[3:0] → cpu_step_id
│ ├─► Model: bits[7:4] → cpu_model_id
│ ├─► Family: bits[11:8] → cpu_family_id
│ └─► Extended family/model for Family 15+ CPUs
│
├─■ Feature Detection (detect_cpu_features @ line 693)
│ ├─► Base features by CPU type (PUSHA, 32BIT, BSWAP)
│ ├─► CPUID feature flags from EDX (TSC, MSR, CLFLUSH)
│ ├─► Cache detection (L1D, L1I, L2 sizes)
│ ├─► FPU presence test (FNINIT/FNSTSW)
│ └─► Extended features (MMX, SSE, SSE2)
│
├─■ CPU Speed Detection (detect_cpu_speed @ line 2806)
│ ├─◇ TSC available? (Pentium+)
│ │ ├─✓ RDTSC-based measurement (5 trials)
│ │ └─✗ PIT channel 2 timing loop (port 42h)
│ ├─► Statistical analysis (median of 5 trials)
│ ├─► Confidence calculation (0-100%)
│ └─► Fallback estimates by CPU type
│
└─■ Special Detection
  ├─► V86 mode detection (SMSW instruction test)
  ├─► Hypervisor detection (CPUID leaf 0x40000000)
  ├─► Cache coherency requirements (CLFLUSH/WBINVD)
  └─► Invariant TSC check (power management safe)
```

**Key Data Structures:**
```c
typedef struct {
    cpu_type_t cpu_type;        // 8086/286/386/486/CPUID_CAPABLE
    cpu_vendor_t cpu_vendor;    // INTEL/AMD/CYRIX/VIA/etc.
    char vendor_string[13];     // "GenuineIntel" etc.
    uint8_t cpu_family;         // Family ID from CPUID
    uint8_t cpu_model;          // Model ID from CPUID
    uint8_t stepping;           // Stepping ID
    uint32_t features;          // Feature flags bitmap
    uint16_t cpu_mhz;          // Detected speed in MHz
    uint8_t speed_confidence;   // 0-100% confidence
    char cpu_name[32];         // "Pentium III"
    char cpu_codename[32];     // "Katmai"
} cpu_info_t;
```

### Stage 2: Platform Detection

**Primary Files:**
- `src/c/platform_probe_early.c` - Early DMA policy detection
- `src/c/dma_policy.c` - DMA safety assessment
- `src/c/vds.c` - Virtual DMA Services detection

**Detailed Implementation:**

```
═══════════════════════════════════════════════════════════════════════
Stage 2: Platform Detection with DMA Policy Assessment
═══════════════════════════════════════════════════════════════════════
├─■ V86 Mode Detection
│ ├─◇ Check CR0 MSW for V86/paging active
│ ├─◇ Detect memory managers (EMM386, QEMM)
│ └─◇ Assess DMA safety based on environment
│
├─■ VDS Presence Check
│ ├─◇ INT 4Bh AX=8102h VDS version query
│ ├─◇ Validate VDS provider capabilities
│ └─◇ Set requires_vds flag if paging active
│
├─■ DMA Policy Determination
│ ├─► DMA_POLICY_FORBID: V86/paging without VDS → PIO only
│ ├─► DMA_POLICY_VDS: VDS present → use for address translation
│ └─► DMA_POLICY_DIRECT: Real mode → direct physical access
│
└─■ PIO Fallback Readiness
  ├─◇ Set pio_fallback_ok = true for graceful degradation
  └─◇ Log platform assessment for diagnostics
```

**Key Operations:**
1. PCI chipset identification via configuration space access
2. Virtual DMA Services (VDS) presence check via INT 4Bh
3. Bus master DMA capability assessment with safety validation
4. Intelligent DMA policy selection based on platform state

Note:
- The driver does not use ISA DMA controllers (8237A). ISA bus-master NICs (e.g., 3C515‑TX) and PCI NICs perform their own bus mastering. VDS is used for address translation and validation where required. When bus mastering is gated or unavailable, transfers fall back to PIO.

### Stage 3: Memory Setup

**Primary Files:**
- `src/c/memory.c` - Memory management core
- `src/c/xms_detect.c` - XMS driver interface
- `src/c/buffer_alloc.c` - Buffer allocation strategies

**Memory Layout:**
```
Conventional Memory (< 1MB):
┌─────────────────────┐ 0xA0000
│   Upper Memory      │
├─────────────────────┤ Driver Load Address
│   TSR Hot Section   │ Resident memory
├─────────────────────┤
│   DMA Buffers       │ 64KB-aligned
├─────────────────────┤
│   Ring Buffers      │
├─────────────────────┤
│   DOS/Applications  │
└─────────────────────┘ 0x00000

Extended Memory (XMS):
┌─────────────────────┐
│   Packet Buffers    │ Via XMS handles
├─────────────────────┤
│   Large Ring Buffer │ If available
└─────────────────────┘
```

#### XMS Usage and DMA (Elaborated)

- XMS is never used directly for NIC DMA. Reasons:
  - Real‑mode ISA DMA (8237) cannot access XMS; we do not use 8237 at all.
  - Many NIC bus masters have addressing limits (e.g., 24–32‑bit, 64KB‑crossing constraints). XMS handles do not guarantee suitable physical layout.
  - On 286/8086 targets and diverse chipsets, assuming DMA access to XMS risks data corruption and instability.
- Policy:
  - Allocate all DMA descriptors, rings, and DMA‑visible packet buffers in conventional memory (<1 MB), aligned and sized to avoid 64KB crossings.
  - Use XMS strictly for copy‑only staging, large non‑DMA pools, and spillover buffers. Copy to/from DMA‑safe conventional buffers for device I/O.
  - Where VDS is present and policy allows bus mastering, query/translate physical addresses for conventional buffers; still treat XMS as copy‑only.
  - When VDS is absent or bus mastering is gated, operate purely in PIO with the same buffer tiering.

### Stage 4: NIC Enumeration

**Purpose**: Discover and enumerate all supported 3Com NICs
**Primary Location**: `src/c/init.c:166` (Phase 3 - calls `detect_and_init_pci_nics`)
**Supporting Files**: `src/c/pci_integration.c`, `src/c/3com_pci_detect.c`, `src/c/3com_detect.c`
**When**: After memory setup completes

### Detection Phases

```
┌──────────────────────────────────────────────┐
│            NIC Detection Pipeline            │
├──────────────────────────────────────────────┤
│                                              │
│  Phase 1: ISA PnP Scan                       │
│  ├── Query PnP BIOS                          │
│  ├── Enumerate ISA devices                   │
│  └── Identify 3Com cards (3C509B, 3C515-TX)  │
│                                              │
│  Phase 2: Legacy ISA Probe                   │
│  ├── Scan I/O ports 0x200-0x3E0              │
│  ├── Check for 3C509B signature              │
│  └── Verify 3C515-TX presence                │
│                                              │
│  Phase 3: PCI/CardBus Bus Scan               │
│  ├── CPU capability check (386+ required)    │
│  ├── Check PCI BIOS presence (INT 1Ah)       │
│  ├── Install PCI BIOS shim for workarounds   │
│  ├── Call detect_and_init_pci_nics()         │
│  │   ├── pci_subsystem_init()                │
│  │   ├── scan_3com_pci_devices()             │
│  │   │   └── Database: 47+ PCI/CardBus NICs  │
│  │   └── init_3com_pci() per device          │
│  └── Generation-based dispatch:              │
│      ├── Vortex → PIO mode handler           │
│      ├── Boomerang → DMA mode handler        │
│      ├── Cyclone → Enhanced DMA handler      │
│      └── Tornado → Advanced features handler │
│                                              │
│  Phase 4: Configuration                      │
│  ├── Assign I/O base addresses               │
│  ├── Configure IRQ settings                  │
│  └── Set media type                          │
│                                              │
│  Phase 5: Capability Detection               │
│  ├── Check bus master support                │
│  ├── Verify DMA capability                   │
│  └── Determine max packet size               │
│                                              │
└──────────────────────────────────────────────┘
```

**Note**: CardBus devices (3C575 series) are detected through the PCI enumeration
process using vendor ID 0x10B7. They do not require separate Socket Services or
Card Services - they appear as standard PCI devices to the driver.

**Primary Files:**
- `src/c/init.c:166` - Main entry point calling `detect_and_init_pci_nics()`
- `src/c/pci_integration.c` - PCI/CardBus integration layer
  - `detect_and_init_pci_nics()` (line 89) - Main orchestration
  - `pci_subsystem_init()` (line 34) - PCI BIOS setup
- `src/c/3com_pci_detect.c` - Device database and enumeration
  - Lines 23-74: Database of 47+ PCI/CardBus variants
  - `scan_3com_pci_devices()` (line 157) - PCI bus scanning
  - `init_3com_pci()` (line 265) - Device initialization
- `src/c/3com_boomerang.c` - Boomerang/Cyclone/Tornado DMA handler
- `src/c/3com_vortex.c` - Vortex PIO handler
- `src/c/hardware.c` - ISA NIC detection and management
- `src/c/3c509b.c` - 3C509B-specific code
- `src/c/3c515.c` - 3C515-TX-specific code

**Detection Sequence:**
1. **ISA PnP Isolation** for both 3C509B and 3C515-TX
   - Both cards support ISA Plug and Play
   - 3C515-TX is a 100BaseTX ISA card (not PCI)
2. **I/O Port Probing** fallback (0x200-0x3E0 range)
3. **PCI/CardBus Enumeration** (init.c Phase 3):
   - CPU check: Requires 386+ for PCI support
   - PCI BIOS detection via INT 1Ah
   - PCI shim installation for BIOS workarounds
   - Device scanning against 47+ model database:
     - **PCI Models**: 3C590/3C595 (Vortex), 3C900/3C905 (Boomerang),
       3C905B (Cyclone), 3C905C (Tornado)
     - **CardBus Models**: 3C575, 3C575B, 3C575C, 3CCFE575BT, 3CCFE575CT,
       3CXFE575BT, 3CXFE575CT (all use vendor ID 0x10B7)
   - Generation-based dispatch determines handler:
     - Vortex → PIO mode (no bus mastering)
     - Boomerang/Cyclone/Tornado → DMA mode (bus mastering)
4. **PC Card Detection** for legacy PCMCIA:
   - 3C589 series (PCMCIA 16-bit, PIO only)
   - Note: CardBus devices handled via PCI enumeration, not here

### Stage 5: Configuration

**Primary Files:**
- `src/c/config.c` - Configuration parser
- `src/c/static_routing.c` - Routing table setup

**CONFIG.SYS Parameters:**
```
DEVICE=3COMPKT.COM /IO1=0x300 /IRQ1=10 /SPEED=AUTO /BUSMASTER=AUTO

Parameters:
/BUSMASTER=AUTO  - Intelligent auto-detection (recommended)
/BUSMASTER=ON    - Enable after validation testing
/BUSMASTER=OFF   - Force PIO mode (safest)
/BUSMASTER=FORCE - Override safety checks (dangerous)
```

**Bus Master Auto-Detection:**
```
┌─────────────────────────────────────────────────────────────────────┐
│ BUSMASTER Parameter Processing                                      │
├─────────────────────────────────────────────────────────────────────┤
│ ├─■ /BUSMASTER=AUTO → Quick validation test                         │
│ ├─■ /BUSMASTER=ON → Full validation + enable if safe                │
│ ├─■ /BUSMASTER=OFF → Force PIO mode                                 │
│ └─■ /BUSMASTER=FORCE → Override safety (dangerous)                  │
└─────────────────────────────────────────────────────────────────────┘
```

### Stage 6: NIC Initialization

**Primary Files:**
- `src/c/init.c` - Main initialization coordinator
- `src/c/nic_init.c` - NIC-specific initialization with DMA/PIO selection
- `src/asm/hardware.asm` - Low-level hardware control

**Initialization Steps:**
1. Hardware reset via command registers
2. MAC address retrieval from EEPROM
3. PHY configuration and link negotiation
4. Intelligent DMA/PIO mode selection
5. Interrupt handler installation

**Intelligent DMA/PIO Mode Selection:**
1. Bus master capability testing (`config_perform_busmaster_auto_test`)
2. DMA validation via AH=97h packet driver calls
3. Runtime safety assessment:
   - Platform DMA policy check
   - VDS validation if required
   - Cache coherency verification
4. Mode decision matrix:
   ```
   ┌──────────────┬────────────┬────────────┬──────────────┐
   │ Platform     │ Bus Master │ Validation │ Result       │
   ├──────────────┼────────────┼────────────┼──────────────┤
   │ DMA_FORBID   │ Any        │ Any        │ PIO Only     │
   │ DMA_VDS      │ Pass       │ Any        │ PIO Fallback │
   │ DMA_DIRECT   │ Pass       │ Pass       │ DMA Direct   │
   │ DMA_DIRECT   │ Pass       │ Pending    │ PIO → DMA    │
   └──────────────┴────────────┴────────────┴──────────────┘
   ```
5. Hardware capability adjustment:
   - Clear HW_CAP_BUS_MASTER if DMA unsafe
   - Set dma_capable and bus_master_capable flags
   - Configure optimal FIFO thresholds per mode

### Intelligent PIO/DMA Auto-Selection

The driver implements sophisticated auto-selection between Programmed I/O (PIO)
and Direct Memory Access (DMA) modes based on comprehensive platform assessment:

**Auto-Selection Decision Flow:**
```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ CPU Detection│────►│Platform State│────►│ NIC Type     │
│   (Stage 1)  │     │   (Stage 2)  │     │  (Stage 4)   │
└──────────────┘     └──────────────┘     └──────────────┘
        │                    │                    │
        └────────────────────┼────────────────────┘
                             │
                    ┌────────▼────────┐
                    │ DMA Policy      │
                    │ Assessment      │
                    └────────┬────────┘
                             │
        ┌────────────────────┼────────────────────┐
        ▼                    ▼                    ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ FORBID       │    │ VDS Required │    │ DIRECT       │
│ →Force PIO   │    │ →Test & Use  │    │ →Test DMA    │
└──────────────┘    └──────────────┘    └──────────────┘
                             │
                    ┌────────▼────────┐
                    │ Bus Master Test │
                    │ (config_perform)│
                    └────────┬────────┘
                             │
                 ┌───────────┴───────────┐
                 ▼                       ▼
         ┌──────────────┐       ┌──────────────┐
         │ Test PASSED  │       │ Test FAILED  │
         │ →Enable DMA  │       │ →Use PIO     │
         └──────────────┘       └──────────────┘
```

**Decision Factors:**
1. **CPU Architecture**: 286 lacks cache control, 386+ provides WBINVD
2. **Platform State**: V86 mode, paging, memory managers (EMM386/QEMM)
3. **VDS Availability**: Required for address translation under paging
4. **Bus Master Testing**: Runtime validation of DMA operations
5. **Cache Coherency**: Platform-specific snooping capabilities

**Safety-First Approach:**
- Default to PIO when platform safety cannot be verified
- Require successful validation before enabling DMA
- Provide graceful fallback if DMA operations fail
- Clear diagnostic logging of mode selection reasoning

The auto-selection ensures optimal operation while maintaining
absolute stability across diverse DOS configurations.

### Stage 7: Safety Patch Application

**Primary Files:**
- `src/c/smc_safety_patches.c` - Runtime safety patches
- `src/c/nic_safety_patches.c` - NIC-specific safety patches

**Safety Optimizations:**
- VDS buffer management patches
- 64KB boundary crossing detection
- PIO fallback stubs for DMA failures
- Bounce buffer implementation for problematic addresses

### Stage 8: TSR Installation

**Purpose**: Install driver as TSR and hook system interrupts
**Location**: `src/c/main.c`, `src/api/unified_interrupt.asm`
**When**: After all hardware initialization completes

**Resident Components:**

```
┌────────────────────────────────────────────────────────┐
│                    TSR Memory Layout                   │
├────────────────────────────────────────────────────────┤
│                                                        │
│    HOT Section (Resident)                              │
│   ┌───────────────────────────────────────────────┐    │
│   │ INT 60h Packet Driver API Handler             │    │
│   ├───────────────────────────────────────────────┤    │
│   │ Hardware Interrupt Service Routines           │    │
│   ├───────────────────────────────────────────────┤    │
│   │ Packet TX/RX Fast Paths (SMC-optimized)       │    │
│   ├───────────────────────────────────────────────┤    │
│   │ Ring Buffer Management Code                   │    │
│   ├───────────────────────────────────────────────┤    │
│   │ Routing Tables & NIC State                    │    │
│   ├───────────────────────────────────────────────┤    │
│   │ DMA Descriptors & Safety Buffers              │    │
│   └───────────────────────────────────────────────┘    │
│                                                        │
│    COLD Section (Discarded After Init)                 │
│   ┌───────────────────────────────────────────────┐    │
│   │ CPU Detection & Platform Probe                │    │
│   ├───────────────────────────────────────────────┤    │
│   │ Hardware Enumeration & Detection              │    │
│   ├───────────────────────────────────────────────┤    │
│   │ Configuration Parser                          │    │
│   ├───────────────────────────────────────────────┤    │
│   │ SMC Patch Generation Engine                   │    │
│   ├───────────────────────────────────────────────┤    │
│   │ Initialization & Setup Routines               │    │
│   └───────────────────────────────────────────────┘    │
│                                                        │
└────────────────────────────────────────────────────────┘
```

**Installation Steps:**
1. Calculate resident memory requirements
2. Hook INT 60h for Packet Driver API
3. Register hardware interrupt handlers
4. Mark hot sections as resident
5. Prepare for cold section discard

### Stage 9: Cold Section Discard

**Primary Files:**
- `src/c/init.c` - Cleanup and TSR exit

**Final Steps:**
1. Release all initialization code segments
2. Free temporary allocation buffers
3. Display configuration summary to user
4. Execute TSR system call: `INT 21h, AH=31h`

#### Explicit Copy‑Down TSR Sequence (Elaborated)

Two supported residency strategies; choose based on linker/layout and desired footprint:

- Shrink‑in‑place (simpler, preferred when segments are contiguous):
  1) Compute resident paragraphs from the map file (hot code/data + stack).
  2) INT 21h AH=4Ah to resize the program’s MCB to the resident size.
  3) Install final interrupt vectors (INT 60h and any hardware ISR) to point to the resident addresses.
  4) INT 21h AH=31h to terminate and stay resident.

- Explicit copy‑down (for precise placement or to discard non‑contiguous cold blocks):
  1) Determine resident memory (paragraphs) from the map; build a relocation list of far pointers/vtables/vectors that reference code/data addresses.
  2) INT 21h AH=48h to allocate a new block for the resident image at the desired segment.
  3) Far‑copy hot code and data into the new block; keep small relocation stubs if needed.
  4) Rebase: update all far pointers, vtables, and interrupt vectors to the new segment; perform a far jump/call into the new code segment to switch CS if required.
  5) INT 21h AH=49h to free the original program block (and any discardable MCBs).
  6) INT 21h AH=31h with the size of the new resident block to TSR.

Safety notes:
- Mask interrupts during vector swaps and relocation. Install vectors only after the resident image is committed.
- Keep the relocation list minimal by ensuring hot code is position‑independent where possible; avoid absolute segment constants.
- Verify that DMA buffers and PIC/ELCR state are configured after relocation and before enabling interrupts.

## Critical Implementation Notes

### Crynwr Packet Driver Semantics

- Interrupt vector: Driver installs at an INT 60h–7Fh vector. At vector+3, the far pointer must reference a null‑terminated string "PKT DRVR" for detection tools.
- Calling convention: AH=function, BX=handle (where applicable); DS:SI and/or ES:DI point to parameter blocks per function definition. AX returns the result.
- Status signaling: CF clear on success; CF set on error with error code in AX. The ISR must update the saved FLAGS on the stack before IRET so callers observe CF correctly.
- Register discipline: Preserve registers per spec; do not invoke DOS/BIOS from the ISR; execution must be bounded and re‑entrant.
- Function coverage: Implement Packet Driver Spec v1.11 base calls; vendor extensions are exposed via higher AH values without violating base semantics.

### Bus Architecture Support

**ISA Cards:**
- **3C509B**: ISA Ethernet card with PnP support, PIO-only operation
- **3C515-TX**: ISA Fast Ethernet card with bus mastering capability

**PCI Cards (Fully Supported):**
- **3C590/3C595 (Vortex)**: First-generation PCI, PIO-only operation
- **3C900/3C905 (Boomerang)**: Bus mastering DMA support
- **3C905B (Cyclone)**: Enhanced DMA with flow control
- **3C905C (Tornado)**: Advanced features including hardware checksums

**PC Cards (Mobile Support):**
- **3C589 series**: PCMCIA 16-bit Ethernet, PIO operation
- **3C575 series**: CardBus 32-bit Fast Ethernet, bus mastering capable

### CPUID Implementation

The driver includes one of the most comprehensive CPUID implementations for DOS:
- **3610 lines** of Assembly code for low-level detection
- **78+ CPU models** with marketing names and codenames
- **Statistical speed measurement** using 5 trials
- **Vendor detection** for Intel, AMD, Cyrix, VIA, Transmeta, NexGen
- **Fallback mechanisms** for pre-CPUID processors

### SMC/JIT Optimization

The driver implements a primitive JIT compiler through SMC:
- Generates machine-specific code at runtime
- Eliminates conditional branches in hot paths
- Creates optimal instruction sequences for detected CPU
- Patches DMA routines based on chipset quirks
- Results in 20-40% performance improvement

## Boot Time Analysis

### Stage Timing by System Type

| Stage | 286 ISA | 386+ ISA | 386+ PCI | 386+ PCMCIA | Critical Path |
|-------|---------|----------|----------|-------------|---------------|
| CPU Detection | 100ms | 50ms | 50ms | 50ms | Speed measurement |
| Platform Detection | 20ms | 15ms | 20ms | 25ms | Bus enumeration |
| Memory Setup | 30ms | 20ms | 20ms | 20ms | XMS allocation |
| NIC Enumeration | 200ms | 150ms | 180ms | 250ms | Card detection |
| Configuration | 10ms | 5ms | 5ms | 5ms | Parameter parsing |
| NIC Initialization | 100ms | 80ms | 100ms | 120ms | PHY negotiation |
| SMC Optimization | 50ms | 30ms | 30ms | 30ms | Code patching |
| TSR Installation | 10ms | 5ms | 5ms | 5ms | Interrupt hooking |
| Cold Discard | 10ms | 5ms | 5ms | 5ms | Memory release |
| **Total** | **530ms** | **360ms** | **415ms** | **510ms** | **Sub-second** |

### NIC-Specific Initialization Times

| NIC Model | Bus | Detection | Init | Total | Notes |
|-----------|-----|-----------|------|-------|-------|
| 3C509B | ISA | 150ms | 80ms | 230ms | PnP isolation |
| 3C515-TX | ISA | 150ms | 100ms | 250ms | 100Base-TX setup |
| 3C590/3C595 | PCI | 50ms | 90ms | 140ms | PCI scan faster |
| 3C900/3C905 | PCI | 50ms | 100ms | 150ms | DMA setup |
| 3C905B | PCI | 50ms | 100ms | 150ms | Enhanced DMA |
| 3C905C | PCI | 50ms | 110ms | 160ms | Advanced features |
| 3C589 | PCMCIA | 200ms | 100ms | 300ms | Socket services |
| 3C575 | CardBus | 180ms | 120ms | 300ms | 32-bit CardBus |

## Success Indicators

Upon successful initialization, the driver will display:
```
3Com Packet Driver v1.0 - (C) 2024
CPU: Intel Pentium III "Katmai" 500MHz
Platform: Intel 440BX chipset, DMA-safe
Memory: XMS detected, 16MB allocated
DMA Policy: DIRECT (real mode, no paging)
Bus Master: Testing PASSED, DMA validated
NIC: 3C515-TX at I/O 0x300, IRQ 10, 100BaseTX Full-Duplex
     MAC: 00:60:08:12:34:56
     Mode: DMA enabled (bus mastering active)
TSR installed at segment 0x2000, 13KB resident
Driver ready - Packet Driver API at INT 60h
```

Alternative display when DMA is unsafe:
```
3Com Packet Driver v1.0 - (C) 2024
CPU: Intel 80386DX 33MHz
Platform: V86 mode detected, VDS not available
Memory: XMS detected, 4MB allocated
DMA Policy: FORBID (paging without VDS)
Bus Master: Disabled for safety
NIC: 3C515-TX at I/O 0x300, IRQ 10, 100BaseTX Full-Duplex
     MAC: 00:60:08:12:34:56
     Mode: PIO fallback (bus mastering disabled)
TSR installed at segment 0x2000, 12KB resident
Driver ready - Packet Driver API at INT 60h
```

## Error Handling

The driver implements a multi-level recovery strategy with exponential backoff to achieve stable operation:

### Recovery Strategy Selection

```
┌─────────────────────────────────────────────────────────────────────┐
│              Hardware Recovery Strategy Matrix                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Failure Type                    →    Recovery Strategy             │
│  ─────────────────────────            ─────────────────────────     │
│                                                                     │
│  LINK_LOST                       →    RECOVERY_SOFT_RESET           │
│  ├─► Reset PHY and MAC                                              │
│  └─► Renegotiate link parameters                                    │
│                                                                     │
│  TX_TIMEOUT/RX_TIMEOUT           →    RECOVERY_HARD_RESET           │
│  ├─► Full hardware reset sequence                                   │
│  └─► Restore configuration from backup                              │
│                                                                     │
│  FIFO_OVERRUN/DMA_ERROR          →    RECOVERY_REINITIALIZE         │
│  ├─► Complete driver cleanup                                        │
│  ├─► Full re-initialization                                         │
│  └─► Reconfigure all parameters                                     │
│                                                                     │
│  REGISTER_CORRUPTION             →    RECOVERY_REINITIALIZE         │
│  ├─► Validate all register states                                   │
│  └─► Complete hardware re-init                                      │
│                                                                     │
│  CRITICAL/REPEATED               →    RECOVERY_FAILOVER or DISABLE  │
│  ├─► Switch to backup NIC if available                              │
│  └─► Disable NIC if no backup exists                                │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Stage-Specific Error Handling

**Initialization Errors:**
- CPU below 286: Abort with clear message
- No compatible NIC: Clean exit with suggestions
- Memory allocation failure: Progressive degradation
  - Try XMS → Conventional → Minimal buffers
  - Reduce ring sizes incrementally
  - Disable non-essential features

**Runtime Error Recovery:**
- **Transmit Errors**:
  - Retry with exponential backoff (100μs, 200μs, 400μs)
  - Reset TX engine if 3 consecutive failures
  - Fall back to PIO if DMA repeatedly fails

- **Receive Errors**:
  - Clear and reinitialize RX ring
  - Adjust FIFO thresholds downward
  - Enable more conservative buffering

- **IRQ Issues**:
  - Detect spurious interrupts and mask temporarily
  - Implement interrupt rate limiting
  - Suggest alternative IRQ in diagnostics

- **DMA Failures**:
  - Immediate fallback to PIO mode
  - Cache failure reason for diagnostics
  - Attempt DMA recovery only after full reset

### Backoff Algorithms

**Exponential Backoff with Jitter:**
```
delay = min(base_delay * (2^attempt) + random_jitter, max_delay)

Where:
- base_delay = 100 microseconds
- random_jitter = 0-50 microseconds
- max_delay = 10 milliseconds
- attempt = current retry count
```

**Gradual Degradation Path:**
1. Full performance mode (all features enabled)
2. Reduce FIFO thresholds by 25%
3. Disable hardware checksums
4. Switch to PIO mode
5. Minimal operation mode (basic packet I/O only)

### State Preservation

During recovery operations, the driver preserves:
- Packet statistics and error counters
- Routing table entries
- Application handle mappings
- Last known good configuration
- Diagnostic event log

This allows returning to optimal performance once transient issues resolve, while maintaining service continuity during problems.

## References

- [Packet Driver Specification v1.11](http://crynwr.com/packet_driver.html)
- [3Com EtherLink III Technical Reference](../refs/3c509b-technical-reference.pdf)
- [ISA Plug and Play Specification v1.0a](../refs/isa-pnp-spec.pdf)
- [Intel CPUID Application Note AP-485](../refs/intel-cpuid-ap485.pdf)

---

**Document Version History:**
- v1.0 (2025-08-31): Initial comprehensive documentation
