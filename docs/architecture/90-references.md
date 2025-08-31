# 3Com Packet Driver Implementation References

## Project Status
**Current Implementation**: ~85% of required information available
**Major Enhancement**: Added comprehensive 3C509 programming guide with detailed procedures
**Remaining**: Advanced DMA programming sequences for bus mastering NICs, edge-case error handling

## Reference Materials Overview

### Hardware Specifications ✅ COMPLETE
Our existing header files provide comprehensive register definitions:

#### 3C509B (10 Mbps ISA)
- **Location**: `include/3c509b.h:1-270`
- **Coverage**: Complete register windows, command codes, status bits
- **Extended Reference**: `refs/hardware/5c509.md:1-428+` (comprehensive programming guide)
- **EISA Configuration**: `refs/hardware/eisa-config.txt` (EISA setup procedures)
- **Legacy Reference**: `refs/legacy-driver/` (original assembly implementation)
- **Key Features**:
  - Windowed register interface with 5 windows (0,1,2,4,6)
  - ISA Plug and Play detection sequence
  - Programmed I/O operation
  - Full interrupt and status management
  - **NEW**: Detailed EEPROM programming procedures
  - **NEW**: Step-by-step window selection procedures
  - **NEW**: Complete register bitfield breakdowns
  - **NEW**: Media auto-selection and manual override procedures

#### 3C515-TX (10/100 Mbps ISA with Bus Mastering)
- **Location**: `include/3c515.h:1-257`
- **Coverage**: Complete register definitions, DMA descriptors
- **Key Features**:
  - 7 register windows with bus master control (Window 7)
  - DMA descriptor structures for Rx/Tx rings
  - MII PHY interface for auto-negotiation
  - Scatter/gather DMA support

### Linux Driver References ✅ COMPLETE

#### 3C509 Implementation Details
- **Location**: `refs/linux-drivers/3c509.c`
- **Key Insights**:
  - ISA detection using ID sequence
  - Multiple card tagging during detection
  - Window-based register model usage
  - Transceiver type configuration

#### 3C59x (3C515) Implementation Details
- **Location**: `refs/linux-drivers/3c59x.c`
- **Key Insights**:
  - Bus master DMA programming
  - Descriptor ring management
  - MII PHY register access
  - Upload/Download engine operation

### Protocol Specifications ✅ COMPLETE

#### Ethernet Frame Format
- **Location**: `refs/protocols/ethernet.md`
- **Source**: RFC 894
- **Coverage**: Frame structure, size limits, type fields, special addresses
- **Implementation Constants**:
  ```c
  #define ETH_HEADER_LEN   14    // 6+6+2 bytes
  #define ETH_MIN_DATA     46    // Pad if needed
  #define ETH_MAX_DATA     1500  // Standard MTU
  #define ETH_MIN_FRAME    64    // Including CRC
  #define ETH_MAX_FRAME    1518  // Including CRC
  ```

#### ARP Protocol
- **Location**: `refs/protocols/arp.md`
- **Source**: RFC 826
- **Coverage**: Packet structure, operation codes, implementation algorithm
- **Memory Layout**: Packed C structure for DOS implementation

### DOS Programming References ✅ COMPLETE

#### TSR Programming
- **Location**: `refs/dos-references/tsr-programming.md`
- **Coverage**: Memory models, interrupt handling, DOS function safety
- **Critical Requirements**:
  - Small memory model (-ms)
  - SS != DS configuration (-zdf -zu)
  - Proper interrupt chaining
  - Memory allocation strategies

#### Packet Driver Specification
- **Location**: `refs/dos-references/packet-driver-spec.md`
- **Coverage**: INT 60h interface, function codes, error handling
- **Key Functions**: driver_info, access_type, send_pkt, receive callbacks

## Implementation Architecture

### Build System ✅ READY
- **Location**: `Makefile:1-203`
- **Features**:
  - Proper TSR memory layout (resident vs initialization code)
  - Open Watcom C + NASM configuration
  - Debug/release build targets
  - Object file categorization for memory efficiency

