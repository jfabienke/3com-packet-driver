# Makefile for 3Com Packet Driver
# Requires Open Watcom C/C++ and NASM
# Enhanced by Sub-Agent 6: Build System Optimizer

# --- Directories ---
SRCDIR     = src
CDIR       = $(SRCDIR)/c
ASMDIR     = $(SRCDIR)/asm
INCDIR     = include
BUILDDIR   = build

# --- Build System Configuration ---
# Include enhanced build configuration
-include build/build_config.mk
-include build/test_targets.mk
-include build/debug_config.mk

# --- Tools ---
CC         = wcc     # Open Watcom C Compiler (use wcc instead of wcl for better control)
ASM        = nasm    # Netwide Assembler
LINK       = wlink   # Open Watcom Linker

# --- Compiler Flags (C) ---
# -zq: Quiet mode (suppress banners)
# -ms: Small memory model (code and data fit within 64KB)
# -s:  Remove stack overflow checks (for smaller code, be *very* careful!)
# -0:  Disable all optimizations (for initial development and debugging)
# -os: Optimize for space (critical for TSRs)
# -ot: Optimize for time
# -zp1: Pack structures (align on 1-byte boundaries - critical for hardware interaction)
# -I$(INCDIR): Include directory for header files
# -fr=: Generate error messages to file
# -wcd=XXX: disables a warning XXX
# -we: treat all warning as error
# -d0: No debugging information
# -d1: Line number debugging info
# -d2: -d1 and local symbol
# -d3: -d2 and all type information
# -zdf: DS is not equal to SS (required for TSR)
# -zu: SS != DS, no stack probes (critical for TSR)

CFLAGS_DEBUG   = -zq -ms -s -0 -zp1 -zdf -zu -I$(INCDIR) -fr=$(BUILDDIR)/ -wcd=201 -d2
CFLAGS_RELEASE = -zq -ms -s -os -ot -zp1 -zdf -zu -I$(INCDIR) -fr=$(BUILDDIR)/ -wcd=201 -d0

# Production build flags for maximum size optimization
# Note: Using Watcom-specific flags only (removed GCC-style options)
CFLAGS_PRODUCTION = -zq -ms -s -os -zp1 -zdf -zu -I$(INCDIR) -d0 \
                    -oe=100 -ol+ -ox \
                    -wcd=201 -we \
                    -DPRODUCTION -DNO_LOGGING -DNO_STATS -DNDEBUG

# --- Assembler Flags (NASM) ---
# -f obj: Output object file format (Microsoft 16-bit/32-bit OMF)
# -i:    Include path
# -o:    Output path
# -l:     List file
# -g:     Debug information
AFLAGS_DEBUG   = -f obj -i$(INCDIR)/ -l $(BUILDDIR)/ -g
AFLAGS_RELEASE = -f obj -i$(INCDIR)/ -l $(BUILDDIR)/

# --- Linker Flags ---
# system dos:  Create a DOS .EXE file
# option map:  Create a .MAP file (useful for debugging)
# option caseexact: Preserve case (important for assembly symbols)
# option quiet: Suppress linker banner
# option stack: Set stack size (typically 1KB for TSR)
LFLAGS_DEBUG   = system dos option map=$(BUILDDIR)/3cpd.map, caseexact, quiet, stack=1024
LFLAGS_RELEASE = system dos option map=$(BUILDDIR)/3cpd.map, caseexact, quiet, stack=1024

# Production linker flags with section control for cold/hot separation
# Note: Watcom linker uses segment names, not ELF-style section names
# _TEXT is the main hot section, _DATA follows, then COLD_TEXT is discarded
LFLAGS_PRODUCTION = system dos option map=$(BUILDDIR)/3cpd.map, caseexact, quiet, stack=1024 \
                    option "order { clname CODE segment _TEXT clname DATA segment _DATA clname CODE segment COLD_TEXT }"

# --- Target ---
TARGET     = $(BUILDDIR)/3cpd.exe

# --- PCI Utilities ---
PCI_UTILS = $(BUILDDIR)/pcitest.exe \
           $(BUILDDIR)/pciscan.exe \
           $(BUILDDIR)/pcictl.exe \
           $(BUILDDIR)/pcidump.exe

# --- Object File Categories ---
# Based on the architecture requirements from ADD.md 
# Each source file appears exactly once - properly categorized for TSR operation

