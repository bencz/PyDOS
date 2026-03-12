#!/bin/bash
# Test runner for PyDOS compiler on macOS
# Builds the compiler, then compiles each .py test file to assembly.
# Usage: tests/run_mac.sh [--no-pir-opt]

set -e

EXTRA_FLAGS="$1"

cd "$(dirname "$0")/.."

echo "=== Building compiler ==="
make -f Makefile.mac compiler

echo ""
if [ -n "$EXTRA_FLAGS" ]; then
    echo "=== Running tests ($EXTRA_FLAGS) ==="
else
    echo "=== Running tests ==="
fi

PASS=0
FAIL=0
TOTAL=0

for pyfile in tests/*.py; do
    name=$(basename "$pyfile" .py)
    TOTAL=$((TOTAL + 1))
    outfile="build/${name}.asm"
    test_flags=""
    if [ -f "tests/${name}.flags" ]; then
        test_flags=$(cat "tests/${name}.flags")
    fi
    if bin/pydos "$pyfile" -o "$outfile" --search-path tests $EXTRA_FLAGS $test_flags 2>/dev/null; then
        echo "  PASS  $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $name"
        bin/pydos "$pyfile" -o "$outfile" --search-path tests $EXTRA_FLAGS $test_flags 2>&1 | head -5
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=== Results: $PASS/$TOTAL passed, $FAIL failed ==="
