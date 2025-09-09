#!/bin/bash
#
# 86Box Test Environment Setup Script for 3C515-TX Packet Driver Testing
# Creates and configures a DOS 6.22 test environment with the 3C515-TX NIC
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
EIGHTYSIXBOX_PATH="/Users/jvindahl/Development/86Box"
EIGHTYSIXBOX_EXEC="${EIGHTYSIXBOX_PATH}/build/src/86Box.app/Contents/MacOS/86Box"
TEST_DIR="${PROJECT_ROOT}/86box_test"
CONFIG_FILE="${TEST_DIR}/86box.cfg"
DISK_IMAGE="${TEST_DIR}/dos622.img"

echo "=== 86Box Test Environment Setup for 3C515-TX ==="
echo

# Check if 86Box is available
if [ ! -f "$EIGHTYSIXBOX_EXEC" ]; then
    echo "Error: 86Box not found at $EIGHTYSIXBOX_EXEC"
    echo "Please build 86Box first"
    exit 1
fi

# Create test directory
echo "1. Creating test environment..."
mkdir -p "$TEST_DIR"

# Create 86Box configuration for Pentium with 3C515-TX
echo "2. Creating 86Box configuration..."
cat > "$CONFIG_FILE" << 'EOF'
[General]
machine = pentium_pci
cpu_family = pentium
cpu_speed = 100000000
cpu_multi = 1.5
cpu = 8
enable_external_fpu = 1
mem_size = 16384
time_sync = local
floppy_01_type = 35_2hd
floppy_01_image_history_01 = 
floppy_01_image_history_02 = 
floppy_01_image_history_03 = 
floppy_01_image_history_04 = 
floppy_01_image_history_05 = 
floppy_01_image_history_06 = 
floppy_01_image_history_07 = 
floppy_01_image_history_08 = 
floppy_01_image_history_09 = 
floppy_01_image_history_10 = 
floppy_01_image_history_11 = 
floppy_01_image_history_12 = 
floppy_01_image_history_13 = 
floppy_01_image_history_14 = 
floppy_01_image_history_15 = 
floppy_01_image_history_16 = 
floppy_01_image_history_17 = 
floppy_01_image_history_18 = 
floppy_01_image_history_19 = 
floppy_01_image_history_20 = 

[Video]
gfxcard = vga

[Input devices]
mouse_type = ps2

[Sound]
fm_driver = nuked
sndcard = none
midi_device = none

[Network]
net_type = slirp
net_card = 3c515

[Network Adapter]
netcard = 3c515
net_base = 0x300
net_irq = 10
net_dma = 5
mac_address = 00:60:8C:12:34:56

[Storage controllers]
hdc = ide_pci
cassette_mode = none

[Intel 82437FX (Triton) Configuration]
infra_red = 0

[Hard disks]
hdd_01_parameters = 63, 16, 1024, 0, ide
hdd_01_fn = dos622.img
hdd_01_speed = ramdisk_fixed
hdd_01_ide_channel = 0:0

[Floppy drives]
fdd_01_type = 35_2hd

[ISA Plug and Play]
isapnp = 1

[Memory Configuration]
himem = 1
umb = 0
ems = 0
xms = 1

[BIOS Settings]
boot_order = hdd
cache_external = 1
cache_internal = 1

[86Box Specific]
enable_discord = 0
enable_crashdump = 1
log_level = 1
EOF

echo "   Configuration created with:"
echo "   - Machine: Pentium PCI 100MHz"
echo "   - Memory: 16MB"
echo "   - Network: 3C515-TX at IO 0x300, IRQ 10"
echo "   - ISA PnP: Enabled"
echo "   - DOS 6.22 hard disk"

# Create DOS batch files for automated testing
echo "3. Creating DOS batch files..."

# AUTOEXEC.BAT
cat > "${TEST_DIR}/AUTOEXEC.BAT" << 'EOF'
@ECHO OFF
REM Packet Driver Test Environment Autoexec
SET PATH=C:\DOS;C:\PKTDRV;C:\BMTEST
SET TEMP=C:\TEMP
PROMPT $P$G
CLS
ECHO 3Com 3C515-TX Packet Driver Test Environment
ECHO ============================================
ECHO.
REM Load HIMEM for XMS
IF EXIST C:\DOS\HIMEM.SYS LH C:\DOS\HIMEM.SYS
ECHO.
REM Show memory configuration
MEM /C
ECHO.
ECHO Ready for testing. Available commands:
ECHO   LOADDRV  - Load packet driver
ECHO   RUNTEST  - Run standard DMA validation
ECHO   STRESS   - Run 10-minute stress test
ECHO   NEGATIVE - Run negative test
ECHO   FULLTEST - Run complete test suite
ECHO.
EOF

# LOADDRV.BAT
cat > "${TEST_DIR}/LOADDRV.BAT" << 'EOF'
@ECHO OFF
ECHO Loading 3C515 Packet Driver...
C:\PKTDRV\3C515PD.COM 0x60 /VERBOSE
IF ERRORLEVEL 1 GOTO ERROR
ECHO Driver loaded successfully at INT 60h
GOTO END
:ERROR
ECHO ERROR: Failed to load driver!
:END
EOF

# RUNTEST.BAT
cat > "${TEST_DIR}/RUNTEST.BAT" << 'EOF'
@ECHO OFF
ECHO Running Standard DMA Validation Tests...
ECHO ========================================
C:\BMTEST\BMTEST.EXE -d -v > DMTEST.TXT
TYPE DMTEST.TXT
ECHO.
ECHO Test complete. Results saved to DMTEST.TXT
EOF

