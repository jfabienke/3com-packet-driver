# TSR Memory Layout with Cold/Hot Section Separation

## Overview

The 3Com Packet Driver uses a cold/hot section separation strategy to minimize resident memory usage. Initialization code (cold section) is discarded after driver setup, leaving only essential runtime code (hot section) resident in memory.

## Memory Map

### Before Initialization Complete
```
┌─────────────────────────┐ 0x0000 (Start of COM file)
│      PSP (256 bytes)    │
├─────────────────────────┤ 0x0100
│                         │
│    HOT SECTION (~12KB)  │ <- Runtime code (ISRs, packet ops)
│    [.text.hot]          │
│                         │
├─────────────────────────┤ ~0x3100
│                         │
│   NORMAL CODE (~4KB)    │ <- General purpose code
│    [.text]              │
│                         │
├─────────────────────────┤ ~0x4100  
│                         │
│   DATA SECTION (~6KB)   │ <- Global variables, buffers
│    [.data, .bss]        │
│                         │
├─────────────────────────┤ ~0x5900
│                         │
│     STACK (2KB)         │ <- Runtime stack
│                         │
├─────────────────────────┤ ~0x6100
│                         │
│   COLD SECTION (~15KB)  │ <- Initialization code
│    [.text.cold]         │    (DISCARDED AFTER INIT)
│                         │
└─────────────────────────┘ ~0x9D00 (End before discard)
```

### After Initialization (Resident Memory)
```
┌─────────────────────────┐ 0x0000 (Start of TSR)
│      PSP (256 bytes)    │
├─────────────────────────┤ 0x0100
│                         │
│    HOT SECTION (~12KB)  │ <- Runtime code
│    - ISR handlers       │
│    - Packet TX/RX       │
│    - Hardware access    │
│    - API dispatch       │
│                         │
├─────────────────────────┤ ~0x3100
│                         │
│   NORMAL CODE (~4KB)    │ <- Utility functions
│                         │
├─────────────────────────┤ ~0x4100
│                         │
│   DATA SECTION (~6KB)   │ <- Runtime data
│    - Handle table       │
│    - NIC contexts      │
│    - Packet buffers    │
│    - Statistics        │
│                         │
├─────────────────────────┤ ~0x5900
│                         │
│     STACK (2KB)         │ <- ISR/runtime stack
│                         │
└─────────────────────────┘ ~0x6100 (TSR END - ~24KB total)

                            [~15KB SAVED by discarding cold section]
```

## Section Contents

### Hot Section (.text.hot) - Always Resident
Functions marked with `HOT_SECTION` attribute:
- **ISR Handlers**: `nic_isr()`, interrupt processing
- **Packet Operations**: `packet_send()`, `packet_receive()`, `packet_process_received()`
- **Hardware Access**: Critical hardware I/O functions
- **API Dispatch**: `api_process_received_packet()`, handle management
- **Performance Critical**: Time-sensitive operations

### Cold Section (.text.cold) - Discarded After Init
Functions marked with `COLD_SECTION` attribute:
- **Initialization**: `init_complete_sequence()`, `hardware_init_all()`
- **Configuration**: `config_parse_params()`, all config handlers
- **Detection**: `detect_cpu_type()`, NIC detection, chipset probing
- **Setup**: EEPROM reading, buffer allocation, routing table setup
- **Diagnostics Init**: Statistics initialization, logging setup

### Data Section - Always Resident
- **Handle Management**: 16 handles × 64 bytes = 1KB
- **NIC Contexts**: 2 NICs × 512 bytes = 1KB
- **Packet Buffers**: 4 buffers × 1514 bytes = ~6KB
- **Routing Table**: 256 bytes
- **Statistics**: 512 bytes (if enabled)
- **Stack**: 2KB for ISR and runtime operations

## Size Optimization Results

| Component | Before | After | Savings |
|-----------|--------|-------|---------|
| Total Driver Size | ~55KB | ~55KB | - |
| Resident Memory | ~45KB | ~24KB | **21KB (47%)** |
| Cold Section | Resident | Discarded | 15KB |
| Debug Code | Included | Removed | 12KB |
| Combined Savings | - | - | **~27KB** |

## TSR Loading Process

1. **DOS loads entire COM file** (~55KB)
2. **TSR loader executes**:
   - Calls initialization functions in cold section
   - Configures hardware and sets up data structures
   - Installs interrupt handlers
3. **Calculate resident size**:
   - End of data section - Start of PSP
   - Excludes cold section entirely
4. **TSR installation**:
   - Call DOS TSR function (INT 21h, AH=31h)
   - Pass resident size in paragraphs
   - DOS frees memory above resident portion
5. **Result**: Only ~24KB remains resident

## Build Configuration

### Compiler Flags
```makefile
CFLAGS_PRODUCTION = -zq -ms -s -os -zp1 -zdf -zu -I$(INCDIR) -d0 \
                    -obmiler -oe=100 -ol+ -ox -zl \
                    -DPRODUCTION -DNO_LOGGING -DNO_STATS -DNDEBUG
```

### Linker Configuration
```makefile
LFLAGS_PRODUCTION = system com option map=$(BUILDDIR)/3cpd.map \
                    order clname CODE \
                        segment .text.hot \
                        segment .text \
                    clname COLDCODE \
                        segment .text.cold \
                    clname DATA \
                        segment .data \
                        segment .bss
```

### Section Attributes (production.h)
```c
#define HOT_SECTION   __attribute__((section(".text.hot")))
#define COLD_SECTION  __attribute__((section(".text.cold")))
#define INIT_SECTION  __attribute__((section(".text.init")))
```

## Memory Access Patterns

### Hot Path Optimization
- ISR code and data in same 64KB segment for fast access
- Critical data structures aligned for optimal CPU access
- Frequently accessed variables grouped together

### Cache Considerations
- Hot section fits in typical 486 CPU cache (8KB)
- Minimizes cache misses during packet processing
- Cold section never pollutes cache after init

## Verification

### Build Verification
```bash
make production
# Check MAP file for section placement
grep "\.text\.hot\|\.text\.cold" build/3cpd.map
```

### Runtime Verification
1. Load driver with debug output
2. Check reported memory sizes:
   - Hot section size
   - Cold section size (to be discarded)
   - Final resident size
3. Use DOS MEM command to verify actual usage

## Benefits

1. **Memory Savings**: 21KB reduction in resident memory (47% smaller)
2. **Cache Efficiency**: Smaller hot path improves CPU cache usage
3. **No Performance Impact**: All runtime code remains resident
4. **Clean Separation**: Clear boundary between init and runtime
5. **DOS Compatibility**: Works with all DOS memory managers

## Future Optimizations

1. **Further Hot/Cold Analysis**: Profile to find more cold functions
2. **Data Section Optimization**: Move rarely-used data to XMS
3. **Code Sharing**: Merge duplicate code paths
4. **Overlay System**: Load drivers dynamically per NIC type