#!/bin/bash
#
# build_macos.sh - Build 3Com Packet Driver on macOS with Open Watcom + NASM
#
# Part of 3Com DOS Packet Driver Project
# Last Updated: 2026-02-01 14:50:00 CET
# Phase 7: JIT copy-down + SMC pure-ASM TSR architecture
# Phase 6: Replaced 15 *_rt.c files with consolidated rt_stubs.c
#
# Usage:
#   ./build_macos.sh              Build release (default)
#   ./build_macos.sh release      Build release version
#   ./build_macos.sh debug        Build debug version
#   ./build_macos.sh production   Build production (size-optimized)
#   ./build_macos.sh clean        Remove build artifacts
#   ./build_macos.sh info         Show build configuration
#

set -e  # Exit on error

# =============================================================================
# CONFIGURATION
# =============================================================================

# Open Watcom paths
WATCOM="${WATCOM:-/Users/johnfabienke/Development/macos-open-watcom/open-watcom-v2/rel}"
CC="$WATCOM/armo64/wcc"
LINK="$WATCOM/armo64/wlink"
export INCLUDE="$WATCOM/h"
export LIB="$WATCOM/lib286/dos:$WATCOM/lib286"

# NASM Assembler (unified toolchain - better local label support)
ASM="nasm"
ASM_FLAGS="-f obj"

# Directories
SRCDIR="src"
CDIR="$SRCDIR/c"
ASMDIR="$SRCDIR/asm"
INCDIR="include"
BUILDDIR="build"
LOADERDIR="$SRCDIR/loader"

# Target
TARGET="$BUILDDIR/3cpd.exe"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# =============================================================================
# COMPILER FLAGS
# =============================================================================

# Common flags for all builds
# -zq: Quiet mode
# -ml: Large memory model (code+data > 64KB, required for overlay system)
# -s: No stack checking
# -zp1: Pack structures on 1-byte boundary
# -of: Overlay-safe function prologue/epilogue
# -zc: Put string literals in code segment (reduces CONST segment)
# -zdf: DS != DGROUP (for TSR)
CFLAGS_COMMON="-zq -ml -s -zp1 -of -zc -zdf -i=$INCDIR/ -i=$WATCOM/h/ -wcd=201"

# ROOT segment flag (always-resident modules)
CFLAGS_ROOT="-dROOT_SEGMENT"

# COLD section flag (overlay, discarded after init)
CFLAGS_COLD="-dCOLD_SECTION"

# Debug flags
CFLAGS_DEBUG="$CFLAGS_COMMON -0 -d2 -dINIT_DIAG"

# Release flags
# -os: Optimize for space
# -ot: Optimize for time
CFLAGS_RELEASE="$CFLAGS_COMMON -os -ot -d0"

# Production flags (maximum size optimization)
CFLAGS_PRODUCTION="$CFLAGS_COMMON -os -d0 -oe=100 -ol+ -ox -DPRODUCTION -DNO_LOGGING -DNO_STATS -DNDEBUG"

# NASM flags
# -f obj: Output OMF object format (compatible with Watcom linker)
# -I: Include path
AFLAGS_COMMON="-f obj -I$INCDIR/"
AFLAGS_DEBUG="$AFLAGS_COMMON -g"
AFLAGS_RELEASE="$AFLAGS_COMMON"

# All assembly files use NASM syntax (unified toolchain)
# NASM provides better local label support (.label scoped per function)

# Linker flags
LFLAGS_COMMON="system dos option caseexact, quiet, stack=1024"

# =============================================================================
# OBJECT FILE LISTS
# =============================================================================

# Loader (contains both hot and cold)
LOADER_OBJ="tsrldr"

# HOT SECTION - Assembly (resident after init)
HOT_ASM_OBJS=(
    pktapi nicirq hwsmc pcmisr flowrt dirpio
    pktops pktcopy tsrcom tsrwrap pci_io pciisr
    linkasm hwpkt hwcfg hwcoord hwinit hweep hwdma cacheops
    rt_stubs
)

