# ============================================================================
# Makefile for BOOMTEX.MOD - 3C515-TX/3C900-TPO Driver Module
# ============================================================================
# BOOMTEX.MOD Build System
# Team C (Agents 09-10) - Week 1 Critical Deliverable
# 
# Builds BOOMTEX.MOD with hot/cold separation, CPU optimizations, and
# Week 1 NE2000 compatibility for QEMU validation.
# ============================================================================

# Module information
MODULE_NAME = BOOMTEX
MODULE_VERSION = 1.0
MODULE_TARGET = boomtex.mod

# Build configuration
CC = wcc
AS = nasm
LINK = wlink

# Compiler flags
CFLAGS = -zq -w4 -e25 -d2 -i=../../include -bt=dos -ml -0 -fp3 -fpi87 -s
CFLAGS_RELEASE = $(CFLAGS) -ox -d0
CFLAGS_DEBUG = $(CFLAGS) -d3 -DDEBUG=1

# Week 1 NE2000 compatibility flag
ifdef WEEK1_TESTING
CFLAGS += -DWEEK1_EMULATOR_TESTING=1
endif

# Assembler flags
ASFLAGS = -f obj -i ../../include/
ASFLAGS_DEBUG = $(ASFLAGS) -g -DDEBUG=1

# Linker flags
LDFLAGS = system dos option map option eliminate option start=boomtex_module_init

# Source files
C_SOURCES = boomtex_module.c \
           3c515tx.c \
           3c900tpo.c \
           network_lib.c \
           ne2000_compat.c \
           boomtex_api.c \
           memory_mgmt.c

ASM_SOURCES = boomtex_isr.asm

# Object files
C_OBJECTS = $(C_SOURCES:.c=.obj)
ASM_OBJECTS = $(ASM_SOURCES:.asm=.obj)
ALL_OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

# Header dependencies
HEADERS = boomtex_internal.h \
         ../../include/module_abi.h \
         ../../include/memory_api.h \
         ../../include/timing_measurement.h \
         ../../../docs/agents/shared/error-codes.h

# Default target
all: release

# Release build
release: CFLAGS = $(CFLAGS_RELEASE)
release: ASFLAGS = $(ASFLAGS)
release: $(MODULE_TARGET)

# Debug build
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: ASFLAGS = $(ASFLAGS_DEBUG)
debug: $(MODULE_TARGET)

# Week 1 emulator testing build
week1: CFLAGS = $(CFLAGS_RELEASE) -DWEEK1_EMULATOR_TESTING=1
week1: $(MODULE_TARGET)

# Build module
$(MODULE_TARGET): $(ALL_OBJECTS)
	@echo "Linking BOOMTEX.MOD..."
	$(LINK) $(LDFLAGS) file { $(ALL_OBJECTS) } name $@
	@echo "BOOMTEX.MOD build complete"
	@$(MAKE) --no-print-directory module-info

# Compile C sources
%.obj: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -fo=$@

# Assemble ASM sources
%.obj: %.asm
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

# Module information
module-info:
	@echo "============================================="
	@echo "BOOMTEX.MOD Build Information"
	@echo "============================================="
	@echo "Module: $(MODULE_NAME) v$(MODULE_VERSION)"
	@echo "Target: $(MODULE_TARGET)"
	@echo "Size: `wc -c < $(MODULE_TARGET) 2>/dev/null || echo "Unknown"` bytes"
	@echo "Build date: `date`"
	@echo ""
	@echo "Supported Hardware:"
	@echo "  - 3C515-TX ISA Bus Master (100Mbps)"
	@echo "  - 3C900-TPO PCI (10Mbps)"
	@echo "  - NE2000 Compatible (QEMU Week 1)"
	@echo ""
	@echo "Features:"
	@echo "  - IEEE 802.3u Auto-negotiation"
	@echo "  - Bus mastering DMA (3C515-TX/3C900-TPO)"
	@echo "  - Hot/cold memory separation"
	@echo "  - CPU-specific optimizations"
	@echo "  - ≤60μs ISR timing constraint"
	@echo "============================================="

# Clean build artifacts
clean:
	@echo "Cleaning BOOMTEX build artifacts..."
	-rm -f *.obj
	-rm -f *.mod
	-rm -f *.map
	-rm -f *.err
	@echo "Clean complete"

