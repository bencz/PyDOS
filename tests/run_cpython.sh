#!/usr/bin/env bash
# Validate or regenerate golden outputs with the reference CPython 3.12.
#
# Usage:
#   tests/run_cpython.sh check [test ...]
#   tests/run_cpython.sh update [test ...]
#
# Environment:
#   PYDOS_CPYTHON=/path/to/python3.12
#   PYDOS_CPYTHON_TIMEOUT=30s

set -uo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
RESULT_ROOT="$ROOT_DIR/build/cpython"
CPYTHON=${PYDOS_CPYTHON:-python3.12}
TEST_TIMEOUT=${PYDOS_CPYTHON_TIMEOUT:-30s}

usage() {
    echo "Usage: $0 check|update [test ...]" >&2
    exit 2
}

if [[ $# -lt 1 ]]; then
    usage
fi

MODE=$1
shift
case "$MODE" in
    check|update) ;;
    *) usage ;;
esac

if ! command -v "$CPYTHON" >/dev/null 2>&1; then
    if command -v podman >/dev/null 2>&1 &&
            podman image exists docker.io/library/python:3.12-slim; then
        exec podman run --rm --network=none --userns=keep-id \
            --security-opt label=disable \
            -v "$ROOT_DIR:/work:rw" -w /work \
            docker.io/library/python:3.12-slim \
            tests/run_cpython.sh "$MODE" "$@"
    fi
    echo "ERROR: CPython 3.12 not found: $CPYTHON" >&2
    echo "Set PYDOS_CPYTHON or install the python:3.12-slim Podman image." >&2
    exit 2
fi

read -r implementation major minor patch < <(
    "$CPYTHON" -c 'import sys; print(sys.implementation.name, *sys.version_info[:3])'
)
if [[ $implementation != cpython || $major != 3 || $minor != 12 ]]; then
    echo "ERROR: golden outputs require CPython 3.12; found $implementation $major.$minor.$patch" >&2
    exit 2
fi

declare -a TESTS=()
if [[ $# -gt 0 ]]; then
    for test_name in "$@"; do
        test_name=${test_name##*/}
        test_name=${test_name%.py}
        TESTS+=("$test_name")
    done
else
    for source_file in "$ROOT_DIR"/tests/*.py; do
        test_name=${source_file##*/}
        test_name=${test_name%.py}
        if [[ -f "$ROOT_DIR/tests/$test_name.exp" ]]; then
            TESTS+=("$test_name")
        fi
    done
fi

mkdir -p "$RESULT_ROOT"
work_root=$(mktemp -d "${TMPDIR:-/tmp}/pydos-cpython.XXXXXX") || exit 2
trap 'rm -rf "$work_root"' EXIT

passed=0
failed=0
updated=0

echo "=== CPython $major.$minor.$patch golden-output $MODE ==="

for test_name in "${TESTS[@]}"; do
    source_file="$ROOT_DIR/tests/$test_name.py"
    expected_file="$ROOT_DIR/tests/$test_name.exp"
    test_result_dir="$RESULT_ROOT/$test_name"
    test_work_dir="$work_root/$test_name"
    raw_output="$test_result_dir/$test_name.raw"
    output_file="$test_result_dir/$test_name.out"
    error_file="$test_result_dir/$test_name.err"
    diff_file="$test_result_dir/$test_name.diff"

    if [[ ! -f $source_file ]]; then
        echo "  FAIL  $test_name (missing source)"
        failed=$((failed + 1))
        continue
    fi

    mkdir -p "$test_result_dir" "$test_work_dir"
    test_pythonpath="$ROOT_DIR/tests"
    requires_pydos=0
    entry_name=
    flags_file="$ROOT_DIR/tests/$test_name.flags"
    if [[ -f $flags_file ]]; then
        read -r -a test_flags <"$flags_file"
        flag_index=0
        while [[ $flag_index -lt ${#test_flags[@]} ]]; do
            case ${test_flags[$flag_index]} in
                --search-path)
                    flag_index=$((flag_index + 1))
                    test_pythonpath="$ROOT_DIR/${test_flags[$flag_index]}:$test_pythonpath"
                    if grep -REq '(^|[[:space:]])(from|import)[[:space:]]+pydos([.]|[[:space:]]|$)' \
                            "$ROOT_DIR/${test_flags[$flag_index]}"; then
                        requires_pydos=1
                    fi
                    ;;
                --entry)
                    flag_index=$((flag_index + 1))
                    entry_name=${test_flags[$flag_index]}
                    ;;
            esac
            flag_index=$((flag_index + 1))
        done
    fi

    if grep -Eq '(^|[[:space:]])(from|import)[[:space:]]+pydos([.]|[[:space:]]|$)' \
            "$source_file"; then
        requires_pydos=1
    fi
    if [[ $requires_pydos -eq 1 ]]; then
        test_pythonpath="$ROOT_DIR/stdlib:$test_pythonpath"
    fi

    if [[ -n $entry_name ]]; then
        python_command=(
            "$CPYTHON" -X utf8 "$ROOT_DIR/tests/cpython_reference.py"
            "$source_file" "$entry_name"
        )
    else
        python_command=(
            "$CPYTHON" -X utf8 "$ROOT_DIR/tests/cpython_reference.py"
            "$source_file"
        )
    fi

    (
        cd "$test_work_dir" || exit 2
        env \
            LC_ALL=C.UTF-8 \
            PYTHONHASHSEED=0 \
            PYTHONDONTWRITEBYTECODE=1 \
            PYTHONIOENCODING=utf-8 \
            PYTHONPATH="$test_pythonpath" \
            timeout "$TEST_TIMEOUT" "${python_command[@]}"
    ) >"$raw_output" 2>"$error_file"
    status=$?
    if [[ $status -ne 0 ]]; then
        echo "  FAIL  $test_name (CPython exit $status)"
        sed -n '1,8p' "$error_file"
        failed=$((failed + 1))
        continue
    fi

    sed 's/\r$//' "$raw_output" >"$output_file"

    if [[ $MODE == update ]]; then
        if [[ ! -f $expected_file ]] || ! cmp -s "$expected_file" "$output_file"; then
            cp "$output_file" "$expected_file"
            echo "  UPDATE $test_name"
            updated=$((updated + 1))
        else
            echo "  OK     $test_name"
        fi
        passed=$((passed + 1))
        continue
    fi

    if [[ ! -f $expected_file ]]; then
        echo "  FAIL  $test_name (missing expected output)"
        failed=$((failed + 1))
    elif diff -u "$expected_file" "$output_file" >"$diff_file"; then
        echo "  PASS  $test_name"
        passed=$((passed + 1))
    else
        echo "  FAIL  $test_name (output)"
        sed -n '1,80p' "$diff_file"
        failed=$((failed + 1))
    fi
done

echo "=== CPython results: $passed passed, $failed failed, $updated updated ==="
if [[ $failed -ne 0 ]]; then
    exit 1
fi