# STRESS.BAT
cat > "${TEST_DIR}/STRESS.BAT" << 'EOF'
@ECHO OFF
ECHO Starting 10-Minute Stress Test...
ECHO ==================================
ECHO This will take approximately 10 minutes.
C:\BMTEST\BMTEST.EXE -s -j > STRESS.JSON
TYPE STRESS.JSON
ECHO.
ECHO Stress test complete. JSON output saved to STRESS.JSON
EOF

# NEGATIVE.BAT
cat > "${TEST_DIR}/NEGATIVE.BAT" << 'EOF'
@ECHO OFF
ECHO Running Negative Test (Force Failure)...
ECHO ========================================
C:\BMTEST\BMTEST.EXE -n -v > NEGATIVE.TXT
TYPE NEGATIVE.TXT
ECHO.
ECHO Negative test complete. Results saved to NEGATIVE.TXT
EOF

# FULLTEST.BAT
cat > "${TEST_DIR}/FULLTEST.BAT" << 'EOF'
@ECHO OFF
ECHO Running Complete Test Suite...
ECHO ==============================
ECHO.
ECHO Phase 1: Loading Driver
CALL LOADDRV.BAT
IF ERRORLEVEL 1 GOTO END
ECHO.
ECHO Phase 2: Standard DMA Validation
C:\BMTEST\BMTEST.EXE -d -j > STANDARD.JSON
ECHO Standard tests complete
ECHO.
ECHO Phase 3: Stress Test (10 minutes)
C:\BMTEST\BMTEST.EXE -s -seed 0x12345678 -rate 100 -j > STRESS.JSON
ECHO Stress test complete
ECHO.
ECHO Phase 4: Negative Test
C:\BMTEST\BMTEST.EXE -n -j > NEGATIVE.JSON
ECHO Negative test complete
ECHO.
ECHO ==============================
ECHO ALL TESTS COMPLETE
ECHO Results saved to:
ECHO   STANDARD.JSON
ECHO   STRESS.JSON
ECHO   NEGATIVE.JSON
:END
EOF

echo "   Batch files created:"
echo "   - AUTOEXEC.BAT: Environment setup"
echo "   - LOADDRV.BAT: Load packet driver"
echo "   - RUNTEST.BAT: Run standard tests"
echo "   - STRESS.BAT: Run stress test"
echo "   - NEGATIVE.BAT: Run negative test"
echo "   - FULLTEST.BAT: Complete test suite"

# Create directory structure on disk image
echo "4. Creating directory structure..."
cat > "${TEST_DIR}/makedirs.bat" << 'EOF'
@ECHO OFF
MD C:\PKTDRV
MD C:\BMTEST
MD C:\TEMP
MD C:\RESULTS
EOF

# Create test execution script
echo "5. Creating test execution script..."
cat > "${TEST_DIR}/run_tests.sh" << 'EOF'
#!/bin/bash
# Run 86Box with test configuration

86BOX_EXEC="/Users/jvindahl/Development/86Box/build/src/86Box.app/Contents/MacOS/86Box"
CONFIG_DIR="$(dirname "$0")"

echo "Starting 86Box with 3C515-TX test configuration..."
echo "Once DOS boots:"
echo "1. Type LOADDRV to load the packet driver"
echo "2. Type RUNTEST for standard DMA validation"
echo "3. Type STRESS for 10-minute stress test"
echo "4. Type FULLTEST for complete test suite"
echo

cd "$CONFIG_DIR"
"$86BOX_EXEC" --config "$CONFIG_DIR/86box.cfg"
EOF

chmod +x "${TEST_DIR}/run_tests.sh"

# Prepare files to copy
echo "6. Preparing files to copy to disk image..."
cat > "${TEST_DIR}/files_to_copy.txt" << EOF
Driver files:
  ${PROJECT_ROOT}/build/3C515PD.COM -> C:\PKTDRV\3C515PD.COM

Test utilities:
  ${PROJECT_ROOT}/build/BMTEST.EXE -> C:\BMTEST\BMTEST.EXE
  
Configuration files:
  AUTOEXEC.BAT -> C:\AUTOEXEC.BAT
  LOADDRV.BAT -> C:\LOADDRV.BAT  
  RUNTEST.BAT -> C:\RUNTEST.BAT
  STRESS.BAT -> C:\STRESS.BAT
  NEGATIVE.BAT -> C:\NEGATIVE.BAT
  FULLTEST.BAT -> C:\FULLTEST.BAT
EOF

echo
echo "=== Setup Complete ==="
echo
echo "Test environment created at: $TEST_DIR"
echo
echo "Next steps:"
echo "1. Build the packet driver and BMTEST:"
echo "   cd ${PROJECT_ROOT}"
echo "   make clean && make release bmtest"
echo
echo "2. Create or mount a DOS 6.22 disk image at:"
echo "   ${DISK_IMAGE}"
echo
echo "3. Copy the following files to the disk image:"
cat "${TEST_DIR}/files_to_copy.txt"
echo
echo "4. Run the tests:"
echo "   ${TEST_DIR}/run_tests.sh"
echo
echo "The 86Box emulator will start with the 3C515-TX configured."
echo "Use the batch files to run different test scenarios."