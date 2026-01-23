#!/bin/bash
#
# build_macos.sh - Build 3Com Packet Driver on macOS with Open Watcom + NASM
#
# Part of 3Com DOS Packet Driver Project
# Last Updated: 2026-01-23 18:33:40 CET
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

# Watcom Assembler (MASM-compatible)
ASM="$WATCOM/armo64/wasm"

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
# -ms: Small memory model
# -s: No stack checking
# -zp1: Pack structures on 1-byte boundary
# -zdf: DS != DGROUP (for TSR)
# -zu: SS != DGROUP (for TSR)
CFLAGS_COMMON="-zq -ms -s -zp1 -zdf -zu -I$INCDIR"

# Debug flags
CFLAGS_DEBUG="$CFLAGS_COMMON -0 -d2"

# Release flags
# -os: Optimize for space
# -ot: Optimize for time
CFLAGS_RELEASE="$CFLAGS_COMMON -os -ot -d0"

# Production flags (maximum size optimization)
CFLAGS_PRODUCTION="$CFLAGS_COMMON -os -d0 -oe=100 -ol+ -ox -DPRODUCTION -DNO_LOGGING -DNO_STATS -DNDEBUG"

# WASM flags (Watcom Assembler - MASM compatible)
# -zq: Quiet mode
# -I: Include path
# Note: Don't specify memory model/CPU - let each file define its own
AFLAGS_COMMON="-zq -I$INCDIR"
AFLAGS_DEBUG="$AFLAGS_COMMON -d1"
AFLAGS_RELEASE="$AFLAGS_COMMON"

# All assembly files now use WASM/MASM syntax (unified toolchain)
# NASM support removed - all files converted to WASM format

# Linker flags
LFLAGS_COMMON="system dos option caseexact, quiet, stack=1024"

# =============================================================================
# OBJECT FILE LISTS
# =============================================================================

# HOT SECTION - Assembly (resident after init)
HOT_ASM_OBJS=(
    pktapi nicirq hwsmc pcmisr flowrt dirpio
    pktops pktcopy tsrcom tsrwrap pci_io pciisr
)

# Loader (contains both hot and cold)
LOADER_OBJ="tsrldr"

# HOT SECTION - C (resident after init)
HOT_C_OBJS=(
    api routing pci_shim pcimux dmamap dmabnd
    hwchksm dos_idle irq_bind rtcfg irqmit rxbatch txlazy
)

# COLD SECTION - Assembly (discarded after init)
COLD_ASM_OBJS=(
    cpudet pnp promisc smcpat safestub quiesce
)

# COLD SECTION - C (discarded after init)
COLD_C_OBJS=(
    main init config pcmmgr pcmsnap flowctl pcmpebe pcmssbe
    memory xmsdet umbldr eeprom bufaloc bufauto statrt arp
    nic_init hardware hwstubs 3c515 3c509b entval pltprob
    dmacap tsrmgr dmatest dmasafe vds_core vdssafe vdsmgr
    extapi unwind chipdet bmtest pci_bios 3cpcidet 3cvortex
    3cboom pciintg pcishme smc_safety_patches smcserl cachemgt
    dmapol vds
)

# Debug-only objects
DEBUG_C_OBJS=(diag logging stats)

# Loader C files
LOADER_C_OBJS=(cpudet patch_apply)

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

    if [ ! -x "$ASM" ]; then
        echo -e "${RED}Error: WASM assembler not found at $ASM${NC}"
        missing=1
    fi

    if [ $missing -eq 1 ]; then
        exit 1
    fi

    echo -e "${GREEN}Build tools verified:${NC}"
    echo "  CC:   $CC"
    echo "  LINK: $LINK"
    echo "  ASM:  $ASM"
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

    # All files use WASM (unified MASM-compatible toolchain)
    echo -n "  [WASM] $name.asm... "
    if $ASM $flags "$src" -fo="$obj" 2>/dev/null; then
        echo -e "${GREEN}OK${NC}"
        return 0
    else
        echo -e "${RED}FAILED${NC}"
        $ASM $flags "$src" -fo="$obj" 2>&1 | head -20
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

    # --- Compile HOT C ---
    echo -e "${BLUE}Compiling hot C (resident)...${NC}"
    for name in "${HOT_C_OBJS[@]}"; do
        if [ -f "$CDIR/$name.c" ]; then
            if ! compile_c "$CDIR/$name.c" "$BUILDDIR/$name.obj" "$cflags"; then
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
            if ! compile_c "$CDIR/$name.c" "$BUILDDIR/$name.obj" "$cflags"; then
                failed=1
            fi
            all_objs="$all_objs $BUILDDIR/$name.obj"
        fi
    done

    # --- Compile LOADER C ---
    echo -e "${BLUE}Compiling loader C...${NC}"
    for name in "${LOADER_C_OBJS[@]}"; do
        if [ -f "$LOADERDIR/$name.c" ]; then
            if ! compile_c "$LOADERDIR/$name.c" "$BUILDDIR/loader/$name.obj" "$cflags -DCOLD_SECTION"; then
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
    echo -e "${BLUE}Linking...${NC}"
    echo -n "  [LINK] 3cpd.exe... "

    # Build file list for wlink
    local file_list=$(echo $all_objs | tr ' ' ',')

    if $LINK $LFLAGS_COMMON \
        option map="$BUILDDIR/3cpd.map" \
        libpath "$WATCOM/lib286/dos" \
        libpath "$WATCOM/lib286" \
        library clibs.lib \
        name "$TARGET" \
        file { $all_objs } 2>/dev/null; then
        echo -e "${GREEN}OK${NC}"
    else
        echo -e "${RED}FAILED${NC}"
        # Re-run to show errors
        $LINK $LFLAGS_COMMON \
            option map="$BUILDDIR/3cpd.map" \
            libpath "$WATCOM/lib286/dos" \
            libpath "$WATCOM/lib286" \
            library clibs.lib \
            name "$TARGET" \
            file { $all_objs }
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
