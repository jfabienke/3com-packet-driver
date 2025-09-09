# Configuration Guide

Last Updated: 2025-09-04
Status: canonical
Purpose: Configuration options, examples, and scenarios for the 3Com DOS Packet Driver.

## Table of Contents

1. [Basic Configurations](#basic-configurations)
2. [Memory Optimization](#memory-optimization)
3. [Multi-NIC Configurations](#multi-nic-configurations)
4. [Performance Configurations](#performance-configurations)
5. [Application-Specific Configurations](#application-specific-configurations)
6. [Advanced Configurations](#advanced-configurations)
7. [Troubleshooting Configurations](#troubleshooting-configurations)
8. [Environment-Specific Examples](#environment-specific-examples)

## Basic Configurations

### Minimal Configuration (Plug and Play)

**Use Case**: Simple single NIC setup with automatic detection

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE
```

**Memory Usage**: 43KB conventional memory
**Features**: Basic packet driver functionality
**Compatibility**: All supported NICs and DOS versions

---

### Standard Configuration

**Use Case**: Reliable operation with common features

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON /LOG=ON
```

**Memory Usage**: 6KB resident + XMS buffers
**Features**: XMS memory support, basic logging
**Benefits**: Minimal conventional memory usage

---

### Enterprise Configuration

**Use Case**: Business environment with full features

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICEHIGH=C:\NET\3CPD.EXE /XMS=ON /UMB=ON /BUFFERS=16 /LOG=VERBOSE
```

**Memory Usage**: UMB resident + XMS buffers
**Features**: All memory optimizations, detailed logging
**Benefits**: Maximum conventional memory available

## Memory Optimization

### Low Memory Systems (<512KB)

**Challenge**: Limited conventional memory available

**Solution 1 - Minimal Features**:
```
DEVICE=C:\NET\3CPD.EXE /BUFFERS=4 /LOG=OFF
```
Memory usage: 35KB

**Solution 2 - Basic XMS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON /BUFFERS=6
```
Memory usage: 6KB resident + XMS

---

### Standard Memory Systems (512-640KB)

**Recommended Configuration**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON /BUFFERS=16
```

**With Upper Memory**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICEHIGH=C:\NET\3CPD.EXE /XMS=ON /UMB=ON
```

---

### High Memory Systems (>1MB)

**Optimal Configuration**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS UMB I=B000-B7FF
DEVICEHIGH=C:\NET\3CPD.EXE /XMS=ON /UMB=ON /BUFFERS=32 /LOG=VERBOSE
```

**Features Enabled**:
- Maximum buffer allocation
- Comprehensive logging
- Zero conventional memory usage
- All advanced features

---

### Memory Allocation Table

| Configuration | Resident | Buffers | Total | Features |
|---------------|----------|---------|-------|----------|
| Minimal | 35KB | Conv | 35KB | Basic only |
| Standard | 43KB | Conv | 43KB | Standard features |
| XMS Basic | 6KB | XMS | 6KB+XMS | Memory optimized |
| UMB Optimal | 0KB | XMS | XMS only | All features |

## Multi-NIC Configurations

### Dual NIC - Load Balancing

**Hardware**: Two identical NICs for increased bandwidth

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7 /ROUTE=BALANCE
```

**Load Balancing Modes**:
```
/ROUTE=ROUND_ROBIN    # Alternate packets between NICs
/ROUTE=WEIGHTED       # Weight based on NIC performance  
/ROUTE=PERFORMANCE    # Dynamic based on current load
/ROUTE=FLOW_AWARE     # Maintain connection symmetry
```

**Verification**:
```batch
3CPD /STATUS
3CPD /STATS
```

---

### Dual NIC - Failover/Redundancy

**Hardware**: Primary + backup NIC for fault tolerance

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7 /ROUTE=FAILOVER
```

**Advanced Failover**:
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7 /FAILOVER_TIME=1000 /RECOVERY_TIME=5000
```

**Parameters**:
- `/FAILOVER_TIME=ms`: Time before switching to backup
- `/RECOVERY_TIME=ms`: Time before switching back to primary

---

### Dual NIC - Network Separation

**Use Case**: Separate networks (LAN + Internet, Office + Lab)

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7
```

**Routing Configuration**:
```batch
REM Office network via NIC 1
3CPD /ROUTE ADD 192.168.1.0 255.255.255.0 1

REM Lab network via NIC 2
3CPD /ROUTE ADD 10.0.0.0 255.0.0.0 2

REM Default route via NIC 1
3CPD /ROUTE ADD 0.0.0.0 0.0.0.0 1
```

---

### Mixed NIC Configuration

**Hardware**: 3C515-TX (fast) + 3C509B (backup)

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /SPEED1=100 /DUPLEX1=FULL /IO2=0x320 /IRQ2=7 /SPEED2=10 /DUPLEX2=HALF
```

**Optimization**:
```
REM Prefer faster NIC for bulk transfers
/ROUTE=PERFORMANCE

REM Enable bus mastering on 3C515-TX only
/BUSMASTER1=AUTO /BUSMASTER2=OFF
```

## Performance Configurations

### Maximum Throughput (3C515-TX)

**Use Case**: High-speed data transfer applications

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /SPEED=100 /DUPLEX=FULL /BUSMASTER=AUTO /BM_TEST=FULL /BUFFERS=32 /TX_THRESH=LOW /RX_THRESH=HIGH
```

**Parameters**:
- `/SPEED=100`: Force 100 Mbps operation
- `/DUPLEX=FULL`: Enable full-duplex mode
- `/BUSMASTER=AUTO`: Automatic bus mastering configuration
- `/BM_TEST=FULL`: Complete capability testing (45 seconds)
- `/BUFFERS=32`: Maximum buffer allocation
- `/TX_THRESH=LOW`: Low transmit threshold for quick sends
- `/RX_THRESH=HIGH`: High receive threshold for efficiency

**Expected Performance**:
- Throughput: 85-95 Mbps
- Latency: <50μs
- CPU usage: <10%

---

### Low Latency (Real-time Applications)

**Use Case**: Gaming, real-time control, VoIP

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_PRIORITY=HIGH /BUFFERS=8 /TX_THRESH=IMMEDIATE /RX_THRESH=LOW /INTERRUPT_MITIGATION=OFF
```

**Parameters**:
- `/IRQ_PRIORITY=HIGH`: Highest interrupt priority
- `/BUFFERS=8`: Minimal buffering for low latency
- `/TX_THRESH=IMMEDIATE`: Send packets immediately
- `/RX_THRESH=LOW`: Process packets as soon as received
- `/INTERRUPT_MITIGATION=OFF`: No interrupt coalescing

**Expected Performance**:
- Latency: <20μs
- Jitter: <5μs
- Real-time response

---

### High Connection Count

**Use Case**: BBS systems, multiple simultaneous connections

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /HANDLES=32 /BUFFERS=64 /HASH_SIZE=256 /CONNECTION_POOL=128
```

**Parameters**:
- `/HANDLES=32`: Support up to 32 applications
- `/BUFFERS=64`: Large buffer pool for queuing
- `/HASH_SIZE=256`: Larger hash table for connection tracking
- `/CONNECTION_POOL=128`: Pre-allocated connection structures

**Memory Impact**: Additional 15KB resident + XMS buffers

---

### CPU Optimization

#### 8086/8088 Systems
```
DEVICE=C:\NET\3CPD.EXE /CPU=8086 /BUFFERS=4 /CHECKSUMS=OFF /FLOW_CONTROL=OFF
```

#### 80286 Systems
```
DEVICE=C:\NET\3CPD.EXE /CPU=80286 /BUFFERS=8 /CHECKSUMS=ON /FLOW_CONTROL=ON
```

#### 80386+ Systems (with 3C515-TX)
```
DEVICE=C:\NET\3CPD.EXE /CPU=80386 /BUSMASTER=AUTO /BUFFERS=16 /CHECKSUMS=HW /DMA_CHANNELS=2
```

## Application-Specific Configurations

### mTCP (TCP/IP Internet Access)

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON /BUFFERS=16 /MTU=1500
```

**Environment Setup**:
```batch
SET MTCPCFG=C:\MTCP\TCP.CFG
SET PACKETINT=0x60
```

**MTCP.CFG Example**:
```
PACKETINT 0x60
HOSTNAME DOSBOX
MTU 1500
TCPBUFSIZE 4096
TCPSOCKETBUFSIZE 4096
UDPBUFSIZE 2048
```

---

### Novell NetWare (IPX/SPX)

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /PROTOCOL=IPX /FRAME=802.2 /BUFFERS=12
```

**Protocol Stack**:
```batch
LSL
ETH_II
IPXODI
NETX
```

**Alternative Using PDIPX**:
```batch
LSL
PDIPX
NETX
```

---

### Windows 3.x Enhanced Mode

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /WINDOWS=ENHANCED /BUFFERS=8 /VIRT86=ON
```

**System.ini**:
```ini
[386Enh]
device=vnetbios.386
TimerCriticalSection=10000
```

**Limitations**:
- Reduced performance in Windows
- Some advanced features disabled
- Compatibility mode operation

---

### DOS Games (IPX Gaming)

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /GAME_MODE=ON /LATENCY=LOW /BUFFERS=4
```

**Features**:
- Optimized for low latency
- Minimal memory usage
- IPX broadcast support
- Reduced interrupt overhead

---

### Network Boot (Diskless Workstation)

**CONFIG.SYS** (on boot disk):
```
DEVICE=A:\NET\3CPD.EXE /NETBOOT=ON /TFTP=ON /BOOTP=ON
```

**Boot Sequence**:
1. BIOS network boot
2. Load minimal DOS from network
3. Execute CONFIG.SYS with packet driver
4. Continue network boot process

---

### File Server/BBS Applications

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /SERVER_MODE=ON /HANDLES=16 /BUFFERS=32 /PRIORITY=HIGH
```

**Features**:
- Multiple simultaneous connections
- Optimized for server workloads
- Enhanced buffer management
- High interrupt priority

## Advanced Configurations

### Bus Mastering Optimization (3C515-TX)

**Automatic Configuration (Recommended)**:
```
DEVICE=C:\NET\3CPD.EXE /BUSMASTER=AUTO /BM_TEST=FULL
```

**Manual Configuration**:
```
DEVICE=C:\NET\3CPD.EXE /BUSMASTER=ON /DMA_BURST=32 /DMA_THRESHOLD=128 /PCI_LATENCY=64
```

**Parameters**:
- `/DMA_BURST=bytes`: DMA burst size (8, 16, 32, 64)
- `/DMA_THRESHOLD=bytes`: DMA threshold (64, 128, 256, 512)
- `/PCI_LATENCY=clocks`: PCI latency timer (32-255)

**Testing Configuration**:
```
DEVICE=C:\NET\3CPD.EXE /BUSMASTER=AUTO /BM_TEST=FULL /BM_VERIFY=ON /BM_LOG=VERBOSE
```

---

### Interrupt Optimization

**High Performance**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_PRIORITY=HIGH /IRQ_SHARING=OFF /EOI_OPTIMIZATION=ON
```

**Shared IRQ Environment**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_SHARING=ON /IRQ_CHAIN=ON /SAFE_MODE=ON
```

**Low Latency**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_MITIGATION=OFF /IRQ_COALESCING=OFF /IMMEDIATE_ACK=ON
```

---

### Security Configurations

**Promiscuous Mode (Network Monitoring)**:
```
DEVICE=C:\NET\3CPD.EXE /PROMISC=ON /BUFFERS=64 /CAPTURE=ON /LOG=PACKETS
```

**Restricted Mode (High Security)**:
```
DEVICE=C:\NET\3CPD.EXE /PROMISC=OFF /BROADCAST=FILTER /MULTICAST=FILTER /MAC_FILTER=ON
```

**MAC Address Filtering**:
```batch
3CPD /MAC_FILTER ADD 00:A0:24:12:34:56
3CPD /MAC_FILTER ADD 00:A0:24:78:9A:BC
3CPD /MAC_FILTER ENABLE
```

---

### Debug and Development

**Maximum Logging**:
```
DEVICE=C:\NET\3CPD.EXE /LOG=VERBOSE /LOGFILE=C:\NET.LOG /DEBUG=ON /TRACE=ALL
```

**Performance Analysis**:
```
DEVICE=C:\NET\3CPD.EXE /PERF_COUNTERS=ON /TIMING=ON /STATS_INTERVAL=1000
```

**Hardware Testing**:
```
DEVICE=C:\NET\3CPD.EXE /HW_TEST=EXTENDED /SELF_TEST=ON /LOOPBACK=INTERNAL
```

## Troubleshooting Configurations

### Memory Conflict Resolution

**Problem**: Driver fails to load due to memory conflicts

**Solution 1 - Minimal Memory**:
```
DEVICE=C:\NET\3CPD.EXE /BUFFERS=4 /LOG=OFF /FEATURES=BASIC
```

**Solution 2 - XMS Only**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON /CONV_MEMORY=0
```

**Solution 3 - Load Order**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICE=C:\NET\3CPD.EXE    REM Load before other drivers
DEVICE=C:\MOUSE.SYS
DEVICE=C:\SOUND.SYS
```

---

### IRQ Conflict Resolution

**Problem**: Network card not responding, possible IRQ conflict

**Solution 1 - Alternative IRQs**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ1=10    REM Try IRQ 10 instead of 5
```

**Solution 2 - IRQ Sharing**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_SHARING=ON /IRQ_LEVEL=EDGE
```

**Solution 3 - Manual Detection**:
```batch
3CPD /DETECT /IRQ_SCAN=ALL
```

---

### Performance Problems

**Problem**: Poor network performance

**Solution 1 - Basic Optimization**:
```
DEVICE=C:\NET\3CPD.EXE /BUFFERS=16 /TX_THRESH=AUTO /RX_THRESH=AUTO
```

**Solution 2 - Advanced Optimization**:
```
DEVICE=C:\NET\3CPD.EXE /BUSMASTER=AUTO /BUFFERS=32 /IRQ_PRIORITY=HIGH
```

**Solution 3 - Compatibility Mode**:
```
DEVICE=C:\NET\3CPD.EXE /COMPAT_MODE=ON /SAFE_BUFFERS=ON /TIMING=CONSERVATIVE
```

---

### Hardware Detection Issues

**Problem**: Network card not detected

**Solution 1 - Force Detection**:
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /FORCE_DETECT=ON
```

**Solution 2 - Extended Scan**:
```
DEVICE=C:\NET\3CPD.EXE /SCAN_RANGE=0x200-0x3F0 /PNP=FORCE
```

**Solution 3 - Manual Configuration**:
```
DEVICE=C:\NET\3CPD.EXE /NIC_TYPE=3C509B /IO1=0x300 /IRQ1=5 /MAC=00:A0:24:12:34:56
```

## Environment-Specific Examples

### Laboratory/Testing Environment

**Requirements**: 
- Multiple test configurations
- Easy reconfiguration
- Comprehensive logging

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /CONFIG_FILE=C:\NET\TEST.CFG /LOG=VERBOSE /DEBUG=ON
```

**TEST.CFG**:
```ini
[DEFAULT]
BUFFERS=16
XMS=ON
LOG=VERBOSE

[TEST1]
SPEED=10
DUPLEX=HALF
BUSMASTER=OFF

[TEST2]  
SPEED=100
DUPLEX=FULL
BUSMASTER=AUTO
```

**Usage**:
```batch
3CPD /CONFIG=TEST1
3CPD /CONFIG=TEST2
```

---

### Production Server Environment

**Requirements**:
- Maximum reliability
- Optimal performance
- Comprehensive monitoring

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICEHIGH=C:\NET\3CPD.EXE /XMS=ON /UMB=ON /BUFFERS=32 /REDUNDANCY=ON /LOG=ERRORS /WATCHDOG=ON
```

**Features**:
- Dual NIC failover
- Error recovery
- Performance monitoring
- Automatic restarts

---

### Embedded/Industrial Environment

**Requirements**:
- Minimal resource usage
- High reliability
- Restricted feature set

**CONFIG.SYS**:
```
DEVICE=C:\NET\3CPD.EXE /EMBEDDED=ON /BUFFERS=4 /LOG=ERRORS /FEATURES=BASIC /WATCHDOG=ON
```

**Features**:
- <40KB memory usage
- Essential features only
- Hardware watchdog
- Error reporting

---

### Educational Environment

**Requirements**:
- Easy configuration
- Student-friendly
- Multiple applications

**CONFIG.SYS**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON /HANDLES=8 /HELP=ON /CONFIG_WIZARD=ON
```

**AUTOEXEC.BAT**:
```batch
REM Show configuration helper
3CPD /HELP

REM Display current status
3CPD /STATUS

REM Load common applications
CALL C:\NET\LOADAPPS.BAT
```

---

## Configuration File Format

### INI-Style Configuration

**NETWORK.CFG**:
```ini
[DRIVER]
Type=3COM_PACKET_DRIVER
Version=1.0

[NIC1]
Type=3C515TX
IO=0x300
IRQ=5
Speed=100
Duplex=FULL
BusMaster=AUTO

[NIC2] 
Type=3C509B
IO=0x320
IRQ=7
Speed=10
Duplex=HALF
BusMaster=OFF

[MEMORY]
XMS=ON
UMB=ON
Buffers=16
ConventionalMemory=6KB

[PERFORMANCE]
IRQPriority=HIGH
Optimization=THROUGHPUT
CPUType=AUTO

[ROUTING]
Mode=LOAD_BALANCE
FailoverTime=1000
LoadBalanceAlgorithm=ROUND_ROBIN

[LOGGING]
Level=INFO
File=C:\NET.LOG
MaxSize=1MB
Rotate=YES
```

**Usage**:
```
DEVICE=C:\NET\3CPD.EXE /CONFIG=C:\NET\NETWORK.CFG
```

---

### Command-Line Override

Even with configuration files, command-line parameters take precedence:

```
DEVICE=C:\NET\3CPD.EXE /CONFIG=C:\NET\NETWORK.CFG /IRQ1=10 /LOG=VERBOSE
```

This loads the configuration file but overrides IRQ1 and logging level.

---

## Configuration Best Practices

### 1. Start Simple
Begin with automatic configuration and add parameters only as needed:
```
DEVICE=C:\NET\3CPD.EXE
```

### 2. Use XMS Memory
Always use XMS when available to minimize conventional memory usage:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON
```

### 3. Enable Logging During Setup
Use logging during initial configuration, disable in production:
```
DEVICE=C:\NET\3CPD.EXE /LOG=VERBOSE    REM Setup phase
DEVICE=C:\NET\3CPD.EXE /LOG=ERRORS     REM Production
```

### 4. Test Configurations
Always test new configurations thoroughly:
```batch
3CPD /TEST
3CPD /STATS
PING 192.168.1.1
```

### 5. Document Your Configuration
Keep notes about why specific parameters were chosen:
```
REM IRQ 10 used due to conflict with sound card on IRQ 5
DEVICE=C:\NET\3CPD.EXE /IRQ1=10
```

### 6. Use Configuration Files for Complex Setups
For systems with many parameters, use configuration files:
```
DEVICE=C:\NET\3CPD.EXE /CONFIG=C:\NET\PRODUCTION.CFG
```

### 7. Plan for Growth
Configure for future expansion:
```
DEVICE=C:\NET\3CPD.EXE /HANDLES=16 /BUFFERS=32    REM Support future applications
```

---

For additional information, see:
- [User Manual](USER_MANUAL.md) - Complete user guide
- [Troubleshooting Guide](TROUBLESHOOTING.md) - Problem resolution
- [Performance Tuning Guide](../development/PERFORMANCE_TUNING.md) - Optimization strategies
- [API Reference](../api/API_REFERENCE.md) - Programming interface
\n\n## Additional Configuration Examples (legacy)
# Configuration Reference

## Command Line Parameters

| Parameter | Description | Example |
|-----------|-------------|---------|
| `/IO1`, `/IO2` | I/O base addresses | `/IO1=0x300 /IO2=0x320` |
| `/IRQ1`, `/IRQ2` | IRQ assignments | `/IRQ1=5 /IRQ2=7` |
| `/SPEED` | Network speed (10/100) | `/SPEED=100` |
| `/DUPLEX` | Duplex mode | `/DUPLEX=FULL` |
| `/BUSMASTER` | Bus mastering control | `/BUSMASTER=AUTO` |
| `/BM_TEST` | Bus mastering capability testing | `/BM_TEST=FULL` |
| `/LOG` | Enable diagnostic logging | `/LOG=ON` |
| `/ROUTE` | Static routing rules | `/ROUTE=192.168.1.0,255.255.255.0,1` |
| `/PNP` | Plug and Play control | `/PNP=OFF` |

## Example Configurations

### Single NIC Setup
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /LOG=ON
```

### Dual NIC Setup with Routing
```
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7 /ROUTE=192.168.1.0,255.255.255.0,1
```

### Automatic Bus Mastering Configuration
```
REM Recommended: Let driver test and configure bus mastering automatically
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /BUSMASTER=AUTO /BM_TEST=FULL
```

**Bus Mastering Options:**
- `/BUSMASTER=AUTO` - Automatic capability testing and configuration (recommended)
- `/BUSMASTER=ON` - Force enable bus mastering (advanced users only)
- `/BUSMASTER=OFF` - Force disable bus mastering (safe mode)

**Testing Options:**
- `/BM_TEST=FULL` - Complete 45-second capability test (recommended)
- `/BM_TEST=QUICK` - Fast 10-second basic test
- `/BM_TEST=OFF` - Skip testing, use manual configuration

**Benefits of Automatic Mode:**
- Eliminates manual parameter tuning
- Provides optimal performance for your specific hardware
- Automatically handles 80286 chipset limitations
- Safe fallback to programmed I/O if issues detected

For detailed configuration examples, see [config-demo.md](../development/config-demo.md).
