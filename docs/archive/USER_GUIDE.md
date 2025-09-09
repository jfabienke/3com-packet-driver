# 3Com DOS Packet Driver - User Guide

Last Updated: 2025-09-04
Status: archived
Purpose: Historical user guide. Content consolidated into USER_MANUAL.md.

Note: This guide has been consolidated into USER_MANUAL.md. Please refer to USER_MANUAL.md for the current canonical user documentation.

## Table of Contents
1. [Introduction](#introduction)
2. [System Requirements](#system-requirements)
3. [Installation](#installation)
4. [Configuration](#configuration)
5. [Command Line Options](#command-line-options)
6. [Troubleshooting](#troubleshooting)
7. [Performance Tuning](#performance-tuning)
8. [Multi-NIC Setup](#multi-nic-setup)
9. [Uninstallation](#uninstallation)

## Introduction

The 3Com DOS Packet Driver provides network connectivity for DOS applications using 3Com 3C509B (10 Mbps) and 3C515-TX (100 Mbps) network interface cards. This driver implements the Packet Driver Specification v1.11 and is compatible with popular DOS networking applications like mTCP, WATTCP, and others.

### Key Features
- Ultra-compact 13KB resident memory footprint
- Automatic CPU optimization (286 through Pentium 4+)
- TSR defensive programming for stability
- Multi-NIC support with load balancing
- XMS memory support for buffer management

## System Requirements

### Minimum Requirements
- **CPU**: Intel 80286 or compatible
- **DOS**: Version 2.0 or higher
- **Memory**: 13KB free conventional memory
- **Network Card**: 3Com 3C509B or 3C515-TX

### Recommended Requirements
- **CPU**: Intel 80386 or higher for best performance
- **DOS**: Version 5.0 or higher for UMB support
- **Memory**: 64KB free conventional memory
- **XMS**: Version 2.0+ for extended buffer support

## Installation

### Method 1: CONFIG.SYS (Recommended)

Add the following line to your CONFIG.SYS file:

```
DEVICE=C:\NET\3CPKT.EXE /I:60
```

This loads the driver during system startup using interrupt 60h.

### Method 2: AUTOEXEC.BAT

Add the following line to your AUTOEXEC.BAT file:

```
C:\NET\3CPKT.EXE /I:60 /V
```

The `/V` option provides verbose output during initialization.

### Method 3: Manual Loading

You can load the driver manually from the DOS prompt:

```
C:\NET\3CPKT.EXE /I:60
```

## Configuration

### Automatic Configuration

By default, the driver automatically detects:
- 3Com network cards using Plug and Play
- Available I/O addresses
- Free IRQ lines
- CPU type for optimization

### Manual Configuration

For systems without PnP support or to override auto-detection:

```
3CPKT.EXE /I:60 /IO:300 /IRQ:10
```

### Common I/O Addresses

| Card Type | Default I/O | Alternatives |
|-----------|-------------|--------------|
| 3C509B | 0x300 | 0x310, 0x320, 0x330 |
| 3C515-TX | 0x280 | 0x290, 0x2A0, 0x2B0 |

### Common IRQ Settings

| IRQ | Usage | Availability |
|-----|-------|--------------|
| 3 | COM2/COM4 | Often available |
| 5 | LPT2/Sound | Check for conflicts |
| 10 | Free | Usually available |
| 11 | Free | Usually available |

## Command Line Options

### Basic Options

| Option | Description | Example |
|--------|-------------|---------|
| `/I:nn` | Packet driver interrupt (60-80 hex) | `/I:60` |
| `/IO:nnn` | I/O base address | `/IO:300` |
| `/IRQ:n` | Hardware interrupt | `/IRQ:10` |
| `/V` | Verbose initialization | `/V` |
| `/U` | Unload driver | `/U` |

### Advanced Options

| Option | Description | Example |
|--------|-------------|---------|
| `/XMS` | Enable XMS buffer migration | `/XMS` |
| `/DMA` | Force DMA mode (3C515 only) | `/DMA` |
| `/PIO` | Force PIO mode | `/PIO` |
| `/MULTI` | Enable multi-NIC support | `/MULTI` |

### Examples

**Basic installation:**
```
3CPKT.EXE /I:60
```

**Manual configuration:**
```
3CPKT.EXE /I:60 /IO:300 /IRQ:10 /V
```

**Multi-NIC setup:**
```
3CPKT.EXE /I:60 /MULTI /V
```

**XMS optimization:**
```
3CPKT.EXE /I:60 /XMS
```

## Troubleshooting

### Common Issues and Solutions

#### Driver Won't Load

**Symptom:** "No 3Com network card detected"

**Solutions:**
1. Verify the card is properly seated in the ISA slot
2. Check for I/O address conflicts with other devices
3. Try manual configuration with known-good settings
4. Ensure the card is enabled in system BIOS

#### Network Not Working

**Symptom:** Driver loads but no network connectivity

**Solutions:**
1. Verify cable connection and link lights
2. Check IRQ conflicts with `MSD.EXE` or similar tools
3. Ensure packet driver interrupt is correct in applications
4. Try forcing PIO mode with `/PIO` option

#### System Hangs or Crashes

**Symptom:** System freezes during driver loading or operation

**Solutions:**
1. Check for IRQ conflicts with other devices
2. Disable bus mastering with `/PIO` option
3. Reduce packet buffer size in memory-constrained systems
4. Ensure adequate stack space in CONFIG.SYS (STACKS=9,256)

#### Performance Issues

**Symptom:** Slow network transfers or high CPU usage

**Solutions:**
1. Enable XMS buffers with `/XMS` option
2. Ensure CPU optimization is working (check verbose output)
3. For 3C515, ensure DMA is enabled and working
4. Check for excessive collisions on the network

### Diagnostic Commands

**Check driver status:**
```
3CPKT.EXE /STATUS
```

**View statistics:**
```
3CPKT.EXE /STATS
```

**Test packet transmission:**
```
3CPKT.EXE /TEST
```

## Performance Tuning

### CPU Optimization

The driver automatically detects and optimizes for your CPU:

| CPU | Optimization | Performance Gain |
|-----|--------------|------------------|
| 286 | Baseline | Reference |
| 386 | 32-bit operations | +15% |
| 486 | BSWAP, cache management | +20% |
| Pentium | Dual pipeline | +25% |
| Pentium 4+ | CLFLUSH cache control | +30% |

### Memory Optimization

**Conventional Memory:**
- Driver uses only 13KB resident
- Packet buffers: 3KB included
- Stack space: 512 bytes

**XMS Memory:**
- Enable with `/XMS` option
- Moves buffers to extended memory
- Reduces conventional memory to ~8KB

**UMB Loading (DOS 5.0+):**
```
DEVICEHIGH=C:\NET\3CPKT.EXE /I:60
```

### Network Optimization

**For 3C509B (10 Mbps):**
- PIO mode is optimized for CPU
- Full duplex reduces collisions
- Hardware checksum offload enabled

**For 3C515-TX (100 Mbps):**
- Bus master DMA reduces CPU load
- Auto-negotiation for best speed
- 4-tier cache coherency for stability

## Multi-NIC Setup

### Configuration

The driver supports up to 4 NICs simultaneously:

```
3CPKT.EXE /I:60 /MULTI /V
```

### Load Balancing

Three algorithms available:
1. **Round-robin** (default) - Alternates between NICs
2. **Weighted** - Based on NIC speed (100 Mbps preferred)
3. **Adaptive** - Based on current load and errors

### Failover

Automatic failover when a NIC fails:
- Detection within 100ms
- Transparent to applications
- Automatic failback when recovered

### Example Multi-NIC CONFIG.SYS

```
DEVICE=C:\NET\3CPKT.EXE /I:60 /MULTI /XMS
```

## Uninstallation

### Temporary Unload

To unload the driver from memory:

```
3CPKT.EXE /U
```

**Note:** Only works if driver is the last TSR loaded.

### Permanent Removal

1. Remove the DEVICE line from CONFIG.SYS
2. Remove any references from AUTOEXEC.BAT
3. Delete the driver file if desired
4. Reboot the system

### Clean Uninstall Checklist

- [ ] Driver unloaded successfully
- [ ] Interrupt vector restored
- [ ] No memory leaks reported
- [ ] Network applications reconfigured

## Appendix

### Error Codes

| Code | Description | Solution |
|------|-------------|----------|
| 01 | No card found | Check hardware |
| 02 | I/O conflict | Change I/O address |
| 03 | IRQ conflict | Change IRQ |
| 04 | Memory allocation failed | Free conventional memory |
| 05 | XMS not available | Install HIMEM.SYS |

### Compatible Software

Tested and compatible with:
- mTCP suite
- WATTCP applications
- NCSA Telnet
- Trumpet Winsock
- DOS network games
- Novel NetWare client

### Technical Support

For issues not covered in this guide:
1. Check RELEASE_NOTES.md for known issues
2. Review docs/TROUBLESHOOTING.md for advanced diagnostics
3. Enable verbose mode (`/V`) and capture output
4. Document system configuration and error messages

---

*3Com DOS Packet Driver v1.0.0 - User Guide*
