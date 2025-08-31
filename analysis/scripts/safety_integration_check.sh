#!/bin/bash
# Safety Integration Check for 3Com Packet Driver
# Verifies which critical safety features are missing from live code

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
LIVE_SRC="$PROJECT_ROOT/src/c"
ORPHANED_SRC="$PROJECT_ROOT/src/c"
ASM_SRC="$PROJECT_ROOT/src/asm"

echo "🔍 3Com Packet Driver Safety Integration Check"
echo "============================================="
echo "Project root: $PROJECT_ROOT"
echo ""

# Colors for output
RED='\033[0;31m'
YELLOW='\033[0;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Function to check if a function exists in live code
check_function_in_live() {
    local func_name="$1"
    local description="$2"
    
    if rg -q "$func_name" "$LIVE_SRC"/*.c 2>/dev/null; then
        echo -e "✅ ${GREEN}$description${NC} - Found in live code"
        return 0
    else
        echo -e "❌ ${RED}$description${NC} - MISSING from live code"
        return 1
    fi
}

# Function to check if a file exists
check_file_exists() {
    local file_path="$1"
    local description="$2"
    
    if [[ -f "$file_path" ]]; then
        echo -e "✅ ${GREEN}$description${NC} - Available"
        return 0
    else
        echo -e "❌ ${RED}$description${NC} - Missing"
        return 1
    fi
}

# Safety check counters
CRITICAL_MISSING=0
HIGH_MISSING=0
MEDIUM_MISSING=0

echo "🔴 CRITICAL SAFETY FEATURES CHECK"
echo "================================="

# DMA Safety Checks
echo ""
echo "1. DMA Safety Framework:"
if ! check_function_in_live "dma_check_boundary\|dma_validate_transfer\|dma_allocate_bounce" "DMA boundary checking"; then
    CRITICAL_MISSING=$((CRITICAL_MISSING + 1))
fi

if ! check_function_in_live "vds_lock_region\|vds_unlock_region" "VDS integration"; then
    CRITICAL_MISSING=$((CRITICAL_MISSING + 1))
fi

if ! check_file_exists "$ORPHANED_SRC/dma_safety.c" "DMA safety module"; then
    echo -e "   ${RED}⚠️  Critical: dma_safety.c not available for integration${NC}"
fi

# Cache Coherency Checks
echo ""
echo "2. Cache Coherency Framework:"
if ! check_function_in_live "cache_coherency_init\|cache_coherency_test" "Cache coherency detection"; then
    CRITICAL_MISSING=$((CRITICAL_MISSING + 1))
fi

if ! check_function_in_live "cache_invalidate\|cache_flush" "Cache control functions"; then
    CRITICAL_MISSING=$((CRITICAL_MISSING + 1))
fi

if ! rg -q "CLFLUSH\|WBINVD\|cache.*flush" "$ASM_SRC"/*.asm 2>/dev/null; then
    echo -e "❌ ${RED}Cache control assembly instructions${NC} - MISSING from live code"
    CRITICAL_MISSING=$((CRITICAL_MISSING + 1))
else
    echo -e "✅ ${GREEN}Cache control assembly instructions${NC} - Found in live code"
fi

if ! check_file_exists "$ORPHANED_SRC/cache_coherency.c" "Cache coherency module"; then
    echo -e "   ${RED}⚠️  Critical: cache_coherency.c not available for integration${NC}"
fi

if ! check_file_exists "$ASM_SRC/cache_ops.asm" "Cache operations assembly"; then
    echo -e "   ${RED}⚠️  Critical: cache_ops.asm not available for integration${NC}"
fi

echo ""
echo "🟡 HIGH VALUE FEATURES CHECK"
echo "============================"

# XMS Buffer Migration
echo ""
echo "3. XMS Buffer Migration:"
if ! check_function_in_live "xms_migrate_buffer\|xms_buffer_pool" "XMS buffer migration"; then
    HIGH_MISSING=$((HIGH_MISSING + 1))
fi

# Check if XMS is only detected but not used for buffers
if rg -q "xms_detect\|XMS" "$LIVE_SRC"/*.c 2>/dev/null; then
    echo -e "ℹ️  ${YELLOW}XMS detection found but no buffer migration${NC}"
fi

# Cache Management
echo ""
echo "4. Cache Management System:"
if ! check_function_in_live "cache_mgmt_init\|cache_tier" "4-tier cache management"; then
    HIGH_MISSING=$((HIGH_MISSING + 1))
fi

echo ""
echo "🟢 PRODUCTION FEATURES CHECK" 
echo "============================"

# Runtime Configuration
echo ""
echo "5. Runtime Configuration:"
if ! check_function_in_live "runtime_config_set\|config_callback" "Runtime configuration API"; then
    MEDIUM_MISSING=$((MEDIUM_MISSING + 1))
fi

# Enhanced Multi-NIC
echo ""
echo "6. Advanced Multi-NIC Coordination:"
if rg -q "multi_nic\|load.*balance" "$LIVE_SRC"/*.c 2>/dev/null; then
    echo -e "ℹ️  ${YELLOW}Basic multi-NIC support found${NC}"
    if ! check_function_in_live "load_balance_.*algorithm\|adaptive_selection" "Advanced load balancing"; then
        MEDIUM_MISSING=$((MEDIUM_MISSING + 1))
    fi
else
    echo -e "❌ ${RED}Multi-NIC coordination${NC} - MISSING from live code"
    MEDIUM_MISSING=$((MEDIUM_MISSING + 1))
fi

# Specific Vulnerability Checks
echo ""
echo "🚨 SPECIFIC VULNERABILITY ANALYSIS"
echo "================================="

echo ""
echo "Checking for DMA boundary violations in live code..."
if rg -n "dma.*transfer\|DMA.*size" "$LIVE_SRC"/*.c | rg -v "boundary\|64.*KB\|65536" 2>/dev/null; then
    echo -e "⚠️  ${RED}Found DMA operations without boundary checking:${NC}"
    rg -n "dma.*transfer\|DMA.*size" "$LIVE_SRC"/*.c | rg -v "boundary\|64.*KB\|65536" | head -5
else
    echo -e "ℹ️  No obvious DMA boundary violations found"
fi

echo ""
echo "Checking for cache-unaware DMA operations..."
if rg -n "packet.*receive\|rx.*buffer\|tx.*buffer" "$LIVE_SRC"/*.c | rg -v "cache\|coherent\|flush" 2>/dev/null; then
    echo -e "⚠️  ${RED}Found DMA operations without cache management:${NC}"
    rg -n "packet.*receive\|rx.*buffer\|tx.*buffer" "$LIVE_SRC"/*.c | rg -v "cache\|coherent\|flush" | head -5
else
    echo -e "ℹ️  No obvious cache-unaware DMA operations found"
fi

echo ""
echo "Checking memory allocation patterns..."
CONV_ALLOCS=$(rg -c "malloc\|alloc.*conventional" "$LIVE_SRC"/*.c 2>/dev/null | awk -F: '{sum += $2} END {print sum+0}')
XMS_ALLOCS=$(rg -c "xms.*alloc\|alloc.*xms" "$LIVE_SRC"/*.c 2>/dev/null | awk -F: '{sum += $2} END {print sum+0}')

echo -e "   Conventional memory allocations: ${CONV_ALLOCS:-0}"
echo -e "   XMS memory allocations: ${XMS_ALLOCS:-0}"

if [[ ${XMS_ALLOCS:-0} -eq 0 ]]; then
    echo -e "⚠️  ${YELLOW}No XMS allocations found - may be wasting conventional memory${NC}"
fi

# Generate Summary Report
echo ""
echo "📊 SAFETY INTEGRATION SUMMARY"
echo "=============================="
echo -e "Critical safety features missing: ${RED}$CRITICAL_MISSING${NC}"
echo -e "High value features missing: ${YELLOW}$HIGH_MISSING${NC}"
echo -e "Production features missing: ${GREEN}$MEDIUM_MISSING${NC}"
echo ""

# Risk Assessment
TOTAL_MISSING=$((CRITICAL_MISSING + HIGH_MISSING + MEDIUM_MISSING))

if [[ $CRITICAL_MISSING -gt 0 ]]; then
    echo -e "🔴 ${RED}RISK LEVEL: CRITICAL${NC}"
    echo -e "   Driver unsafe for production use"
    echo -e "   Data corruption and system instability likely"
    echo -e "   Immediate integration of safety modules required"
elif [[ $HIGH_MISSING -gt 2 ]]; then
    echo -e "🟡 ${YELLOW}RISK LEVEL: HIGH${NC}"
    echo -e "   Driver functional but suboptimal"
    echo -e "   Memory inefficient, limited features"
elif [[ $MEDIUM_MISSING -gt 0 ]]; then
    echo -e "🟢 ${GREEN}RISK LEVEL: MEDIUM${NC}"
    echo -e "   Driver functional with basic features"
    echo -e "   Missing some production conveniences"
else
    echo -e "✅ ${GREEN}RISK LEVEL: LOW${NC}"
    echo -e "   All major features integrated"
fi

echo ""
echo "📋 RECOMMENDED ACTIONS"
echo "====================="

if [[ $CRITICAL_MISSING -gt 0 ]]; then
    echo "IMMEDIATE (Phase 1):"
    echo "  1. Integrate dma_safety.c for DMA boundary checking"
    echo "  2. Integrate cache_coherency.c for cache detection"
    echo "  3. Integrate cache_ops.asm for cache control"
    echo "  4. Integrate cache_management.c for cache tiers"
    echo ""
fi

if [[ $HIGH_MISSING -gt 0 ]]; then
    echo "HIGH PRIORITY (Phase 2):"
    echo "  1. Integrate xms_buffer_migration.c to save conventional memory"
    echo "  2. Add cache management for performance"
    echo ""
fi

if [[ $MEDIUM_MISSING -gt 0 ]]; then
    echo "MEDIUM PRIORITY (Phase 3):"
    echo "  1. Add runtime_config.c for hot reconfiguration"
    echo "  2. Enhance multi-NIC coordination with load balancing"
    echo "  3. Implement handle compaction for memory efficiency"
fi

echo ""
echo "📄 Integration guides available in:"
echo "   docs/FEATURE_INTEGRATION_ANALYSIS.md"
echo "   docs/INTEGRATION_ROADMAP.md"
echo ""

# Exit with appropriate code
if [[ $CRITICAL_MISSING -gt 0 ]]; then
    exit 2  # Critical issues
elif [[ $HIGH_MISSING -gt 2 ]]; then
    exit 1  # High issues  
else
    exit 0  # OK
fi