# Makefile for Diagnostics Module - Agent 13
# 3Com Packet Driver - Week 1 Implementation

# Compiler and tools
CC = wcc
ASM = nasm
LINK = wlink

# Compiler flags for DOS 16-bit real mode
CFLAGS = -bt=dos -ms -0 -w4 -e25 -zq -ox -d1 -I../../include -I../../docs/agents/shared

# Source files
CSRCS = diagnostic_monitor.c \
        statistical_analysis.c \
        debug_logging.c \
        error_tracking.c \
        network_analysis.c \
        memory_monitor.c \
        module_integration.c \
        diagnostic_dashboard.c

# Object files
OBJS = $(CSRCS:.c=.obj)

# Output library
TARGET = diagnostics.lib

# Default target
all: $(TARGET)

# Build library
$(TARGET): $(OBJS)
	@echo Building diagnostics library...
	wlib -q -n $@ $(OBJS)

# Compile C source files
%.obj: %.c
	@echo Compiling $<...
	$(CC) $(CFLAGS) -fo=$@ -c $<

# Clean build artifacts
clean:
	@echo Cleaning diagnostics module...
	@if exist *.obj del *.obj
	@if exist *.lib del *.lib
	@if exist *.err del *.err
	@if exist *.map del *.map

# Install to main project
install: $(TARGET)
	@echo Installing diagnostics library...
	@if not exist ..\..\build mkdir ..\..\build
	@copy $(TARGET) ..\..\build\

# Test build (compile only, no linking)
test:
	@echo Testing compilation...
	$(CC) $(CFLAGS) -zs $(CSRCS)

# Dependencies
diagnostic_monitor.obj: diagnostic_monitor.c ../../include/diagnostics.h ../../docs/agents/shared/timing-measurement.h
statistical_analysis.obj: statistical_analysis.c ../../include/diagnostics.h ../../docs/agents/shared/error-codes.h
debug_logging.obj: debug_logging.c ../../include/diagnostics.h ../../include/logging.h
error_tracking.obj: error_tracking.c ../../include/diagnostics.h ../../include/error_handling.h
network_analysis.obj: network_analysis.c ../../include/diagnostics.h ../../include/packet_ops.h
memory_monitor.obj: memory_monitor.c ../../include/diagnostics.h ../../include/memory.h
module_integration.obj: module_integration.c ../../include/diagnostics.h ../../include/api.h
diagnostic_dashboard.obj: diagnostic_dashboard.c ../../include/diagnostics.h

.PHONY: all clean install test