# 3Com Packet Driver ANSI Color Demo

This directory contains a demonstration program showcasing the Quarterdeck-style ANSI color interface capabilities of the 3Com packet driver.

## Overview

The ANSI color system provides a professional, retro-style interface that DOS power users and system administrators will recognize and appreciate. It brings that classic 90s system utility aesthetic while maintaining full functionality on systems without ANSI support.

## Features Demonstrated

### 1. **Driver Banner and Startup Sequence**
- Classic blue header with centered title
- System information display
- Professional startup progression

### 2. **Hardware Detection Display**
- Real-time scanning progress
- Color-coded detection results
- Professional status indicators

### 3. **Network Monitor Interface**
- Full-screen network activity display
- Real-time packet statistics
- ASCII art traffic graphs with color coding
- Multiple NIC support

### 4. **Color Palette Test**
- Complete 16-color ANSI palette
- Quarterdeck-style color scheme
- Status indicator demonstrations

### 5. **Box Drawing Characters**
- Single and double-line borders
- Nested box examples
- ASCII fallback compatibility

### 6. **Diagnostic Messages**
- Timestamped log entries
- Color-coded message levels (INFO, WARNING, ERROR, SUCCESS)
- Real-time message display

## Building the Demo

### Requirements
- Turbo C 2.0 or compatible DOS C compiler
- ANSI.SYS or compatible driver for best results
- DOS 3.3 or later

### Compilation
```bash
cd demos
make
```

This produces `ansi_demo.exe` which can be run on any DOS system.

### Build Options
```bash
make debug    # Build with debug symbols
make release  # Build optimized version
make clean    # Clean build files
```

## ANSI.SYS Setup

For the best experience, ensure ANSI.SYS is loaded in your DOS system:

### Method 1: CONFIG.SYS (Recommended)
Add this line to your `CONFIG.SYS` file:
```
DEVICE=C:\DOS\ANSI.SYS
```

For MS-DOS 6.x and later with extended features:
```
DEVICE=C:\DOS\ANSI.SYS /X
```

### Method 2: Alternative ANSI Drivers
- **NANSI.SYS**: Faster replacement for ANSI.SYS
- **ANSI.COM**: TSR version that loads from command line
- **FANSI.SYS**: Extended features and better performance

### Method 3: Environment Variable
Set the ANSI environment variable:
```
SET ANSI=ON
```

## Running the Demo

```bash
ansi_demo.exe
```

The demo provides an interactive menu with the following options:

1. **Driver Banner and Startup** - Shows the professional startup sequence
2. **Hardware Detection** - Simulates NIC detection with progress indicators  
3. **Network Monitor** - Real-time network activity display with graphs
4. **Color Palette Test** - Demonstrates all available colors
5. **Box Drawing** - Shows box drawing capabilities
6. **Diagnostic Messages** - Real-time log message display

### Navigation
- **Number keys**: Select demo options
- **ESC or 0**: Exit demo
- **Any key**: Continue after each demonstration

## Compatibility

### With ANSI Support
- Full color display
- Professional box drawing
- Real-time traffic graphs
- Complete Quarterdeck-style interface

### Without ANSI Support
- Graceful fallback to plain text
- All functionality preserved
- Compatible with any DOS terminal

## Testing ANSI Support

Test if ANSI colors work on your system:
```bash
make color-test
```

This displays colored text. If you see colors, ANSI is working properly.

## Integration with Main Driver

The console system is designed to integrate seamlessly with the main packet driver:

```c
#include "console.h"
#include "nic_display.h"

int main(void) {
    // Initialize console system
    console_init();
    
    // Display professional startup
    display_driver_banner("1.0");
    display_detection_progress();
    
    // ... driver initialization ...
    
    // Show results
    display_nic_status_summary(nics, nic_count);
    display_tsr_loaded(segment, interrupt, size_kb);
    
    // Cleanup
    console_cleanup();
    return 0;
}
```

## Technical Details

### Color Coding Standards
- **Blue header backgrounds**: Main titles and headers
- **Bright green**: Success, active, link up
- **Yellow**: Warnings, ready states
- **Bright red**: Errors, failures, link down
- **Bright cyan**: Information labels
- **White**: Data values
- **Gray**: Frames and borders

### Performance
- Minimal overhead when ANSI not detected
- Fast box drawing with character caching
- Efficient color management
- Low memory footprint

### Memory Usage
- Console system: ~2KB
- Display functions: ~3KB  
- Total overhead: ~5KB

## Files

- `ansi_demo.c` - Main demonstration program
- `console.c` - Core ANSI console implementation
- `nic_display.c` - Network interface display functions
- `console.h` - Console system header
- `nic_display.h` - Display functions header
- `Makefile` - Build configuration
- `README.md` - This documentation

## Future Enhancements

Potential improvements for the ANSI system:

1. **Mouse Support**: Add mouse support for interactive elements
2. **Configuration Dialogs**: Full-screen configuration interfaces
3. **Help System**: Context-sensitive help with F1 key
4. **Status Bar**: Persistent status information
5. **Window Management**: Multiple overlapping windows
6. **Theme Support**: User-selectable color themes

## Troubleshooting

### Colors Don't Appear
- Check ANSI.SYS is loaded in CONFIG.SYS
- Try alternative ANSI drivers (NANSI.SYS, FANSI.SYS)
- Set `SET ANSI=ON` environment variable
- Reboot after changing CONFIG.SYS

### Box Drawing Issues
- Some terminals may not support extended ASCII
- The system automatically falls back to standard ASCII characters
- No functionality is lost

### Performance Issues
- The demo includes artificial delays for demonstration
- Real driver integration has minimal overhead
- Disable colors with environment variable if needed

## License

This ANSI color demo is part of the 3Com Packet Driver project and follows the same licensing terms as the main driver.

---

*The Quarterdeck-style interface brings that authentic DOS utility feel that system administrators and power users remember. Professional, functional, and unmistakably retro.*