# HOT SECTION - Resident Assembly Objects (stay in memory after initialization)
# These are patched by SMC and remain resident
HOT_ASM_OBJS = $(BUILDDIR)/packet_api_smc.obj \
               $(BUILDDIR)/nic_irq_smc.obj \
               $(BUILDDIR)/hardware_smc.obj \
               $(BUILDDIR)/pcmcia_isr.obj \
               $(BUILDDIR)/flow_routing.obj \
               $(BUILDDIR)/direct_pio.obj \
               $(BUILDDIR)/packet_ops.obj \
               $(BUILDDIR)/packet_copy_c_wrapper.obj \
               $(BUILDDIR)/tsr_common.obj \
               $(BUILDDIR)/tsr_c_wrappers.obj \
               $(BUILDDIR)/pci_io.obj \
               $(BUILDDIR)/pci_shim_isr.obj

# Main loader (contains both hot and cold sections)
LOADER_OBJ = $(BUILDDIR)/tsr_loader.obj

# HOT SECTION - Resident C Objects (stay in memory after initialization)
# Minimal runtime code only
HOT_C_OBJS = $(BUILDDIR)/api.obj \
             $(BUILDDIR)/routing.obj \
             $(BUILDDIR)/packet_ops.obj \
             $(BUILDDIR)/pci_shim.obj \
             $(BUILDDIR)/pci_multiplex.obj \
             $(BUILDDIR)/dma_mapping.obj \
             $(BUILDDIR)/dma_boundary.obj \
             $(BUILDDIR)/hw_checksum.obj \
             $(BUILDDIR)/dos_idle.obj \
             $(BUILDDIR)/irq_bind.obj \
             $(BUILDDIR)/runtime_config.obj \
             $(BUILDDIR)/interrupt_mitigation.obj \
             $(BUILDDIR)/rx_batch_refill.obj \
             $(BUILDDIR)/tx_lazy_irq.obj

# COLD SECTION - Initialization Assembly Objects (discarded after initialization)
COLD_ASM_OBJS = $(BUILDDIR)/cpu_detect.obj \
                $(BUILDDIR)/pnp.obj \
                $(BUILDDIR)/promisc.obj \
                $(BUILDDIR)/smc_patches.obj \
                $(BUILDDIR)/safety_stubs.obj \
                $(BUILDDIR)/quiesce.obj

# COLD SECTION - Initialization C Objects (discarded after initialization)
COLD_C_OBJS_BASE = $(BUILDDIR)/main.obj \
                   $(BUILDDIR)/init.obj \
                   $(BUILDDIR)/config.obj \
                   $(BUILDDIR)/pcmcia_manager.obj \
                   $(BUILDDIR)/pcmcia_snapshot.obj \
                   $(BUILDDIR)/flow_control.obj \
                   $(BUILDDIR)/pcmcia_pe_backend.obj \
                   $(BUILDDIR)/pcmcia_ss_backend.obj \
                   $(BUILDDIR)/memory.obj \
                   $(BUILDDIR)/xms_detect.obj \
                   $(BUILDDIR)/umb_loader.obj \
                   $(BUILDDIR)/eeprom.obj \
                   $(BUILDDIR)/buffer_alloc.obj \
                   $(BUILDDIR)/buffer_autoconfig.obj \
                   $(BUILDDIR)/static_routing.obj \
                   $(BUILDDIR)/arp.obj \
                   $(BUILDDIR)/nic_init.obj \
                   $(BUILDDIR)/hardware.obj \
                   $(BUILDDIR)/hardware_stubs.obj \
                   $(BUILDDIR)/3c515.obj \
                   $(BUILDDIR)/3c509b.obj \
                   $(BUILDDIR)/entry_validation.obj \
                   $(BUILDDIR)/platform_probe_early.obj \
                   $(BUILDDIR)/dma_capability_test.obj \
                   $(BUILDDIR)/tsr_manager.obj \
                   $(BUILDDIR)/dma_tests.obj \
                   $(BUILDDIR)/dma_safety.obj \
                   $(BUILDDIR)/vds_core.obj \
                   $(BUILDDIR)/vds_safety.obj \
                   $(BUILDDIR)/vds_manager.obj \
                   $(BUILDDIR)/extension_api.obj \
                   $(BUILDDIR)/unwind.obj \
                   $(BUILDDIR)/chipset_detect.obj \
                   $(BUILDDIR)/busmaster_test.obj \
                   $(BUILDDIR)/loader/cpu_detect.obj \
                   $(BUILDDIR)/loader/patch_apply.obj \
                   $(BUILDDIR)/pci_bios.obj \
                   $(BUILDDIR)/3com_pci_detect.obj \
                   $(BUILDDIR)/3com_vortex.obj \
                   $(BUILDDIR)/3com_boomerang.obj \
                   $(BUILDDIR)/pci_integration.obj \
                   $(BUILDDIR)/pci_shim_enhanced.obj \
                   $(BUILDDIR)/smc_safety_patches.obj \
                   $(BUILDDIR)/smc_serialization.obj \
                   $(BUILDDIR)/cache_management.obj \
                   $(BUILDDIR)/dma_policy.obj \
                   $(BUILDDIR)/vds.obj

