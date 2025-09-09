#!/bin/bash
# Verify BMTEST stress test integration

echo "=== BMTEST Stress Test Integration Verification ==="
echo

# Check if stress_test.c exists in tools directory
echo "1. Checking stress_test.c location..."
if [ -f "tools/stress_test.c" ]; then
    echo "   ✓ tools/stress_test.c exists"
else
    echo "   ✗ tools/stress_test.c not found"
    exit 1
fi

# Check if bmtest.c has proper forward declarations
echo "2. Checking bmtest.c forward declarations..."
if grep -q "run_stress_test" tools/bmtest.c && \
   grep -q "run_soak_test" tools/bmtest.c && \
   grep -q "run_negative_test" tools/bmtest.c && \
   grep -q "get_stress_stats" tools/bmtest.c; then
    echo "   ✓ All forward declarations found"
else
    echo "   ✗ Missing forward declarations"
    exit 1
fi

# Check if bmtest.c has command-line option handling
echo "3. Checking command-line options..."
if grep -q '"-s"' tools/bmtest.c && \
   grep -q '"-S"' tools/bmtest.c && \
   grep -q '"-n"' tools/bmtest.c && \
   grep -q '"-d"' tools/bmtest.c; then
    echo "   ✓ All command-line options present"
else
    echo "   ✗ Missing command-line options"
    exit 1
fi

# Check if Makefile includes stress_test.c
echo "4. Checking Makefile integration..."
if grep -q "stress_test.obj" Makefile && \
   grep -q "tools/stress_test.c" Makefile; then
    echo "   ✓ Makefile properly includes stress_test.c"
else
    echo "   ✗ Makefile missing stress_test.c integration"
    exit 1
fi

# Check stress_test.c exports
echo "5. Checking stress_test.c exports..."
if grep -q "int run_stress_test" tools/stress_test.c && \
   grep -q "int run_soak_test" tools/stress_test.c && \
   grep -q "int run_negative_test" tools/stress_test.c && \
   grep -q "void get_stress_stats" tools/stress_test.c; then
    echo "   ✓ All required functions exported"
else
    echo "   ✗ Missing function exports"
    exit 1
fi

# Check JSON output integration
echo "6. Checking JSON output for stress tests..."
if grep -q '\\"test\\": \\"stress\\"' tools/bmtest.c && \
   grep -q '\\"test\\": \\"soak\\"' tools/bmtest.c && \
   grep -q '\\"test\\": \\"negative\\"' tools/bmtest.c; then
    echo "   ✓ JSON output properly integrated"
else
    echo "   ✗ JSON output missing for stress tests"
    exit 1
fi

# Summary
echo
echo "=== Integration Summary ==="
echo "✓ stress_test.c successfully integrated into BMTEST"
echo "✓ Command-line options: -s (stress), -S (soak), -n (negative), -d (standard)"
echo "✓ JSON output format supported for all test modes"
echo "✓ Makefile properly configured for building"
echo
echo "Usage examples:"
echo "  BMTEST -d           # Run standard DMA validation tests"
echo "  BMTEST -s           # Run 10-minute stress test"
echo "  BMTEST -S 30        # Run 30-minute soak test"
echo "  BMTEST -n           # Run negative test (force failure)"
echo "  BMTEST -s -j        # Stress test with JSON output"
echo "  BMTEST -v -s        # Verbose stress test"