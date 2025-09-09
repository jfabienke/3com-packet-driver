# Troubleshooting Guide

Last Updated: 2025-09-04
Status: canonical
Purpose: Problem resolution guide for installation, configuration, and runtime issues.

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
DIR C:\NET\3CPD.EXE
REM (Legacy) DIR C:\NET\*.MOD

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
DEVICE=C:\NET\3CPD.EXE /BUFFERS=4 /LOG=OFF
```

**Solution - Use XMS Memory**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON
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
DEVICE=C:\NET\3CPD.EXE /SAFE_MODE=ON /NO_DETECT=ON
```

**Cause 2: Memory Manager Conflict**

**Solution - Load Order**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE    REM Load before EMM386
DEVICE=C:\EMM386.EXE NOEMS
```

**Cause 3: IRQ Conflict**

**Solution - Manual IRQ**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ1=10 /IO1=0x300
```

### Problem: "Bad or Missing Module" Error

**Symptoms**:
- Driver loads but shows module errors
- Some features don't work

**Cause**: Missing or corrupted module files

**Solution**:
```batch
REM Verify module files
REM (Legacy) DIR C:\NET\*.MOD

REM Check module integrity  
3CPD /VERIFY_MODULES

REM Reinstall modules if needed
REM (Legacy) COPY A:\NET\*.MOD C:\NET\
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
DEVICE=C:\NET\3CPD.EXE /FORCE_DETECT=ON /IO1=0x300 /IRQ1=5
```

**Solution 2 - Specific NIC Type**:
```
DEVICE=C:\NET\3CPD.EXE /NIC_TYPE=3C509B /IO1=0x300
```

**Solution 3 - Extended Address Scan**:
```
DEVICE=C:\NET\3CPD.EXE /SCAN_RANGE=0x200-0x3F0 /IRQ_RANGE=3-15
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
DEVICE=C:\NET\3CPD.EXE /NIC_TYPE=3C515TX /IO1=0x300 /IRQ1=5
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
DEVICE=C:\NET\3CPD.EXE /PCI_FORCE=ON /PCI_SCAN=ALL
```

### Problem: Multiple Cards, Only One Detected

**Symptoms**:
- System has 2+ NICs
- Driver only finds first card

**Cause**: IRQ or I/O address conflict

**Solution**:
```batch
REM Manual configuration for both NICs
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7

REM Or let driver assign automatically
DEVICE=C:\NET\3CPD.EXE /AUTO_ASSIGN=ON
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
DEVICE=C:\NET\3CPD.EXE /BUFFERS=4
```

**Solution 2 - Use XMS Memory**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE /XMS=ON
```

**Solution 3 - Load High**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\EMM386.EXE NOEMS
DEVICEHIGH=C:\NET\3CPD.EXE
```

### Problem: Memory Manager Conflicts

**Symptoms**:
- Works without EMM386, fails with it
- Memory corruption messages

**Solution 1 - Load Order**:
```
DEVICE=C:\HIMEM.SYS
DEVICE=C:\NET\3CPD.EXE    REM Before EMM386
DEVICE=C:\EMM386.EXE NOEMS
```

**Solution 2 - Exclude Memory Regions**:
```
DEVICE=C:\EMM386.EXE NOEMS X=D000-D7FF
DEVICE=C:\NET\3CPD.EXE /MEM_BASE=D000
```

**Solution 3 - Use QEMM Instead**:
```
DEVICE=C:\QEMM386.SYS
DEVICEHIGH=C:\NET\3CPD.EXE
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
DEVICEHIGH=C:\NET\3CPD.EXE
```

**Solution 2 - Force UMB Region**:
```
DEVICE=C:\EMM386.EXE NOEMS I=E000-EFFF
DEVICEHIGH=C:\NET\3CPD.EXE /UMB_SEGMENT=E000
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
DEVICE=C:\NET\3CPD.EXE /IRQ1=10
```

