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
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /LOG=ON
```

### Dual NIC Setup with Routing
```
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /IO2=0x320 /IRQ2=7 /ROUTE=192.168.1.0,255.255.255.0,1
```

### Automatic Bus Mastering Configuration
```
REM Recommended: Let driver test and configure bus mastering automatically
DEVICE=C:\NET\3CPD.COM /IO1=0x300 /IRQ1=5 /BUSMASTER=AUTO /BM_TEST=FULL
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