# HOT SECTION - C (resident after init, ROOT segment)
# Phase 6: 15 individual *_rt files consolidated into rt_stubs
HOT_C_OBJS=(
    api routing pci_shim pcimux dmamap dmabnd
    hwchksm dos_idle irq_bind rtcfg irqmit rxbatch txlazy
    init_main xms_core pktops_c linkstubs
    dos_io
)

# JIT Module ASM Objects (Phase 7 - linked into EXE, hot sections copied at init)
MODULE_ASM_OBJS=(
    mod_isr mod_irq mod_pktbuf mod_data
    mod_3c509b_rt mod_3c515_rt mod_vortex_rt mod_boom_rt
    mod_cyclone_rt mod_tornado_rt
    mod_pio mod_dma_isa mod_dma_busmaster mod_dma_descring mod_dma_bounce
    mod_cache_none mod_cache_wbinvd mod_cache_clflush mod_cache_snoop
    mod_copy_8086 mod_copy_286 mod_copy_386 mod_copy_pent
)

# JIT Engine C Objects (Phase 7 - OVERLAY, discarded after init)
JIT_C_OBJS=(
    mod_select jit_build jit_patch jit_reloc
)

# COLD SECTION - Assembly (discarded after init)
COLD_ASM_OBJS=(
    cpudet pnp promisc smcpat safestub quiesce
    hwdet hwbus pcipwr
)

# COLD SECTION - C (discarded after init)
COLD_C_OBJS=(
    main init config pcmmgr pcmsnap flowctl pcmpebe pcmssbe
    memory xmsdet umbldr eeprom bufaloc bufauto statrt arp
    nic_init hardware hwstubs 3c515 3c509b entval pltprob
    dmacap tsrmgr dmatest dmasafe vds_core vdssafe vdsmgr
    extapi unwind chipdet bmtest pci_bios 3cpcidet 3cvortex
    3cboom pciintg pcishme smcserl cachemgt dmapol vds
    hardware_init 3c509b_init 3c515_init
    api_init dmabnd_init dmamap_init pci_shim_init pcimux_init
    hwchksm_init irqmit_init rxbatch_init txlazy_init
    xms_core_init pktops_init logging_init
    pcirst smcpat_c
)

# Debug-only objects
DEBUG_C_OBJS=(diag logging stats)

# Loader C files
LOADER_C_OBJS=(cpudet patch_apply init_stubs)

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

check_tools() {
    local missing=0

    if [ ! -x "$CC" ]; then
        echo -e "${RED}Error: Open Watcom C compiler not found at $CC${NC}"
        echo "Set WATCOM environment variable or edit this script"
        missing=1
    fi

    if [ ! -x "$LINK" ]; then
        echo -e "${RED}Error: Open Watcom linker not found at $LINK${NC}"
        missing=1
    fi

    if ! command -v "$ASM" &> /dev/null; then
        echo -e "${RED}Error: NASM assembler not found${NC}"
        echo "Install with: brew install nasm"
        missing=1
    fi

    if [ $missing -eq 1 ]; then
        exit 1
    fi

    echo -e "${GREEN}Build tools verified:${NC}"
    echo "  CC:   $CC"
    echo "  LINK: $LINK"
    echo "  ASM:  $ASM ($(nasm -v 2>&1 | head -1))"
}

compile_c() {
    local src="$1"
    local obj="$2"
    local flags="$3"
    local name=$(basename "$src" .c)

    echo -n "  [CC] $name.c... "
    if $CC $flags "$src" -fo="$obj" 2>/dev/null; then
        echo -e "${GREEN}OK${NC}"
        return 0
    else
        echo -e "${RED}FAILED${NC}"
        # Re-run to show errors
        $CC $flags "$src" -fo="$obj" 2>&1 | head -20
        return 1
    fi
}

compile_asm() {
    local src="$1"
    local obj="$2"
    local flags="$3"
    local name=$(basename "$src" .asm)

    # All files use NASM (unified toolchain with local label support)
    echo -n "  [NASM] $name.asm... "
    if $ASM $flags "$src" -o "$obj" 2>/dev/null; then
        echo -e "${GREEN}OK${NC}"
        return 0
    else
        echo -e "${RED}FAILED${NC}"
        $ASM $flags "$src" -o "$obj" 2>&1 | head -20
        return 1
    fi
}

