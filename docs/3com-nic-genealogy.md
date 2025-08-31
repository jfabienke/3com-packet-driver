# 3Com Network Interface Card Genealogy (1981-2004)

This comprehensive genealogy traces the complete evolution of 3Com's network interface card architectures from their first Ethernet adapter through their final in-house designs. The progression shows 3Com's transformation from simple 8-bit ISA adapters to sophisticated processor-based NICs.

## Complete Chronological Timeline

### Pre-Modern Era (1981-1992)

**3c501 "EtherLink"** (GA: 1981)
- Architecture: 8-bit ISA, single buffer design
- Chipset: Custom 3Com design
- Features: First commercial Ethernet adapter, no DMA
- Legacy: Historic importance despite poor performance
- Successor: 3c503

**3c503 "EtherLink II"** (GA: 1985)  
- Architecture: 8/16-bit ISA with shared memory
- Chipset: National DP8390
- Features: Shared memory buffer, major performance improvement
- Legacy: Established 3Com as serious Ethernet vendor
- Successor: 3c505

**3c505 "EtherLink Plus"** (GA: 1987)
- Architecture: 16-bit ISA with onboard 80186 processor
- Features: First intelligent NIC, protocol offloading
- Legacy: Early concept of embedded processing
- Performance: Complex but powerful for servers
- Successor: 3c507

**3c507 "EtherLink 16"** (GA: 1989)
- Architecture: 16-bit ISA, Intel 82586 chipset
- Features: Part of Parallel Tasking architecture
- Performance: 32KB shared memory buffer
- Target: Server and workstation markets
- Successor: 3c507-TP variants

**3c523 "EtherLink/MC"** (GA: 1988)
- Architecture: MicroChannel for IBM PS/2
- Chipset: Intel 82586
- Features: High-performance bus architecture
- Market: IBM-compatible servers
- Legacy: Parallel development to ISA line

### Parallel Tasking II Era (1992-1994)

**3c507-TP Enhanced** (GA: 1992)
- Architecture: 16-bit ISA with enhanced buffering
- Features: 10BaseT support, 32KB memory
- Performance: Improved interrupt handling
- Variants: VLB and EISA server versions
- Successor: 3c509 auto-detection architecture

**3c529 "EtherLink III/MC"** (GA: 1993)
- Architecture: MicroChannel with window registers
- Features: First implementation of windowed architecture
- Legacy: Testing ground for 3c509 concepts
- Target: IBM PS/2 and compatible systems
- Successor: Merged into 3c509 family

### ISA Auto-Detection Revolution (1993-1997)

**3c509 "EtherLink III"** (GA: 1993)
- Architecture: ISA with revolutionary auto-detection
- Features: LFSR-based ID sequence, window registers
- Innovation: Software-configurable without jumpers
- Performance: Programmed I/O, multi-media support
- Variants: 3c509B (enhanced), 3c509-TP, 3c509-Combo
- Legacy: Foundation for all future 3Com designs
- Successor: 3c515 and PCI transition

**3c589 Series "EtherLink III PCMCIA"** (GA: 1993-1994)
- Architecture: 16-bit PCMCIA (ISA-based) with EtherLink III core
- Features: Same 3c509 controller in PCMCIA form factor
- Performance: 10Mbps with 8KB SRAM buffer
- Variants:
  - **3c589**: Original PCMCIA version
  - **3c589B**: Enhanced version (no full-duplex)
  - **3c589C**: Combo 10Base-T/10Base-2
  - **3c589D**: Combo with full-duplex support
- Market: Early laptop networking before built-in NICs
- Innovation: Brought EtherLink III to mobile computing
- Legacy: Template for laptop networking solutions

**3c562 "EtherLink III LAN+Modem"** (GA: 1994)
- Architecture: 16-bit PCMCIA combo card with dual functions
- Features: EtherLink III networking + 14.4k/28.8k modem
- Performance: 10Mbps Ethernet using 3c589 core
- Innovation: Space-saving dual-function PCMCIA design
- Market: Mobile professionals requiring both networking and dial-up
- Technical: Ethernet portion identical to 3c589 series
- Legacy: Early convergence device concept

**3c574 "Fast EtherLink PCMCIA"** (GA: 1996-1997)
- Architecture: 16-bit PCMCIA Fast Ethernet
- Features: 10/100Mbps in PCMCIA form factor
- Performance: Enhanced controller with parallel tasking
- Innovation: Fast Ethernet for mobile computing
- Market: High-performance laptop networking
- Technical: Advanced architecture beyond basic 3c589 design
- Legacy: Bridge to CardBus technology