**Solution 2 - Enable IRQ Sharing**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_SHARING=ON
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
DEVICE=C:\NET\3CPD.EXE /IO1=0x280
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
DEVICE=C:\NET\3CPD.EXE /BUSMASTER=OFF
```

**Solution 2 - Use Different DMA Channel**:
```
DEVICE=C:\NET\3CPD.EXE /DMA_CHANNEL=5
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
DEVICE=C:\NET\3CPD.EXE /BUFFERS=16
```

**Solution 2 - Enable Bus Mastering**:
```
DEVICE=C:\NET\3CPD.EXE /BUSMASTER=AUTO
```

**Solution 3 - Increase IRQ Priority**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_PRIORITY=HIGH
```

### Problem: High CPU Usage

**Symptoms**:
- System slow during network activity
- Applications become unresponsive

**Cause 1: Interrupt Storm**

**Solution**:
```
DEVICE=C:\NET\3CPD.EXE /IRQ_MITIGATION=ON /INTERRUPT_COALESCING=ON
```

**Cause 2: Inefficient Polling**

**Solution**:
```
DEVICE=C:\NET\3CPD.EXE /POLLING=OFF /INTERRUPT_DRIVEN=ON
```

**Cause 3: Too Many Buffers**

**Solution**:
```
DEVICE=C:\NET\3CPD.EXE /BUFFERS=8
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
DEVICE=C:\NET\3CPD.EXE /BUFFERS=32
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
DEVICE=C:\NET\3CPD.EXE /SPEED=10 /DUPLEX=HALF
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
DEVICE=C:\NET\3CPD.EXE /SPEED=100 /DUPLEX=FULL
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
DEVICE=C:\NET\3CPD.EXE /BROADCAST=ON /IPX_COMPAT=ON
```

### Problem: Windows 3.x Issues

**Symptoms**:
- Network works in DOS, not Windows
- Windows hangs on startup
- General Protection Faults

**Solution 1 - Windows Mode**:
```
DEVICE=C:\NET\3CPD.EXE /WINDOWS=STANDARD
```

**Solution 2 - Disable Advanced Features**:
```
DEVICE=C:\NET\3CPD.EXE /ADVANCED=OFF /SAFE_MODE=ON
```

**Solution 3 - Load Order**:
```batch
REM Load packet driver before Windows
3CPD.EXE
WIN
```

### Problem: Multiple Applications Conflict

**Symptoms**:
- First application works
- Second application fails
- "No handles available"

