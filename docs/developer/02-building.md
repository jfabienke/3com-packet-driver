# Building the 3Com Packet Driver

## Prerequisites

- **Open Watcom C/C++ 1.9** or later
- **NASM** (Netwide Assembler)  
- **GNU Make** compatible make utility
- **Module tools** (for Phase 3A modular builds)

## Build Commands

### Monolithic Build (Legacy)
```bash
wmake              # Build monolithic release version (~55KB)
wmake debug        # Build debug version with symbols
wmake clean        # Clean build directory
wmake test         # Run test suite
```

### Modular Build (Phase 3A - Recommended)
```bash
wmake modular      # Build complete modular system
wmake core         # Build core loader only (3CPD.COM ~30KB)
wmake modules      # Build all modules (.MOD files)
wmake hardware     # Build hardware modules only
wmake features     # Build feature modules only
wmake dist         # Create distribution package
```

### Individual Module Builds
```bash
wmake ETHRLINK3.MOD    # Build EtherLink III family module
wmake CORKSCREW.MOD    # Build Corkscrew family module
wmake ROUTING.MOD      # Build routing feature module
wmake STATS.MOD        # Build statistics module
wmake FLOWCTRL.MOD     # Build flow control module
wmake DIAG.MOD         # Build diagnostics module
wmake PROMISC.MOD      # Build promiscuous mode module
```

## Build Output

### Monolithic Build
- `build/3cpd.com` - Complete monolithic driver (~55KB)

### Modular Build (Phase 3A)
- `build/3cpd.com` - Core loader (~30KB)
- `build/ETHRLINK3.MOD` - EtherLink III family driver (~13KB)
- `build/CORKSCREW.MOD` - Corkscrew family driver (~17KB)
- `build/ROUTING.MOD` - Multi-NIC routing (~9KB)
- `build/FLOWCTRL.MOD` - Flow control (~8KB)
- `build/STATS.MOD` - Statistics collection (~5KB)
- `build/DIAG.MOD` - Diagnostics (init-only)
- `build/PROMISC.MOD` - Promiscuous mode (~2KB)
- `build/3cpd-modular.zip` - Complete distribution package

## Build Configuration

### Release Build
- Optimized for size and performance
- Debug symbols stripped
- Full optimization enabled

### Debug Build
- Debug symbols included
- Minimal optimization
- Verbose logging enabled

## Build System Details

### Monolithic Build System (Legacy)
The traditional build system produces a single executable:
- Mixed C and Assembly compilation
- Automatic dependency tracking  
- Cross-referenced documentation builds

### Modular Build System (Phase 3A)
The revolutionary modular build system supports the new architecture:

#### Core Loader Build
```makefile
# Core loader (minimal footprint)
3cpd.com: core_loader.obj module_mgr.obj buffer_mgr.obj cache_mgr.obj packet_api.obj
	$(LINKER) $(CORE_LDFLAGS) -o $@ $^
	$(STRIP) $@
```

#### Hardware Module Build
```makefile
# Hardware modules (family-based)
ETHRLINK3.MOD: etherlink3_main.obj etherlink3_eeprom.obj etherlink3_media.obj \
               etherlink3_3c509.obj etherlink3_3c509b.obj etherlink3_pio.obj
	$(MODULE_LINKER) -T scripts/module.ld -o $@ $^
	$(MODULE_VERIFY) $@
	$(MODULE_CHECKSUM) $@

CORKSCREW.MOD: corkscrew_main.obj corkscrew_dma.obj corkscrew_ring.obj \
               corkscrew_3c515.obj corkscrew_cache.obj
	$(MODULE_LINKER) -T scripts/module.ld -o $@ $^
	$(MODULE_VERIFY) $@
	$(MODULE_CHECKSUM) $@
```

