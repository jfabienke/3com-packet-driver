# Troubleshooting Guide

Comprehensive problem resolution guide for the 3Com Packet Driver.

## Table of Contents

1. [Quick Diagnostics](#quick-diagnostics)
2. [Installation Problems](#installation-problems)
3. [Hardware Detection Issues](#hardware-detection-issues)
4. [Memory Conflicts](#memory-conflicts)
5. [IRQ and I/O Conflicts](#irq-and-io-conflicts)
6. [Performance Issues](#performance-issues)
7. [Network Connectivity Problems](#network-connectivity-problems)
8. [Application Compatibility Issues](#application-compatibility-issues)
9. [Multi-NIC Problems](#multi-nic-problems)
10. [Error Messages](#error-messages)
11. [Advanced Diagnostics](#advanced-diagnostics)
12. [Recovery Procedures](#recovery-procedures)

## Quick Diagnostics

### Step 1: Basic Status Check

```batch
3CPD /STATUS
```

**Expected Output (Working System)**:
```
3Com Packet Driver v1.0 - Active
Memory: 6KB resident (XMS buffers)
NICs: 1 detected, 1 active
NIC 0: 3C515-TX at 0x300, IRQ 5, 100 Mbps, Full Duplex, Link UP
Handles: 2 active, 14 available
Statistics: 1250 packets in, 890 packets out, 0 errors
```

**Problem Indicators**:
- "Driver not loaded" → Installation problem
- "No NICs detected" → Hardware detection issue
- "Link DOWN" → Network connectivity problem
- High error count → Hardware or configuration issue

### Step 2: Hardware Detection

```batch
3CPD /DETECT
```

**Expected Output**:
```
Scanning for 3Com network cards...
Found: 3C515-TX at 0x300, IRQ 5, MAC: 00:A0:24:12:34:56
Found: 3C509B at 0x320, IRQ 7, MAC: 00:A0:24:78:9A:BC
Total NICs: 2
```

### Step 3: Memory Check

```batch
MEM /C | FIND "3CPD"
```

**Expected Output**:
```
3CPD     6,144   (6K)   Upper Memory Block
```

### Step 4: Interrupt Test

```batch
3CPD /TEST
```

**Expected Output**:
```
Testing NIC 0 (3C515-TX):
  Hardware test: PASS
  Interrupt test: PASS
  Loopback test: PASS
  DMA test: PASS
All tests completed successfully.
```

## Installation Problems

### Problem: Driver Won't Load

**Symptoms**:
- "Device driver not found" error
- System hangs during boot
- "Insufficient memory" message

**Cause 1: File Not Found**

**Solution**:
```batch
REM Verify files exist
DIR C:\NET\3CPD.COM
DIR C:\NET\*.MOD

REM Check CONFIG.SYS path
TYPE CONFIG.SYS | FIND "3CPD"
```

**Cause 2: Insufficient Memory**

**Check Available Memory**:
```batch
MEM
```

**Solution - Reduce Memory Usage**:
```
REM In CONFIG.SYS
DEVICE=C:\NET\3CPD.COM /BUFFERS=4 /LOG=OFF
```

**Solution - Use XMS Memory**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.COM /XMS=ON
```

**Cause 3: DOS Version Incompatibility**

**Check DOS Version**:
```batch
VER
```

**Solution**:
- DOS 2.x: Use minimal configuration
- DOS 3.x+: Full features available

### Problem: System Hangs During Load

**Symptoms**:
- System freezes when loading driver
- Boot process stops at driver load

**Cause 1: Hardware Conflict**

**Solution - Safe Mode Loading**:
```
DEVICE=C:\NET\3CPD.COM /SAFE_MODE=ON /NO_DETECT=ON
```

**Cause 2: Memory Manager Conflict**

**Solution - Load Order**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.COM    REM Load before EMM386
DEVICE=C:\EMM386.EXE NOEMS
```

**Cause 3: IRQ Conflict**

**Solution - Manual IRQ**:
```
DEVICE=C:\NET\3CPD.COM /IRQ1=10 /IO1=0x300
```

### Problem: "Bad or Missing Module" Error

**Symptoms**:
- Driver loads but shows module errors
- Some features don't work

**Cause**: Missing or corrupted module files

**Solution**:
```batch
REM Verify module files
DIR C:\NET\*.MOD

REM Check module integrity  
3CPD /VERIFY_MODULES

REM Reinstall modules if needed
COPY A:\NET\*.MOD C:\NET\
```

## Hardware Detection Issues

### Problem: No Network Cards Detected

**Symptoms**:
- "No compatible NICs found"
- Driver loads but shows 0 NICs

**Diagnostic Steps**:

1. **Check Hardware Installation**:
   - Verify card is properly seated
   - Check for bent pins or damage
   - Ensure slot compatibility (ISA/PCI)

2. **BIOS Configuration**:
   - Enable Plug and Play OS
   - Disable PnP for legacy cards
   - Check IRQ assignments

3. **Manual Detection**:
```batch
3CPD /DETECT /SCAN_ALL
```

**Solution 1 - Force Detection**:
```
DEVICE=C:\NET\3CPD.COM /FORCE_DETECT=ON /IO1=0x300 /IRQ1=5
```

**Solution 2 - Specific NIC Type**:
```
DEVICE=C:\NET\3CPD.COM /NIC_TYPE=3C509B /IO1=0x300
```

**Solution 3 - Extended Address Scan**:
```
DEVICE=C:\NET\3CPD.COM /SCAN_RANGE=0x200-0x3F0 /IRQ_RANGE=3-15
```

### Problem: Wrong NIC Type Detected

**Symptoms**:
- Driver detects different NIC model
- Features don't work correctly

**Diagnostic**:
```batch
3CPD /DETECT /VERBOSE
```

**Solution - Override Detection**:
```
DEVICE=C:\NET\3CPD.COM /NIC_TYPE=3C515TX /IO1=0x300 /IRQ1=5
```

### Problem: PCI Card Not Detected

**Symptoms**:
- ISA cards work, PCI cards don't
- "No PCI BIOS detected"

**Requirements**:
- 80386+ CPU
- PCI BIOS support
- Proper slot configuration

**Solution**:
```batch
REM Check PCI BIOS
3CPD /PCI_INFO

REM Force PCI scanning
DEVICE=C:\NET\3CPD.COM /PCI_FORCE=ON /PCI_SCAN=ALL
```

### Problem: Multiple Cards, Only One Detected

**Symptoms**:
- System has 2+ NICs
- Driver only finds first card

**Cause**: IRQ or I/O address conflict

**Solution**:
```batch
REM Manual configuration for both NICs
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7

REM Or let driver assign automatically
DEVICE=C:\NET\3CPD.COM /AUTO_ASSIGN=ON
```

## Memory Conflicts

### Problem: "Insufficient Memory" Error

**Symptoms**:
- Driver reports memory allocation failure
- System has memory but driver can't use it

**Diagnostic**:
```batch
MEM /C /P
3CPD /MEMORY_INFO
```

**Solution 1 - Reduce Buffer Count**:
```
DEVICE=C:\NET\3CPD.COM /BUFFERS=4
```

**Solution 2 - Use XMS Memory**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.COM /XMS=ON
```

**Solution 3 - Load High**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICEHIGH=C:\NET\3CPD.COM
```

### Problem: Memory Manager Conflicts

**Symptoms**:
- Works without EMM386, fails with it
- Memory corruption messages

**Solution 1 - Load Order**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.COM    REM Before EMM386
DEVICE=C:\EMM386.EXE NOEMS
```

**Solution 2 - Exclude Memory Regions**:
```
DEVICE=C:\EMM386.EXE NOEMS X=D000-D7FF
DEVICE=C:\NET\3CPD.COM /MEM_BASE=D000
```

**Solution 3 - Use QEMM Instead**:
```
DEVICE=C:\QEMM386.SYS
DEVICEHIGH=C:\NET\3CPD.COM
```

### Problem: UMB Loading Fails

**Symptoms**:
- DEVICEHIGH doesn't work
- Driver loads in conventional memory

**Diagnostic**:
```batch
MEM /C | FIND "Upper Memory"
```

**Solution 1 - Enable UMB**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS UMB
DOS=HIGH,UMB
DEVICEHIGH=C:\NET\3CPD.COM
```

**Solution 2 - Force UMB Region**:
```
DEVICE=C:\EMM386.EXE NOEMS I=E000-EFFF
DEVICEHIGH=C:\NET\3CPD.COM /UMB_SEGMENT=E000
```

## IRQ and I/O Conflicts

### Problem: IRQ Conflicts

**Symptoms**:
- Network doesn't work
- System instability
- "IRQ conflict detected" message

**Diagnostic**:
```batch
3CPD /IRQ_TEST
MSD      REM Microsoft Diagnostics
```

**Common IRQ Conflicts**:
- IRQ 5: Sound cards, parallel ports
- IRQ 7: Parallel port, sound cards
- IRQ 10: Usually safe
- IRQ 11: Usually safe on AT systems

**Solution 1 - Use Different IRQ**:
```
DEVICE=C:\NET\3CPD.COM /IRQ1=10
```

**Solution 2 - Enable IRQ Sharing**:
```
DEVICE=C:\NET\3CPD.COM /IRQ_SHARING=ON
```

**Solution 3 - Disable Conflicting Device**:
```batch
REM In CONFIG.SYS, comment out conflicting driver
REM DEVICE=C:\SOUND.SYS
```

### Problem: I/O Address Conflicts

**Symptoms**:
- Card detected but doesn't respond
- "I/O port conflict" message

**Common Conflicts**:
- 0x300: Common default, often conflicts
- 0x320: Secondary address
- 0x340: Sound cards

**Solution 1 - Use Alternative Address**:
```
DEVICE=C:\NET\3CPD.COM /IO1=0x280
```

**Solution 2 - Scan for Free Address**:
```batch
3CPD /SCAN_IO
```

**Solution 3 - Configure in BIOS**:
- Enter BIOS setup
- Configure PnP settings
- Assign specific resources

### Problem: DMA Conflicts (3C515-TX)

**Symptoms**:
- Bus mastering doesn't work
- Poor performance with 3C515-TX
- DMA error messages

**Diagnostic**:
```batch
3CPD /DMA_TEST
```

**Solution 1 - Disable Bus Mastering**:
```
DEVICE=C:\NET\3CPD.COM /BUSMASTER=OFF
```

**Solution 2 - Use Different DMA Channel**:
```
DEVICE=C:\NET\3CPD.COM /DMA_CHANNEL=5
```

**Solution 3 - Check System Requirements**:
- Requires 80386+ CPU
- Adequate system memory
- Compatible chipset

## Performance Issues

### Problem: Slow Network Performance

**Symptoms**:
- Low throughput
- High latency
- Timeouts

**Diagnostic Steps**:

1. **Check Link Status**:
```batch
3CPD /STATUS
```

2. **Check Statistics**:
```batch
3CPD /STATS
```

3. **Monitor Performance**:
```batch
3CPD /MONITOR 30
```

**Solution 1 - Optimize Buffers**:
```
DEVICE=C:\NET\3CPD.COM /BUFFERS=16
```

**Solution 2 - Enable Bus Mastering**:
```
DEVICE=C:\NET\3CPD.COM /BUSMASTER=AUTO
```

**Solution 3 - Increase IRQ Priority**:
```
DEVICE=C:\NET\3CPD.COM /IRQ_PRIORITY=HIGH
```

### Problem: High CPU Usage

**Symptoms**:
- System slow during network activity
- Applications become unresponsive

**Cause 1: Interrupt Storm**

**Solution**:
```
DEVICE=C:\NET\3CPD.COM /IRQ_MITIGATION=ON /INTERRUPT_COALESCING=ON
```

**Cause 2: Inefficient Polling**

**Solution**:
```
DEVICE=C:\NET\3CPD.COM /POLLING=OFF /INTERRUPT_DRIVEN=ON
```

**Cause 3: Too Many Buffers**

**Solution**:
```
DEVICE=C:\NET\3CPD.COM /BUFFERS=8
```

### Problem: Packet Loss

**Symptoms**:
- Applications report lost packets
- High error counters
- Intermittent connectivity

**Diagnostic**:
```batch
3CPD /STATS
PING -t 192.168.1.1
```

**Solution 1 - Increase Buffers**:
```
DEVICE=C:\NET\3CPD.COM /BUFFERS=32
```

**Solution 2 - Check Hardware**:
```batch
3CPD /TEST /EXTENDED
```

**Solution 3 - Network Issues**:
- Check cables
- Test with different hub/switch
- Verify duplex settings

## Network Connectivity Problems

### Problem: No Network Connection

**Symptoms**:
- "Network unreachable"
- PING fails to local gateway
- Link LED off

**Diagnostic Steps**:

1. **Check Physical Connection**:
   - Cable properly connected
   - Hub/switch power on
   - Cable not damaged

2. **Check Link Status**:
```batch
3CPD /STATUS
```

3. **Test Loopback**:
```batch
3CPD /TEST /LOOPBACK
```

**Solution 1 - Check Duplex Settings**:
```batch
3CPD /DUPLEX=AUTO
3CPD /SPEED=AUTO
```

**Solution 2 - Force Link Settings**:
```
DEVICE=C:\NET\3CPD.COM /SPEED=10 /DUPLEX=HALF
```

**Solution 3 - Reset Network Interface**:
```batch
3CPD /RESET
```

### Problem: Intermittent Connection

**Symptoms**:
- Connection works sometimes
- Frequent disconnections
- Link flapping

**Cause 1: Cable Issues**

**Solution**:
- Replace network cable
- Check for interference
- Test with short cable

**Cause 2: Duplex Mismatch**

**Solution**:
```
REM Force same settings on both ends
DEVICE=C:\NET\3CPD.COM /SPEED=100 /DUPLEX=FULL
```

**Cause 3: Hardware Problems**

**Solution**:
```batch
REM Extended hardware test
3CPD /TEST /COMPREHENSIVE
```

### Problem: Can't Reach Internet

**Symptoms**:
- Local network works
- Internet sites unreachable
- DNS resolution fails

**Diagnostic**:
```batch
PING 192.168.1.1     REM Test gateway
PING 8.8.8.8         REM Test Internet
NSLOOKUP google.com  REM Test DNS
```

**Cause**: Usually application configuration, not driver

**Solution**:
- Check TCP/IP configuration
- Verify gateway settings
- Test with different application

## Application Compatibility Issues

### Problem: mTCP Doesn't Work

**Symptoms**:
- "No packet driver found"
- DHCP fails
- Connection timeouts

**Solution 1 - Verify Packet Driver**:
```batch
3CPD /STATUS
```

**Solution 2 - Check Environment**:
```batch
SET MTCPCFG
SET PACKETINT
```

**Solution 3 - Correct Configuration**:
```batch
SET MTCPCFG=C:\MTCP\TCP.CFG
SET PACKETINT=0x60
```

### Problem: IPX Games Don't Work

**Symptoms**:
- "Network not found"
- Can't see other players
- IPX initialization fails

**Solution 1 - Check IPX Stack**:
```batch
LSL
IPXODI
NET
```

**Solution 2 - Use PDIPX**:
```batch
PDIPX
NET
```

**Solution 3 - Enable Broadcast**:
```
DEVICE=C:\NET\3CPD.COM /BROADCAST=ON /IPX_COMPAT=ON
```

### Problem: Windows 3.x Issues

**Symptoms**:
- Network works in DOS, not Windows
- Windows hangs on startup
- General Protection Faults

**Solution 1 - Windows Mode**:
```
DEVICE=C:\NET\3CPD.COM /WINDOWS=STANDARD
```

**Solution 2 - Disable Advanced Features**:
```
DEVICE=C:\NET\3CPD.COM /ADVANCED=OFF /SAFE_MODE=ON
```

**Solution 3 - Load Order**:
```batch
REM Load packet driver before Windows
3CPD.COM
WIN
```

### Problem: Multiple Applications Conflict

**Symptoms**:
- First application works
- Second application fails
- "No handles available"

**Solution 1 - Increase Handles**:
```
DEVICE=C:\NET\3CPD.COM /HANDLES=16
```

**Solution 2 - Check Handle Usage**:
```batch
3CPD /HANDLES
```

**Solution 3 - Application Load Order**:
- Load permanent applications first
- Load temporary applications last

## Multi-NIC Problems

### Problem: Only One NIC Works

**Symptoms**:
- Multiple NICs detected
- Only first NIC active
- Load balancing doesn't work

**Diagnostic**:
```batch
3CPD /STATUS /VERBOSE
3CPD /NIC_INFO
```

**Solution 1 - Manual Configuration**:
```
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7
```

**Solution 2 - Check IRQ Conflicts**:
```batch
3CPD /IRQ_TEST /ALL_NICS
```

### Problem: Load Balancing Issues

**Symptoms**:
- Traffic only uses one NIC
- Uneven load distribution
- Performance not improved

**Solution 1 - Verify Configuration**:
```batch
3CPD /ROUTE /STATUS
```

**Solution 2 - Force Load Balancing**:
```
DEVICE=C:\NET\3CPD.COM /ROUTE=ROUND_ROBIN
```

**Solution 3 - Check Application**:
- Some applications don't support load balancing
- Use connection-aware applications

### Problem: Failover Doesn't Work

**Symptoms**:
- Primary NIC fails
- Traffic doesn't switch to backup
- Connection lost

**Diagnostic**:
```batch
REM Test failover manually
3CPD /NIC 0 /DISABLE
3CPD /STATUS
```

**Solution**:
```
DEVICE=C:\NET\3CPD.COM /FAILOVER=ON /FAILOVER_TIME=1000
```

## Error Messages

### "Driver not loaded"

**Cause**: Driver initialization failed

**Solutions**:
1. Check file path in CONFIG.SYS
2. Verify sufficient memory
3. Test with minimal configuration

### "No compatible NICs found"

**Cause**: Hardware detection failed

**Solutions**:
1. Force manual detection
2. Check hardware installation
3. Update BIOS settings

### "Insufficient memory"

**Cause**: Memory allocation failed

**Solutions**:
1. Reduce buffer count
2. Use XMS memory
3. Load in upper memory

### "IRQ conflict detected"

**Cause**: IRQ already in use

**Solutions**:
1. Use different IRQ
2. Enable IRQ sharing
3. Remove conflicting device

### "Hardware initialization failed"

**Cause**: NIC hardware problem

**Solutions**:
1. Check card installation
2. Test with different slot
3. Update card firmware

### "Bus mastering not supported"

**Cause**: System doesn't support DMA

**Solutions**:
1. Disable bus mastering
2. Check CPU requirements (80386+)
3. Update system BIOS

### "Network cable disconnected"

**Cause**: Physical connection problem

**Solutions**:
1. Check cable connections
2. Test with different cable
3. Verify hub/switch operation

### "Packet buffer overflow"

**Cause**: Insufficient buffers for traffic

**Solutions**:
1. Increase buffer count
2. Reduce network load
3. Check for hardware issues

## Advanced Diagnostics

### Hardware Diagnostics

**Complete Hardware Test**:
```batch
3CPD /TEST /COMPREHENSIVE
```

**Tests Performed**:
- Register read/write test
- Memory test
- Interrupt test
- DMA test (if applicable)
- Loopback test
- Cable test

**EEPROM Analysis**:
```batch
3CPD /EEPROM /DUMP
```

**Performance Benchmarking**:
```batch
3CPD /BENCHMARK /DURATION=60
```

### Network Diagnostics

**Cable Testing**:
```batch
3CPD /CABLE_TEST
```

**Network Analysis**:
```batch
3CPD /ANALYZE /DURATION=300
```

**Packet Capture**:
```batch
3CPD /CAPTURE /FILE=PACKETS.CAP /DURATION=60
```

### System Integration Testing

**Memory Manager Compatibility**:
```batch
3CPD /TEST /MEMORY_MANAGERS
```

**Application Compatibility**:
```batch
3CPD /TEST /APPLICATIONS
```

**Multi-tasking Test**:
```batch
3CPD /TEST /MULTITASK
```

## Recovery Procedures

### Emergency Recovery

If the system becomes unstable:

1. **Boot from Floppy**:
   - Use emergency boot disk
   - Remove network driver from CONFIG.SYS
   - Reboot from hard disk

2. **Safe Mode Loading**:
```
DEVICE=C:\NET\3CPD.COM /SAFE_MODE=ON /MINIMAL=ON
```

3. **Reset to Defaults**:
```batch
3CPD /RESET_CONFIG
```

### Configuration Recovery

**Backup Current Configuration**:
```batch
3CPD /SAVE_CONFIG C:\NET\BACKUP.CFG
```

**Restore Previous Configuration**:
```batch
3CPD /LOAD_CONFIG C:\NET\BACKUP.CFG
```

**Factory Reset**:
```batch
3CPD /FACTORY_RESET
```

### Hardware Recovery

**Force Hardware Reset**:
```batch
3CPD /HARDWARE_RESET
```

**EEPROM Recovery**:
```batch
3CPD /EEPROM /RESTORE /FILE=FACTORY.EEP
```

**Firmware Update**:
```batch
3CPD /FIRMWARE /UPDATE /FILE=LATEST.ROM
```

### Data Recovery

**Recover Network Statistics**:
```batch
3CPD /STATS /SAVE=STATS.LOG
```

**Export Configuration**:
```batch
3CPD /CONFIG /EXPORT=CONFIG.INI
```

**Log File Analysis**:
```batch
3CPD /LOG /ANALYZE /FILE=NET.LOG
```

---

## Getting Additional Help

### Built-in Help System

```batch
3CPD /HELP                 REM General help
3CPD /HELP /PARAMETERS     REM Parameter reference
3CPD /HELP /EXAMPLES       REM Configuration examples
3CPD /HELP /TROUBLESHOOT   REM Quick troubleshooting
```

### Diagnostic Report Generation

```batch
3CPD /REPORT /COMPREHENSIVE > SYSTEM.RPT
```

This creates a complete system report including:
- Hardware configuration
- Driver status
- Network statistics
- Error logs
- Configuration settings

### Contact Information

For additional support:
- Check project documentation
- Review FAQ section
- Search issue tracker
- Submit bug report with diagnostic report

---

For related information, see:
- [User Manual](USER_MANUAL.md) - Complete user guide
- [Configuration Guide](CONFIGURATION.md) - Detailed configuration examples
- [Performance Tuning Guide](PERFORMANCE_TUNING.md) - Optimization strategies
- [API Reference](API_REFERENCE.md) - Programming interface