**3c515 "Corkscrew"** (GA: 1996)
- Architecture: ISA Fast Ethernet with bus mastering
- Features: Boomerang DMA adapted for ISA constraints
- Innovation: Bus mastering on legacy ISA bus
- Performance: 100Mbps with descriptor rings
- Legacy: Last major ISA-only design
- Market: Bridge technology for ISA systems

### EtherLink XL Era - Vortex Generation (1995-1997)

**3c590 "Vortex"** (GA: 1995)
- Architecture: PCI with programmed I/O FIFO
- Features: First PCI design, window registers
- Performance: 10Mbps with improved buffering
- Marketing: EtherLink III PCI
- Innovation: PCI implementation of proven ISA architecture
- Successor: 3c595 Fast Ethernet

**3c592 "EISA Demon/Vortex"** (GA: 1995)
- Architecture: EISA bus with Vortex core
- Features: Enhanced buffering for servers
- Performance: Server-optimized timing
- Market: High-end workstations and servers
- Legacy: Parallel development to PCI line

**3c595 "Vortex 100"** (GA: 1996)
- Architecture: PCI Fast Ethernet, programmed I/O
- Features: 100Mbps with media auto-detection
- Variants: 595-TX, 595-T4, 595-MII
- Marketing: Fast EtherLink XL
- Innovation: Unified driver architecture foundation
- Successor: 3c905 with bus mastering

**3c597 "EISA Fast Demon"** (GA: 1996)
- Architecture: EISA 100Mbps variant
- Features: Server-oriented enhancements
- Performance: High-speed EISA implementation
- Market: Enterprise servers
- Legacy: Final EISA design

### EtherLink XL Era - Boomerang Generation (1997-1998)

**3c900 "Boomerang"** (GA: 1997)
- Architecture: Full bus-master DMA with descriptor rings
- Features: Zero-copy networking, scatter-gather DMA
- Performance: 10Mbps with minimal CPU overhead
- Variants: 900-TPO, 900-Combo, 900-TPC
- Marketing: EtherLink XL
- Innovation: Modern DMA networking architecture
- Successor: 3c905 and Cyclone generation

**3c905 "Boomerang 100"** (GA: 1997)
- Architecture: 100Mbps bus-master DMA
- Features: High-performance descriptor rings
- Variants: 905-TX, 905-T4
- Performance: Full line-rate 100Mbps
- Legacy: Core of Donald Becker's unified driver
- Innovation: Template for all future designs
- Successor: 3c905B Cyclone

### Hurricane Architecture - Cyclone Generation (1998-1999)

**3c900B/3c905B "Cyclone"** (GA: 1998)
- Architecture: Enhanced bus-master with hardware acceleration
- Features: Hardware checksumming, NWAY auto-negotiation
- Performance: Zero-copy with offload engines
- Variants: 905B-TX, 905B-T4, 905B-FX, 900B-FL
- Marketing: EtherLink XL 10/100
- Innovation: Hardware protocol acceleration
- Successor: 3c905C Tornado

**3c918 "Cyclone LOM"** (GA: 1998)
- Architecture: LAN-on-Motherboard Cyclone variant
- Features: Embedded chipset integration
- Market: OEM motherboard manufacturers
- Performance: Standard Cyclone with space optimization
- Legacy: Template for embedded networking

**3c980 "Cyclone Server"** (GA: 1999)
- Architecture: Server-optimized Cyclone
- Features: Enhanced reliability and management
- Variants: 980-TX, 980B-TX
- Performance: Tuned for server workloads
- Market: Enterprise and data center
- Successor: 3c980C Python-T

**3c555 "Laptop Hurricane"** (GA: 1999)
- Architecture: MiniPCI/CardBus derivative of Cyclone
- Features: 8-bit EEPROM, power optimization
- Performance: Mobile-tuned for battery life
- Market: Laptop and portable systems
- Note: Not a separate architecture, but Cyclone adaptation
- Successor: 3c556 Tornado variant

### Hurricane Architecture - Tornado Generation (2000-2002)

**3c905C "Tornado"** (GA: 2000)
- Architecture: Enhanced Cyclone with advanced features
- Features: VLAN support (802.1Q), improved error recovery
- Performance: Optimized interrupt handling
- Variants: 905C-TX, 905CX-TX
- Innovation: VLAN hardware support
- Legacy: Final mainstream desktop NIC design

**3c920 "Tornado LOM"** (GA: 2001)
- Architecture: Embedded LAN-on-Motherboard solution
- Features: Chipset integration for motherboards
- Market: OEM partnerships, not graphics-related
- Performance: Standard Tornado in embedded package
- Legacy: Transition to outsourced designs

**3c556 "Laptop Tornado"** (GA: 2000)
- Architecture: MiniPCI Tornado derivative
- Features: Enhanced power management
- Variants: 556B with improved ACPI support
- Performance: Mobile-optimized Tornado
- Market: High-end laptop systems

