# 3COM Packet Driver Configuration Tools Guide

## Overview

The 3COM Packet Driver provides comprehensive configuration options for optimal deployment in DOS environments. This guide covers all CONFIG.SYS parameters, configuration validation, and troubleshooting procedures for successful driver installation and operation.

## CONFIG.SYS Parameter Reference

### Basic Installation Syntax

```dos
DEVICE=3CPD.COM [parameters]
```

### Core Parameters

#### Network Interface Configuration

| Parameter | Description | Valid Values | Default |
|-----------|-------------|--------------|---------|
| `IO1=` | First NIC I/O base address | 0x200-0x3F0 (32-byte aligned) | 0x300 |
| `IO2=` | Second NIC I/O base address | 0x200-0x3F0 (32-byte aligned) | 0x320 |
| `IRQ1=` | First NIC IRQ number | 3,5,7,9,10,11,12,15 | 5 |
| `IRQ2=` | Second NIC IRQ number | 3,5,7,9,10,11,12,15 | 10 |
| `SPEED=` | Network speed setting | 10, 100, AUTO | AUTO |
| `BUSMASTER=` | Bus mastering mode | ON, OFF, AUTO | AUTO |

#### Memory and Buffer Configuration

| Parameter | Description | Valid Values | Default |
|-----------|-------------|--------------|---------|
| `XMS=` | Use XMS memory | 0, 1 | 1 |
| `BUFFERS=` | Number of packet buffers | 1-16 | 4 |
| `BUFSIZE=` | Buffer size in bytes | 64-65536 | 1514 |
| `INTVEC=` | Packet driver interrupt vector | 0x60-0x80 | 0x60 |

#### Diagnostic and Logging

| Parameter | Description | Valid Values | Default |
|-----------|-------------|--------------|---------|
| `DEBUG=` | Debug verbosity level | 0-3 | 0 |
| `LOG=` | Enable diagnostic logging | ON, OFF | ON |
| `STATS=` | Enable statistics collection | 0, 1 | 1 |
| `PROMISC=` | Promiscuous mode | 0, 1 | 0 |
| `TEST=` | Test mode operation | 0, 1 | 0 |

#### Routing Configuration

| Parameter | Description | Format | Example |
|-----------|-------------|---------|---------|
| `ROUTING=` | Enable packet routing | 0, 1 | 1 |
| `STATIC_ROUTING=` | Enable static routes | 0, 1 | 0 |
| `ROUTE=` | Static route entry | network/mask,nic[,gateway] | 192.168.1.0/24,1 |

## Configuration Examples

### Single NIC Basic Configuration

```dos
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=5 /SPEED=AUTO
```

**Use Case**: Simple single-NIC installation with automatic speed detection.

### Dual NIC Load Balancing

```dos
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=10 /BUSMASTER=AUTO /LOG=ON
```

**Use Case**: Dual-NIC configuration with load balancing and diagnostic logging.

### High-Performance Server Configuration

```dos
DEVICE=3CPD.COM /IO1=0x240 /IRQ1=11 /SPEED=100 /BUSMASTER=ON /BUFFERS=16 /BUFSIZE=1600 /XMS=1
```

**Use Case**: Optimized for high-throughput applications with maximum buffer allocation.

### Static Routing Configuration

```dos
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=7 /ROUTE=192.168.1.0/24,1 /ROUTE=10.0.0.0/8,2 /STATIC_ROUTING=1
```

**Use Case**: Multi-homed system with specific network routing requirements.

### Debug and Development

```dos
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=5 /DEBUG=3 /LOG=ON /TEST=1 /PROMISC=1
```

**Use Case**: Development and troubleshooting with maximum diagnostic output.

### Legacy System Configuration

```dos
DEVICE=3CPD.COM /IO1=0x280 /IRQ1=3 /SPEED=10 /BUSMASTER=OFF /XMS=0 /BUFFERS=2
```

**Use Case**: 80286 systems with limited memory and older hardware.

## Hardware-Specific Configuration Options

### 3C515-TX Fast EtherLink ISA (Bus Mastering)

**Recommended Configuration:**
```dos
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=5 /SPEED=100 /BUSMASTER=ON /BUFFERS=8
```

**Key Considerations:**
- Requires 386+ CPU for bus mastering
- Optimal at 100 Mbps with proper cabling
- Benefits from XMS memory allocation
- Use IRQ 5, 10, 11, or 15 for best performance

### 3C509B EtherLink III ISA

**Recommended Configuration:**
```dos
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=5 /SPEED=10 /BUSMASTER=OFF /BUFFERS=4
```