### Code Organization ✅ STRUCTURED
```
src/
├── asm/           # Assembly modules (8 files)
│   ├── main.asm           # TSR entry point
│   ├── packet_api.asm     # INT 60h handler
│   ├── cpu_detect.asm     # CPU feature detection
│   ├── nic_irq.asm        # Hardware interrupts
│   ├── packet_ops.asm     # Optimized operations
│   ├── hardware.asm       # Low-level I/O
│   ├── flow_routing.asm   # Fast routing table
│   └── pnp.asm           # Plug and Play detection
└── c/             # C modules (19 files)
    ├── init.c             # Driver initialization
    ├── hardware.c         # Hardware abstraction
    ├── 3c509b.c          # 3C509B NIC driver
    ├── 3c515.c           # 3C515-TX NIC driver
    └── ...               # Additional modules
```

## Critical Implementation Details

### 3C509B Programming Procedures (NEW)
From `refs/hardware/5c509.md`:
- **Window Selection**: Mandatory CMD_BUSY check before switching windows
- **EEPROM Access**: Specific read/write sequences for configuration storage
- **Media Control**: Auto-detection vs manual override procedures
- **FIFO Management**: TX free space checking, RX status validation
- **Interrupt Handling**: Proper IRQ configuration and status acknowledgment

### CPU Optimization Strategy
- **Detection**: Runtime CPU identification (286/386+/486+)
- **Optimization**: Use 0x66 prefix for 32-bit operations on 386+ in real mode
- **Fallback**: Maintain 16-bit compatibility for 286 systems

### Memory Management Architecture
- **XMS Extended**: > 1MB, highest performance buffers
- **UMB Upper**: 640KB-1MB, medium performance
- **Conventional**: < 640KB, compatibility fallback
- **Buffer Allocation**: Three-tier system with automatic fallback

### Hardware Abstraction
- **Polymorphic Design**: Virtual function tables for NIC operations
- **Runtime Selection**: Detect hardware and bind appropriate driver
- **Shared Interface**: Common API for all 65 supported 3Com NICs

## Development Priorities

### Phase 1: Core TSR Framework
1. **main.asm**: Basic TSR installation and memory layout
2. **cpu_detect.asm**: Runtime CPU feature detection
3. **init.c**: Command line parsing and hardware detection
4. **hardware.c**: Hardware abstraction layer implementation

### Phase 2: Network Interface Implementation
1. **3c509b.c**: Complete programmed I/O implementation
2. **3c515.c**: Bus master DMA implementation with 386+ detection
3. **packet_api.asm**: INT 60h Packet Driver API
4. **nic_irq.asm**: Hardware interrupt handlers

### Phase 3: Protocol Support
1. **Ethernet frame processing**: Header validation, CRC checking
2. **ARP implementation**: Address resolution table management
3. **Routing system**: Static configuration + flow-aware optimization
4. **Buffer management**: Efficient memory usage patterns

### Phase 4: Advanced Features
1. **Multi-homing**: Multiple NIC support with routing
2. **Performance optimization**: CPU-specific code paths
3. **Diagnostics**: Error reporting and statistics
4. **Configuration**: Runtime parameter adjustment

## Success Metrics
- **Memory Usage**: < 32KB resident portion
- **Performance**: > 90% of hardware capability
- **Compatibility**: Works on 286+ systems with both NICs
- **Compliance**: Full Packet Driver Specification conformance

## Next Steps
With comprehensive references now available:

1. **Begin Phase 1 Implementation**: Start with main.asm TSR framework
2. **Hardware Detection**: Implement PnP and non-PnP NIC detection
3. **Memory Layout**: Establish proper resident vs initialization code separation
4. **Interrupt Installation**: Set up INT 60h handler and hardware IRQ
5. **Basic Packet Flow**: Implement minimal send/receive functionality

The foundation is complete - all necessary technical documentation is available for full implementation.