**3c980C "Python-T"** (GA: 2001)
- Architecture: Server-class Tornado implementation
- Features: Advanced management, enhanced reliability
- Performance: Still 10/100Mbps, not Gigabit-ready
- Market: Enterprise servers requiring proven technology
- Legacy: Final pure 3Com server design

**3c982 "Hydra"** (GA: 2001)
- Architecture: Dual-port server adapter
- Features: Two Tornado chips on single card
- Performance: Load balancing and failover support
- Market: High-availability server environments
- Innovation: Multi-port architecture

### CardBus/PCMCIA Family (1998-2002)

**3c575 "Megahertz CardBus"** (GA: 1998)
- Architecture: CardBus adaptation of Boomerang
- Features: PCMCIA 3.0 compliance
- Performance: Mobile-optimized DMA
- Brand: Acquired Megahertz technology
- Market: Business laptops

**3CCFE575BT "CardBus Cyclone"** (GA: 1999)
- Architecture: CardBus Cyclone implementation
- Features: Hardware checksumming in mobile form
- Performance: Full Cyclone features in CardBus
- Market: High-end mobile workstations

**3CCFE575CT "CardBus Tornado"** (GA: 2000)
- Architecture: CardBus Tornado variant
- Features: VLAN support in mobile package
- Performance: Complete Tornado feature set
- Market: Enterprise mobile users

**3CCFE656 "Cyclone CardBus"** (GA: 2000)
- Architecture: Enhanced CardBus Cyclone
- Features: Improved power management
- Performance: Extended battery life optimization
- Market: Mainstream business laptops

**3CCFEM656B "Cyclone+Winmodem"** (GA: 2000)
- Architecture: Integrated networking and modem
- Features: Dual-function card for space savings
- Performance: Cyclone networking with 56K modem
- Market: Ultra-portable systems

**3CXFEM656C "Tornado+Winmodem"** (GA: 2001)
- Architecture: Tornado with integrated modem
- Features: VLAN support plus modem functionality
- Performance: Full Tornado plus communications
- Market: Mobile professional systems

### 3XP Processor Era (2001-2004)

**3CR990 "Typhoon" Series** (GA: 2001)
- Architecture: Revolutionary 3XP embedded processor
- Features: Firmware-driven networking, hardware IPsec
- Performance: Advanced protocol offloading
- Innovation: Programmable network processor
- Variants:
  - **3CR990-TX-95**: DES cryptographic acceleration
  - **3CR990-TX-97**: 3DES cryptographic acceleration  
  - **3CR990-FX-95**: Fiber optic with DES crypto
  - **3CR990-FX-97**: Fiber optic with 3DES crypto
  - **3CR990SVR95/97**: Server variants with crypto
- Legacy: Forerunner of modern SmartNIC designs
- Market: High-security and high-performance environments

### Outsourced Gigabit Era (2002-2004)

**3c2000 "Mariner"** (GA: 2002)
- Architecture: Gigabit Ethernet using Broadcom BCM5701
- Features: Hardware TCP/IP offload (TOE)
- Performance: Gigabit with CPU offloading
- Variants: 3c2000-T (copper), 3c996-T
- Note: Not true 3Com silicon, rebadged Broadcom
- Market: Gigabit server transition
- Legacy: End of in-house 3Com NIC development

**3c940 "Gigabit LOM"** (GA: 2003)
- Architecture: Marvell Yukon chipset integration
- Features: OEM motherboard embedding
- Performance: Standard Gigabit Ethernet
- Market: Motherboard manufacturers
- Legacy: Final 3Com-branded NIC design

## Architecture Evolution Paths

```
Pre-Modern (1981-1992)
3c501 → 3c503 → 3c505 → 3c507 → Parallel Tasking II
                                        ↓
ISA Auto-Detection (1993-1997)         ↓
3c529 ← ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ → 3c509 → 3c515
                                        ├── PCMCIA: 3c589 → 3c562
                                        └── PCMCIA: 3c574
                                        ↓
PCI Transition (1995-1998)             ↓
3c590 → 3c595 → 3c900 → 3c905 ← ─ ─ ─ ─ ┘
   ↓       ↓       ↓       ↓
EISA:   3c592   3c597      │
                           ↓
Hurricane Architecture (1998-2002)
Cyclone: 3c900B/905B → 3c905C Tornado
    ├── Server: 3c980 → 3c980C → 3c982
    ├── Mobile: 3c555 → 3c556
    ├── LOM: 3c918 → 3c920
    └── CardBus: 3c575 → 3CCFE575xx → 3CCFE656xx
                           ↓
3XP Processor (2001-2004)  ↓
3CR990 Typhoon Series ← ─ ─ ┘
                           ↓
Outsourced Gigabit (2002-2004)
3c2000 Mariner (Broadcom) → 3c940 (Marvell)
```

