# Quick Start Guide

Get your 3Com network card working in under 5 minutes with the most common configuration.

## Prerequisites

- DOS 5.0 or higher
- 3Com network interface card (any of the 65 supported models)
- Available conventional memory: minimum 43KB

## Simple Installation

### Step 1: Copy Files
```batch
COPY 3CPD.COM C:\
COPY ETL3.MOD C:\
COPY BOOMTEX.MOD C:\
```

### Step 2: Load Driver
Add this line to your CONFIG.SYS:
```
DEVICE=C:\3CPD.COM
```

**That's it!** Reboot and your network card will be automatically detected.

## Most Common Configurations

### Basic Networking (43KB memory usage)
```batch
REM Basic packet driver - works with all applications
DEVICE=C:\3CPD.COM
```

### Business/Enterprise Setup (59KB memory usage)
```batch
REM Includes VLAN, Wake-on-LAN, and professional monitoring
DEVICE=C:\3CPD.COM /STANDARD
```

### Power User Setup (69KB memory usage)
```batch
REM Full enterprise features with diagnostics
DEVICE=C:\3CPD.COM /ADVANCED
```

## Common Network Applications

### TCP/IP Internet Access
After loading the driver:
```batch
REM Example with mTCP
SET MTCPCFG=C:\MTCP\TCP.CFG
DHCP
```

### File Sharing (NetBIOS)
```batch
REM Load Microsoft Network Client
NET START
```

### DOS Games (IPX/SPX)
```batch
REM Load IPX driver
LSL
ETH_II.COM
IPXODI
```

## Troubleshooting

### Driver Won't Load
1. **Check memory**: Need at least 43KB free conventional memory
2. **Check NIC compatibility**: Run `3CPD /DETECT` to list supported cards
3. **IRQ conflicts**: Try specifying interrupt manually: `DEVICE=C:\3CPD.COM /I:60`

### Network Not Working
1. **Check cable connection**: Ensure network cable is properly connected
2. **Test with diagnostics**: Load with `/V` flag for detailed status
3. **Try different interrupt**: Common alternatives: `/I:5A`, `/I:5B`, `/I:5C`

### Memory Issues
1. **Reduce features**: Use basic configuration (no /STANDARD or /ADVANCED)
2. **Load high**: Try `DEVICEHIGH=C:\3CPD.COM` if you have upper memory
3. **Check other TSRs**: Some programs may conflict - try loading driver first

## Next Steps

- **Full Configuration**: See [Configuration Guide](configuration.md) for all options
- **Advanced Features**: See [User Manual](installation.md) for enterprise features
- **Application Integration**: See [Compatibility Guide](compatibility.md) for specific programs
- **Performance Tuning**: See [Deployment Guide](deployment.md) for optimization tips

## Supported Network Cards

Your 3Com card is automatically detected. The driver supports:

- **3C509 Family**: All EtherLink III variants (ISA)
- **3C589 Family**: PCMCIA cards for laptops  
- **3C590/3C595**: PCI EtherLink XL series
- **3C900/3C905**: Fast EtherLink XL (10/100)
- **Plus 57 additional variants** - complete 3Com genealogy support

**Having problems?** Check the [Troubleshooting Guide](troubleshooting.md) or [report an issue](https://github.com/yourusername/3com-packet-driver/issues).