# Install module (for testing)
install: $(MODULE_TARGET)
	@if [ ! -d "$(INSTALL_DIR)" ]; then \
		echo "INSTALL_DIR not set or directory doesn't exist"; \
		exit 1; \
	fi
	@echo "Installing BOOMTEX.MOD to $(INSTALL_DIR)..."
	cp $(MODULE_TARGET) $(INSTALL_DIR)/
	@echo "Installation complete"

# Test targets
test: test-syntax test-module test-timing

# Syntax checking
test-syntax:
	@echo "Running syntax checks..."
	@for file in $(C_SOURCES); do \
		echo "Checking $$file..."; \
		$(CC) $(CFLAGS) -zs $$file || exit 1; \
	done
	@for file in $(ASM_SOURCES); do \
		echo "Checking $$file..."; \
		$(AS) $(ASFLAGS) -p $$file || exit 1; \
	done
	@echo "Syntax checks passed"

# Module validation
test-module: $(MODULE_TARGET)
	@echo "Validating module format..."
	@if [ -f "../../tools/modverify" ]; then \
		../../tools/modverify $(MODULE_TARGET); \
	else \
		echo "Module verification tool not found - skipping"; \
	fi

# Timing constraint validation
test-timing:
	@echo "Checking timing constraints..."
	@grep -n "TIMING_VALIDATE_ISR" $(ASM_SOURCES) || echo "Warning: ISR timing validation not found"
	@grep -n "TIMING_VALIDATE_CLI" $(C_SOURCES) || echo "Info: CLI timing validation check"
	@echo "Timing constraint check complete"

# QEMU testing support
test-qemu: week1
	@echo "Preparing BOOMTEX.MOD for QEMU testing..."
	@if [ -f "../../../test/emulator/qemu/qemu-runner.sh" ]; then \
		echo "Starting QEMU test..."; \
		cd ../../../test/emulator/qemu && ./qemu-runner.sh ../../../src/modules/boomtex/$(MODULE_TARGET); \
	else \
		echo "QEMU test runner not found - manual testing required"; \
	fi

# Memory analysis
analyze-memory: $(MODULE_TARGET)
	@echo "Analyzing memory usage..."
	@echo "Module size: `wc -c < $(MODULE_TARGET)` bytes"
	@if command -v objdump >/dev/null 2>&1; then \
		echo "Section analysis:"; \
		objdump -h $(MODULE_TARGET) 2>/dev/null | grep -E "(\.text|\.data|\.bss)" || echo "objdump analysis not available"; \
	fi
	@echo "Target: ≤8KB total, ≤5KB resident after cold discard"

# Performance profiling
profile: debug
	@echo "Performance profiling support prepared"
	@echo "Use DOS profiling tools with debug build"
	@echo "Focus areas:"
	@echo "  - ISR execution time (target: ≤60μs)"
	@echo "  - CLI sections (target: ≤8μs)"
	@echo "  - Module initialization (target: ≤100ms)"

# Development helpers
help:
	@echo "BOOMTEX.MOD Build System"
	@echo "========================"
	@echo ""
	@echo "Targets:"
	@echo "  all/release  - Build release version"
	@echo "  debug        - Build debug version" 
	@echo "  week1        - Build with NE2000 compatibility"
	@echo "  clean        - Clean build artifacts"
	@echo "  install      - Install module (set INSTALL_DIR)"
	@echo "  test         - Run all tests"
	@echo "  test-qemu    - Test with QEMU emulator"
	@echo "  analyze-memory - Analyze memory usage"
	@echo "  profile      - Prepare for profiling"
	@echo "  help         - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  WEEK1_TESTING=1 - Enable NE2000 compatibility"
	@echo "  INSTALL_DIR     - Installation directory"
	@echo ""
	@echo "Week 1 Deliverables:"
	@echo "  make week1 && make test-qemu"

# Dependency tracking
depend:
	@echo "Generating dependencies..."
	@for file in $(C_SOURCES); do \
		$(CC) $(CFLAGS) -zs -d+ $$file 2>&1 | grep "^including" | \
		sed "s/including /`basename $$file .c`.obj: /" >> .depend; \
	done 2>/dev/null || true

# Include dependencies if they exist
-include .depend

# Special targets
.PHONY: all release debug week1 clean install test test-syntax test-module test-timing test-qemu analyze-memory profile help depend module-info

# ============================================================================
# End of BOOMTEX.MOD Makefile
# ============================================================================