**Key Considerations:**
- 10 Mbps operation (no 100 Mbps support)
- Does not support bus mastering
- Works well with conventional memory
- Compatible with all supported CPU types

### Dual NIC Configurations

**Mixed NIC Types:**
```dos
DEVICE=3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=10 /SPEED=AUTO /BUSMASTER=AUTO
```

**Performance Optimization:**
- Use different IRQs (avoid conflicts)
- Ensure 32-byte aligned I/O addresses
- Enable bus mastering only if both NICs support it
- Consider load balancing for high-traffic scenarios

## Parameter Validation Rules

### I/O Address Validation

**Valid Range**: 0x200 to 0x3F0
**Alignment**: Must be 32-byte aligned (0x20 boundary)
**Conflict Detection**: IO1 and IO2 must not overlap

**Valid Examples:**
- 0x200, 0x220, 0x240, 0x260, 0x280, 0x2A0, 0x2C0, 0x2E0
- 0x300, 0x320, 0x340, 0x360, 0x380, 0x3A0, 0x3C0, 0x3E0

**Invalid Examples:**
- 0x310 (not 32-byte aligned)
- 0x1F0 (below minimum range)
- 0x400 (above maximum range)

### IRQ Validation

**Valid IRQs**: 3, 5, 7, 9, 10, 11, 12, 15
**Conflict Detection**: IRQ1 and IRQ2 must be different
**Hardware Compatibility**: All IRQs must be available on target system

**IRQ Selection Guidelines:**
- **IRQ 3**: Usually available (COM2/COM4 alternate)
- **IRQ 5**: Commonly available (LPT2 alternate)
- **IRQ 7**: May conflict with LPT1
- **IRQ 9**: Usually available (redirected from IRQ 2)
- **IRQ 10**: Often available
- **IRQ 11**: Usually available
- **IRQ 12**: May be used by PS/2 mouse
- **IRQ 15**: Usually available (IDE secondary)

### Cross-Parameter Validation

**Bus Mastering Requirements:**
- `BUSMASTER=ON` requires 386+ CPU
- Automatically validated against CPU detection
- Fallback to `BUSMASTER=OFF` on incompatible systems

**Memory Configuration:**
- XMS memory preferred for bus mastering operations
- Buffer size must be reasonable for available memory
- Buffer count scales with available resources

**Route Configuration:**
- Maximum 16 static routes supported
- Network addresses must be valid IP format
- NIC IDs must correspond to configured interfaces

## Error Codes and Resolution

### Configuration Error Codes

| Error Code | Description | Resolution |
|------------|-------------|------------|
| `CONFIG_ERR_INVALID_IO_RANGE` | I/O address out of valid range | Use address between 0x200-0x3F0 |
| `CONFIG_ERR_IO_CONFLICT` | I/O address conflict between NICs | Ensure 32-byte separation minimum |
| `CONFIG_ERR_INVALID_IRQ_RANGE` | Invalid IRQ number | Use IRQ 3,5,7,9,10,11,12,15 |
| `CONFIG_ERR_IRQ_CONFLICT` | IRQ conflict between NICs | Use different IRQs for each NIC |
| `CONFIG_ERR_CPU_REQUIRED` | CPU doesn't support feature | Disable BUSMASTER on <386 systems |
| `CONFIG_ERR_ROUTE_SYNTAX` | Invalid route syntax | Check network/mask,nic format |
| `CONFIG_ERR_TOO_MANY_ROUTES` | Exceeded route limit | Maximum 16 routes supported |
| `CONFIG_ERR_INVALID_SPEED` | Invalid speed setting | Use 10, 100, or AUTO |

### Common Configuration Issues

#### "Invalid I/O Base Address"

**Symptoms**: Driver fails to load with I/O address error
**Causes**:
- Address not 32-byte aligned
- Address outside valid range
- Conflict with existing hardware

**Solutions**:
1. Use standard addresses: 0x300, 0x320, 0x340
2. Check for hardware conflicts with MSD.EXE
3. Ensure addresses don't overlap (minimum 0x20 apart)

#### "IRQ Conflict Detected"

**Symptoms**: Driver loads but no network activity
**Causes**:
- IRQ already in use by other hardware
- Same IRQ specified for both NICs
- IRQ not supported by hardware

**Solutions**:
1. Use IRQ detection utilities (MSD.EXE)
2. Try alternative IRQs: 5, 10, 11, 15
3. Disable conflicting hardware if possible

#### "Bus Mastering Not Supported"

**Symptoms**: Warning message during driver load
**Causes**:
- CPU type <386
- Hardware doesn't support bus mastering
- Insufficient memory for DMA buffers

