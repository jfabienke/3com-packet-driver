#!/bin/bash
# Generate dependency graphs for 3Com Packet Driver
# Usage: ./generate_dependency_graph.sh [output_dir]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
OUTPUT_DIR="${1:-$PROJECT_ROOT/analysis/graphs}"

echo "=== 3Com Packet Driver Dependency Graph Generator ==="
echo "Project root: $PROJECT_ROOT"
echo "Output directory: $OUTPUT_DIR"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check for required tools
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "Error: $1 is not installed. Please install it first."
        exit 1
    fi
}

echo "Checking for required tools..."
check_tool "dot"
echo "✓ Graphviz (dot) found"

# Function to create DOT graph
create_dot_graph() {
    local name="$1"
    local title="$2"
    local color="$3"
    local files="$4"
    
    cat > "$OUTPUT_DIR/${name}.dot" << EOF
digraph ${name} {
    rankdir=LR;
    node [shape=box, style=filled, fillcolor=${color}];
    
    label="${title}";
    labelloc=t;
    fontsize=16;
    fontname="Arial Bold";
    
    subgraph cluster_c {
        label="C Files";
        color=blue;
        style=dashed;
EOF
    
    # Add C files
    echo "$files" | grep '\.c$' | while read -r file; do
        if [[ -n "$file" ]]; then
            basename_file=$(basename "$file" .c)
            echo "        \"${basename_file}_c\" [label=\"$basename_file.c\"];" >> "$OUTPUT_DIR/${name}.dot"
        fi
    done
    
    cat >> "$OUTPUT_DIR/${name}.dot" << EOF
    }
    
    subgraph cluster_asm {
        label="Assembly Files";
        color=red;
        style=dashed;
EOF
    
    # Add ASM files
    echo "$files" | grep '\.asm$' | while read -r file; do
        if [[ -n "$file" ]]; then
            basename_file=$(basename "$file" .asm)
            echo "        \"${basename_file}_asm\" [label=\"$basename_file.asm\"];" >> "$OUTPUT_DIR/${name}.dot"
        fi
    done
    
    cat >> "$OUTPUT_DIR/${name}.dot" << EOF
    }
    
    // Dependencies would be added here based on analysis
    // For now, this is a structural view
}
EOF
}

# Generate live code graph
echo "Generating live code dependency graph..."
LIVE_FILES=$(cat << 'EOF'
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
)

create_dot_graph "live_code" "Live Code Dependencies" "lightgreen" "$LIVE_FILES"

# Generate orphaned PCI code graph
echo "Generating PCI/Vortex orphaned code graph..."
PCI_FILES=$(cat << 'EOF'
src/c/3com_boomerang.c
src/c/3com_vortex.c
src/c/3com_vortex_init.c
src/c/3com_init.c
src/c/3com_performance.c
src/c/3com_power.c
src/c/3com_windows.c
src/asm/3com_smc_opt.asm
src/asm/enhanced_hardware.asm
src/asm/enhanced_irq.asm
src/asm/enhanced_pnp.asm
EOF
)

create_dot_graph "pci_orphaned" "Orphaned PCI/Vortex Code" "lightcoral" "$PCI_FILES"

# Generate DMA safety code graph
echo "Generating DMA safety code graph..."
DMA_FILES=$(cat << 'EOF'
src/c/dma_safety.c
src/c/dma_mapping.c
src/c/dma_boundary.c
src/c/cache_coherency.c
src/c/cache_coherency_enhanced.c
src/c/cache_management.c
src/c/dma_self_test.c
src/c/vds_mapping.c
src/asm/cache_coherency_asm.asm
src/asm/cache_ops.asm
EOF
)

create_dot_graph "dma_safety" "DMA Safety Infrastructure" "lightyellow" "$DMA_FILES"

# Generate comprehensive overview
echo "Generating comprehensive overview..."
cat > "$OUTPUT_DIR/overview.dot" << 'EOF'
digraph overview {
    rankdir=TB;
    node [shape=box, style=filled];
    
    label="3Com Packet Driver Architecture Overview";
    labelloc=t;
    fontsize=18;
    fontname="Arial Bold";
    
    subgraph cluster_live {
        label="Live Code (33 files)";
        color=green;
        style=filled;
        fillcolor=lightgreen;
        
        "Hot Section" [fillcolor=red];
        "Cold Section" [fillcolor=orange];
        "Debug Code" [fillcolor=yellow];
    }
    
    subgraph cluster_orphaned {
        label="Orphaned Code (125 files)";
        color=red;
        style=filled;
        fillcolor=lightcoral;
        
        "PCI/Vortex" [fillcolor=pink];
        "DMA Safety" [fillcolor=lightyellow];
        "Test Code" [fillcolor=lightgray];
        "Modules" [fillcolor=lavender];
    }
    
    subgraph cluster_deletion {
        label="Deletion Candidates";
        color=darkred;
        style=dashed;
        
        "Safe to Delete" [fillcolor=lightcoral];
        "Archive" [fillcolor=lightblue];
        "DO NOT DELETE" [fillcolor=red, fontcolor=white];
    }
    
    // Dependencies
    "Hot Section" -> "Cold Section" [label="initialization"];
    "Cold Section" -> "DMA Safety" [label="safety features", style=dashed];
    "Live Code (33 files)" -> "Orphaned Code (125 files)" [label="79% orphaned", color=red];
}
EOF

# Generate all PNG files
echo "Converting DOT files to PNG..."
for dot_file in "$OUTPUT_DIR"/*.dot; do
    if [[ -f "$dot_file" ]]; then
        base_name=$(basename "$dot_file" .dot)
        echo "  Converting $base_name.dot -> $base_name.png"
        dot -Tpng "$dot_file" -o "$OUTPUT_DIR/${base_name}.png"
    fi
done

# Generate summary report
cat > "$OUTPUT_DIR/README.md" << 'EOF'
# Dependency Graph Analysis

This directory contains dependency graphs for the 3Com Packet Driver codebase.

## Generated Graphs

### Structure Graphs
- `overview.png` - High-level architecture overview
- `live_code.png` - Active code dependencies (33 files)
- `pci_orphaned.png` - Orphaned PCI/Vortex code (11 files)
- `dma_safety.png` - DMA safety infrastructure (10 files)

### DOT Source Files
- `*.dot` - Graphviz source files for each graph

## Usage

1. **View graphs**: Open PNG files in any image viewer
2. **Modify graphs**: Edit DOT files and regenerate with `dot -Tpng file.dot -o file.png`
3. **Add dependencies**: Edit DOT files to add specific function call relationships

## Tools Required

- **Graphviz**: For DOT file processing (`brew install graphviz` or `apt-get install graphviz`)

## Regeneration

Run `./generate_dependency_graph.sh` from the scripts directory to regenerate all graphs.
EOF

echo ""
echo "=== Dependency Graph Generation Complete ==="
echo "Generated files in: $OUTPUT_DIR"
echo ""
echo "Files created:"
ls -la "$OUTPUT_DIR"
echo ""
echo "To view graphs:"
echo "  open $OUTPUT_DIR/overview.png"
echo "  open $OUTPUT_DIR/live_code.png"
echo ""