**Solution 1 - Increase Handles**:
```
DEVICE=C:\NET\3CPD.EXE /HANDLES=16
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
DEVICE=C:\NET\3CPD.EXE /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7
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
DEVICE=C:\NET\3CPD.EXE /ROUTE=ROUND_ROBIN
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
DEVICE=C:\NET\3CPD.EXE /FAILOVER=ON /FAILOVER_TIME=1000
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
DEVICE=C:\NET\3CPD.EXE /SAFE_MODE=ON /MINIMAL=ON
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
- [Performance Tuning Guide](../development/PERFORMANCE_TUNING.md) - Optimization strategies
- [API Reference](../api/API_REFERENCE.md) - Programming interface
\n\n## Additional Troubleshooting (legacy)
# 3COM Packet Driver Troubleshooting Guide

## Overview

This guide provides comprehensive troubleshooting procedures for the 3COM Packet Driver, including common issues and solutions, error code reference, diagnostic procedures, hardware failure identification, and performance problem resolution.

## Common Issues and Solutions

### Driver Installation Problems

#### Issue: "Driver Failed to Load"

**Symptoms:**
- Error message during boot: "3CPD.EXE: Driver initialization failed"
- System hangs during CONFIG.SYS processing
- No network functionality available
- TSR installation error messages

**Common Causes and Solutions:**

**1. File Not Found or Corrupted**
```dos
REM Verify driver file exists and is not corrupted
DIR C:\NETWORK\3CPD.EXE
REM Expected: File should exist with size > 50KB
```

**Solution:**
- Verify file path in CONFIG.SYS is correct
- Re-copy driver from original media
- Check for file corruption with antivirus scan

**2. Insufficient Memory**
```dos
REM Check available conventional memory
MEM /C
REM Need at least 64KB free conventional memory
```

**Solution:**
- Load memory managers (HIMEM.SYS, EMM386.EXE) before driver
- Move other drivers to high memory with DEVICEHIGH
- Reduce buffer allocation: `/BUFFERS=2`

**3. CONFIG.SYS Syntax Error**
```dos
REM Common syntax errors:
REM Wrong: DEVICE=3CPD.EXE IO1=0x300    (missing /)
REM Wrong: DEVICE=3CPD.EXE /IO1 0x300   (missing =)
REM Right: DEVICE=3CPD.EXE /IO1=0x300
```

**Solution:**
- Verify all parameters use `/PARAM=VALUE` format
- Check for typos in parameter names
- Ensure no spaces around = signs

#### Issue: "Hardware Not Found"

**Symptoms:**
- Driver loads but reports "No network interfaces detected"
- Network card detected by other utilities but not by driver
- Hardware appears functional but driver cannot access it

**Diagnostic Steps:**

**1. Verify Hardware Installation**
```dos
REM Use system diagnostics to verify card presence
MSD.EXE
REM Check "Computer" section for installed devices
```

**2. Check Card Configuration**
```dos
REM For 3C509B cards
3C5X9CFG.EXE /DISPLAY
REM For 3C515-TX cards  
3C5X5CFG.EXE /DISPLAY
```

**3. Force Hardware Detection**
```dos
REM Override auto-detection with specific settings
DEVICE=3CPD.EXE /IO1=0x300 /IRQ1=5 /FORCE
```

**Solutions:**
- Verify card is properly seated in ISA slot
- Check jumper settings match configuration
- Test card in different ISA slot
- Update card BIOS if available
- Use manual configuration instead of auto-detection

### Network Connectivity Issues

#### Issue: "No Network Communication"

**Symptoms:**
- Driver loads successfully
- Hardware detected correctly
- PING commands fail
- No network traffic visible

**Diagnostic Procedure:**

**1. Test Loopback**
```dos
REM Test internal loopback first
PING 127.0.0.1
REM This should always work if TCP/IP stack is loaded
```

**2. Check Physical Layer**
```dos
REM Use driver diagnostics to check link status
3CPD /STATUS
REM Look for "Link Status: Connected" message
```

**3. Test Local Network**
```dos
REM Ping local gateway
PING 192.168.1.1
REM Replace with your actual gateway address
```

**Common Solutions:**

**Physical Layer Issues:**
- Check cable connections (both ends)
- Verify cable type (straight-through vs. crossover)
- Test with known good cable
- Check hub/switch port functionality
- Verify network speed/duplex settings

**Configuration Issues:**
- Verify IP address configuration
- Check subnet mask settings
- Confirm gateway configuration
- Verify DNS settings if using hostnames

**Driver Issues:**
- Check IRQ conflicts with other devices
- Verify I/O address conflicts
- Test with different IRQ/I/O combinations
- Try different network speed settings

#### Issue: "Intermittent Network Problems"

**Symptoms:**
- Network works sometimes, fails other times
- Connections drop unexpectedly
- Periodic timeouts or slow response
- Error messages appear sporadically

**Diagnostic Approach:**

**1. Monitor Error Rates**
```dos
REM Enable statistics and monitor for patterns
3CPD /STATS /MONITOR
```

**2. Check System Resources**
```dos
REM Monitor memory usage and conflicts
MEM /DEBUG
```

**3. Test Under Different Loads**
```dos
REM Test with minimal system load
REM Test with heavy disk/CPU activity
REM Identify correlation with system state
```

**Common Causes and Solutions:**

**Hardware Issues:**
- Intermittent cable connections
- Failing network interface card
- Power supply fluctuations
- Temperature-related problems

**Software Conflicts:**
- IRQ sharing with other devices
- Memory manager conflicts
- TSR interaction problems
- Timing-sensitive applications

**Environmental Issues:**
- Electrical interference
- Cable length limitations
- Network congestion
- Hub/switch problems

### Performance Problems

#### Issue: "Slow Network Performance"

**Symptoms:**
- File transfers much slower than expected
- High network latency
- Applications timeout frequently
- Poor interactive response

**Performance Analysis:**

**1. Baseline Measurement**
```dos
REM Establish current performance baseline
3CPD /BENCHMARK
```

**2. Check Resource Utilization**
```dos
REM Monitor CPU and memory usage
3CPD /STATS /CPU /MEMORY
```

**3. Analyze Traffic Patterns**
```dos
REM Check for errors and retransmissions
3CPD /STATS /ERRORS
```

**Optimization Strategies:**

**CPU Optimization:**
- Enable bus mastering on 386+ systems: `/BUSMASTER=ON`
- Use appropriate speed setting: `/SPEED=100` for 100 Mbps cards
- Increase buffer allocation: `/BUFFERS=8` or higher
- Enable XMS memory: `/XMS=1`

**Memory Optimization:**
- Load driver into high memory with DEVICEHIGH
- Increase buffer size for large transfers: `/BUFSIZE=1600`
- Ensure adequate XMS memory available
- Optimize memory manager configuration

**Hardware Optimization:**
- Use high-priority IRQs (10, 11, 15)
- Verify optimal I/O address selection
- Check for ISA bus speed issues
- Consider network card upgrade

## Error Code Reference and Descriptions

### Configuration Error Codes

| Error Code | Description | Possible Causes | Solutions |
|------------|-------------|-----------------|-----------|
| **CONFIG_ERR_INVALID_PARAM** | Invalid parameter specified | Typo in parameter name | Check parameter spelling |
| **CONFIG_ERR_INVALID_VALUE** | Parameter value out of range | Wrong value for parameter | Check valid value ranges |
| **CONFIG_ERR_MEMORY** | Memory allocation failure | Insufficient memory | Free memory, reduce buffers |
| **CONFIG_ERR_IO_CONFLICT** | I/O address conflict | Overlapping I/O ranges | Use different I/O addresses |
| **CONFIG_ERR_IRQ_CONFLICT** | IRQ conflict detected | Same IRQ for both NICs | Use different IRQs |
| **CONFIG_ERR_CPU_REQUIRED** | CPU doesn't support feature | Bus mastering on <286 | Disable bus mastering |
| **CONFIG_ERR_ROUTE_SYNTAX** | Invalid route format | Wrong route syntax | Check route format |
| **CONFIG_ERR_TOO_MANY_ROUTES** | Exceeded route limit | >16 routes specified | Reduce route count |
| **CONFIG_ERR_INVALID_SPEED** | Invalid speed setting | Wrong speed value | Use 10, 100, or AUTO |
| **CONFIG_ERR_INVALID_IO_RANGE** | I/O address out of range | Address <0x200 or >0x3F0 | Use valid I/O range |
| **CONFIG_ERR_INVALID_IRQ_RANGE** | IRQ not supported | IRQ not in valid set | Use valid IRQ numbers |

### Hardware Error Codes

| Error Code | Description | Hardware Issue | Diagnostic Steps |
|------------|-------------|----------------|------------------|
| **NIC_ERROR** | General NIC failure | Card malfunction | Test in different slot |
| **_3C509B_ERR_RX_CRC** | CRC error on receive | Bad cable/connector | Check cable connections |
| **_3C509B_ERR_RX_FRAMING** | Frame alignment error | Electrical interference | Check for interference |
| **_3C509B_ERR_INVALID_PACKET** | Malformed packet | Network/driver issue | Check network configuration |
| **_3C509B_ERR_ADAPTER_FAILURE** | Hardware failure | Card failure | Replace network card |
| **_3C515_TX_STATUS_ADAPTER_FAILURE** | 3C515 hardware failure | Card failure | Replace network card |
| **_3C515_TX_RXSTAT_ERROR** | Receive error | Reception problem | Check cable/hub |
| **_3C515_TX_TXSTAT_JABBER** | Jabber error | Transmission problem | Check cable quality |

### Memory Error Codes

| Error Code | Description | Memory Issue | Solutions |
|------------|-------------|--------------|-----------|
| **MEM_ERROR_OUT_OF_MEMORY** | Insufficient memory | Memory exhausted | Free memory, reduce buffers |
| **MEM_ERROR_INVALID_POINTER** | Invalid memory pointer | Corruption detected | Restart system |
| **MEM_ERROR_DOUBLE_FREE** | Double free detected | Driver bug | Update driver |
| **MEM_ERROR_CORRUPTION** | Memory corruption | System instability | Check memory hardware |
| **MEM_ERROR_ALIGNMENT** | Alignment error | DMA issue | Check DMA configuration |
| **MEM_ERROR_POOL_FULL** | Buffer pool full | Resource exhaustion | Increase buffer count |

### Buffer Error Codes

| Error Code | Description | Buffer Issue | Solutions |
|------------|-------------|--------------|-----------|
| **BUFFER_ERROR_OUT_OF_MEMORY** | Buffer allocation failed | Memory shortage | Reduce buffer requirements |
| **BUFFER_ERROR_POOL_FULL** | Buffer pool exhausted | High traffic load | Increase buffer pool size |
| **BUFFER_ERROR_INVALID_BUFFER** | Invalid buffer pointer | Corruption | Restart driver |
| **BUFFER_ERROR_BUFFER_IN_USE** | Buffer still in use | Reference counting error | Check for driver bugs |
| **BUFFER_ERROR_SIZE_MISMATCH** | Wrong buffer size | Configuration error | Check buffer size settings |
| **BUFFER_ERROR_ALIGNMENT** | Buffer alignment error | DMA alignment issue | Check memory alignment |
| **BUFFER_ERROR_CORRUPTION** | Buffer corruption | Memory problem | Check system stability |

## Diagnostic Procedures and Tools

### Built-in Diagnostic Commands

#### Driver Status Check
```dos
3CPD /STATUS
```

**Output Interpretation:**
```
3COM Packet Driver Status:
  Driver Version: 1.0
  Driver Status: Active
  NICs Detected: 1
  NIC 1: 3C515-TX at IO=0x300, IRQ=5
    Link Status: Connected
    Speed: 100 Mbps
    Duplex: Full
    MAC Address: 00:60:97:12:34:56
