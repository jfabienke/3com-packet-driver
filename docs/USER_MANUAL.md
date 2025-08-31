# 3Com Packet Driver User Manual

Complete user guide for the 3Com Packet Driver supporting 3C515-TX (100 Mbps) and 3C509B (10 Mbps) network interface cards.

## Table of Contents

1. [Overview](#overview)
2. [System Requirements](#system-requirements)
3. [Installation](#installation)
4. [Basic Usage](#basic-usage)
5. [Configuration Parameters](#configuration-parameters)
6. [Supported Hardware](#supported-hardware)
7. [Application Integration](#application-integration)
8. [Memory Management](#memory-management)
9. [Multi-NIC Operations](#multi-nic-operations)
10. [Advanced Features](#advanced-features)
11. [Command Reference](#command-reference)
12. [Examples](#examples)

## Overview

The 3Com Packet Driver is a high-performance DOS TSR (Terminate and Stay Resident) program that provides Packet Driver Specification-compliant networking support for 3Com network interface cards. It enables DOS networking applications like mTCP, WatTCP, and Trumpet Winsock to access Ethernet networks through standardized INT 60h API calls.

### Key Features

- **Dual NIC Support**: 3C515-TX Fast Ethernet (100 Mbps) and 3C509B Ethernet (10 Mbps)
- **Multi-homing**: Support for multiple NICs under single interrupt
- **Application Multiplexing**: Up to 16 applications can share network access
- **Memory Efficiency**: <6KB resident footprint with XMS support
- **Auto-detection**: Plug and Play automatic hardware detection
- **Performance Optimization**: CPU-specific optimizations (8086-Pentium)
- **Flow-aware Routing**: Maintains connection symmetry across NICs

## System Requirements

### Minimum Requirements
- **Operating System**: DOS 2.0 or higher
- **Memory**: 640KB conventional memory (43KB free for basic operation)
- **CPU**: Intel 80286 or compatible
- **Network Card**: Supported 3Com network interface card

### Recommended Requirements
- **Operating System**: DOS 5.0 or higher
- **Memory**: 640KB conventional + XMS/UMB support
- **CPU**: Intel 80386 or higher (for bus mastering support)
- **Network Card**: 3C515-TX for optimal performance

### Supported DOS Environments
- MS-DOS 2.0 through 6.x
- PC-DOS 2.0 through 7.0
- DR-DOS 5.0 and higher
- Windows 3.x DOS box (limited functionality)
- Windows 95/98 DOS mode

## Installation

### Step 1: Copy Driver Files

Copy the driver files to your system:

```batch
COPY 3CPD.COM C:\NET\
COPY ETL3.MOD C:\NET\
COPY BOOMTEX.MOD C:\NET\
```

### Step 2: Load in CONFIG.SYS

Add the driver to your CONFIG.SYS file:

```
DEVICE=C:\NET\3CPD.COM
```

### Step 3: Reboot System

Restart your computer to load the driver. The driver will automatically detect and configure supported network cards.

### Alternative Loading Methods

#### Load from AUTOEXEC.BAT
```batch
C:\NET\3CPD.COM
```

#### Load with Memory Optimization
```
DEVICEHIGH=C:\NET\3CPD.COM
```

#### Load with Specific Configuration
```
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /LOG=ON
```

## Basic Usage

### Automatic Configuration (Recommended)

The simplest installation uses automatic detection:

```
DEVICE=C:\NET\3CPD.COM
```

This configuration:
- Automatically detects all supported 3Com NICs
- Configures optimal I/O addresses and IRQs
- Enables basic packet driver functionality
- Uses 43KB of conventional memory

### Manual Configuration

For systems requiring specific settings:

```
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /SPEED=100
```

### Verification

After loading, verify the driver is active:

```batch
3CPD /STATUS
```

Expected output:
```
3Com Packet Driver v1.0 - Active
NICs detected: 1
NIC 0: 3C515-TX at 0x300, IRQ 5, 100 Mbps, Link UP
```

## Configuration Parameters

### I/O and IRQ Settings

| Parameter | Description | Values | Example |
|-----------|-------------|--------|---------|
| `/IO1=addr` | First NIC I/O base | 0x200-0x3F0 | `/IO1=0x300` |
| `/IO2=addr` | Second NIC I/O base | 0x200-0x3F0 | `/IO2=0x320` |
| `/IRQ1=num` | First NIC IRQ | 3, 5, 7, 9-15 | `/IRQ1=5` |
| `/IRQ2=num` | Second NIC IRQ | 3, 5, 7, 9-15 | `/IRQ2=7` |

### Network Settings

| Parameter | Description | Values | Example |
|-----------|-------------|--------|---------|
| `/SPEED=rate` | Network speed | 10, 100, AUTO | `/SPEED=100` |
| `/DUPLEX=mode` | Duplex mode | HALF, FULL, AUTO | `/DUPLEX=FULL` |
| `/BUSMASTER=mode` | Bus mastering | ON, OFF, AUTO | `/BUSMASTER=AUTO` |

### Advanced Settings

| Parameter | Description | Values | Example |
|-----------|-------------|--------|---------|
| `/PNP=mode` | Plug and Play | ON, OFF | `/PNP=OFF` |
| `/LOG=level` | Logging level | OFF, ON, VERBOSE | `/LOG=ON` |
| `/ROUTE=rule` | Static routing | network,mask,nic | `/ROUTE=192.168.1.0,255.255.255.0,1` |

### Memory Settings

| Parameter | Description | Values | Example |
|-----------|-------------|--------|---------|
| `/XMS=mode` | XMS memory usage | ON, OFF, AUTO | `/XMS=ON` |
| `/UMB=mode` | Upper memory blocks | ON, OFF, AUTO | `/UMB=ON` |
| `/BUFFERS=count` | Buffer count | 4-32 | `/BUFFERS=16` |

## Supported Hardware

### 3C515-TX Fast Ethernet (Recommended)

**Features:**
- 100 Mbps operation with 10 Mbps fallback
- Full-duplex support
- Bus mastering DMA (requires 80286+ with chipset support)
- Auto-negotiation
- PCI interface

**Requirements:**
- Intel 80386 or higher CPU (for bus mastering)
- Available PCI slot
- 16MB+ system memory (recommended)

**Performance:**
- Throughput: Up to 95 Mbps
- Latency: <50μs interrupt response
- Memory: <4KB additional overhead

### 3C509B Ethernet

**Features:**
- 10 Mbps operation
- Half/full duplex support
- ISA interface
- Plug and Play compatible
- Low power consumption

**Requirements:**
- Intel 80286 or higher CPU
- Available ISA slot
- Standard DOS memory

**Performance:**
- Throughput: Up to 9.5 Mbps
- Latency: <100μs interrupt response
- Memory: <2KB overhead

### Hardware Detection

The driver automatically detects:
- NIC presence and type
- I/O address ranges
- IRQ assignments
- EEPROM configuration
- Link status and speed

#### Manual Detection

Force hardware detection:
```batch
3CPD /DETECT
```

Expected output:
```
Scanning for 3Com network cards...
Found: 3C515-TX at 0x300, IRQ 5
Found: 3C509B at 0x320, IRQ 7
Total NICs: 2
```

## Application Integration

### Packet Driver API Compatibility

The driver implements Packet Driver Specification v1.11:

#### Basic Functions (INT 60h)
- **Function 01h**: Driver Information
- **Function 02h**: Access Type
- **Function 03h**: Release Type
- **Function 04h**: Send Packet
- **Function 05h**: Terminate
- **Function 06h**: Get Address
- **Function 07h**: Reset Interface

#### Extended Functions
- **Function 14h**: Set Receive Mode
- **Function 15h**: Get Receive Mode
- **Function 18h**: Get Statistics
- **Function 19h**: Set Address

### Common Applications

#### TCP/IP Stacks

**mTCP (Recommended)**
```batch
SET MTCPCFG=C:\MTCP\TCP.CFG
DHCP
PING 192.168.1.1
```

**WatTCP**
```batch
SET WATTCP.CFG=C:\WATTCP\WATTCP.CFG
TCPINFO
```

**Trumpet Winsock**
```batch
REM Configure in TRUMPWSK.INI
REM Use packet driver mode
```

#### IPX/SPX Networks

**Novell NetWare**
```batch
LSL
3CPD          REM Packet driver already loaded
IPXODI
NET
```

#### NetBIOS Applications

**Microsoft Network Client**
```batch
NET START
NET USE Z: \\SERVER\SHARE
```

### Programming Interface

#### C Programming Example
```c
#include <dos.h>

/* Packet Driver interrupt */
#define PD_INT 0x60

/* Get driver info */
void get_driver_info(void) {
    union REGS regs;
    
    regs.h.ah = 1;      /* Function 1: Driver Info */
    regs.h.dl = 0;      /* Interface 0 */
    
    int86(PD_INT, &regs, &regs);
    
    if (regs.x.cflag == 0) {
        printf("Driver version: %d.%d\n", 
               regs.h.bh, regs.h.bl);
    }
}
```

#### Assembly Programming Example
```asm
; Get MAC address
mov ah, 6           ; Function 6: Get Address  
mov dl, 0           ; Interface 0
int 60h             ; Packet driver interrupt
jc error            ; Check for error
; Address returned in ES:DI
```

## Memory Management

### Memory Layout

```
┌─────────────────┐ 0xA0000
│  Video Memory   │
├─────────────────┤ 0x9FC00
│  BIOS Data      │
├─────────────────┤ 0x9F800
│  Driver TSR     │ ← 3Com Packet Driver (6KB)
├─────────────────┤ 0x9E000
│  Available      │
│  DOS Memory     │
├─────────────────┤ 0x00400
│  Interrupt      │
│  Vectors        │
└─────────────────┘ 0x00000
```

### Memory Options

#### Conventional Memory (Default)
- **Size**: 43KB basic, 59KB standard, 69KB advanced
- **Compatibility**: All DOS systems
- **Performance**: Good

#### XMS Memory (Recommended)
```
DEVICE=C:\NET\3CPD.COM /XMS=ON
```
- **Size**: 6KB resident, buffers in XMS
- **Compatibility**: Requires HIMEM.SYS
- **Performance**: Excellent

#### Upper Memory Blocks
```
DEVICEHIGH=C:\NET\3CPD.COM /UMB=ON
```
- **Size**: Loaded in UMB area
- **Compatibility**: Requires EMM386 or QEMM
- **Performance**: Optimal

### Memory Configuration Examples

#### Minimum Memory System
```
DEVICE=C:\NET\3CPD.COM /BUFFERS=4
```
Memory usage: 35KB

#### Standard Configuration
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.COM /XMS=ON
```
Memory usage: 6KB resident + XMS buffers

#### Optimized Configuration
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE RAM
DEVICEHIGH=C:\NET\3CPD.COM /XMS=ON /UMB=ON
```
Memory usage: UMB resident + XMS buffers

## Multi-NIC Operations

### Dual NIC Configuration

For systems with two 3Com NICs:

```
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7
```

### Load Balancing

#### Round-Robin Mode
```
DEVICE=C:\NET\3CPD.COM /ROUTE=BALANCE
```

#### Performance-Based
```
DEVICE=C:\NET\3CPD.COM /ROUTE=PERFORMANCE
```

#### Manual Routing
```
DEVICE=C:\NET\3CPD.COM /ROUTE=192.168.1.0,255.255.255.0,1 /ROUTE=10.0.0.0,255.0.0.0,2
```

### Failover Support

The driver automatically handles NIC failures:

1. **Link Detection**: Continuous monitoring of link status
2. **Automatic Switchover**: Traffic rerouted to working NIC
3. **Recovery**: Automatic return when failed NIC recovers
4. **Application Transparency**: No application changes required

### Multi-homing Example

```batch
REM Configure primary and backup NICs
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7

REM Set static routes
REM Office network via NIC 1
3CPD /ROUTE ADD 192.168.1.0 255.255.255.0 1

REM Internet via NIC 2  
3CPD /ROUTE ADD 0.0.0.0 0.0.0.0 2
```

## Advanced Features

### CPU Optimization

The driver includes CPU-specific optimizations:

#### 8086/8088 Mode
- Basic packet operations
- Minimal memory usage
- Compatible instruction set

#### 80286 Mode
- Enhanced interrupt handling
- Improved memory management
- Protected mode compatibility

#### 80386+ Mode
- Bus mastering support (3C515-TX)
- 32-bit operations
- DMA optimizations

### Bus Mastering (3C515-TX Only)

#### Automatic Configuration (Recommended)
```
DEVICE=C:\NET\3CPD.COM /BUSMASTER=AUTO
```

#### Manual Control
```
DEVICE=C:\NET\3CPD.COM /BUSMASTER=ON /BM_TEST=FULL
```

#### Test Options
- `/BM_TEST=FULL`: 45-second comprehensive test
- `/BM_TEST=QUICK`: 10-second basic test
- `/BM_TEST=OFF`: Skip testing

### Performance Monitoring

#### Real-time Statistics
```batch
3CPD /STATS
```

#### Continuous Monitoring
```batch
3CPD /MONITOR 5
```

#### Performance Logging
```
DEVICE=C:\NET\3CPD.COM /LOG=VERBOSE /LOGFILE=C:\NET.LOG
```

### Promiscuous Mode

For network monitoring applications:

```batch
3CPD /PROMISC ON
```

**Note**: Requires administrative privileges and compatible hardware.

## Command Reference

### Driver Loading

| Command | Description | Example |
|---------|-------------|---------|
| `3CPD.COM` | Load driver | `3CPD.COM /IO1=0x300` |
| `3CPD /UNLOAD` | Unload driver | `3CPD /UNLOAD` |
| `3CPD /RELOAD` | Reload configuration | `3CPD /RELOAD` |

### Status Commands

| Command | Description | Output |
|---------|-------------|--------|
| `3CPD /STATUS` | Show driver status | NIC status and configuration |
| `3CPD /DETECT` | Detect hardware | List all detected NICs |
| `3CPD /VERSION` | Show version | Driver version information |
| `3CPD /INFO` | Detailed information | Complete system information |

### Configuration Commands

| Command | Description | Example |
|---------|-------------|---------|
| `3CPD /CONFIG` | Show configuration | Display current settings |
| `3CPD /RESET` | Reset NICs | Reinitialize hardware |
| `3CPD /TEST` | Hardware test | Test NIC functionality |

### Statistics Commands

| Command | Description | Example |
|---------|-------------|---------|
| `3CPD /STATS` | Show statistics | Display packet counters |
| `3CPD /CLEAR` | Clear statistics | Reset all counters |
| `3CPD /MONITOR time` | Monitor performance | `3CPD /MONITOR 10` |

### Routing Commands

| Command | Description | Example |
|---------|-------------|---------|
| `3CPD /ROUTE` | Show routing table | Display current routes |
| `3CPD /ROUTE ADD` | Add route | `/ROUTE ADD 192.168.1.0 255.255.255.0 1` |
| `3CPD /ROUTE DEL` | Delete route | `/ROUTE DEL 192.168.1.0` |

## Examples

### Example 1: Basic Single NIC Setup

**Hardware**: Single 3C509B at default settings

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.COM
```

**Verification**:
```batch
3CPD /STATUS
```

**Expected Output**:
```
3Com Packet Driver v1.0 - Active
Memory: 6KB resident (XMS buffers)
NICs: 1 detected, 1 active
NIC 0: 3C509B at 0x300, IRQ 5, 10 Mbps, Full Duplex, Link UP
```

### Example 2: Dual NIC with Load Balancing

**Hardware**: 3C515-TX (primary) + 3C509B (backup)

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICEHIGH=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7 /ROUTE=BALANCE
```

**Testing**:
```batch
REM Test primary NIC
PING -S 192.168.1.100 192.168.1.1

REM Test load balancing
3CPD /MONITOR 30
```

### Example 3: High-Performance Configuration

**Hardware**: 3C515-TX with bus mastering

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICEHIGH=C:\NET\3CPD.COM /BUSMASTER=AUTO /BM_TEST=FULL /SPEED=100 /DUPLEX=FULL /BUFFERS=32
```

**Optimization**:
```batch
REM Set interrupt priorities
3CPD /IRQ_PRIORITY HIGH

REM Enable performance monitoring
3CPD /MONITOR 60 /LOG=PERFORMANCE
```

### Example 4: Network Monitoring Setup

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.COM /PROMISC=ON /BUFFERS=64 /LOG=VERBOSE
```

**Usage**:
```batch
REM Start packet capture
3CPD /CAPTURE START /FILE=PACKETS.CAP

REM Monitor for 5 minutes
3CPD /MONITOR 300

REM Stop capture
3CPD /CAPTURE STOP
```

### Example 5: Multiple Applications

**Applications**: mTCP + IPX games + NetBIOS file sharing

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.COM /HANDLES=16
```

**AUTOEXEC.BAT**:
```batch
REM Load TCP/IP stack
SET MTCPCFG=C:\MTCP\TCP.CFG
DHCP

REM Load IPX stack
LSL
IPXODI
NETX

REM Start NetBIOS
NET START
```

**Verification**:
```batch
REM Check active handles
3CPD /HANDLES

REM Monitor multiplexing
3CPD /STATS
```

---

For troubleshooting, configuration examples, performance tuning, and API reference, see the companion documentation:

- [Configuration Guide](CONFIGURATION.md) - Detailed configuration examples
- [Troubleshooting Guide](TROUBLESHOOTING.md) - Problem resolution
- [Performance Tuning Guide](PERFORMANCE_TUNING.md) - Optimization strategies  
- [API Reference](API_REFERENCE.md) - Programming interface

**Technical Support**: For additional assistance, consult the project documentation or visit the project repository.