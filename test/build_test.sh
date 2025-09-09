#!/bin/bash
# Build script for SMC safety system integration test
# Compiles with Open Watcom for DOS target

echo "Building SMC Safety System Integration Test..."

# Set up Watcom environment if not already set
if [ -z "$WATCOM" ]; then
    export WATCOM=/opt/watcom
    export PATH=$WATCOM/binl:$PATH
    export INCLUDE=$WATCOM/h
fi

# Create build directory for tests
mkdir -p ../build/test

# Compile test with debugging symbols
echo "Compiling test_smc_safety.c..."
wcc -bt=dos -ms -ox -d2 -i=../src/include -fo=../build/test/test_smc_safety.obj test_smc_safety.c

if [ $? -ne 0 ]; then
    echo "ERROR: Compilation failed"
    exit 1
fi

# Link test modules with safety system
echo "Linking test executable..."
wlink system dos \
    file ../build/test/test_smc_safety.obj \
    file ../build/obj/smc_safety_patches.obj \
    file ../build/obj/cache_coherency.obj \
    file ../build/obj/cpu_detect.obj \
    file ../build/obj/smc_serialization.obj \
    file ../build/asm/safety_stubs.obj \
    name ../build/test/test_smc.exe \
    option map=../build/test/test_smc.map \
    option quiet

if [ $? -ne 0 ]; then
    echo "ERROR: Linking failed"
    exit 1
fi

echo "Test executable built: ../build/test/test_smc.exe"
echo ""
echo "To run the test:"
echo "  1. Copy test_smc.exe to DOS system"
echo "  2. Run: TEST_SMC.EXE"
echo ""
echo "Expected output:"
echo "  - Tier selection based on CPU"
echo "  - Patch point validation"
echo "  - Overhead measurements matching analysis"
echo "  - ISA bandwidth limitation confirmation"

exit 0