#!/bin/bash
# Boot Sequence Validation Script for 3Com Packet Driver
# Validates critical boot sequence safety checks and timing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
SRC_C="$PROJECT_ROOT/src/c"
SRC_ASM="$PROJECT_ROOT/src/asm"

echo "🔍 3Com Packet Driver Boot Sequence Validation"
echo "=============================================="
echo "Project root: $PROJECT_ROOT"
echo ""

# Colors for output
RED='\033[0;31m'
YELLOW='\033[0;33m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters for issues
CRITICAL_ISSUES=0
HIGH_ISSUES=0
MEDIUM_ISSUES=0
WARNINGS=0

# Function to check for critical boot sequence issues
check_critical_issue() {
    local test_name="$1"
    local description="$2"
    local check_result="$3"
    
    if [[ $check_result -eq 0 ]]; then
        echo -e "✅ ${GREEN}$test_name${NC}: $description"
        return 0
    else
        echo -e "❌ ${RED}$test_name${NC}: $description"
        CRITICAL_ISSUES=$((CRITICAL_ISSUES + 1))
        return 1
    fi
}

check_high_issue() {
    local test_name="$1"
    local description="$2"
    local check_result="$3"
    
    if [[ $check_result -eq 0 ]]; then
        echo -e "✅ ${GREEN}$test_name${NC}: $description"
        return 0
    else
        echo -e "⚠️  ${YELLOW}$test_name${NC}: $description"
        HIGH_ISSUES=$((HIGH_ISSUES + 1))
        return 1
    fi
}

check_warning() {
    local test_name="$1"
    local description="$2"
    local check_result="$3"
    
    if [[ $check_result -eq 0 ]]; then
        echo -e "✅ ${GREEN}$test_name${NC}: $description"
        return 0
    else
        echo -e "ℹ️  ${BLUE}$test_name${NC}: $description"
        WARNINGS=$((WARNINGS + 1))
        return 1
    fi
}

echo "🔴 CRITICAL BOOT SEQUENCE ISSUES"
echo "================================="

