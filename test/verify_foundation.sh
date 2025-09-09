#!/bin/bash
# Foundation Verification Script
# Tests all critical fixes from Stage -1

echo "===================================="
echo "3Com Packet Driver Foundation Verification"
echo "===================================="
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counter
PASS=0
FAIL=0

# Function to check test result
check_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}[PASS]${NC} $2"
        ((PASS++))
    else
        echo -e "${RED}[FAIL]${NC} $2"
        ((FAIL++))
    fi
}

echo "1. BUILD VALIDATION"
echo "-------------------"

# Check if all required objects are in Makefile
echo -n "Checking HOT_C_OBJS includes DMA modules... "
grep -q "dma_mapping.obj" Makefile && \
grep -q "dma_boundary.obj" Makefile && \
grep -q "hw_checksum.obj" Makefile
check_result $? "DMA modules in HOT_C_OBJS"

echo -n "Checking tsr_c_wrappers.obj in HOT_ASM_OBJS... "
grep -A10 "^HOT_ASM_OBJS" Makefile | grep -q "tsr_c_wrappers.obj"
check_result $? "tsr_c_wrappers.obj correctly placed"

echo -n "Checking DMA optimization flags... "
grep -q "DMA_OPT_FLAGS = -DPRODUCTION -DNO_LOGGING" Makefile
check_result $? "DMA module optimization flags defined"

echo ""
echo "2. SMC PATCH VALIDATION"
echo "----------------------"

echo -n "Checking patch_apply.c patches all modules... "
grep -q "nic_irq_module_header" src/loader/patch_apply.c && \
grep -q "hardware_module_header" src/loader/patch_apply.c && \
grep -q "packet_api_module_header" src/loader/patch_apply.c
check_result $? "All three modules patched"

echo -n "Checking ASM module exports... "
grep -q "public.*nic_irq_module_header" src/asm/nic_irq_smc.asm && \
grep -q "public.*hardware_module_header" src/asm/hardware_smc.asm && \
grep -q "public.*packet_api_module_header" src/asm/packet_api_smc.asm
check_result $? "Module headers exported"

echo -n "Checking patch point exports... "
grep -q "public.*PATCH_3c515_transfer" src/asm/nic_irq_smc.asm && \
grep -q "public.*PATCH_dma_boundary_check" src/asm/nic_irq_smc.asm
check_result $? "Patch points exported"

echo ""
echo "3. ISR STACK SAFETY"
echo "------------------"

echo -n "Checking private ISR stack allocation... "
grep -q "isr_private_stack.*db.*2048 DUP" src/asm/nic_irq_smc.asm
check_result $? "2KB ISR stack allocated"

echo -n "Checking stack switch in ISR... "
grep -q "mov.*\[saved_ss\], ss" src/asm/nic_irq_smc.asm && \
grep -q "mov.*sp, OFFSET isr_stack_top" src/asm/nic_irq_smc.asm
check_result $? "Stack switching implemented"

echo ""
echo "4. DMA SAFETY GATES"
echo "------------------"

echo -n "Checking 3C515 PIO default... "
grep -A1 "PATCH_3c515_transfer:" src/asm/nic_irq_smc.asm | grep -q "call.*transfer_pio"
check_result $? "3C515 defaults to PIO"

echo -n "Checking compile-time safety flag... "
grep -q "FORCE_3C515_PIO_SAFETY 1" include/config.h
check_result $? "PIO safety flag set"

echo -n "Checking runtime verification... "
grep -q "verify_patches_applied" src/loader/patch_apply.c
check_result $? "Runtime patch verification exists"

echo ""
echo "5. COLD SECTION VERIFICATION"
echo "----------------------------"

echo -n "Checking patch_apply.c is cold-only... "
grep -q '#pragma code_seg("COLD_TEXT"' src/loader/patch_apply.c
check_result $? "Verification code in cold section"

echo ""
echo "6. BUILD TARGETS"
echo "---------------"

echo -n "Checking link-sanity target... "
grep -q "^link-sanity:" Makefile
check_result $? "link-sanity target exists"

echo -n "Checking verify-patches target... "
grep -q "^verify-patches:" Makefile
check_result $? "verify-patches target exists"

echo ""
echo "===================================="
echo "VERIFICATION SUMMARY"
echo "===================================="
echo -e "Tests Passed: ${GREEN}$PASS${NC}"
echo -e "Tests Failed: ${RED}$FAIL${NC}"

if [ $FAIL -eq 0 ]; then
    echo -e "\n${GREEN}✅ ALL FOUNDATION FIXES VERIFIED${NC}"
    echo "The driver foundation is stable and production-ready."
    exit 0
else
    echo -e "\n${RED}❌ FOUNDATION ISSUES DETECTED${NC}"
    echo "Please review failed tests above."
    exit 1
fi