# =============================================================================
# BUILD FUNCTIONS
# =============================================================================

build_driver() {
    local mode="$1"
    local cflags="$2"
    local aflags="$3"
    local include_debug="$4"

    echo -e "${YELLOW}Building 3Com Packet Driver ($mode)...${NC}"
    echo ""

    # Create build directory
    mkdir -p "$BUILDDIR"
    mkdir -p "$BUILDDIR/loader"

    local all_objs=""
    local failed=0

    # --- Compile Loader ASM ---
    echo -e "${BLUE}Compiling loader assembly...${NC}"
    if [ -f "$ASMDIR/$LOADER_OBJ.asm" ]; then
        if ! compile_asm "$ASMDIR/$LOADER_OBJ.asm" "$BUILDDIR/$LOADER_OBJ.obj" "$aflags"; then
            failed=1
        fi
        all_objs="$all_objs $BUILDDIR/$LOADER_OBJ.obj"
    fi

    # --- Compile HOT ASM ---
    echo -e "${BLUE}Compiling hot assembly (resident)...${NC}"
    for name in "${HOT_ASM_OBJS[@]}"; do
        if [ -f "$ASMDIR/$name.asm" ]; then
            if ! compile_asm "$ASMDIR/$name.asm" "$BUILDDIR/$name.obj" "$aflags"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/$name.obj"
        fi
    done

    # --- Compile HOT C (ROOT segment) ---
    echo -e "${BLUE}Compiling hot C (resident)...${NC}"
    for name in "${HOT_C_OBJS[@]}"; do
        if [ -f "$CDIR/$name.c" ]; then
            if ! compile_c "$CDIR/$name.c" "$BUILDDIR/$name.obj" "$cflags $CFLAGS_ROOT"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/$name.obj"
        fi
    done

    # --- Compile JIT Module ASM (Phase 7) ---
    echo -e "${BLUE}Compiling JIT module assembly...${NC}"
    for name in "${MODULE_ASM_OBJS[@]}"; do
        if [ -f "$ASMDIR/$name.asm" ]; then
            if ! compile_asm "$ASMDIR/$name.asm" "$BUILDDIR/$name.obj" "$aflags"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/$name.obj"
        fi
    done

    # --- Compile JIT Engine C (Phase 7 - OVERLAY) ---
    echo -e "${BLUE}Compiling JIT engine C (overlay)...${NC}"
    for name in "${JIT_C_OBJS[@]}"; do
        if [ -f "$CDIR/$name.c" ]; then
            if ! compile_c "$CDIR/$name.c" "$BUILDDIR/$name.obj" "$cflags $CFLAGS_COLD"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/$name.obj"
        fi
    done

    # --- Compile COLD ASM ---
    echo -e "${BLUE}Compiling cold assembly (init-only)...${NC}"
    for name in "${COLD_ASM_OBJS[@]}"; do
        if [ -f "$ASMDIR/$name.asm" ]; then
            if ! compile_asm "$ASMDIR/$name.asm" "$BUILDDIR/$name.obj" "$aflags"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/$name.obj"
        fi
    done

    # --- Compile COLD C ---
    echo -e "${BLUE}Compiling cold C (init-only)...${NC}"
    for name in "${COLD_C_OBJS[@]}"; do
        if [ -f "$CDIR/$name.c" ]; then
            if ! compile_c "$CDIR/$name.c" "$BUILDDIR/$name.obj" "$cflags $CFLAGS_COLD"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/$name.obj"
        fi
    done

    # --- Compile LOADER C ---
    echo -e "${BLUE}Compiling loader C...${NC}"
    for name in "${LOADER_C_OBJS[@]}"; do
        local loader_src=""
        if [ -f "$LOADERDIR/$name.c" ]; then
            loader_src="$LOADERDIR/$name.c"
        elif [ -f "$CDIR/$name.c" ]; then
            loader_src="$CDIR/$name.c"
        fi
        if [ -n "$loader_src" ]; then
            if ! compile_c "$loader_src" "$BUILDDIR/loader/$name.obj" "$cflags $CFLAGS_COLD"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/loader/$name.obj"
        fi
    done

    # --- Compile DEBUG C (if enabled) ---
    if [ "$include_debug" = "1" ]; then
        echo -e "${BLUE}Compiling debug modules...${NC}"
        for name in "${DEBUG_C_OBJS[@]}"; do
            if [ -f "$CDIR/$name.c" ]; then
                if ! compile_c "$CDIR/$name.c" "$BUILDDIR/$name.obj" "$cflags"; then
                    failed=1
                fi
                all_objs="$all_objs $BUILDDIR/$name.obj"
            fi
        done
    fi

    # --- Check for failures ---
    if [ $failed -eq 1 ]; then
        echo ""
        echo -e "${RED}Build failed due to compilation errors${NC}"
        return 1
    fi

    # --- Link ---
    echo ""
    echo -e "${BLUE}Linking (overlay system via 3cpd.lnk)...${NC}"
    echo -n "  [LINK] 3cpd.exe... "

    # Use the overlay linker directive file which defines ROOT + OVERLAY sections
    if $LINK @3cpd.lnk 2>/dev/null; then
        echo -e "${GREEN}OK${NC}"
    else
        echo -e "${RED}FAILED${NC}"
        # Re-run to show errors
        $LINK @3cpd.lnk 2>&1 | tail -60
        return 1
    fi

    # --- Report ---
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build complete: $TARGET${NC}"
    if [ -f "$TARGET" ]; then
        local size=$(stat -f%z "$TARGET" 2>/dev/null || stat -c%s "$TARGET" 2>/dev/null)
        echo "Binary size: $size bytes ($(expr $size / 1024) KB)"
    fi
    echo "Map file: $BUILDDIR/3cpd.map"
    echo -e "${GREEN}========================================${NC}"
}

