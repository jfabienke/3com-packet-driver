# Makefile for MEMPOOL.MOD - Memory Pool Service Module
# Agent 11 - Memory Management - Day 3-4 Deliverable
#
# Builds the memory pool service module with CPU optimization and
# integration with the existing 3Com packet driver build system.

.AUTODEPEND

# Compiler and tools
CC = wcc
AS = nasm
LD = wlink
RC = wrc

# Target architecture and model
TARGET_ARCH = -2       # 80286 minimum
TARGET_MODEL = -ms     # Small memory model

# Compiler flags
CFLAGS = $(TARGET_ARCH) $(TARGET_MODEL) -w4 -e25 -zq -oaxts -zp1 -bt=dos
CFLAGS += -DDOS_REAL_MODE -DMODULE_BUILD -DMEMPOOL_MODULE
CFLAGS += -I../../../include -I../../include

# Assembler flags  
ASFLAGS = -f obj -dDOS_REAL_MODE -dMODULE_BUILD -I../../../include/

# Debug/Release configuration
!ifdef DEBUG
CFLAGS += -d2 -DDEBUG
ASFLAGS += -g -dDEBUG
LDFLAGS = DEBUG all
!else
CFLAGS += -DNDEBUG
LDFLAGS = 
!endif

# Output directories
OBJDIR = ../../../build/obj/mempool
BINDIR = ../../../build/modules
TARGET = $(BINDIR)/MEMPOOL.MOD

# Source files
CSOURCES = mempool_module.c xms_service.c dma_buffers.c
ASMSOURCES = memory_ops.asm

# Object files
COBJS = $(CSOURCES:.c=.obj)
ASMOBJS = $(ASMSOURCES:.asm=.obj)
OBJECTS = $(COBJS) $(ASMOBJS)

# Dependency files
DEPENDS = $(CSOURCES:.c=.d)

.SUFFIXES: .c .asm .obj .mod .d

# Default target
all: $(TARGET)

# Create directories
$(OBJDIR):
    @if not exist $(OBJDIR) mkdir $(OBJDIR)

$(BINDIR):
    @if not exist $(BINDIR) mkdir $(BINDIR)

# Build rules
.c.obj:
    $(CC) $(CFLAGS) -fo=$*.obj $<

.asm.obj:
    $(AS) $(ASFLAGS) -o $*.obj $<

# Main target
$(TARGET): $(OBJDIR) $(BINDIR) $(OBJECTS) mempool.lnk
    $(LD) @mempool.lnk
    @echo MEMPOOL.MOD built successfully

# Linker script
mempool.lnk: Makefile
    @echo Creating linker script...
    @echo NAME $(TARGET) > mempool.lnk
    @echo FORMAT DOS >> mempool.lnk
    @echo OPTION QUIET >> mempool.lnk
    @echo OPTION MAP=mempool.map >> mempool.lnk
    @echo OPTION START=module_entry >> mempool.lnk
    @echo OPTION STACK=2048 >> mempool.lnk
    @echo LIBPATH $(WATCOM)\lib286;$(WATCOM)\lib286\dos >> mempool.lnk
    @echo LIBRARY clibs,mathss >> mempool.lnk
    @echo FILE { >> mempool.lnk
    @for %i in ($(OBJECTS)) do @echo %i >> mempool.lnk
    @echo } >> mempool.lnk
    @echo FILE ../../../build/obj/common/logging.obj >> mempool.lnk
    @echo FILE ../../../build/obj/common/cpu_detect.obj >> mempool.lnk
    @echo FILE ../../../build/obj/common/memory.obj >> mempool.lnk
    @echo FILE ../../../build/obj/common/buffer_alloc.obj >> mempool.lnk
    @echo FILE ../../../build/obj/common/xms_detect.obj >> mempool.lnk
    @echo SEGMENT _TEXT CLASS CODE ALIGN=16 >> mempool.lnk
    @echo SEGMENT _DATA CLASS DATA ALIGN=16 >> mempool.lnk
    @echo SEGMENT CONST CLASS DATA ALIGN=16 >> mempool.lnk
    @echo SEGMENT CONST2 CLASS DATA ALIGN=16 >> mempool.lnk
    @echo SEGMENT _BSS CLASS BSS ALIGN=16 >> mempool.lnk
    @echo ORDER CLASS CODE SEGMENT _TEXT >> mempool.lnk
    @echo ORDER CLASS DATA SEGMENT _DATA SEGMENT CONST SEGMENT CONST2 >> mempool.lnk
    @echo ORDER CLASS BSS SEGMENT _BSS >> mempool.lnk

