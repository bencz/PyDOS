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

# Not PyDOS tests: cpython_reference is the CPython harness used by
# run_cpython.sh (it relies on host-only names such as __name__).
# abc456 is an experimental scratch suite, not part of the test set yet
# (it depends on unimplemented features such as subclassing builtin
# exceptions).
SKIP_TESTS=" cpython_reference abc456 "

# Tests that must FAIL to compile.  Prints the substring the diagnostic
# must contain; returns non-zero for tests expected to compile.
expect_fail_pattern() {
    case "$1" in
        imp_err) echo "use 'from mod_hlp import" ;;
        *) return 1 ;;
    esac
}

for pyfile in tests/*.py; do
    name=$(basename "$pyfile" .py)
    case "$SKIP_TESTS" in *" $name "*) continue ;; esac
    TOTAL=$((TOTAL + 1))
    outfile="build/${name}.asm"
    test_flags=""
    if [ -f "tests/${name}.flags" ]; then
        test_flags=$(cat "tests/${name}.flags")
    fi
    if required=$(expect_fail_pattern "$name"); then
        compile_ok=0
        if output=$(bin/pydos "$pyfile" -o "$outfile" --search-path tests \
                    $EXTRA_FLAGS $test_flags 2>&1); then
            compile_ok=1
        fi
        if [ $compile_ok -eq 0 ] && \
           printf '%s' "$output" | grep -qF "$required"; then
            echo "  PASS  $name (rejected as expected)"
            PASS=$((PASS + 1))
        else
            echo "  FAIL  $name (expected compile error containing: $required)"
            printf '%s\n' "$output" | head -5
            FAIL=$((FAIL + 1))
        fi
        continue
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
