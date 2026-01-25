#!/bin/bash
export WATCOM="/Users/johnfabienke/Development/macos-open-watcom/open-watcom-v2/rel"
export PATH="$WATCOM/armo64:$PATH"
export INCLUDE="$WATCOM/h"

rm -rf build
mkdir -p build

CFLAGS="-zq -ms -s -os -ot -zp1 -zdf -zu -Iinclude -fr=build/ -wcd=201 -d0"

echo "=== Compiling C files ==="
for f in src/c/*.c; do
    echo "Compiling: $f"
    wcc $CFLAGS -fo="build/$(basename $f .c).obj" "$f" 2>&1 | grep -E "Error|error:|E[0-9]{4}:" | head -5
done

echo ""
echo "=== Build summary ==="
ls -la build/*.obj 2>/dev/null | wc -l | xargs echo "Object files created:"
