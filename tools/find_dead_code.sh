#!/bin/bash
# find_dead_code.sh - Identify potentially unused functions in the codebase
# Week 1 Optimization - Dead Code Detection

echo "=== Dead Code Analysis for 3Com Packet Driver ==="
echo "Date: $(date)"
echo ""

# Find all static functions that are only declared but never called
echo "=== Potentially Unused Static Functions ==="
echo "--------------------------------------------"

for file in src/c/*.c; do
    if [ ! -f "$file" ]; then continue; fi
    
    # Find all static function definitions
    grep -n "^static.*(" "$file" 2>/dev/null | while IFS=: read -r line_num line; do
        # Extract function name
        func_name=$(echo "$line" | sed -n 's/.*static.*\s\+\([a-zA-Z_][a-zA-Z0-9_]*\)\s*(.*/\1/p')
        
        if [ -z "$func_name" ]; then continue; fi
        
        # Count occurrences of this function in the file
        count=$(grep -c "\b${func_name}\b" "$file")
        
        # If only appears once (the definition), it's likely unused
        if [ "$count" -eq 1 ]; then
            basename=$(basename "$file")
            echo "$basename:$line_num: static $func_name() - UNUSED"
        fi
    done
done

echo ""
echo "=== Global Functions Never Referenced ==="
echo "------------------------------------------"

# Find all non-static function definitions
for file in src/c/*.c; do
    if [ ! -f "$file" ]; then continue; fi
    
    grep -n "^[a-zA-Z].*(" "$file" 2>/dev/null | \
    grep -v "^static" | \
    grep -v "^#" | \
    grep -v "//" | \
    grep -v "/\*" | while IFS=: read -r line_num line; do
        # Extract function name
        func_name=$(echo "$line" | sed -n 's/.*\s\+\([a-zA-Z_][a-zA-Z0-9_]*\)\s*(.*/\1/p')
        
        if [ -z "$func_name" ]; then continue; fi
        if [ "$func_name" = "if" ] || [ "$func_name" = "for" ] || [ "$func_name" = "while" ]; then continue; fi
        
        # Check if this function is called anywhere else in the codebase
        count=$(grep -r "\b${func_name}\b" src/ 2>/dev/null | grep -v "\.o:" | wc -l)
        
        # If only appears once or twice (definition + prototype), likely unused
        if [ "$count" -le 2 ]; then
            basename=$(basename "$file")
            echo "$basename:$line_num: $func_name() - POTENTIALLY UNUSED (refs: $count)"
        fi
    done
done | head -20  # Limit output

echo ""
echo "=== Unused Header Includes ==="
echo "-------------------------------"

# Check for potentially unnecessary includes
for file in src/c/*.c; do
    if [ ! -f "$file" ]; then continue; fi
    basename=$(basename "$file")
    
    # Count includes
    include_count=$(grep -c "^#include" "$file")
    if [ "$include_count" -gt 10 ]; then
        echo "$basename: $include_count includes (may have redundant)"
    fi
done

echo ""
echo "=== Large Functions (Optimization Targets) ==="
echo "----------------------------------------------"

# Find functions larger than 100 lines
for file in src/c/*.c; do
    if [ ! -f "$file" ]; then continue; fi
    basename=$(basename "$file")
    
    awk '/^[a-zA-Z].*\(.*\).*{/ {
        start = NR
        name = $0
        gsub(/.*\s+/, "", name)
        gsub(/\(.*/, "", name)
    }
    /^}$/ {
        if (start > 0) {
            lines = NR - start
            if (lines > 100) {
                printf "%s:%d: %s() - %d lines\n", FILENAME, start, name, lines
            }
            start = 0
        }
    }' "$file" | while read line; do
        echo "$line" | sed "s|src/c/||"
    done
done | sort -t: -k4 -rn | head -10

echo ""
echo "=== Debug/Test Code to Remove ==="
echo "----------------------------------"

# Find test and debug files
for pattern in "test_" "debug_" "demo_" "mock_"; do
    count=$(find src/ -name "${pattern}*.c" 2>/dev/null | wc -l)
    if [ "$count" -gt 0 ]; then
        echo "Found $count files matching '${pattern}*':"
        find src/ -name "${pattern}*.c" 2>/dev/null | while read file; do
            size=$(wc -c < "$file")
            echo "  $(basename "$file") - $size bytes"
        done
    fi
done

echo ""
echo "=== Duplicate String Literals ==="
echo "---------------------------------"

# Find duplicate string literals (basic check)
temp_file="/tmp/strings_$$.txt"
for file in src/c/*.c; do
    if [ ! -f "$file" ]; then continue; fi
    grep -o '"[^"]\{10,\}"' "$file" 2>/dev/null >> "$temp_file"
done

if [ -f "$temp_file" ]; then
    sort "$temp_file" | uniq -c | sort -rn | head -10 | while read count str; do
        if [ "$count" -gt 1 ]; then
            echo "$count occurrences: $str"
        fi
    done
    rm -f "$temp_file"
fi

echo ""
echo "=== Summary ==="
echo "---------------"
echo "Total C files: $(find src/c -name "*.c" | wc -l)"
echo "Total lines of C code: $(wc -l src/c/*.c 2>/dev/null | tail -1 | awk '{print $1}')"
echo "Total assembly files: $(find src/asm -name "*.asm" | wc -l)"
echo "Total lines of assembly: $(wc -l src/asm/*.asm 2>/dev/null | tail -1 | awk '{print $1}')"

echo ""
echo "=== Recommendations ==="
echo "----------------------"
echo "1. Remove unused static functions (immediate ~2KB savings)"
echo "2. Eliminate debug/test files from production build (~4KB)"
echo "3. Consolidate duplicate strings into string table (~1KB)"
echo "4. Remove unnecessary #includes to reduce compile time"
echo "5. Consider splitting large functions for better optimization"
echo ""
echo "Note: Without compiler, these are estimates based on typical overhead"