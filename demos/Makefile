# Makefile for 3Com Packet Driver ANSI Color Demo
# Demonstrates Quarterdeck-style interface capabilities
#
# Usage:
#   make          - Build ANSI demo
#   make clean    - Clean build files
#   make test     - Run demo in DOSBox (if available)

# Compiler settings for DOS
CC = tcc
CFLAGS = -mc -1 -a -d -G -O -r -w -X
LDFLAGS = 

# Directories
SRC_DIR = ../src/c
INC_DIR = ../include
DEMO_DIR = .

# Target executable
TARGET = ansi_demo.exe

# Source files
SOURCES = ansi_demo.c console.c nic_display.c
OBJECTS = $(SOURCES:.c=.obj)

# Include paths
INCLUDES = -I$(INC_DIR)

# Default target
all: $(TARGET)

# Build the demo executable
$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -e$(TARGET) $(OBJECTS)

# Compile source files
ansi_demo.obj: $(SRC_DIR)/ansi_demo.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRC_DIR)/ansi_demo.c

console.obj: $(SRC_DIR)/console.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRC_DIR)/console.c

nic_display.obj: $(SRC_DIR)/nic_display.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRC_DIR)/nic_display.c

# Clean build files
clean:
	del *.obj
	del *.exe
	del *.map

# Test in DOSBox (if available)
test: $(TARGET)
	@echo Testing ANSI demo...
	@echo NOTE: Ensure ANSI.SYS is loaded for best results
	@echo Add "DEVICE=C:\DOS\ANSI.SYS" to CONFIG.SYS
	$(TARGET)

# Install ANSI.SYS reminder
ansi-setup:
	@echo ============================================================
	@echo  ANSI Color Setup Instructions
	@echo ============================================================
	@echo.
	@echo To enable ANSI colors in DOS, add this line to CONFIG.SYS:
	@echo.
	@echo    DEVICE=C:\DOS\ANSI.SYS
	@echo.
	@echo Or for MS-DOS 6.x and later:
	@echo.
	@echo    DEVICE=C:\DOS\ANSI.SYS /X
	@echo.
	@echo Alternative ANSI drivers:
	@echo   - NANSI.SYS (faster replacement)
	@echo   - ANSI.COM (TSR version)
	@echo   - FANSI.SYS (extended features)
	@echo.
	@echo After editing CONFIG.SYS, reboot and run:
	@echo    ansi_demo.exe
	@echo ============================================================

# Show color test without building
color-test:
	@echo Testing basic ANSI color support...
	@echo [91mRed Text[0m
	@echo [92mGreen Text[0m  
	@echo [94mBlue Text[0m
	@echo [93mYellow Text[0m
	@echo [95mMagenta Text[0m
	@echo [96mCyan Text[0m
	@echo [97mWhite Text[0m
	@echo.
	@echo If you see colors above, ANSI is working!
	@echo If not, check your ANSI.SYS configuration.

# Development helpers
headers:
	@echo Checking header dependencies...
	@$(CC) $(CFLAGS) $(INCLUDES) -zd $(SRC_DIR)/ansi_demo.c
	@$(CC) $(CFLAGS) $(INCLUDES) -zd $(SRC_DIR)/console.c
	@$(CC) $(CFLAGS) $(INCLUDES) -zd $(SRC_DIR)/nic_display.c

debug: CFLAGS += -v -y
debug: $(TARGET)
	@echo Debug version built with extra symbols

release: CFLAGS += -d -O2
release: $(TARGET)
	@echo Release version built with optimizations

.PHONY: all clean test ansi-setup color-test headers debug release