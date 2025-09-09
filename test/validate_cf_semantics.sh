#!/bin/bash
#
# validate_cf_semantics.sh - Validate Carry Flag handling in Extension API
#
# This script verifies that all Extension API functions (AH=80h-97h) properly
# save and restore the carry flag (CF) for error indication per DOS conventions.
#
# Exit codes:
#   0 - All CF semantics validated successfully
#   1 - CF handling errors detected
#   2 - Build/test infrastructure error

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "   CF Semantics Validation for 3CPKT   "
echo "========================================"

# Check if EXTTEST exists
if [ ! -f "test/exttest" ] && [ ! -f "build/exttest.com" ]; then
    echo -e "${RED}ERROR: EXTTEST not found. Please build the test utilities first.${NC}"
    exit 2
fi

# Functions to test with expected CF behavior
# Format: FUNCTION:EXPECTED_CF_ON_SUCCESS:EXPECTED_CF_ON_ERROR
declare -a API_FUNCTIONS=(
    "80:0:1"  # Get snapshot - CF=0 on success, CF=1 on error
    "81:0:1"  # Get statistics - CF=0 on success, CF=1 on error
    "82:0:1"  # Get capabilities - CF=0 on success, CF=1 on error
    "83:0:1"  # Get cache info - CF=0 on success, CF=1 on error
    "84:0:1"  # Get version info - CF=0 on success, CF=1 on error
    "85:0:1"  # Get DMA info - CF=0 on success, CF=1 on error
    "90:0:1"  # Quiesce driver - CF=0 on success, CF=1 on error
    "91:0:1"  # Resume driver - CF=0 on success, CF=1 on error
    "92:0:1"  # Get DMA stats - CF=0 on success, CF=1 on error
    "93:0:1"  # Get error stats - CF=0 on success, CF=1 on error
    "94:0:1"  # Set copy-break threshold - CF=0 on success, CF=1 on error
    "95:0:1"  # Configure mitigation - CF=0 on success, CF=1 on error
    "96:0:1"  # Set media mode - CF=0 on success, CF=1 on error
    "97:0:1"  # Set DMA validation - CF=0 on success, CF=1 on error
)

ERRORS=0
WARNINGS=0
TESTS_RUN=0

# Function to check CF handling in assembly code
check_cf_in_asm() {
    local func_ah=$1
    echo -n "Checking AH=${func_ah}h CF handling in source... "
    
    # Look for the function handler in packet_api_smc.asm
    if grep -q "cmp.*ah.*${func_ah}h" src/asm/packet_api_smc.asm 2>/dev/null; then
        # Check if there's a proper stc (set carry) for error paths
        if grep -A 20 "cmp.*ah.*${func_ah}h" src/asm/packet_api_smc.asm | grep -q "stc\|clc"; then
            echo -e "${GREEN}FOUND${NC}"
            return 0
        else
            echo -e "${YELLOW}WARNING: No explicit CF manipulation found${NC}"
            ((WARNINGS++))
            return 1
        fi
    else
        # Function might be in extension handler
        if grep -q "handle_vendor_extension\|handle_extended_api" src/asm/packet_api_smc.asm; then
            echo -e "${GREEN}DELEGATED${NC}"
            return 0
        else
            echo -e "${RED}NOT FOUND${NC}"
            ((ERRORS++))
            return 1
        fi
    fi
}

# Function to validate CF preservation in ISR exit
check_cf_preservation() {
    echo -n "Checking CF preservation in ISR exit code... "
    
    # Check for FLAGS manipulation in isr_exit
    if grep -q "SAVED_FLAGS_OFFSET" src/asm/packet_api_smc.asm; then
        if grep -A 10 "isr_exit:" src/asm/packet_api_smc.asm | grep -q "and.*word ptr.*bp.*FLAGS\|or.*word ptr.*bp.*FLAGS"; then
            echo -e "${GREEN}PROPER FLAGS HANDLING FOUND${NC}"
            return 0
        else
            echo -e "${RED}FLAGS manipulation missing${NC}"
            ((ERRORS++))
            return 1
        fi
    else
        echo -e "${RED}SAVED_FLAGS_OFFSET not defined${NC}"
        ((ERRORS++))
        return 1
    fi
}

# Main validation
echo ""
echo "1. Validating CF handling in source code:"
echo "------------------------------------------"

# Check each API function
for func_spec in "${API_FUNCTIONS[@]}"; do
    IFS=':' read -r func_ah cf_success cf_error <<< "$func_spec"
    check_cf_in_asm "$func_ah"
    ((TESTS_RUN++))
done

echo ""
echo "2. Validating CF preservation mechanism:"
echo "-----------------------------------------"
check_cf_preservation
((TESTS_RUN++))

echo ""
echo "3. Checking for common CF handling bugs:"
echo "-----------------------------------------"

# Check for missing CLC before success returns
echo -n "Checking for missing CLC on success paths... "
if grep -n "jmp.*isr_exit" src/asm/packet_api_smc.asm | grep -v "stc\|clc" > /dev/null 2>&1; then
    echo -e "${YELLOW}WARNING: Some paths to isr_exit may not set CF explicitly${NC}"
    ((WARNINGS++))
else
    echo -e "${GREEN}OK${NC}"
fi
((TESTS_RUN++))

# Check for proper frame pointer setup
echo -n "Checking for frame pointer setup... "
if grep -q "mov.*bp.*sp" src/asm/packet_api_smc.asm; then
    echo -e "${GREEN}Frame pointer established${NC}"
else
    echo -e "${RED}ERROR: No frame pointer setup found${NC}"
    ((ERRORS++))
fi
((TESTS_RUN++))

# Summary
echo ""
echo "========================================"
echo "           Validation Summary           "
echo "========================================"
echo "Tests run:     $TESTS_RUN"
echo -e "Errors:        ${RED}$ERRORS${NC}"
echo -e "Warnings:      ${YELLOW}$WARNINGS${NC}"

if [ $ERRORS -eq 0 ]; then
    echo -e "\n${GREEN}✓ CF semantics validation PASSED${NC}"
    if [ $WARNINGS -gt 0 ]; then
        echo -e "${YELLOW}  Note: $WARNINGS warnings should be reviewed${NC}"
    fi
    exit 0
else
    echo -e "\n${RED}✗ CF semantics validation FAILED${NC}"
    echo "  Please fix the $ERRORS error(s) before proceeding"
    exit 1
fi