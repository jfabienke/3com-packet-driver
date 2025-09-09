#!/bin/bash
# Extension API Test Runner
# Tests all vendor API functions (AH=80h-97h) for the 3Com packet driver

echo "================================"
echo "Extension API Test Suite"
echo "================================"
echo ""

# Check if driver is loaded
if ! lsmod | grep -q "3c5x9pkt"; then
    echo "ERROR: 3Com packet driver not loaded"
    echo "Please load the driver first with: insmod 3c5x9pkt.sys"
    exit 1
fi

# Compile the test if needed
if [ ! -f exttest_enhanced.exe ] || [ exttest_enhanced.c -nt exttest_enhanced.exe ]; then
    echo "Compiling enhanced test suite..."
    wcc -ms -0 -os -zq exttest_enhanced.c -fo=exttest_enhanced.obj
    wlink system dos file exttest_enhanced.obj name exttest_enhanced.exe
    if [ $? -ne 0 ]; then
        echo "ERROR: Compilation failed"
        exit 1
    fi
fi

# Run the enhanced test suite
echo "Running enhanced Extension API tests..."
echo ""
./exttest_enhanced.exe
RESULT=$?

echo ""
echo "================================"
if [ $RESULT -eq 0 ]; then
    echo "✓ ALL TESTS PASSED"
else
    echo "✗ SOME TESTS FAILED"
    echo "Exit code: $RESULT"
fi
echo "================================"

# Optional: Save results to log
if [ "$1" = "--log" ]; then
    echo ""
    echo "Saving results to exttest_results.log..."
    ./exttest_enhanced.exe > exttest_results.log 2>&1
    echo "Log saved."
fi

exit $RESULT