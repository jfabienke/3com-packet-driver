# 3COM Packet Driver Troubleshooting Guide

## Overview

This guide provides comprehensive troubleshooting procedures for the 3COM Packet Driver, including common issues and solutions, error code reference, diagnostic procedures, hardware failure identification, and performance problem resolution.

## Common Issues and Solutions

### Driver Installation Problems

#### Issue: "Driver Failed to Load"

**Symptoms:**
- Error message during boot: "3CPD.COM: Driver initialization failed"
- System hangs during CONFIG.SYS processing
- No network functionality available
- TSR installation error messages

**Common Causes and Solutions:**

**1. File Not Found or Corrupted**
```dos
REM Verify driver file exists and is not corrupted
DIR C:\NETWORK\3CPD.COM
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
REM Wrong: DEVICE=3CPD.COM IO1=0x300    (missing /)
REM Wrong: DEVICE=3CPD.COM /IO1 0x300   (missing =)
REM Right: DEVICE=3CPD.COM /IO1=0x300
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
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=5 /FORCE
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
   DEVICE=3CPD.COM /BUSMASTER=AUTO /BM_TEST=FULL
   ```

2. **Fall back to programmed I/O:**
   ```dos
   DEVICE=3CPD.COM /BUSMASTER=OFF /BUFFERS=6
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
   DEVICE=3CPD.COM /BUSMASTER=AUTO /BM_TEST=FULL
   ```

2. **If corruption persists, disable bus mastering:**
   ```dos
   DEVICE=3CPD.COM /BUSMASTER=OFF
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
   DEVICE=3CPD.COM /BUSMASTER=OFF
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
   DEVICE=3CPD.COM /BUSMASTER=AUTO /BM_TEST=FULL
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
   DEVICE=3CPD.COM /BUSMASTER=OFF
   ```

#### Emergency Recovery

**If System Won't Boot After Enabling Bus Mastering:**

1. **Boot from floppy disk**
2. **Edit CONFIG.SYS on hard drive:**
   ```dos
   REM Comment out problematic line
   REM DEVICE=C:\NET\3CPD.COM /BUSMASTER=ON
   DEVICE=C:\NET\3CPD.COM /BUSMASTER=OFF
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
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=11 /BUSMASTER=ON /SPEED=100
```

**For Memory Bottlenecks:**
```dos
REM Optimize memory configuration
DEVICE=3CPD.COM /XMS=1 /BUFFERS=16 /BUFSIZE=1600
```

**For I/O Bottlenecks:**
```dos
REM Optimize I/O configuration
DEVICE=3CPD.COM /IRQ1=15 /BUSMASTER=ON
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
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=11 /SPEED=100 /BUSMASTER=ON /BUFFERS=16 /BUFSIZE=1600 /XMS=1
```

**Expected Improvement:** 30-50% faster transfer rates

#### Issue: "High Network Latency"

**Optimization Strategy:**
```dos
REM Low-latency configuration
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=15 /SPEED=AUTO /BUFFERS=6 /DEBUG=0
```

**Expected Improvement:** 20-40% latency reduction

#### Issue: "System Becomes Unresponsive"

**Optimization Strategy:**
```dos
REM Balanced configuration
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=10 /BUSMASTER=AUTO /BUFFERS=8 /XMS=1
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
   REM DEVICE=C:\NETWORK\3CPD.COM /IO1=0x300 /IRQ1=5
   DEVICE=C:\DOS\HIMEM.SYS
   ```

### 🚨 EMERGENCY: Bus Mastering System Hang

**If system locks up completely when bus mastering is enabled:**

1. **Hard Reset:** Power off system completely (hold power button)
2. **Boot with Minimal Config:**
   ```dos
   REM Minimal CONFIG.SYS:
   DEVICE=C:\DOS\HIMEM.SYS
   DEVICE=C:\NETWORK\3CPD.COM /BUSMASTER=OFF /IO1=0x300 /IRQ1=5
   ```
3. **Never use BUSMASTER=ON again on this system**