#### Feature Module Build
```makefile
# Feature modules (optional capabilities)
ROUTING.MOD: routing_main.obj static_routing.obj flow_routing.obj
	$(MODULE_LINKER) -T scripts/module.ld -o $@ $^
	$(MODULE_VERIFY) $@
	$(MODULE_CHECKSUM) $@

STATS.MOD: statistics_main.obj ring_stats.obj performance_stats.obj
	$(MODULE_LINKER) -T scripts/module.ld -o $@ $^
	$(MODULE_VERIFY) $@
	$(MODULE_CHECKSUM) $@
```

#### Module Tools
```makefile
# Module verification and packaging tools
MODULE_VERIFY = tools/verify_module
MODULE_CHECKSUM = tools/calc_checksum
MODULE_LINKER = $(LINKER)

# Module-specific compiler flags
MODULE_CFLAGS = -DMODULE_BUILD -fPIC -Os
MODULE_LDFLAGS = -T scripts/module.ld -nostdlib
```

## Build Targets Reference

### Primary Targets
| Target | Description | Output |
|--------|-------------|---------|
| `modular` | Complete modular build | Core + all modules |
| `monolithic` | Legacy single-file build | 3cpd.com (~55KB) |
| `core` | Core loader only | 3cpd.com (~30KB) |
| `modules` | All modules | *.MOD files |
| `hardware` | Hardware modules only | ETHRLINK3.MOD, CORKSCREW.MOD |
| `features` | Feature modules only | ROUTING.MOD, STATS.MOD, etc. |
| `dist` | Distribution package | 3cpd-modular.zip |

### Module-Specific Targets
| Module | Size | Description |
|--------|------|-------------|
| `ETHRLINK3.MOD` | ~13KB | Complete EtherLink III family |
| `CORKSCREW.MOD` | ~17KB | Complete Corkscrew family |
| `ROUTING.MOD` | ~9KB | Multi-NIC routing engine |
| `FLOWCTRL.MOD` | ~8KB | 802.3x flow control |
| `STATS.MOD` | ~5KB | Advanced statistics |
| `DIAG.MOD` | ~0KB | Diagnostics (init-only) |
| `PROMISC.MOD` | ~2KB | Promiscuous mode |

### Utility Targets
| Target | Description |
|--------|-------------|
| `clean` | Remove all build artifacts |
| `clean-modules` | Remove module files only |
| `test` | Run comprehensive test suite |
| `test-modules` | Test module loading/unloading |
| `validate` | Validate all modules |
| `package` | Create distribution packages |

## Module Build Process

### 1. Source Organization
```
src/
├── core/              # Core loader sources
│   ├── loader.c
│   ├── module_mgr.c
│   ├── buffer_mgr.c
│   └── packet_api.c
├── modules/
│   ├── hardware/      # Hardware modules
│   │   ├── etherlink3/
│   │   └── corkscrew/
│   └── features/      # Feature modules
│       ├── routing/
│       ├── stats/
│       └── flowctrl/
└── common/           # Shared code
    ├── include/
    └── utils/
```

### 2. Module Compilation
```makefile
# Module compilation with special flags
%.mod.obj: %.c
	$(CC) $(CFLAGS) $(MODULE_CFLAGS) -DMODULE_NAME=\"$(basename $@)\" -c $< -o $@

# Module assembly
%.mod.obj: %.asm
	$(ASM) $(ASMFLAGS) $(MODULE_ASMFLAGS) $< -o $@
```

### 3. Module Linking
```makefile
# Module linker script ensures proper layout
%.MOD: %.mod.obj
	$(MODULE_LINKER) $(MODULE_LDFLAGS) -o $@ $^
	@echo "Linking module: $@"
	@echo "  Input objects: $^"
	@echo "  Module size: $$(stat -c%s $@) bytes"
```