```

#### Statistics Display
```dos
3CPD /STATS
```

**Output Analysis:**
```
3COM Packet Driver Statistics:
  Packets Transmitted: 12,345
  Packets Received: 23,456
  Transmit Errors: 2 (0.02%)
  Receive Errors: 1 (0.004%)
  Buffer Allocation Failures: 0
  Memory Usage: 156 KB
  CPU Utilization: 8%
```

**Error Rate Analysis:**
- **Normal**: Error rate <0.01%
- **Warning**: Error rate 0.01-0.1%
- **Critical**: Error rate >0.1%

#### Hardware Test
```dos
3CPD /TEST
```

**Test Sequence:**
1. Hardware register test
2. Memory allocation test
3. Interrupt functionality test
4. Loopback test
5. Buffer management test

### External Diagnostic Tools

#### System Configuration Check
```dos
REM Check system configuration
MSD.EXE
```

**Key Information to Check:**
- Available IRQs
- I/O address usage
- Memory configuration
- ISA bus settings

#### Network Interface Utilities

**For 3C509B Cards:**
```dos
3C5X9CFG.EXE /TEST
3C5X9CFG.EXE /DISPLAY
```

**For 3C515-TX Cards:**
```dos
3C5X5CFG.EXE /TEST
3C5X5CFG.EXE /DISPLAY
```

#### Memory Diagnostic
```dos
REM Check memory configuration and usage
MEM /C /P
CHKDSK C: /F
```

### Advanced Diagnostic Procedures

#### IRQ Conflict Detection

**Step 1: Baseline IRQ Usage**
```dos
REM Document current IRQ usage before driver load
MSD.EXE > BEFORE.TXT
```

**Step 2: Load Driver and Check**
```dos
REM Boot with driver loaded
REM Check for conflicts
MSD.EXE > AFTER.TXT
```

**Step 3: Compare and Analyze**
```dos
REM Look for shared IRQs or conflicts
FC BEFORE.TXT AFTER.TXT
```

#### I/O Address Scanning

**Manual I/O Test:**
```dos
REM Test specific I/O addresses
3CPD /IO1=0x300 /TEST
3CPD /IO1=0x320 /TEST
3CPD /IO1=0x340 /TEST
```

**Conflict Resolution:**
1. Try standard addresses first: 0x300, 0x320, 0x340
2. Check 32-byte alignment
3. Verify no overlap with other devices
4. Test each address individually

#### Memory Alignment Verification

**Check DMA Buffer Alignment:**
```dos
3CPD /STATS /MEMORY /VERBOSE
```

**Look for:**
- DMA buffer alignment warnings
- Memory allocation failures
- XMS vs. conventional memory usage
- Buffer pool efficiency statistics

## 80286 Bus Mastering Issues

### Understanding 80286 Bus Mastering Limitations

**Background:**
80286 systems have inherent bus mastering limitations due to chipset design predating widespread DMA adoption. The experimental `/BUSMASTER=ON` support attempts to work around these limitations.

#### Common 80286 Bus Mastering Problems

**Issue: "DMA Timeout" Errors**

**Symptoms:**
- Frequent "DMA timeout" error messages in logs
- Network performance worse than programmed I/O mode
- Intermittent packet loss

**Diagnosis:**
```dos
REM Check DMA timeout statistics
3CPD /STATS /DMA
```

**Solutions:**
1. **Use automatic configuration (recommended):**
   ```dos
   DEVICE=3CPD.EXE /BUSMASTER=AUTO /BM_TEST=FULL
   ```

2. **Fall back to programmed I/O:**
   ```dos
   DEVICE=3CPD.EXE /BUSMASTER=OFF /BUFFERS=6
   ```

**Issue: Data Corruption with Bus Mastering**

**Symptoms:**
- Checksum errors in driver diagnostics
- Garbled data received over network
- Application data corruption

**Diagnosis:**
```dos
REM Check if automatic testing detected this issue
3CPD /SHOW_BM_TEST
```

**Solutions:**
1. **Use automatic configuration (handles verification automatically):**
   ```dos
   DEVICE=3CPD.EXE /BUSMASTER=AUTO /BM_TEST=FULL
   ```

2. **If corruption persists, disable bus mastering:**
   ```dos
   DEVICE=3CPD.EXE /BUSMASTER=OFF
   ```

**Issue: System Hangs with Bus Mastering**

**Symptoms:**
- System locks up during network activity
- Hard reset required
- May only occur under heavy load

**Diagnosis:**
- If system hangs, the chipset is incompatible
- Test with minimal configuration first

**Solutions:**
1. **Disable bus mastering immediately:**
   ```dos
   DEVICE=3CPD.EXE /BUSMASTER=OFF
   ```

2. **Do not attempt to use bus mastering on this system**

#### Chipset Compatibility Reference

**Known Compatible Chipsets:**
- Compaq proprietary chipsets (1989+)
- IBM AT 5170 Model 339 with specific BIOS
- ALR 286 systems with Phoenix BIOS 1.03+

**Known Incompatible Chipsets:**
- Intel 82C206 (NEAT chipset) - No bus mastering support
- Chips & Technologies 82C206 - Data corruption issues
- Most clone 286 systems - Variable quality

**Simplified Testing Procedure:**
1. **Use automatic testing (recommended):**
   ```dos
   DEVICE=3CPD.EXE /BUSMASTER=AUTO /BM_TEST=FULL
   ```

2. **Check test results during boot:**
   - Driver displays confidence level during initialization
   - Look for "Bus mastering: HIGH/MEDIUM/LOW/FAILED"

3. **View detailed results:**
   ```dos
   3CPD /SHOW_BM_TEST
   ```

4. **Manual override if needed:**
   ```dos
   REM Force disable if automatic test failed to detect issues
   DEVICE=3CPD.EXE /BUSMASTER=OFF
   ```

#### Emergency Recovery

**If System Won't Boot After Enabling Bus Mastering:**

1. **Boot from floppy disk**
2. **Edit CONFIG.SYS on hard drive:**
   ```dos
   REM Comment out problematic line
   REM DEVICE=C:\NET\3CPD.EXE /BUSMASTER=ON
   DEVICE=C:\NET\3CPD.EXE /BUSMASTER=OFF
   ```
3. **Reboot system**

**Performance Monitoring Commands:**
```dos
REM Check overall driver health
3CPD /STATS