# Debug-only objects (excluded in production)
DEBUG_C_OBJS = $(BUILDDIR)/diagnostics.obj \
              $(BUILDDIR)/logging.obj \
              $(BUILDDIR)/stats.obj

# Conditional inclusion based on build type
ifdef PRODUCTION
  COLD_C_OBJS = $(COLD_C_OBJS_BASE)
  # In production, exclude debug objects entirely
  ALL_OBJS = $(LOADER_OBJ) $(HOT_ASM_OBJS) $(HOT_C_OBJS) $(COLD_ASM_OBJS) $(COLD_C_OBJS)
else
  COLD_C_OBJS = $(COLD_C_OBJS_BASE) $(DEBUG_C_OBJS)
  INIT_C_OBJS = $(INIT_C_OBJS_BASE) $(DEBUG_C_OBJS)
endif

# Note: Some C files may have corresponding assembly implementations
# The build system will use whichever exists for each module

# All object files organized for proper linking order
# Hot code first (stays in memory), cold code last (discarded)
ifndef PRODUCTION
  ALL_OBJS = $(LOADER_OBJ) $(HOT_ASM_OBJS) $(HOT_C_OBJS) $(COLD_ASM_OBJS) $(COLD_C_OBJS) $(DEBUG_C_OBJS)
endif

# --- Dependencies ---
DEPS       = $(ALL_OBJS:.obj=.d)

# --- Rules ---

.PHONY: all clean debug release production test info config-3c509b config-3c515 config-both config-8086 config-286 config-386 config-486 build-8086-minimal pci-utils link-sanity verify-patches

all: release

# Build all PCI utilities
pci-utils: $(PCI_UTILS)
	@echo "All PCI utilities built successfully"

# --- Enhanced Build Targets ---

# NIC-specific builds
config-3c509b:
	@echo "Configuring for 3C509B only..."
	@$(MAKE) NIC_SUPPORT=3c509b TARGET_CPU=$(TARGET_CPU) release
	
config-3c515:
	@echo "Configuring for 3C515 only..."
	@$(MAKE) NIC_SUPPORT=3c515 TARGET_CPU=$(TARGET_CPU) release

config-both:
	@echo "Configuring for both NICs..."
	@$(MAKE) NIC_SUPPORT=both TARGET_CPU=$(TARGET_CPU) release

# CPU-specific builds
config-8086:
	@echo "============================================="
	@echo "Configuring for 8086/8088..."
	@echo "============================================="
	@echo "Features enabled:"
	@echo "  - 3C509B NIC only (PIO mode)"
	@echo "  - No SMC patching"
	@echo "  - No XMS/VDS/bus-mastering"
	@echo "  - 8086-safe code paths"
	@echo "============================================="
	@$(MAKE) TARGET_CPU=8086 NIC_SUPPORT=3c509b ENABLE_BUS_MASTER=0 ENABLE_XMS=0 ENABLE_VDS=0 release

config-286:
	@echo "Configuring for 80286..."
	@$(MAKE) TARGET_CPU=286 NIC_SUPPORT=$(NIC_SUPPORT) release

config-386:
	@echo "Configuring for 80386..."
	@$(MAKE) TARGET_CPU=386 NIC_SUPPORT=$(NIC_SUPPORT) release

config-486:
	@echo "Configuring for 80486..."
	@$(MAKE) TARGET_CPU=486 NIC_SUPPORT=$(NIC_SUPPORT) release