### 4. Module Verification
```bash
#!/bin/bash
# Module verification process
verify_module() {
    local module="$1"
    
    # Check module header
    if ! check_module_header "$module"; then
        echo "ERROR: Invalid module header in $module"
        return 1
    fi
    
    # Verify checksum
    if ! verify_module_checksum "$module"; then
        echo "ERROR: Checksum verification failed for $module"
        return 1
    fi
    
    # Check size constraints
    local size=$(stat -c%s "$module")
    local max_size=$((64 * 1024))  # 64KB maximum
    if [ $size -gt $max_size ]; then
        echo "ERROR: Module $module too large ($size bytes)"
        return 1
    fi
    
    echo "Module $module verified successfully"
    return 0
}
```

## Build Configuration Options

### Environment Variables
```bash
# Build configuration
export BUILD_TYPE=modular          # modular, monolithic
export OPTIMIZATION_LEVEL=size     # size, speed, debug
export MODULE_VALIDATION=strict    # strict, normal, none
export DISTRIBUTION_FORMAT=zip     # zip, tar, directory

# Compiler configuration
export CC=wcc
export ASM=nasm
export LINKER=wlink
export CFLAGS="-zq -bt=dos -ml -3 -ox -d0"
export ASMFLAGS="-f obj"
```

### Custom Build Configurations
```makefile
# Debug modular build
debug-modular:
	$(MAKE) modular BUILD_TYPE=debug MODULE_VALIDATION=strict

# Minimal build (core + single NIC)
minimal:
	$(MAKE) core ETHRLINK3.MOD

# Gaming build (optimized for memory)
gaming:
	$(MAKE) core ETHRLINK3.MOD OPTIMIZATION_LEVEL=size

# Router build (full features)
router:
	$(MAKE) modular MODULES="hardware features"

# Developer build (with diagnostics)
developer:
	$(MAKE) modular DIAG.MOD BUILD_TYPE=debug
```

## Memory Optimization

The modular build system enables fine-tuned memory optimization:

### Build-Time Optimizations
- **Dead Code Elimination**: Unused functions removed per module
- **Size Optimization**: Each module optimized independently
- **Compression**: Optional module compression for distribution

### Runtime Memory Layout
```
Memory Layout (Modular):
┌─────────────────────────┐ ← High Memory
│ Feature Modules         │ (Optional, relocatable)
│ - ROUTING.MOD  (~9KB)   │
│ - STATS.MOD    (~5KB)   │
├─────────────────────────┤
│ Hardware Modules        │ (Required, stable)
│ - ETHRLINK3.MOD (~13KB) │
│ - CORKSCREW.MOD (~17KB) │
├─────────────────────────┤
│ Core Loader             │ (Always resident)
│ - 3CPD.COM     (~30KB)  │
└─────────────────────────┘ ← TSR Base
```

## Distribution Packaging

### Package Creation
```makefile
# Create distribution package
dist: modular
	mkdir -p dist/3cpd-modular
	cp build/3cpd.com dist/3cpd-modular/
	cp build/*.MOD dist/3cpd-modular/
	cp docs/user/*.md dist/3cpd-modular/docs/
	cp examples/*.bat dist/3cpd-modular/examples/
	cd dist && zip -r 3cpd-modular.zip 3cpd-modular/
	@echo "Distribution package created: dist/3cpd-modular.zip"
```

### Package Contents
```
3cpd-modular.zip
├── 3cpd.com              # Core loader
├── ETHRLINK3.MOD         # EtherLink III family
├── CORKSCREW.MOD         # Corkscrew family  
├── ROUTING.MOD           # Multi-NIC routing
├── FLOWCTRL.MOD          # Flow control
├── STATS.MOD             # Statistics
├── DIAG.MOD              # Diagnostics
├── PROMISC.MOD           # Promiscuous mode
├── docs/
│   ├── README.txt        # Quick start guide
│   ├── INSTALLATION.txt  # Installation instructions
│   └── MODULES.txt       # Module descriptions
└── examples/
    ├── MINIMAL.BAT       # Minimal configuration
    ├── ROUTER.BAT        # Router configuration
    └── GAMING.BAT        # Gaming optimization
```

For advanced build configurations and module development, see [MODULE_DEVELOPMENT.md](MODULE_DEVELOPMENT.md).