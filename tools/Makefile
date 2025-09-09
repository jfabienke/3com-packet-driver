# Makefile for 3Com Packet Driver Tools
# GPT-5 Stage 1: External Sidecar Utilities

# Compiler settings for DOS
CC = wcc
CFLAGS = -bt=dos -ml -ox -w4 -zq
LDFLAGS = -l=dos

# Default target
all: busmaster_test.exe health_diag.exe config_mgr.exe xms_mgr.exe nic_mgr.exe

# Bus mastering test utility
busmaster_test.exe: busmaster_test.c
	$(CC) $(CFLAGS) busmaster_test.c -fe=busmaster_test.exe

# Health diagnostics utility
health_diag.exe: health_diag.c
	$(CC) $(CFLAGS) health_diag.c -fe=health_diag.exe

# Runtime configuration manager
config_mgr.exe: config_mgr.c
	$(CC) $(CFLAGS) config_mgr.c -fe=config_mgr.exe

# XMS buffer manager
xms_mgr.exe: xms_mgr.c
	$(CC) $(CFLAGS) xms_mgr.c -fe=xms_mgr.exe

# Multi-NIC coordination manager
nic_mgr.exe: nic_mgr.c
	$(CC) $(CFLAGS) nic_mgr.c -fe=nic_mgr.exe

# Clean up
clean:
	del *.exe *.obj *.err

# Install to system directory (optional)
install: busmaster_test.exe health_diag.exe config_mgr.exe xms_mgr.exe nic_mgr.exe
	copy busmaster_test.exe C:\UTILS\
	copy health_diag.exe C:\UTILS\
	copy config_mgr.exe C:\UTILS\
	copy xms_mgr.exe C:\UTILS\
	copy nic_mgr.exe C:\UTILS\

.PHONY: all clean install