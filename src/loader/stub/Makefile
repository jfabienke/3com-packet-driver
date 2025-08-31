# Makefile for Module Loader Stub - 3Com Packet Driver Modular Architecture
# Version: 1.0
# Date: 2025-08-22

# Compiler and tools
CC = wcc
CFLAGS = -ml -2 -ox -d0 -w4 -zq
LINK = wlink
LINKFLAGS = system dos

# Include paths
INCLUDES = -i=../../include

# Output files
LOADER = loader.exe
HELLO_MOD = hello.mod

# Object files
LOADER_OBJS = loader_main.obj module_loader.obj
HELLO_OBJS = hello_module.obj

# Default target
all: $(LOADER) $(HELLO_MOD)

# Loader executable
$(LOADER): $(LOADER_OBJS)
	$(LINK) $(LINKFLAGS) file $(LOADER_OBJS) name $(LOADER)

loader_main.obj: loader_main.c ../../include/module_abi.h
	$(CC) $(CFLAGS) $(INCLUDES) -fo=$@ $<

module_loader.obj: module_loader.c ../../include/module_abi.h
	$(CC) $(CFLAGS) $(INCLUDES) -fo=$@ $<

# Hello module (simplified - in real build this would be more complex)
$(HELLO_MOD): $(HELLO_OBJS)
	$(LINK) $(LINKFLAGS) file $(HELLO_OBJS) name $(HELLO_MOD)

hello_module.obj: ../../modules/hello/hello_module.c ../../include/module_abi.h
	$(CC) $(CFLAGS) $(INCLUDES) -fo=$@ $<

# Test target - run loader with hello module
test: $(LOADER) $(HELLO_MOD)
	$(LOADER) $(HELLO_MOD)

# Clean target
clean:
	del *.obj
	del *.exe
	del *.mod
	del *.err

.PHONY: all test clean