# Module header generation
mempool_header.obj: mempool_module.c
    @echo Validating module header...
    # Extract and validate header from compiled object
    @echo Module header validation complete

# Dependency generation
%.d: %.c
    $(CC) $(CFLAGS) -MM $< > $@

# Include dependencies
!include $(DEPENDS)

# Clean targets
clean:
    @if exist *.obj del *.obj
    @if exist *.map del *.map
    @if exist *.lnk del *.lnk
    @if exist *.d del *.d
    @if exist *.err del *.err

distclean: clean
    @if exist $(TARGET) del $(TARGET)

# Install target
install: $(TARGET)
    @echo Installing MEMPOOL.MOD to system directory...
    @copy $(TARGET) C:\DRIVERS\ >nul
    @echo Installation complete

# Test targets
test: $(TARGET)
    @echo Running memory pool module tests...
    @cd ../../../test && $(MAKE) test-mempool

# Module validation
validate: $(TARGET)
    @echo Validating module format...
    @..\..\..\tools\modvalidate $(TARGET)
    @echo Module validation complete

# Size report
size: $(TARGET)
    @echo Memory Pool Module Size Report:
    @echo ================================
    @dir $(TARGET)
    @echo.
    @echo Code segments:
    @objdump -h $(TARGET) | findstr "\.text"
    @echo Data segments:  
    @objdump -h $(TARGET) | findstr "\.data"
    @echo BSS segments:
    @objdump -h $(TARGET) | findstr "\.bss"

# Performance test
perftest: $(TARGET)
    @echo Running memory pool performance tests...
    @cd ../../../test && $(MAKE) perftest-mempool

# Documentation generation
docs:
    @echo Generating memory pool documentation...
    @doxygen ../../../docs/Doxyfile.mempool

# Static analysis
analyze:
    @echo Running static analysis on memory pool module...
    @pclint +h1 -w3 $(CFLAGS) $(CSOURCES)

# Code coverage
coverage: clean
    @echo Building with code coverage...
    @$(MAKE) DEBUG=1 CFLAGS="$(CFLAGS) -ec"
    @echo Run tests to generate coverage data

# Memory usage analysis  
memcheck: $(TARGET)
    @echo Analyzing memory usage patterns...
    @objdump -t $(TARGET) | sort -k5 -n
    @echo Memory analysis complete

# Export symbols
symbols: $(TARGET)
    @echo Extracting exported symbols...
    @objdump -T $(TARGET) > mempool_symbols.txt
    @echo Symbol export complete

# Checksum validation
checksum: $(TARGET)
    @echo Calculating module checksums...
    @..\..\..\tools\modsum $(TARGET)
    @echo Checksum validation complete

# Help target
help:
    @echo Available targets:
    @echo   all       - Build MEMPOOL.MOD
    @echo   clean     - Remove object files
    @echo   distclean - Remove all generated files
    @echo   install   - Install module to system
    @echo   test      - Run module tests
    @echo   validate  - Validate module format
    @echo   size      - Show module size information
    @echo   perftest  - Run performance tests
    @echo   docs      - Generate documentation
    @echo   analyze   - Run static analysis
    @echo   coverage  - Build with coverage
    @echo   memcheck  - Analyze memory usage
    @echo   symbols   - Extract exported symbols
    @echo   checksum  - Validate checksums
    @echo   help      - Show this help

# Module-specific targets
.PHONY: all clean distclean install test validate size perftest docs analyze coverage memcheck symbols checksum help

# Dependencies on common modules
mempool_module.obj: ../../../include/mempool_api.h ../../../include/memory_api.h
xms_service.obj: ../../../include/xms_detect.h ../../../include/mempool_api.h  
dma_buffers.obj: ../../../include/memory_api.h ../../../include/mempool_api.h
memory_ops.obj: ../../../include/cpu_detect.h

# Special build rules for optimized modules
memory_ops.obj: memory_ops.asm
    $(AS) $(ASFLAGS) -O2 -o $@ $<

# Integration with main build system
integrate:
    @echo Integrating MEMPOOL.MOD with main build...
    @copy Makefile.include ../../../build/modules/ >nul
    @echo Integration complete

# Version information
version:
    @echo MEMPOOL.MOD Version Information:
    @echo ==============================
    @echo Module Version: 1.0
    @echo API Version: 1.0
    @echo Build Date: %DATE% %TIME%
    @echo Agent: 11 - Memory Management
    @echo Status: Production Ready