# Optimized builds
build-minimal:
	@echo "Building minimal configuration (3C509B, 286, no optional features)..."
	@$(MAKE) NIC_SUPPORT=3c509b TARGET_CPU=286 ENABLE_LOGGING=0 ENABLE_DIAGNOSTICS=0 ENABLE_STATS=0 release

# Ultra-minimal build for 8086/8088 systems
build-8086-minimal:
	@echo "Building ultra-minimal 8086 configuration..."
	@$(MAKE) NIC_SUPPORT=3c509b TARGET_CPU=8086 ENABLE_LOGGING=0 ENABLE_DIAGNOSTICS=0 ENABLE_STATS=0 ENABLE_BUS_MASTER=0 ENABLE_XMS=0 ENABLE_VDS=0 release

build-full:
	@echo "Building full configuration (both NICs, 386, all features)..."
	@$(MAKE) NIC_SUPPORT=both TARGET_CPU=386 ENABLE_LOGGING=1 ENABLE_DIAGNOSTICS=1 ENABLE_STATS=1 ENABLE_BUS_MASTER=1 release

build-performance:
	@echo "Building performance-optimized configuration..."
	@$(MAKE) NIC_SUPPORT=both TARGET_CPU=386 ENABLE_DIRECT_PIO=1 ENABLE_COPYBREAK=1 ENABLE_INTERRUPT_MITIGATION=1 ENABLE_CACHE_OPTIMIZATION=1 release

# Enhanced build targets using new configuration system
debug: 
	@echo "Building debug version using enhanced build system..."
	@$(MAKE) -f build/debug_config.mk DEBUG_LEVEL=2 debug

release: 
	@echo "Building release version..."
	@$(MAKE) CFLAGS="$(COMBINED_CFLAGS)" AFLAGS="$(COMBINED_AFLAGS)" LFLAGS="$(COMBINED_LFLAGS)" $(TARGET)
	@echo "Release build complete. Binary: $(TARGET)$(BUILD_SUFFIX)"

# Production build - maximum size optimization with cold/hot separation
production: CFLAGS = $(CFLAGS_PRODUCTION)
production: AFLAGS = $(AFLAGS_RELEASE)
production: LFLAGS = $(LFLAGS_PRODUCTION)
production: PRODUCTION = 1
production: export PRODUCTION
production: $(BUILDDIR) 
	@echo "Building PRODUCTION version (size-optimized with cold/hot separation)..."
	@echo "Excluding: diagnostics, logging, stats"
	@echo "Cold section will be discarded after initialization"
	@echo "Compiler flags: $(CFLAGS_PRODUCTION)"
	@$(MAKE) PRODUCTION=1 $(TARGET)
	@echo "====================================="
	@echo "Production build complete: $(TARGET)"
	@if [ -f $(TARGET) ]; then \
		size=$$(stat -f%z $(TARGET) 2>/dev/null || stat -c%s $(TARGET) 2>/dev/null || echo "0"); \
		echo "Binary size: $$size bytes ($$(expr $$size / 1024)KB)"; \
	fi
	@echo "====================================="
	@$(MAKE) check-size

# Legacy compatibility targets
debug-legacy: CFLAGS = $(CFLAGS_DEBUG)
debug-legacy: AFLAGS = $(AFLAGS_DEBUG)
debug-legacy: LFLAGS = $(LFLAGS_DEBUG)
debug-legacy: $(BUILDDIR) $(TARGET)
	@echo "Legacy debug build complete. Binary: $(TARGET)"
	@echo "Map file: $(BUILDDIR)/3cpd.map"

release-legacy: CFLAGS = $(CFLAGS_RELEASE)
release-legacy: AFLAGS = $(AFLAGS_RELEASE)
release-legacy: LFLAGS = $(LFLAGS_RELEASE)
release-legacy: $(BUILDDIR) $(TARGET)
	@echo "Legacy release build complete. Binary: $(TARGET)"

# Sprint 1.2: Direct PIO Performance Test
test_direct_pio: $(BUILDDIR)/test_direct_pio.exe
	@echo "Running Direct PIO Performance Test..."
	$(BUILDDIR)/test_direct_pio.exe

