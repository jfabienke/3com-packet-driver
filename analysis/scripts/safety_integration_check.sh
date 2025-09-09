#!/bin/bash
# Safety Integration Validation Script
# Verifies SMC safety patches are properly integrated in hot paths

echo "=== SMC Safety System Integration Check ==="
echo "Validating patch points and performance analysis"
echo ""

# Move to project root
cd ../..

# Counters
PASS=0
FAIL=0
WARN=0

# Check for patch points in RX path
echo "Checking RX path patch points..."
RX_PATCHES=$(grep -c "SMC patch site" src/c/rx_batch_refill.c 2>/dev/null)
if [ "$RX_PATCHES" -eq 3 ]; then
    echo "✓ Found 3 RX patch points (sites #1, #2, #3)"
    PASS=$((PASS + 1))
else
    echo "✗ Expected 3 RX patch points, found $RX_PATCHES"
    FAIL=$((FAIL + 1))
fi

# Check for patch points in TX path
echo "Checking TX path patch points..."
TX_PATCHES=$(grep -c "SMC patch site" src/c/tx_lazy_irq.c 2>/dev/null)
if [ "$TX_PATCHES" -eq 2 ]; then
    echo "✓ Found 2 TX patch points (sites #4, #5)"
    PASS=$((PASS + 1))
else
    echo "✗ Expected 2 TX patch points, found $TX_PATCHES"
    FAIL=$((FAIL + 1))
fi

# Verify cache management module exists
echo "Checking cache management implementation..."
if [ -f "src/c/cache_management.c" ]; then
    # Check for tier implementations
    TIER1=$(grep -c "cache_tier1_clflush" src/c/cache_management.c)
    TIER2=$(grep -c "cache_tier2_wbinvd" src/c/cache_management.c)
    TIER3=$(grep -c "cache_tier3_software" src/c/cache_management.c)
    TIER4=$(grep -c "cache_tier4_fallback" src/c/cache_management.c)
    
    if [ "$TIER1" -gt 0 ] && [ "$TIER2" -gt 0 ] && [ "$TIER3" -gt 0 ] && [ "$TIER4" -gt 0 ]; then
        echo "✓ All 4 cache tiers implemented"
        PASS=$((PASS + 1))
    else
        echo "✗ Missing cache tier implementations"
        FAIL=$((FAIL + 1))
    fi
else
    echo "✗ cache_management.c not found"
    FAIL=$((FAIL + 1))
fi

# Check for V86 mode handling
echo "Checking V86 mode compatibility..."
V86_CHECK=$(grep -c "in_v86_mode" src/c/cache_management.c 2>/dev/null)
if [ "$V86_CHECK" -gt 0 ]; then
    echo "✓ V86 mode detection present (EMM386/QEMM compatible)"
    PASS=$((PASS + 1))
else
    echo "⚠ V86 mode handling not found in cache management"
    WARN=$((WARN + 1))
fi

# Check documentation accuracy
echo "Checking documentation consistency..."
if [ -f "docs/SMC_SAFETY_PERFORMANCE.md" ]; then
    # Verify ISA bandwidth is correctly documented as 12 Mbps
    ISA_BW=$(grep -c "ISA bus limited to 1.5 MB/s (12 Mbps)" docs/SMC_SAFETY_PERFORMANCE.md)
    if [ "$ISA_BW" -gt 0 ]; then
        echo "✓ ISA bandwidth correctly documented (12 Mbps)"
        PASS=$((PASS + 1))
    else
        echo "✗ ISA bandwidth documentation error"
        FAIL=$((FAIL + 1))
    fi
    
    # Verify DMA worse than PIO on ISA is documented
    DMA_WORSE=$(grep -c "DMA actually uses MORE CPU than PIO" docs/SMC_SAFETY_PERFORMANCE.md)
    if [ "$DMA_WORSE" -gt 0 ]; then
        echo "✓ DMA overhead on ISA correctly documented"
        PASS=$((PASS + 1))
    else
        echo "⚠ DMA vs PIO analysis may need review"
        WARN=$((WARN + 1))
    fi
else
    echo "✗ SMC_SAFETY_PERFORMANCE.md not found"
    FAIL=$((FAIL + 1))
fi

# Performance validation checks
echo ""
echo "Performance Analysis Validation:"

# Check worst-case NOP count
echo "  Worst-case NOPs: 1,920 (4 NICs × 32 packets × 15 NOPs)"
WORST_NOPS=$(grep "1,920 NOPs" docs/SMC_SAFETY_PERFORMANCE.md 2>/dev/null | wc -l)
if [ "$WORST_NOPS" -gt 0 ]; then
    echo "  ✓ Correct worst-case calculation"
    PASS=$((PASS + 1))
else
    echo "  ⚠ Worst-case NOP calculation needs verification"
    WARN=$((WARN + 1))
fi

# Summary
echo ""
echo "=== Integration Check Summary ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"
echo "Warnings: $WARN"

if [ "$FAIL" -eq 0 ]; then
    echo ""
    echo "SUCCESS: SMC safety system properly integrated"
    echo "All patch points present and performance analysis validated"
    exit 0
else
    echo ""
    echo "FAILURE: Integration issues detected"
    echo "Review failed checks and update implementation"
    exit 1
fi