**Solutions**:
1. Set `BUSMASTER=OFF` for <386 systems
2. Use `BUSMASTER=AUTO` for automatic detection
3. Upgrade to 386+ system for optimal performance

## Performance Tuning Parameters

### Memory Configuration for Performance

**High Memory Systems (>2MB):**
```dos
DEVICE=3CPD.COM /XMS=1 /BUFFERS=16 /BUFSIZE=1600
```

**Low Memory Systems (<1MB):**
```dos
DEVICE=3CPD.COM /XMS=0 /BUFFERS=4 /BUFSIZE=1514
```

### CPU-Specific Optimizations

**Pentium Systems:**
```dos
DEVICE=3CPD.COM /BUSMASTER=ON /SPEED=100 /BUFFERS=16
```

**486 Systems:**
```dos
DEVICE=3CPD.COM /BUSMASTER=AUTO /SPEED=AUTO /BUFFERS=8
```

**286 Systems:**
```dos
DEVICE=3CPD.COM /BUSMASTER=OFF /SPEED=10 /BUFFERS=4
```

### Network-Specific Tuning

**File Server Applications:**
- Increase buffer count: `BUFFERS=16`
- Use larger buffers: `BUFSIZE=1600`
- Enable statistics: `STATS=1`

**Interactive Applications:**
- Standard buffer count: `BUFFERS=4`
- Standard buffer size: `BUFSIZE=1514`
- Enable fast response: `SPEED=AUTO`

**Batch Transfer Applications:**
- Maximum buffers: `BUFFERS=16`
- Large buffer size: `BUFSIZE=1600`
- Enable bus mastering: `BUSMASTER=ON`

## Configuration Validation Tools

### Built-in Validation

The driver performs comprehensive validation during load:

1. **Parameter Syntax**: Validates all parameter formats
2. **Range Checking**: Ensures values within valid ranges
3. **Conflict Detection**: Checks for I/O and IRQ conflicts
4. **Cross-Validation**: Verifies parameter compatibility
5. **Hardware Validation**: Confirms hardware supports configuration

### Manual Validation Steps

**Before Installation:**
1. Run `MSD.EXE` to check current hardware usage
2. Verify available IRQs and I/O addresses
3. Confirm CPU type and memory availability
4. Test network cable and hub connectivity

**After Installation:**
1. Check for error messages in boot log
2. Verify driver loads successfully
3. Test network connectivity with PING
4. Monitor performance with built-in statistics

### Troubleshooting Configuration Issues

**Driver Won't Load:**
1. Check CONFIG.SYS syntax
2. Verify file path to 3CPD.COM
3. Check for parameter validation errors
4. Ensure hardware is properly installed

**Driver Loads But No Network:**
1. Verify IRQ configuration
2. Check cable connections
3. Test with different speed settings
4. Enable diagnostic logging

**Poor Performance:**
1. Check for IRQ conflicts
2. Verify bus mastering configuration
3. Adjust buffer settings
4. Monitor CPU utilization

## Configuration File Templates

### Production Server Template
```dos
REM 3COM Packet Driver - Production Server Configuration
REM Optimized for high-throughput file server applications
DEVICE=C:\NETWORK\3CPD.COM /IO1=0x300 /IRQ1=11 /SPEED=100 /BUSMASTER=ON /BUFFERS=16 /BUFSIZE=1600 /XMS=1 /STATS=1 /LOG=OFF
```

### Development Workstation Template
```dos
REM 3COM Packet Driver - Development Configuration
REM Optimized for debugging and testing
DEVICE=C:\NETWORK\3CPD.COM /IO1=0x300 /IRQ1=5 /SPEED=AUTO /DEBUG=2 /LOG=ON /STATS=1 /TEST=0
```

### Legacy System Template
```dos
REM 3COM Packet Driver - Legacy System Configuration
REM Optimized for 80286 systems with limited resources
DEVICE=C:\NETWORK\3CPD.COM /IO1=0x300 /IRQ1=5 /SPEED=10 /BUSMASTER=OFF /XMS=0 /BUFFERS=2 /BUFSIZE=1514
```

### Multi-Homed Router Template
```dos
REM 3COM Packet Driver - Router Configuration
REM Dual-NIC configuration with static routing
DEVICE=C:\NETWORK\3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=10 /ROUTING=1 /STATIC_ROUTING=1 /ROUTE=192.168.1.0/24,1 /ROUTE=192.168.2.0/24,2
```

This configuration guide provides comprehensive coverage of all CONFIG.SYS parameters and configuration scenarios for successful 3COM packet driver deployment in production environments.