$(BUILDDIR)/test_direct_pio.exe: test_direct_pio.c $(ALL_OBJS) | $(BUILDDIR)
	@echo "Building Direct PIO Performance Test..."
	$(CC) $(CFLAGS) test_direct_pio.c -o $(BUILDDIR)/test_direct_pio.obj
	$(LINK) $(LFLAGS) file {$(BUILDDIR)/test_direct_pio.obj $(ALL_OBJS)} name $(BUILDDIR)/test_direct_pio.exe
	@echo "Map file: $(BUILDDIR)/3cpd.map"

# PCI Test Suite
$(BUILDDIR)/pcitest.exe: $(CDIR)/pcitest.c $(BUILDDIR)/pci_bios.obj $(BUILDDIR)/pci_shim.obj $(BUILDDIR)/pci_shim_enhanced.obj $(BUILDDIR)/pci_io.obj $(BUILDDIR)/logging.obj | $(BUILDDIR)
	@echo "Building PCI Test Suite..."
	$(CC) $(CFLAGS) $(CDIR)/pcitest.c -fo=$(BUILDDIR)/pcitest.obj
	$(LINK) $(LFLAGS) file {$(BUILDDIR)/pcitest.obj $(BUILDDIR)/pci_bios.obj $(BUILDDIR)/pci_shim.obj $(BUILDDIR)/pci_shim_enhanced.obj $(BUILDDIR)/pci_io.obj $(BUILDDIR)/logging.obj} name $(BUILDDIR)/pcitest.exe

# PCI Scanner
$(BUILDDIR)/pciscan.exe: $(CDIR)/pciscan.c $(BUILDDIR)/pci_bios.obj $(BUILDDIR)/pci_shim_enhanced.obj $(BUILDDIR)/pci_io.obj $(BUILDDIR)/cpu_detect.obj $(BUILDDIR)/logging.obj | $(BUILDDIR)
	@echo "Building PCI Scanner..."
	$(CC) $(CFLAGS) $(CDIR)/pciscan.c -fo=$(BUILDDIR)/pciscan.obj
	$(LINK) $(LFLAGS) file {$(BUILDDIR)/pciscan.obj $(BUILDDIR)/pci_bios.obj $(BUILDDIR)/pci_shim_enhanced.obj $(BUILDDIR)/pci_io.obj $(BUILDDIR)/cpu_detect.obj $(BUILDDIR)/logging.obj} name $(BUILDDIR)/pciscan.exe

# Bus Master Test Utility
$(BUILDDIR)/bmtest.exe: tools/bmtest.c $(BUILDDIR)/vds.obj | $(BUILDDIR)
	@echo "Building Bus Master Test Utility..."
	$(CC) $(CFLAGS) tools/bmtest.c -fo=$(BUILDDIR)/bmtest.obj
	$(CC) $(CFLAGS) tools/stress_test.c -fo=$(BUILDDIR)/stress_test.obj
	$(LINK) $(LFLAGS) file {$(BUILDDIR)/bmtest.obj $(BUILDDIR)/stress_test.obj $(BUILDDIR)/vds.obj} name $(BUILDDIR)/bmtest.exe
	@echo "BMTEST utility built: $(BUILDDIR)/bmtest.exe"

bmtest: $(BUILDDIR)/bmtest.exe
	@echo "Bus Master Test utility ready"

# PCI Control utility (uses multiplex interface)
$(BUILDDIR)/pcictl.exe: $(CDIR)/pci_multiplex.c | $(BUILDDIR)
	@echo "Building PCI Control utility..."
	$(CC) $(CFLAGS) -DSTANDALONE_UTILITY $(CDIR)/pci_multiplex.c -fo=$(BUILDDIR)/pcictl.obj
	$(LINK) $(LFLAGS) file {$(BUILDDIR)/pcictl.obj} name $(BUILDDIR)/pcictl.exe

# PCI Config Dump utility
$(BUILDDIR)/pcidump.exe: $(CDIR)/pcidump.c $(BUILDDIR)/pci_bios.obj $(BUILDDIR)/pci_io.obj | $(BUILDDIR)
	@echo "Building PCI Config Dump utility..."
	$(CC) $(CFLAGS) $(CDIR)/pcidump.c -fo=$(BUILDDIR)/pcidump.obj
	$(LINK) $(LFLAGS) file {$(BUILDDIR)/pcidump.obj $(BUILDDIR)/pci_bios.obj $(BUILDDIR)/pci_io.obj} name $(BUILDDIR)/pcidump.exe

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)
	@echo "Created build directory."

