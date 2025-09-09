# 3Com Packet Driver - Release Notes

## Version 1.0.0-beta1 - Foundation Stabilization Release

### Overview
This release represents a major stability milestone for the 3Com Packet Driver. All critical foundation issues have been resolved, providing a solid base for future enterprise feature integration.

### Critical Fixes Implemented

#### Build System
- **RESOLVED**: Fixed linker errors by adding missing DMA modules to build
- **RESOLVED**: Added `tsr_c_wrappers.obj` to hot section for proper TSR support
- **NEW**: Added `link-sanity` target for build validation
- **NEW**: Added `verify-patches` target for runtime patch verification

#### SMC Patching Framework
- **FIXED**: Extended patching to all critical modules (was only patching packet_api)
- **FIXED**: Now patches `nic_irq_module_header` for DMA boundary checks
- **FIXED**: Now patches `hardware_module_header` for cache flush points
- **NEW**: Runtime verification ensures patches are active (not NOPs)

#### ISR Stack Safety
- **FIXED**: ISR now uses private 2KB stack instead of caller's stack
- **FIXED**: Stack switching in ISR prolog/epilog prevents TSR corruption
- **IMPACT**: Adds 2052 bytes to resident memory (justified by safety improvement)

#### DMA Safety
- **IMPLEMENTED**: Multiple safety gates for 3C515 DMA operations
- **IMPLEMENTED**: Compile-time assertions require DMA safety integration
- **IMPLEMENTED**: Runtime patch verification with automatic PIO fallback

### Temporary Limitations

#### 3C515-TX Network Card
**LIMITATION**: 3C515-TX currently operates in PIO mode only  
**REASON**: DMA mode disabled pending full validation of bus mastering safety  
**IMPACT**: Reduced throughput compared to theoretical maximum  
**RESOLUTION**: Will be enabled after Stage 1 bus mastering validation completes  

**User Action Required**: None - driver automatically uses safe PIO mode

#### Memory Usage
**CURRENT**: ~2.1KB additional resident memory for safety features  
**BREAKDOWN**:
- ISR private stack: 2048 bytes
- Stack pointers: 4 bytes  
- Patch code: ~70 bytes

**JUSTIFICATION**: Critical safety improvements outweigh memory cost

### Known Issues

1. **EEPROM Configuration** - Deferred to future release
   - Status: Identified but not critical
   - Workaround: Use command-line parameters

2. **Performance in PIO Mode**
   - 3C515 operates below theoretical maximum
   - Acceptable for initial deployment
   - DMA will be enabled after validation

### Testing Requirements

Before deploying this release:

1. Run `make link-sanity` to verify build integrity
2. Run `make verify-patches` to confirm patches are active
3. Test with both 3C509B and 3C515-TX NICs if available
4. Verify TSR loads and unloads cleanly

### Upgrade Notes

This release includes breaking changes to the build system:
- Clean rebuild required (`make clean && make release`)
- Previous configurations may need adjustment
- Check CONFIG.SYS parameters remain valid

### Next Release Preview

Stage 0-3 enterprise features planned:
- Vendor extension API (Stage 0)
- Bus mastering validation and enablement (Stage 1)
- Health diagnostics and monitoring (Stage 2)
- Advanced runtime configuration (Stage 3)

### Support

Report issues at: https://github.com/[your-repo]/3com-packet-driver/issues

### Compatibility

- **DOS Version**: 2.0 or higher
- **CPU**: 80286 or higher (8086 support validated)
- **Memory**: 64KB conventional minimum
- **NICs**: 3Com 3C509B, 3C515-TX

### Build Information

```
Compiler: Open Watcom C/C++ 1.9
Assembler: NASM
Build Flags: -os -ot (size and time optimization)
Production Flags: -DPRODUCTION -DNO_LOGGING -DNO_STATS
```

### Checksums

Binary checksums will be provided with official release.

---

**Release Status**: BETA - Production-ready foundation
**Release Date**: TBD
**Version**: 1.0.0-beta1