## Key Marketing Names vs Technical Architecture Names

| Marketing Name | Technical Name | Architecture | Era |
|---|---|---|---|
| EtherLink | 3c501 | 8-bit ISA | 1981 |
| EtherLink II | 3c503 | DP8390 ISA | 1985 |
| EtherLink Plus | 3c505 | 80186 Intelligent | 1987 |
| EtherLink 16 | 3c507 | 82586 Parallel Tasking | 1989 |
| EtherLink III | 3c509 family | ISA Auto-Detection | 1993 |
| EtherLink III PCI | 3c590 | Vortex | 1995 |
| Fast EtherLink XL | 3c595 | Vortex 100 | 1996 |
| EtherLink XL | 3c900/905 | Boomerang | 1997 |
| EtherLink XL 10/100 | 3c905B | Cyclone | 1998 |
| Hurricane | Cyclone/Tornado | Marketing umbrella | 1998-2002 |
| 3Com Typhoon | 3CR990 | 3XP Processor | 2001 |

## Architecture Characteristics Summary

### ISA Era (1981-1997)
- **Challenge**: Manual configuration, resource conflicts
- **Innovation**: Auto-detection protocols (3c509)
- **Performance**: Programmed I/O limitations
- **Legacy**: Established windowed register model

### PCI Vortex Era (1995-1997)  
- **Challenge**: Transition from ISA to PCI
- **Innovation**: Unified register architecture
- **Performance**: Programmed I/O with better buffering
- **Legacy**: Foundation for unified driver

### PCI Boomerang Era (1997-1998)
- **Challenge**: High-performance networking demands
- **Innovation**: Full bus-master DMA
- **Performance**: Zero-copy networking
- **Legacy**: Modern DMA architecture template

### Hurricane Era (1998-2002)
- **Challenge**: Protocol processing overhead
- **Innovation**: Hardware acceleration engines
- **Performance**: CPU offloading
- **Legacy**: Hardware offload standardization

### 3XP Era (2001-2004)
- **Challenge**: Complex protocol requirements
- **Innovation**: Programmable network processors
- **Performance**: Firmware-driven acceleration
- **Legacy**: Forerunner of SmartNIC designs

### Outsourced Era (2002-2004)
- **Challenge**: Gigabit performance requirements
- **Solution**: Partner with silicon vendors
- **Performance**: Leveraged external expertise
- **Legacy**: End of 3Com's NIC development

## Successor Relationships and Influence

**Direct Evolution Lines:**
1. **ISA**: 3c501 → 3c503 → 3c505 → 3c507 → 3c509 → 3c515
2. **PCMCIA (16-bit)**: 3c589 series → 3c562 (combo) → 3c574 (Fast Ethernet)
3. **PCI**: 3c590 → 3c595 → 3c900 → 3c905 → 3c905B → 3c905C
4. **Server**: 3c507 → 3c980 → 3c980C → 3c982
5. **Mobile (32-bit)**: 3c575 → 3c555 → 3c556 (with CardBus variants)
6. **Embedded**: 3c918 → 3c920 → 3c940

**Architectural Influence:**
- **Window Registers**: 3c509 → All subsequent designs
- **Auto-Detection**: 3c509 → PnP standards
- **Bus Mastering**: 3c515/3c900 → All high-performance NICs
- **Hardware Offload**: Cyclone → Modern acceleration
- **Unified Drivers**: 3c59x → Linux networking standards
- **Embedded Processing**: 3CR990 → SmartNIC evolution

## Legacy and Modern Relevance

The 3Com NIC genealogy represents one of the most complete evolutions in networking history, spanning from the earliest Ethernet adapters through sophisticated processor-based designs. Key contributions include:

1. **Auto-Detection Protocols**: 3c509's LFSR-based detection influenced Plug-and-Play standards
2. **Unified Driver Architecture**: 3c59x driver became template for multi-generation support
3. **Hardware Acceleration**: Hurricane architecture established offload engine patterns
4. **Embedded Processing**: 3XP processors pioneered programmable networking concepts
5. **Power Management**: Mobile variants established laptop networking standards

While 3Com exited the NIC business in 2004, their architectural innovations continue to influence modern networking hardware, particularly in SmartNIC designs that echo the 3XP processor concept developed two decades earlier.

The complete genealogy demonstrates how sustained innovation over 23 years transformed simple 8-bit adapters into sophisticated network processors, establishing patterns and principles that remain relevant in contemporary networking hardware design.