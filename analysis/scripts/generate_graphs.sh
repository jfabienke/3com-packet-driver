#!/bin/bash
# Generate all dependency graphs and documentation

PROJECT_ROOT="/Users/jvindahl/Development/3com-packet-driver"
ANALYSIS_DIR="${PROJECT_ROOT}/analysis"
REPORTS_DIR="${ANALYSIS_DIR}/reports"
GRAPHS_DIR="${ANALYSIS_DIR}/graphs"

echo "Starting comprehensive dependency analysis..."

# Create output directories
mkdir -p "${REPORTS_DIR}" "${GRAPHS_DIR}"

# Run Python dependency analysis
echo "Running dependency analysis script..."
cd "${ANALYSIS_DIR}/scripts"
python3 analyze_dependencies.py "${PROJECT_ROOT}"

# Generate DOT graphs as PNG images
echo "Converting DOT files to PNG..."
cd "${REPORTS_DIR}"
for dot_file in *.dot; do
    if [ -f "$dot_file" ]; then
        basename=$(basename "$dot_file" .dot)
        dot -Tpng "$dot_file" -o "${GRAPHS_DIR}/${basename}.png"
        echo "Generated ${basename}.png"
    fi
done

# Generate Doxygen documentation
echo "Generating Doxygen documentation..."
cd "${ANALYSIS_DIR}/doxygen"

echo "  - Live driver documentation..."
doxygen Doxyfile_live > /dev/null 2>&1

echo "  - Integration features documentation..."
doxygen Doxyfile_integration > /dev/null 2>&1

echo "  - PCI variants documentation..."
doxygen Doxyfile_pci > /dev/null 2>&1

echo "Analysis complete! Results available in:"
echo "  - Reports: ${REPORTS_DIR}/"
echo "  - Graphs: ${GRAPHS_DIR}/"
echo "  - Documentation: ${ANALYSIS_DIR}/doxygen/"