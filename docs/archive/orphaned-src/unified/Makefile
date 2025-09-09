# Makefile for Unified 3Com EtherLink III Driver
# Builds as a separate module that can be integrated with main driver

# --- Directories ---
UNIFIED_DIR = .
CORE_DIR    = ../core
BUS_DIR     = ../bus
DATA_DIR    = ../datapath
HAL_DIR     = ../hal
INC_DIR     = ../../include
BUILD_DIR   = ../../build/unified

# --- Tools ---
CC          = wcc
ASM         = nasm
AR          = wlib
LINK        = wlink

# --- Compiler Flags ---
# Base flags for DOS TSR development
BASE_FLAGS  = -zq -ms -s -zp1 -zdf -zu -I$(INC_DIR) -I$(UNIFIED_DIR)

# Optimization flags by build type
DEBUG_FLAGS   = $(BASE_FLAGS) -0 -d2 -DDEBUG
RELEASE_FLAGS = $(BASE_FLAGS) -os -ot -d0
PERF_FLAGS    = $(BASE_FLAGS) -ox -oe=100 -ol+ -d0 -DPERFORMANCE

# Datapath-specific flags (no HAL, maximum optimization)
DATAPATH_FLAGS = $(PERF_FLAGS) -DEL3_DATAPATH_COMPILATION

# --- Object Files ---

# Core driver objects
CORE_OBJS = $(BUILD_DIR)/el3_core.obj \
            $(BUILD_DIR)/el3_caps.obj

# Bus prober objects  
BUS_OBJS = $(BUILD_DIR)/el3_isa.obj \
           $(BUILD_DIR)/el3_pci.obj

# Datapath objects (HOT PATH - optimized)
DATA_OBJS = $(BUILD_DIR)/el3_pio.obj \
            $(BUILD_DIR)/el3_dma.obj

# HAL object (initialization only)
HAL_OBJS = $(BUILD_DIR)/el3_hal.obj

# Optional enhancement objects
OPT_OBJS = $(BUILD_DIR)/el3_smc.obj \
           $(BUILD_DIR)/el3_offload.obj

# All unified driver objects
UNIFIED_OBJS = $(CORE_OBJS) $(BUS_OBJS) $(DATA_OBJS) $(HAL_OBJS)

# Library output
UNIFIED_LIB = $(BUILD_DIR)/el3_unified.lib

# Test executable
TEST_EXE = $(BUILD_DIR)/test_unified.exe

# --- Build Targets ---

.PHONY: all clean debug release performance test info

all: release

debug: CFLAGS = $(DEBUG_FLAGS)
debug: $(BUILD_DIR) $(UNIFIED_LIB)
	@echo "Debug build complete: $(UNIFIED_LIB)"

release: CFLAGS = $(RELEASE_FLAGS)
release: $(BUILD_DIR) $(UNIFIED_LIB)
	@echo "Release build complete: $(UNIFIED_LIB)"

performance: CFLAGS = $(PERF_FLAGS)
performance: $(BUILD_DIR) $(UNIFIED_LIB)
	@echo "Performance build complete: $(UNIFIED_LIB)"

# --- Library Creation ---

$(UNIFIED_LIB): $(UNIFIED_OBJS)
	@echo "Creating unified driver library..."
	$(AR) -n $@ $(UNIFIED_OBJS)
	@echo "Library created: $@"

# --- Object File Rules ---

# Core driver compilation
$(BUILD_DIR)/el3_core.obj: $(CORE_DIR)/el3_core.c
	@echo "Compiling core driver..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

$(BUILD_DIR)/el3_caps.obj: $(CORE_DIR)/el3_caps.c
	@echo "Compiling capability detection..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

# Bus prober compilation
$(BUILD_DIR)/el3_isa.obj: $(BUS_DIR)/el3_isa.c
	@echo "Compiling ISA bus prober..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

$(BUILD_DIR)/el3_pci.obj: $(BUS_DIR)/el3_pci.c
	@echo "Compiling PCI bus prober..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

# Datapath compilation (HOT PATH - special flags)
$(BUILD_DIR)/el3_pio.obj: $(DATA_DIR)/el3_pio.c
	@echo "Compiling PIO datapath (HOT PATH)..."
	$(CC) $(DATAPATH_FLAGS) -fo=$@ -c $<