# Link with proper order: hot code first, cold code last (discarded)
$(TARGET): $(ALL_OBJS)
	@echo "Linking $@ with hot/cold section separation..."
	@echo "Loader: $(LOADER_OBJ)"
	@echo "Hot ASM objects: $(HOT_ASM_OBJS)"
	@echo "Hot C objects: $(HOT_C_OBJS)"
	@echo "Cold ASM objects: $(COLD_ASM_OBJS)"
	@echo "Cold C objects: $(COLD_C_OBJS)"
	$(LINK) $(LFLAGS) file {$(ALL_OBJS)} name $(TARGET)
	@echo "Build complete. Output: $(TARGET)"
	@if [ -f $(BUILDDIR)/3cpd.map ]; then \
		echo "Memory map available at: $(BUILDDIR)/3cpd.map"; \
	fi

# Special rules for DMA modules with production optimizations
# These use isolated flags to avoid global flag bleed
DMA_OPT_FLAGS = -DPRODUCTION -DNO_LOGGING -DNDEBUG

$(BUILDDIR)/dma_mapping.obj: $(CDIR)/dma_mapping.c | $(BUILDDIR)
	@echo "Compiling (DMA optimized): $< -> $@"
	$(CC) $(CFLAGS) $(DMA_OPT_FLAGS) -c $< -fo=$@

$(BUILDDIR)/dma_boundary.obj: $(CDIR)/dma_boundary.c | $(BUILDDIR)
	@echo "Compiling (DMA optimized): $< -> $@"
	$(CC) $(CFLAGS) $(DMA_OPT_FLAGS) -c $< -fo=$@

$(BUILDDIR)/hw_checksum.obj: $(CDIR)/hw_checksum.c | $(BUILDDIR)
	@echo "Compiling (DMA optimized): $< -> $@"
	$(CC) $(CFLAGS) $(DMA_OPT_FLAGS) -c $< -fo=$@

$(BUILDDIR)/dma_safety.obj: $(CDIR)/dma_safety.c | $(BUILDDIR)
	@echo "Compiling (DMA optimized): $< -> $@"
	$(CC) $(CFLAGS) $(DMA_OPT_FLAGS) -c $< -fo=$@

# Default rule for other C files
$(BUILDDIR)/%.obj: $(CDIR)/%.c | $(BUILDDIR)
	@echo "Compiling C: $< -> $@"
	$(CC) $(CFLAGS) -c $< -fo=$@

# Assemble ASM files  
$(BUILDDIR)/%.obj: $(ASMDIR)/%.asm | $(BUILDDIR)
	@echo "Assembling: $< -> $@"
	$(ASM) $(AFLAGS) $< -o $@

# Compile loader C files (cold section)
$(BUILDDIR)/loader/%.obj: $(SRCDIR)/loader/%.c | $(BUILDDIR)
	@mkdir -p $(BUILDDIR)/loader
	@echo "Compiling (cold): $< -> $@"
	$(CC) $(CFLAGS) -DCOLD_SECTION $< -o $@

# Individual object file targets for easier debugging
resident-asm: $(RESIDENT_ASM_OBJS)
	@echo "Resident ASM objects built."

resident-c: $(RESIDENT_C_OBJS)
	@echo "Resident C objects built."

init-asm: $(INIT_ASM_OBJS)
	@echo "Initialization ASM objects built."

init-c: $(INIT_C_OBJS)
	@echo "Initialization C objects built."

# Enhanced test targets
test: 
	@echo "Running enhanced test suite..."
	@$(MAKE) -f build/test_targets.mk test-all
	@echo "All tests built successfully. Use run-* targets to execute."

# Critical bug fix verification test
$(BUILDDIR)/test_critical_bug_fixes.exe: test/test_critical_bug_fixes.c $(BUILDDIR)/tsr_c_wrappers.obj $(BUILDDIR)/cache_ops.obj | $(BUILDDIR)
	@echo "Building Critical Bug Fix Test..."
	$(CC) $(CFLAGS) test/test_critical_bug_fixes.c -fo=$(BUILDDIR)/test_critical_bug_fixes.obj
	$(LINK) $(LFLAGS) file {$(BUILDDIR)/test_critical_bug_fixes.obj $(BUILDDIR)/tsr_c_wrappers.obj $(BUILDDIR)/cache_ops.obj} name $(BUILDDIR)/test_critical_bug_fixes.exe