# Critical Issue #1: V86 Mode Detection
echo ""
echo "1. V86 Mode Detection:"
if rg -q "v86.*mode|V86.*MODE|detect.*v86|VM.*flag" "$SRC_C"/*.c 2>/dev/null; then
    check_critical_issue "V86_DETECTION" "V86 mode detection found in code" 0
else
    check_critical_issue "V86_DETECTION" "V86 mode detection MISSING - EMM386 corruption risk!" 1
    echo -e "   ${RED}💀 CRITICAL: Driver will corrupt memory under EMM386 without VDS${NC}"
fi

# Critical Issue #2: VDS Detection Timing
echo ""
echo "2. VDS Services Detection:"
if rg -q "vds.*service|VDS.*service|INT.*4B|int.*4b" "$SRC_C"/*.c 2>/dev/null; then
    # Check if VDS detection happens BEFORE DMA operations
    if rg -B5 -A5 "dma_mapping_init" "$SRC_C"/init.c | rg -q "vds" 2>/dev/null; then
        check_critical_issue "VDS_TIMING" "VDS detection before DMA init" 0
    else
        check_critical_issue "VDS_TIMING" "VDS detection happens TOO LATE - after DMA init!" 1
        echo -e "   ${RED}💀 CRITICAL: DMA operations before VDS check = address translation errors${NC}"
    fi
else
    check_critical_issue "VDS_DETECTION" "VDS services detection MISSING" 1
    echo -e "   ${RED}💀 CRITICAL: No VDS support = DMA failures in protected mode${NC}"
fi

# Critical Issue #3: SMC Patching Timing
echo ""
echo "3. Self-Modifying Code (SMC) Patching Timing:"
SMC_FILES=$(find "$SRC_ASM" -name "*smc*" -o -name "*patch*" 2>/dev/null | wc -l)
if [[ $SMC_FILES -gt 0 ]]; then
    # Check if SMC happens after relocation
    if rg -q "relocate.*hot|hot.*section.*relocate" "$SRC_C"/*.c 2>/dev/null; then
        # Look for SMC after relocation
        if rg -A20 "relocate.*hot|hot.*section.*relocate" "$SRC_C"/*.c | rg -q "smc|patch" 2>/dev/null; then
            check_critical_issue "SMC_TIMING" "SMC patching after relocation" 0
        else
            check_critical_issue "SMC_TIMING" "SMC patching timing WRONG - patches before relocation!" 1
            echo -e "   ${RED}💀 CRITICAL: SMC patches applied to cold section will be LOST${NC}"
        fi
    else
        check_critical_issue "SMC_RELOCATION" "Hot section relocation MISSING" 1
        echo -e "   ${RED}💀 CRITICAL: No hot section relocation = SMC patches ineffective${NC}"
    fi
else
    check_warning "SMC_FILES" "No SMC files found (may be integrated elsewhere)" 1
fi

# Critical Issue #4: DMA Coherency Testing
echo ""
echo "4. DMA Coherency Validation:"
if rg -q "dma.*coherenc|coherenc.*test|cache.*coherenc" "$SRC_C"/*.c 2>/dev/null; then
    check_critical_issue "DMA_COHERENCY" "DMA coherency testing found" 0
else
    check_critical_issue "DMA_COHERENCY" "DMA coherency testing MISSING" 1
    echo -e "   ${RED}💀 CRITICAL: No validation that DMA actually works before going live${NC}"
fi

echo ""
echo "🟡 HIGH RISK BOOT SEQUENCE ISSUES"  
echo "=================================="

# High Issue #1: IRQ Safety During Probe
echo ""
echo "5. IRQ Safety During Hardware Probe:"
if rg -q "mask.*irq|disable.*irq.*probe|irq.*mask.*probe" "$SRC_C"/*.c 2>/dev/null; then
    check_high_issue "IRQ_PROBE_SAFETY" "IRQ masking during probe found" 0
else
    check_high_issue "IRQ_PROBE_SAFETY" "IRQ masking during probe MISSING" 1
    echo -e "   ${YELLOW}⚠️  HIGH RISK: Spurious interrupts during hardware detection${NC}"
fi

# High Issue #2: Install Check
echo ""
echo "6. Existing Driver Install Check:"
if rg -q "already.*install|packet.*driver.*install|install.*check" "$SRC_C"/*.c 2>/dev/null; then
    check_high_issue "INSTALL_CHECK" "Install check for existing driver found" 0
else
    check_high_issue "INSTALL_CHECK" "Install check MISSING" 1
    echo -e "   ${YELLOW}⚠️  HIGH RISK: Multiple driver instances, vector conflicts${NC}"
fi

# High Issue #3: Interrupt Vector Validation
echo ""
echo "7. Interrupt Vector Safety:"
if rg -q "vector.*valid|validate.*vector|vector.*safe|INT.*valid" "$SRC_C"/*.c 2>/dev/null; then
    check_high_issue "VECTOR_VALIDATION" "Interrupt vector validation found" 0
else
    check_high_issue "VECTOR_VALIDATION" "Interrupt vector validation MISSING" 1
    echo -e "   ${YELLOW}⚠️  HIGH RISK: Vector conflicts with DOS/BIOS services${NC}"
fi

# High Issue #4: Resource Conflict Detection
echo ""
echo "8. Hardware Resource Conflict Detection:"
if rg -q "resource.*conflict|conflict.*detect|io.*conflict|irq.*conflict" "$SRC_C"/*.c 2>/dev/null; then
    check_high_issue "RESOURCE_CONFLICTS" "Resource conflict detection found" 0
else
    check_high_issue "RESOURCE_CONFLICTS" "Resource conflict detection MISSING" 1
    echo -e "   ${YELLOW}⚠️  HIGH RISK: I/O and IRQ conflicts with other hardware${NC}"
fi

echo ""
echo "🟢 MEDIUM PRIORITY BOOT SEQUENCE ISSUES"
echo "========================================"

# Medium Issue #1: Feature Planning Phase  
echo ""
echo "9. Feature Planning & Sizing:"
if rg -q "feature.*plan|plan.*feature|operational.*mode.*select" "$SRC_C"/*.c 2>/dev/null; then
    check_warning "FEATURE_PLANNING" "Feature planning found" 0
else
    check_warning "FEATURE_PLANNING" "Feature planning phase MISSING" 1
    echo -e "   ${BLUE}ℹ️  Suboptimal: No systematic mode selection and memory planning${NC}"
fi

# Medium Issue #2: ISA DMA Constraint Handling
echo ""
echo "10. ISA DMA Constraints (64KB/16MB limits):"
if rg -q "64.*KB.*boundary|64.*KB.*cross|16.*MB.*limit|ISA.*DMA.*limit" "$SRC_C"/*.c 2>/dev/null; then
    check_warning "ISA_DMA_CONSTRAINTS" "ISA DMA constraint handling found" 0
else
    check_warning "ISA_DMA_CONSTRAINTS" "ISA DMA constraint handling MISSING" 1
    echo -e "   ${BLUE}ℹ️  Suboptimal: No systematic 64KB boundary and 16MB limit handling${NC}"
fi

# Medium Issue #3: Memory Layout Optimization
echo ""
echo "11. Memory Layout & UMB Usage:"
if rg -q "UMB|upper.*memory|umb.*alloc|memory.*layout" "$SRC_C"/*.c 2>/dev/null; then
    check_warning "MEMORY_OPTIMIZATION" "Memory layout optimization found" 0
else
    check_warning "MEMORY_OPTIMIZATION" "Memory layout optimization MISSING" 1
    echo -e "   ${BLUE}ℹ️  Suboptimal: Not using UMB for resident code${NC}"
fi

# Check initialization order in current code
echo ""
echo "🔍 CURRENT INITIALIZATION ORDER ANALYSIS"
echo "========================================="

if [[ -f "$SRC_C/init.c" ]]; then
    echo "Analyzing init.c for current sequence..."
    
    # Extract function call order from init.c
    INIT_ORDER=$(rg -o "^\s*[a-zA-Z_][a-zA-Z0-9_]*\s*\(" "$SRC_C/init.c" | head -20 | sed 's/[[:space:]]*//g' | sed 's/($//')
    
    if [[ -n "$INIT_ORDER" ]]; then
        echo ""
        echo "Current initialization function call sequence:"
        echo "$INIT_ORDER" | nl -nln -s'. '
    fi
    
    # Check specific sequence issues
    echo ""
    echo "Sequence validation:"
    
    # Check if CPU detection comes before hardware init
    CPU_LINE=$(rg -n "cpu.*detect|detect.*cpu" "$SRC_C/init.c" | head -1 | cut -d: -f1)
    HW_LINE=$(rg -n "hardware.*init|init.*hardware" "$SRC_C/init.c" | head -1 | cut -d: -f1)
    
    if [[ -n "$CPU_LINE" && -n "$HW_LINE" ]]; then
        if [[ $CPU_LINE -lt $HW_LINE ]]; then
            echo -e "✅ ${GREEN}CPU detection before hardware init${NC} (line $CPU_LINE < $HW_LINE)"
        else
            echo -e "❌ ${RED}CPU detection after hardware init${NC} (line $CPU_LINE > $HW_LINE)"
            HIGH_ISSUES=$((HIGH_ISSUES + 1))
        fi
    fi
    
    # Check DMA init timing
    DMA_LINE=$(rg -n "dma.*init|dma_mapping_init" "$SRC_C/init.c" | head -1 | cut -d: -f1)
    if [[ -n "$DMA_LINE" ]]; then
        echo -e "ℹ️  DMA initialization at line $DMA_LINE"
        # Should be after platform probe but we don't have platform probe yet
        if [[ $DMA_LINE -lt 50 ]]; then
            echo -e "⚠️  ${YELLOW}DMA init very early - should be after platform probe${NC}"
            HIGH_ISSUES=$((HIGH_ISSUES + 1))
        fi
    fi
else
    echo -e "❌ ${RED}init.c not found!${NC}"
    CRITICAL_ISSUES=$((CRITICAL_ISSUES + 1))
fi

# Boot sequence compliance check
echo ""
echo "📋 BOOT SEQUENCE COMPLIANCE CHECK"
echo "================================="

REQUIRED_PHASES=("Entry Safety" "Platform Probe" "ISA/PnP Prep" "NIC Discovery" "Feature Planning" "Memory Allocation" "Relocation" "SMC Patching" "Interrupt Setup" "DMA Tests" "Final Activation")

echo "Required 10-phase boot sequence compliance:"
PHASES_IMPLEMENTED=0

# Phase 0: Entry & Safety
if rg -q "install.*check|already.*install" "$SRC_C"/*.c 2>/dev/null; then
    echo -e "Phase 0: ${GREEN}Entry & Safety${NC} - Partial (install check found)"
    PHASES_IMPLEMENTED=$((PHASES_IMPLEMENTED + 1))
else
    echo -e "Phase 0: ${RED}Entry & Safety${NC} - MISSING"
fi

# Phase 1: Platform Probe
if rg -q "v86.*mode|vds.*service" "$SRC_C"/*.c 2>/dev/null; then
    echo -e "Phase 1: ${GREEN}Platform Probe${NC} - Partial (some detection found)"
    PHASES_IMPLEMENTED=$((PHASES_IMPLEMENTED + 1))
else
    echo -e "Phase 1: ${RED}Platform Probe${NC} - MISSING"
fi

# Phase 3: NIC Discovery (this one is implemented)
if rg -q "nic.*detect|detect.*nic" "$SRC_C"/*.c 2>/dev/null; then
    echo -e "Phase 3: ${GREEN}NIC Discovery${NC} - IMPLEMENTED"
    PHASES_IMPLEMENTED=$((PHASES_IMPLEMENTED + 1))
else
    echo -e "Phase 3: ${RED}NIC Discovery${NC} - MISSING"
fi

# Phase 7: SMC Patching  
if [[ $SMC_FILES -gt 0 ]]; then
    echo -e "Phase 7: ${YELLOW}SMC Patching${NC} - EXISTS (timing may be wrong)"
    PHASES_IMPLEMENTED=$((PHASES_IMPLEMENTED + 1))
else
    echo -e "Phase 7: ${RED}SMC Patching${NC} - MISSING"
fi

# Phase 10: Final Activation (TSR)
if rg -q "tsr|TSR|terminate.*stay.*resident" "$SRC_ASM"/*.asm 2>/dev/null; then
    echo -e "Phase 10: ${GREEN}Final Activation${NC} - IMPLEMENTED"
    PHASES_IMPLEMENTED=$((PHASES_IMPLEMENTED + 1))
else
    echo -e "Phase 10: ${RED}Final Activation${NC} - MISSING"
fi

echo ""
echo -e "Boot sequence compliance: ${PHASES_IMPLEMENTED}/10 phases (${GREEN}$(( PHASES_IMPLEMENTED * 10 ))%${NC})"

# Generate final report
echo ""
echo "📊 BOOT SEQUENCE VALIDATION SUMMARY"
echo "===================================="
echo -e "Critical issues (production blockers): ${RED}$CRITICAL_ISSUES${NC}"
echo -e "High risk issues (stability problems): ${YELLOW}$HIGH_ISSUES${NC}"  
echo -e "Medium issues (suboptimal): ${BLUE}$WARNINGS${NC}"
echo -e "Boot sequence completeness: ${PHASES_IMPLEMENTED}/10 phases"
echo ""

# Risk assessment
TOTAL_ISSUES=$((CRITICAL_ISSUES + HIGH_ISSUES))

if [[ $CRITICAL_ISSUES -gt 0 ]]; then
    echo -e "🔴 ${RED}RISK LEVEL: CRITICAL - UNSAFE FOR PRODUCTION${NC}"
    echo -e "   Boot sequence has fatal flaws that will cause:"
    echo -e "   • Memory corruption under EMM386/Windows"
    echo -e "   • SMC optimization failures" 
    echo -e "   • DMA operations without validation"
    echo -e "   • Interrupt race conditions"
    echo ""
    echo -e "   ${RED}IMMEDIATE ACTION REQUIRED${NC}: Implement Phase 0 boot fixes"
    
elif [[ $HIGH_ISSUES -gt 2 ]]; then
    echo -e "🟡 ${YELLOW}RISK LEVEL: HIGH - STABILITY ISSUES LIKELY${NC}"
    echo -e "   Boot sequence missing important safety checks"
    echo -e "   • System hangs possible"
    echo -e "   • Hardware conflicts likely"
    echo -e "   • Vector conflicts possible"
    
elif [[ $TOTAL_ISSUES -gt 0 ]]; then
    echo -e "🟢 ${GREEN}RISK LEVEL: MEDIUM - FUNCTIONAL BUT SUBOPTIMAL${NC}"
    echo -e "   Boot sequence works but missing optimizations"
    
else
    echo -e "✅ ${GREEN}RISK LEVEL: LOW - BOOT SEQUENCE COMPLIANT${NC}"
    echo -e "   All critical safety checks implemented"
fi

echo ""
echo "📋 RECOMMENDED ACTIONS"
echo "====================="

if [[ $CRITICAL_ISSUES -gt 0 ]]; then
    echo "IMMEDIATE PRIORITY (Phase 0 - Boot Sequence Fixes):"
    echo "  1. Add V86 mode detection to prevent EMM386 corruption"
    echo "  2. Move VDS detection to platform probe phase"
    echo "  3. Fix SMC patching to happen AFTER relocation"
    echo "  4. Add DMA coherency validation before activation"
    echo "  5. Add IRQ masking during hardware probe"
    echo ""
fi

if [[ $HIGH_ISSUES -gt 0 ]]; then
    echo "HIGH PRIORITY (Safety & Stability):"
    echo "  1. Add install check at program entry"
    echo "  2. Add interrupt vector validation"
    echo "  3. Add resource conflict detection"
    echo "  4. Implement proper initialization sequence"
    echo ""
fi

if [[ $WARNINGS -gt 0 ]]; then
    echo "MEDIUM PRIORITY (Optimization):"
    echo "  1. Add feature planning phase"
    echo "  2. Implement ISA DMA constraint handling"
    echo "  3. Add memory layout optimization"
    echo "  4. Add UMB utilization"
fi

echo ""
echo "📄 Documentation:"
echo "  - Boot sequence architecture: docs/BOOT_SEQUENCE_ARCHITECTURE.md"
echo "  - Gap analysis details: docs/BOOT_SEQUENCE_GAPS.md"
echo "  - Integration roadmap: docs/INTEGRATION_ROADMAP.md"
echo ""

# Set exit code based on severity
if [[ $CRITICAL_ISSUES -gt 0 ]]; then
    exit 2  # Critical issues - unsafe for production
elif [[ $HIGH_ISSUES -gt 2 ]]; then
    exit 1  # High issues - stability problems
else
    exit 0  # OK or minor issues only
fi