$(BUILD_DIR)/el3_dma.obj: $(DATA_DIR)/el3_dma.c
	@echo "Compiling DMA datapath (HOT PATH)..."
	$(CC) $(DATAPATH_FLAGS) -fo=$@ -c $<

# HAL compilation (init only)
$(BUILD_DIR)/el3_hal.obj: $(HAL_DIR)/el3_hal.c
	@echo "Compiling HAL (init only)..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

# Optional enhancement compilation
$(BUILD_DIR)/el3_smc.obj: $(CORE_DIR)/el3_smc.c
	@echo "Compiling SMC optimizer..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

$(BUILD_DIR)/el3_offload.obj: $(CORE_DIR)/el3_offload.c
	@echo "Compiling hardware offload..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

# --- Test Harness ---

TEST_OBJS = $(BUILD_DIR)/test_unified.obj

$(BUILD_DIR)/test_unified.obj: test_unified.c
	@echo "Compiling test harness..."
	$(CC) $(CFLAGS) -fo=$@ -c $<

$(TEST_EXE): $(TEST_OBJS) $(UNIFIED_LIB)
	@echo "Linking test executable..."
	$(LINK) system dos option map=$(BUILD_DIR)/test_unified.map \
	        file {$(TEST_OBJS)} library {$(UNIFIED_LIB)} \
	        name $(TEST_EXE)

test: $(BUILD_DIR) $(TEST_EXE)
	@echo "Running unified driver tests..."
	$(TEST_EXE)

# --- Directory Creation ---

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@echo "Created build directory: $(BUILD_DIR)"

# --- Integration Target ---

integrate: release
	@echo "Integrating unified driver with main build..."
	@echo "Add the following to main Makefile:"
	@echo "  UNIFIED_LIB = src/unified/$(UNIFIED_LIB)"
	@echo "  LINK ... library {$$(UNIFIED_LIB)} ..."
	@echo ""
	@echo "To use unified driver, define USE_UNIFIED_DRIVER in main.c"

# --- Information ---

info:
	@echo "=== Unified 3Com EtherLink III Driver Build ==="
	@echo ""
	@echo "Architecture:"
	@echo "  - Capability-driven polymorphism"
	@echo "  - No HAL in datapath (performance critical)"
	@echo "  - Supports 3C509B through Tornado"
	@echo "  - Clean separation of concerns"
	@echo ""
	@echo "Components:"
	@echo "  Core:     el3_core.c, el3_caps.c"
	@echo "  Bus:      el3_isa.c, el3_pci.c"
	@echo "  Datapath: el3_pio.c, el3_dma.c (HOT PATH)"
	@echo "  HAL:      el3_hal.c (init only)"
	@echo ""
	@echo "Targets:"
	@echo "  wmake              - Release build (default)"
	@echo "  wmake debug        - Debug build with symbols"
	@echo "  wmake performance  - Maximum optimization"
	@echo "  wmake test         - Build and run tests"
	@echo "  wmake integrate    - Show integration instructions"
	@echo "  wmake clean        - Remove build artifacts"
	@echo "  wmake info         - Show this information"
	@echo ""
	@echo "Output:"
	@echo "  Library: $(UNIFIED_LIB)"
	@echo "  Test:    $(TEST_EXE)"

# --- Size Analysis ---

size: $(UNIFIED_LIB)
	@echo "=== Unified Driver Size Analysis ==="
	@echo "Library: $(UNIFIED_LIB)"
	@wlib $(UNIFIED_LIB) -l
	@echo ""
	@echo "Individual object sizes:"
	@for obj in $(UNIFIED_OBJS); do \
		if [ -f $$obj ]; then \
			size=$$(stat -f%z $$obj 2>/dev/null || echo "N/A"); \
			echo "  $$obj: $$size bytes"; \
		fi; \
	done

# --- Clean ---

clean:
	@echo "Cleaning unified driver build..."
	@if [ -d $(BUILD_DIR) ]; then \
		rm -rf $(BUILD_DIR); \
	fi
	@echo "Clean complete."

# --- Dependency tracking ---

depend:
	@echo "Generating dependencies..."
	@for src in $(CORE_DIR)/*.c $(BUS_DIR)/*.c $(DATA_DIR)/*.c $(HAL_DIR)/*.c; do \
		if [ -f $$src ]; then \
			obj=$$(basename $$src .c).obj; \
			echo "$(BUILD_DIR)/$$obj: $$src" >> Makefile.dep; \
		fi; \
	done

-include Makefile.dep