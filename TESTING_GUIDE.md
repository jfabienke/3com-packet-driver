# Testing Guide for 3Com DOS Packet Driver

## 🚧 Alpha Testing - Your Help is Needed!

This driver is in alpha stage and requires extensive real hardware testing. We especially need testers with:
- Various 3Com NIC models (3C509B, 3C515-TX, 3C589, Vortex/Boomerang family)
- Different DOS versions (MS-DOS, PC-DOS, DR-DOS, FreeDOS)
- Various CPU types (286, 386, 486, Pentium)
- Different chipsets and motherboards

## Prerequisites

### Build Requirements
- Open Watcom C/C++ 1.9 or later
- NASM (Netwide Assembler)
- DOS system for testing

### Test Environment
- DOS 2.0 or later
- At least 512KB RAM (640KB recommended)
- Supported 3Com NIC installed
- Network connection for testing

## Building the Driver

```bash
# Build release version
wmake

# Build debug version with symbols
wmake debug

# Clean build artifacts
wmake clean
```

## Installation

1. Add to CONFIG.SYS:
```
DEVICE=3COMPKT.COM [options]
```

2. Optional parameters:
- `/IO1=0x300` - Set I/O base address for first NIC
- `/IRQ1=10` - Set IRQ for first NIC
- `/BUSMASTER=ON` - Enable bus mastering (3C515-TX/PCI only)
- `/LOG=ON` - Enable diagnostic logging
- `/DEBUG=ON` - Enable debug output

## Basic Functionality Tests

### 1. Driver Loading
- Verify driver loads without errors
- Check TSR memory footprint (should be <6KB)
- Confirm hardware detection messages

### 2. Hardware Detection
```
3COMPKT.COM /DETECT
```
- Should list all detected 3Com NICs
- Verify correct model identification
- Check I/O and IRQ assignments

### 3. Packet Driver API
Test with standard packet driver utilities:
- `PKTSTAT` - Display driver statistics
- `PKTADDR` - Show MAC address
- `PKTSEND` - Send test packets
- `PKTRECV` - Receive packets

### 4. Network Connectivity
Test with DOS network applications:
- mTCP suite (FTP, Telnet, IRC)
- Arachne web browser
- NCSA Telnet
- MS-DOS Network Client

## Performance Testing

### Throughput Tests
1. Use FTP to transfer large files
2. Record transfer rates
3. Compare with theoretical maximums:
   - 3C509B: ~1.1 MB/s (10 Mbps)
   - 3C515-TX: ~3-4 MB/s (ISA bus limited)
   - PCI cards: ~11 MB/s (100 Mbps)

### CPU Usage
1. Monitor system responsiveness during transfers
2. Test with interrupt mitigation on/off
3. Compare PIO vs DMA modes (where applicable)

## Advanced Feature Testing

### Multi-NIC Support
1. Install multiple 3Com NICs
2. Configure with different I/O and IRQ:
```
DEVICE=3COMPKT.COM /IO1=0x300 /IRQ1=10 /IO2=0x320 /IRQ2=11
```
3. Test routing between interfaces

### Memory Configurations
1. Test with conventional memory only
2. Enable XMS and verify buffer allocation
3. Test UMB usage if available

### Bus Mastering (3C515-TX/PCI)
1. Enable with `/BUSMASTER=ON`
2. Verify DMA transfers work
3. Check for data corruption
4. Test with different chipsets

## Troubleshooting Tests

### Debug Mode
```
3COMPKT.COM /DEBUG=ON /LOG=ON
```
- Capture debug output
- Check for error messages
- Monitor packet flow

### Hardware Conflicts
1. Test different I/O addresses
2. Try different IRQ assignments
3. Disable conflicting devices
4. Check for DMA channel conflicts

## Reporting Issues

When reporting issues, please include:

1. **System Information**
   - DOS version and type
   - CPU model and speed
   - Motherboard/chipset
   - Amount of RAM
   - Other installed devices

2. **NIC Information**
   - Exact model number
   - Revision/version
   - I/O and IRQ settings
   - PnP or manual configuration

3. **Problem Description**
   - Steps to reproduce
   - Error messages
   - Debug output (if available)
   - Network configuration

4. **Test Results**
   - What works
   - What doesn't work
   - Performance measurements
   - Comparison with other drivers (if applicable)

## Test Coverage Matrix

| Feature | 3C509B | 3C515-TX | 3C589 | Vortex | Boomerang |
|---------|--------|----------|-------|--------|-----------|
| Basic TX/RX | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ⚠️ |
| Multi-NIC | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ⚠️ |
| Bus Master | N/A | ⚠️ | N/A | ⚠️ | ⚠️ |
| XMS Buffers | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ⚠️ |
| Routing | ⚠️ | ⚠️ | ⚠️ | ⚠️ | ⚠️ |
| PCMCIA Hot-plug | N/A | N/A | ⚠️ | N/A | N/A |

Legend: ✅ Tested Working | ⚠️ Needs Testing | ❌ Known Issues | N/A Not Applicable

## Contributing Test Results

Please submit test results via GitHub issues with the label "test-results". Include all relevant information from the reporting section above.

Your testing helps make this driver better for the entire DOS community!