REM Check specific DMA statistics  
3CPD /STATS /DMA

REM Detailed debugging information
3CPD /STATS /DEBUG /VERBOSE
```

## Hardware Failure Identification

### Network Interface Card Failures

#### Complete Card Failure

**Symptoms:**
- Card not detected by any software
- No LED activity on card
- System may fail to boot with card installed
- Card appears dead

**Diagnostic Steps:**
1. **Visual Inspection**: Check for physical damage, burned components
2. **Power Test**: Verify card receives power (LED indicators)
3. **Slot Test**: Try card in different ISA slot
4. **Cable Test**: Verify with known good cable
5. **Replacement Test**: Try known good card in same slot

**Resolution:**
- Replace network interface card
- Check system power supply adequacy
- Verify ISA slot functionality

#### Partial Card Failure

**Symptoms:**
- Card detected but unreliable operation
- Intermittent network connectivity
- High error rates
- Temperature-sensitive behavior

**Diagnostic Indicators:**
```dos
REM Monitor error statistics for patterns
3CPD /STATS /ERRORS /CONTINUOUS
```

**Look for:**
- Increasing CRC error rates
- Frame alignment errors
- Timeout errors during specific operations
- Correlation with system temperature

#### Transmission Problems

**Symptoms:**
- Can receive packets but not transmit
- Transmit errors increase over time
- Jabber errors reported
- Network becomes unusable

**Diagnostic Tests:**
```dos
REM Test transmit functionality specifically
3CPD /TEST /TX
```

**Common Causes:**
- Transmit buffer failures
- Driver circuit problems
- Cable or connector issues
- Hub/switch port problems

#### Reception Problems

**Symptoms:**
- Can transmit but not receive reliably
- High receive error rates
- Missed packets
- Timeouts waiting for responses

**Diagnostic Tests:**
```dos
REM Test receive functionality
3CPD /TEST /RX
```

**Common Causes:**
- Receive buffer failures
- Clock recovery problems
- Cable quality issues
- Electrical interference

### System Hardware Issues

#### ISA Bus Problems

**Symptoms:**
- Multiple ISA cards failing
- Timing-related errors
- System instability with network load
- Performance degradation

**Diagnostic Approach:**
1. Test with minimal ISA card configuration
2. Check ISA bus speed settings in BIOS
3. Verify adequate power supply capacity
4. Test with cards in different slots

#### Memory System Issues

**Symptoms:**
- Memory allocation failures
- Buffer corruption errors
- System crashes under network load
- Inconsistent performance

**Memory Tests:**
```dos
REM Extended memory test
HIMEM.SYS /TESTMEM:ON
REM Check for memory errors during operation
3CPD /STATS /MEMORY /VERBOSE
```

#### Interrupt System Issues

**Symptoms:**
- IRQ conflicts with other devices
- Missed interrupts
- System lockups
- Poor network performance

**Interrupt Diagnostics:**
1. Use MSD.EXE to map IRQ usage
2. Test with different IRQ assignments
3. Check for IRQ sharing issues
4. Verify interrupt controller programming

## Performance Problem Resolution

### Systematic Performance Troubleshooting

#### Step 1: Establish Baseline

**Measure Current Performance:**
```dos
REM Get baseline measurements
3CPD /BENCHMARK /FULL
```

**Document Results:**
- Throughput (Mbps)
- Latency (milliseconds)
- CPU utilization (%)
- Error rates (%)
- Memory usage (KB)

#### Step 2: Identify Bottlenecks

**CPU Bottleneck Indicators:**
- CPU utilization >80%
- System becomes unresponsive during network activity
- Performance scales poorly with load

**Memory Bottleneck Indicators:**
- Frequent buffer allocation failures
- High memory usage (>90% of available)
- Swapping or paging activity

**I/O Bottleneck Indicators:**
- High interrupt latency
- DMA transfer delays
- Hardware timeout errors

#### Step 3: Apply Targeted Optimizations

**For CPU Bottlenecks:**
```dos
REM Enable CPU-specific optimizations
DEVICE=3CPD.EXE /IO1=0x300 /IRQ1=11 /BUSMASTER=ON /SPEED=100
```

**For Memory Bottlenecks:**
```dos
REM Optimize memory configuration
DEVICE=3CPD.EXE /XMS=1 /BUFFERS=16 /BUFSIZE=1600
```

**For I/O Bottlenecks:**
```dos
REM Optimize I/O configuration
DEVICE=3CPD.EXE /IRQ1=15 /BUSMASTER=ON
```

### Performance Optimization Checklist

**Hardware Optimization:**
- [ ] Network card supports required speed (10/100 Mbps)
- [ ] Bus mastering enabled on capable systems
- [ ] Optimal IRQ assignment (avoid sharing)
- [ ] I/O addresses properly aligned
- [ ] Adequate system memory available

**Software Configuration:**
- [ ] Latest driver version installed
- [ ] Optimal buffer configuration
- [ ] XMS memory enabled if available
- [ ] Memory managers properly configured
- [ ] No conflicting TSR programs

**Network Configuration:**
- [ ] Correct speed/duplex settings
- [ ] Quality network cables used
- [ ] Hub/switch ports functioning properly
- [ ] No excessive network collisions
- [ ] Appropriate network protocols

**System Environment:**
- [ ] Adequate power supply capacity
- [ ] Proper system cooling
- [ ] No electrical interference
- [ ] Stable system operation
- [ ] Regular maintenance performed

### Common Performance Issues and Solutions

#### Issue: "Slow File Transfers"

**Optimization Strategy:**
```dos
REM High-throughput configuration
DEVICE=3CPD.EXE /IO1=0x300 /IRQ1=11 /SPEED=100 /BUSMASTER=ON /BUFFERS=16 /BUFSIZE=1600 /XMS=1
```

**Expected Improvement:** 30-50% faster transfer rates

#### Issue: "High Network Latency"

**Optimization Strategy:**
```dos
REM Low-latency configuration
DEVICE=3CPD.EXE /IO1=0x300 /IRQ1=15 /SPEED=AUTO /BUFFERS=6 /DEBUG=0
```

**Expected Improvement:** 20-40% latency reduction

#### Issue: "System Becomes Unresponsive"

**Optimization Strategy:**
```dos
REM Balanced configuration
DEVICE=3CPD.EXE /IO1=0x300 /IRQ1=10 /BUSMASTER=AUTO /BUFFERS=8 /XMS=1
```

**Expected Improvement:** Better system responsiveness

This troubleshooting guide provides comprehensive diagnostic and resolution procedures for all common 3COM packet driver issues, enabling efficient problem resolution and optimal system performance.

## Quick Reference Emergency Procedures

### ⚠️ EMERGENCY: System Won't Boot After Driver Installation

**Immediate Recovery Steps:**

1. **Boot from Emergency Floppy:**
   ```dos
   REM Create emergency boot disk beforehand:
   FORMAT A: /S
   COPY CONFIG.SYS A:\CONFIG.BAK
   COPY AUTOEXEC.BAT A:\AUTOEXEC.BAK
   ```

2. **Restore Previous Configuration:**
   ```dos
   REM Boot from floppy, then:
   COPY A:\CONFIG.BAK C:\CONFIG.SYS
   COPY A:\AUTOEXEC.BAK C:\AUTOEXEC.BAT
   ```

3. **Safe Mode Recovery:**
   ```dos
   REM Edit CONFIG.SYS to disable driver temporarily:
   REM DEVICE=C:\NETWORK\3CPD.EXE /IO1=0x300 /IRQ1=5
   DEVICE=C:\DOS\HIMEM.SYS
   ```

### 🚨 EMERGENCY: Bus Mastering System Hang

**If system locks up completely when bus mastering is enabled:**

1. **Hard Reset:** Power off system completely (hold power button)
2. **Boot with Minimal Config:**
   ```dos
   REM Minimal CONFIG.SYS:
   DEVICE=C:\DOS\HIMEM.SYS
   DEVICE=C:\NETWORK\3CPD.EXE /BUSMASTER=OFF /IO1=0x300 /IRQ1=5
   ```
3. **Never use BUSMASTER=ON again on this system**