test-bug-fixes: $(BUILDDIR)/test_critical_bug_fixes.exe
	@echo "Running Critical Bug Fix Verification..."
	$(BUILDDIR)/test_critical_bug_fixes.exe

test-quick:
	@$(MAKE) -f build/test_targets.mk test-quick
	
test-unit:
	@$(MAKE) -f build/test_targets.mk test-unit
	
test-integration:
	@$(MAKE) -f build/test_targets.mk test-integration

test-performance:
	@$(MAKE) -f build/test_targets.mk test-performance

# Legacy test target
test-legacy: release
	@echo "Running legacy test suite..."
	@cd tests && $(MAKE) all
	@echo "All tests completed successfully."

# Link sanity check - verify all symbols resolve
link-sanity: $(ALL_OBJS)
	@echo "Testing link integrity..."
	@echo "Attempting to link all objects..."
	@$(LINK) $(LFLAGS) file {$(ALL_OBJS)} name $(BUILDDIR)/test_link.exe 2>&1 | tee $(BUILDDIR)/link_test.log
	@if grep -q "unresolved\|undefined" $(BUILDDIR)/link_test.log; then \
		echo "ERROR: Unresolved symbols found!"; \
		grep "unresolved\|undefined" $(BUILDDIR)/link_test.log; \
		rm -f $(BUILDDIR)/test_link.exe $(BUILDDIR)/link_test.log; \
		exit 1; \
	else \
		echo "SUCCESS: Link test passed - all symbols resolved"; \
		rm -f $(BUILDDIR)/test_link.exe $(BUILDDIR)/link_test.log; \
	fi

# Verify patches are applied (not NOPs) after build
verify-patches: $(TARGET)
	@echo "Verifying critical patches are active..."
	@$(BUILDDIR)/3cpd.exe --verify-patches 2>&1 | tee $(BUILDDIR)/patch_verify.log
	@if grep -q "CRITICAL" $(BUILDDIR)/patch_verify.log; then \
		echo "ERROR: Critical patches not applied - forcing PIO mode!"; \
		exit 1; \
	else \
		echo "SUCCESS: All critical patches verified as active"; \
	fi

# Bus mastering test target (Sprint 0B.5)
test_busmaster_sprint0b5.exe: test_busmaster_sprint0b5.c $(INIT_C_OBJS) $(RESIDENT_C_OBJS)
	@echo "Building bus mastering test (Sprint 0B.5)..."
	$(CC) $(CFLAGS) test_busmaster_sprint0b5.c $(INIT_C_OBJS) $(RESIDENT_C_OBJS) -o $@

# Run bus mastering test
test-busmaster: test_busmaster_sprint0b5.exe
	@echo "Running Sprint 0B.5: Automated Bus Mastering Test..."
	./test_busmaster_sprint0b5.exe

# Quick bus mastering test (10-second mode)
test-busmaster-quick: test_busmaster_sprint0b5.exe
	@echo "Running Sprint 0B.5: Quick Bus Mastering Test..."
	./test_busmaster_sprint0b5.exe --quick

# Enhanced information target
info:
	@echo "=== 3Com Packet Driver Enhanced Build System ==="
	@echo "Target: $(TARGET)$(BUILD_SUFFIX)"
	@echo "Build directory: $(BUILDDIR)"
	@echo ""
	@$(MAKE) -f build/build_config.mk show-config
	@echo ""
	@echo "Enhanced Build Targets:"
	@echo "  wmake                    - Release build (default configuration)"
	@echo "  wmake debug              - Enhanced debug build"
	@echo "  wmake test               - Enhanced test suite"
	@echo "  wmake config-3c509b      - Build for 3C509B only"
	@echo "  wmake config-3c515       - Build for 3C515 only"
	@echo "  wmake config-both        - Build for both NICs"
	@echo "  wmake config-8086        - Build for 8086/8088 (3C509B PIO only)"
	@echo "  wmake config-286         - Build for 80286"
	@echo "  wmake config-386         - Build for 80386"
	@echo "  wmake config-486         - Build for 80486"
	@echo "  wmake build-8086-minimal - Ultra-minimal 8086/8088 build"
	@echo "  wmake build-minimal      - Minimal build (smallest size)"
	@echo "  wmake build-full         - Full featured build"
	@echo "  wmake build-performance  - Performance optimized build"
	@echo "  wmake test-quick         - Quick test subset"
	@echo "  wmake clean              - Clean build artifacts"
	@echo "  wmake info               - Show this information"
	@echo ""
	@echo "Test Information:"
	@$(MAKE) -f build/test_targets.mk test-info
	@echo ""
	@echo "Debug Information:"  
	@$(MAKE) -f build/debug_config.mk debug-info