do_clean() {
    echo -e "${YELLOW}Cleaning build artifacts...${NC}"
    rm -rf "$BUILDDIR"
    echo -e "${GREEN}Clean complete${NC}"
}

show_info() {
    echo "=== 3Com Packet Driver - macOS Build System ==="
    echo ""
    echo "Configuration:"
    echo "  WATCOM:    $WATCOM"
    echo "  CC:        $CC"
    echo "  LINK:      $LINK"
    echo "  ASM:       $ASM"
    echo "  Target:    $TARGET"
    echo ""
    echo "Build Targets:"
    echo "  ./build_macos.sh release     - Release build (default)"
    echo "  ./build_macos.sh debug       - Debug build with symbols"
    echo "  ./build_macos.sh production  - Size-optimized production build"
    echo "  ./build_macos.sh clean       - Remove build artifacts"
    echo "  ./build_macos.sh info        - Show this information"
    echo ""
    echo "Source Files:"
    echo "  C files:   $(ls $CDIR/*.c 2>/dev/null | wc -l | tr -d ' ')"
    echo "  ASM files: $(ls $ASMDIR/*.asm 2>/dev/null | wc -l | tr -d ' ')"
    echo "  Headers:   $(ls $INCDIR/*.h $INCDIR/*.inc 2>/dev/null | wc -l | tr -d ' ')"
}

# =============================================================================
# MAIN
# =============================================================================

cd "$(dirname "$0")"

case "${1:-release}" in
    release)
        check_tools
        build_driver "RELEASE" "$CFLAGS_RELEASE" "$AFLAGS_RELEASE" "1"
        ;;
    debug)
        check_tools
        build_driver "DEBUG" "$CFLAGS_DEBUG" "$AFLAGS_DEBUG" "1"
        ;;
    production)
        check_tools
        build_driver "PRODUCTION" "$CFLAGS_PRODUCTION" "$AFLAGS_RELEASE" "0"
        ;;
    clean)
        do_clean
        ;;
    info)
        show_info
        ;;
    *)
        echo "Usage: $0 [release|debug|production|clean|info]"
        exit 1
        ;;
esac
