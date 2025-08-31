#!/bin/bash
# Generate cscope databases for 3Com Packet Driver code analysis
# Usage: ./generate_cscope.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
ANALYSIS_DIR="$PROJECT_ROOT/analysis"

echo "=== 3Com Packet Driver CScope Database Generator ==="
echo "Project root: $PROJECT_ROOT"
echo "Analysis directory: $ANALYSIS_DIR"

# Check for cscope
if ! command -v cscope &> /dev/null; then
    echo "Warning: cscope is not installed. Install with:"
    echo "  macOS: brew install cscope"
    echo "  Ubuntu: sudo apt-get install cscope"
    echo ""
    echo "Continuing without cscope - file lists will still be generated..."
fi

# Create analysis subdirectories
mkdir -p "$ANALYSIS_DIR/databases/live"
mkdir -p "$ANALYSIS_DIR/databases/orphaned"
mkdir -p "$ANALYSIS_DIR/databases/dma_safety"
mkdir -p "$ANALYSIS_DIR/databases/all"

cd "$PROJECT_ROOT"

# Generate file list for live code
echo "Generating live code file list..."
cat > "$ANALYSIS_DIR/databases/live/cscope.files" << 'EOF'
src/c/api.c
src/c/routing.c
src/c/packet_ops.c
src/c/init.c
src/c/config.c
src/c/memory.c
src/c/xms_detect.c
src/c/umb_loader.c
src/c/eeprom.c
src/c/buffer_alloc.c
src/c/buffer_autoconfig.c
src/c/static_routing.c
src/c/arp.c
src/c/nic_init.c
src/c/hardware.c
src/c/3c515.c
src/c/3c509b.c
src/c/diagnostics.c
src/c/logging.c
src/c/stats.c
src/loader/cpu_detect.c
src/loader/patch_apply.c
src/asm/packet_api_smc.asm
src/asm/nic_irq_smc.asm
src/asm/hardware_smc.asm
src/asm/flow_routing.asm
src/asm/direct_pio.asm
src/asm/tsr_common.asm
src/asm/tsr_loader.asm
src/asm/cpu_detect.asm
src/asm/pnp.asm
src/asm/promisc.asm
src/asm/smc_patches.asm
EOF

# Generate file list for DMA safety (critical orphaned code)
echo "Generating DMA safety code file list..."
cat > "$ANALYSIS_DIR/databases/dma_safety/cscope.files" << 'EOF'
src/c/dma_safety.c
src/c/dma_mapping.c
src/c/dma_boundary.c
src/c/cache_coherency.c
src/c/cache_coherency_enhanced.c
src/c/cache_management.c
src/c/dma_self_test.c
src/c/vds_mapping.c
src/c/busmaster_test.c
src/c/performance_enabler.c
src/asm/cache_coherency_asm.asm
src/asm/cache_ops.asm
EOF

# Generate file list for orphaned code
echo "Generating orphaned code file list..."
cat > "$ANALYSIS_DIR/databases/orphaned/cscope.files" << 'EOF'
src/c/3com_boomerang.c
src/c/3com_vortex.c
src/c/3com_vortex_init.c
src/c/3com_init.c
src/c/3com_performance.c
src/c/3com_power.c
src/c/3com_windows.c
src/c/3c515_enhanced.c
src/c/3c509b_pio.c
src/c/ansi_demo.c
src/c/console.c
src/c/dma_mapping_test.c
src/c/performance_monitor.c
src/c/multi_nic_coord.c
src/c/runtime_config.c
src/c/api.c.bak
src/c/config.c.bak
src/c/packet_ops.c.bak
src/asm/3com_smc_opt.asm
src/asm/enhanced_hardware.asm
src/asm/enhanced_irq.asm
src/asm/enhanced_pnp.asm
src/asm/api.asm
src/asm/hardware.asm
src/asm/packet_api.asm
src/asm/packet_ops.asm
src/asm/nic_irq.asm
src/asm/routing.asm
src/asm/main.asm
EOF

# Generate combined file list
echo "Generating combined file list..."
cat "$ANALYSIS_DIR/databases/live/cscope.files" > "$ANALYSIS_DIR/databases/all/cscope.files"
cat "$ANALYSIS_DIR/databases/orphaned/cscope.files" >> "$ANALYSIS_DIR/databases/all/cscope.files"
cat "$ANALYSIS_DIR/databases/dma_safety/cscope.files" >> "$ANALYSIS_DIR/databases/all/cscope.files"

# Build cscope databases if available
if command -v cscope &> /dev/null; then
    echo "Building cscope databases..."
    
    echo "  Building live code database..."
    cd "$ANALYSIS_DIR/databases/live"
    cscope -b -q -i cscope.files
    
    echo "  Building DMA safety database..."
    cd "$ANALYSIS_DIR/databases/dma_safety"
    cscope -b -q -i cscope.files
    
    echo "  Building orphaned code database..."
    cd "$ANALYSIS_DIR/databases/orphaned"
    cscope -b -q -i cscope.files
    
    echo "  Building combined database..."
    cd "$ANALYSIS_DIR/databases/all"
    cscope -b -q -i cscope.files
    
    echo "✓ All cscope databases built successfully"
else
    echo "⚠ Cscope not available - only file lists generated"
fi

echo ""
echo "=== CScope Database Generation Complete ==="
echo ""
echo "Databases created:"
echo "  Live code:     $(wc -l < "$ANALYSIS_DIR/databases/live/cscope.files") files"
echo "  DMA safety:    $(wc -l < "$ANALYSIS_DIR/databases/dma_safety/cscope.files") files"
echo "  Orphaned:      $(wc -l < "$ANALYSIS_DIR/databases/orphaned/cscope.files") files"
echo "  Combined:      $(wc -l < "$ANALYSIS_DIR/databases/all/cscope.files") files"
echo ""