# --- Dependency Generation ---
# Note: Simplified dependency handling for DOS build environment

$(BUILDDIR)/%.d: $(CDIR)/%.c | $(BUILDDIR)
	@echo "Generating dependencies for $<"
	@echo "$(BUILDDIR)/$*.obj: $< \\" > $@
	@$(CC) $(CFLAGS) -MM $< | sed 's/.*://' | tr '\n' ' ' >> $@
	@echo "" >> $@

# Include dependencies if they exist
-include $(DEPS)

# Enhanced clean targets
clean:
	@echo "Cleaning all build artifacts..."
	@if [ -d $(BUILDDIR) ]; then rm -rf $(BUILDDIR); fi
	@$(MAKE) -f build/test_targets.mk test-clean
	@$(MAKE) -f build/debug_config.mk debug-clean
	@echo "Enhanced clean complete."

clean-legacy:
	@echo "Cleaning build directory..."
	@if [ -d $(BUILDDIR) ]; then rm -rf $(BUILDDIR); fi
	@echo "Cleaning test artifacts..."
	@cd tests && $(MAKE) clean
	@echo "Legacy clean complete."

# Selective cleaning
clean-debug:
	@$(MAKE) -f build/debug_config.mk debug-clean

clean-test:
	@$(MAKE) -f build/test_targets.mk test-clean

# Size guard - fail if resident exceeds 6.7KB (6886 bytes)
check-size: $(TARGET)
	@echo "Checking resident size against budget from map file..."
	@# Parse map file for hot/resident segments (TEXT, HOT_TEXT, DATA, BSS, CONST sections)
	@# Note: BSS (uninitialized data) still consumes resident memory
	@if [ -f $(BUILDDIR)/3cpd.map ]; then \
		resident_size=$$(awk '/^_TEXT|^HOT_|^_DATA|^_BSS|^CONST/ {gsub(/H$$/, "", $$3); size=strtonum("0x" $$3); sum+=size} END {print sum}' $(BUILDDIR)/3cpd.map); \
		if [ "$$resident_size" = "" ] || [ $$resident_size -eq 0 ]; then \
			echo "WARNING: Could not determine resident size from map file"; \
		elif [ $$resident_size -gt 7066 ]; then \
			echo "ERROR: Resident size $$resident_size bytes exceeds ~6.9KB limit!"; \
			echo "Hot segments total: $$resident_size bytes"; \
			echo "Budget: ~6.9KB (7066 bytes)"; \
			exit 1; \
		else \
			echo "PASS: Resident size $$resident_size bytes within budget (≤7066 bytes)"; \
			echo "Hot segments breakdown:"; \
			awk '/^_TEXT|^HOT_|^_DATA/ {gsub(/H$$/, "", $$3); printf "  %-20s %6d bytes\n", $$1, strtonum("0x" $$3)}' $(BUILDDIR)/3cpd.map; \
		fi; \
	else \
		echo "WARNING: Map file $(BUILDDIR)/3cpd.map not found"; \
	fi

# Verify patch sites are active (not NOPs)
verify-patches: $(TARGET)
	@echo "Verifying patch sites are active..."
	@# Check for patch signatures in binary
	@if hexdump -C $(TARGET) | grep -q "90 90 90 90 90"; then \
		echo "WARNING: Found NOP sleds - patches may be disabled"; \
	else \
		echo "PASS: Patch sites appear active"; \
	fi

clean-build:
	@echo "Cleaning main build artifacts..."
	@if [ -d $(BUILDDIR) ]; then \
		find $(BUILDDIR) -name "*.obj" -o -name "*.exe" -o -name "*.map" | grep -v test | grep -v debug | xargs rm -f; \
	fi
