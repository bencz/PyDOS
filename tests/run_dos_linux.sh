#!/usr/bin/env bash
# Build and run the DOS integration suite from a Linux host.
#
# Usage:
#   WATCOM=/path/to/openwatcom tests/run_dos_linux.sh 8086 [test ...]
#   WATCOM=/path/to/openwatcom tests/run_dos_linux.sh 386  [test ...]
#   WATCOM=/path/to/openwatcom tests/run_dos_linux.sh all  [test ...]

set -uo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_ROOT="$ROOT_DIR/build/dos-linux"
TEST_TIMEOUT=${PYDOS_TEST_TIMEOUT:-30s}
LOCAL_WATCOM="$ROOT_DIR/toolchains/openwatcom/distribution"

usage() {
    echo "Usage: WATCOM=/path/to/openwatcom $0 8086|386|all [test ...]" >&2
    exit 2
}

if [[ $# -lt 1 ]]; then
    usage
fi

REQUESTED_TARGET=$1
shift

case "$REQUESTED_TARGET" in
    8086|386)
        TARGETS=("$REQUESTED_TARGET")
        ;;
    all)
        TARGETS=(8086 386)
        ;;
    *)
        usage
        ;;
esac

if [[ -z "${WATCOM:-}" && -d "$LOCAL_WATCOM" ]]; then
    WATCOM=$LOCAL_WATCOM
fi

if [[ -z "${WATCOM:-}" ]]; then
    echo "ERROR: Open Watcom not found in $LOCAL_WATCOM and WATCOM is unset" >&2
    exit 2
fi

WATCOM=$(cd "$WATCOM" 2>/dev/null && pwd) || {
    echo "ERROR: WATCOM directory does not exist: ${WATCOM}" >&2
    exit 2
}
export WATCOM
export INCLUDE="$WATCOM/h"
export PATH="$ROOT_DIR/toolchains/openwatcom/hosts/linux/bin:$WATCOM/binl:$WATCOM/binw:$PATH"

for tool in wcc wcc386 wasm wlib wlink dosemu; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required tool not found: $tool" >&2
        exit 2
    fi
done

cd "$ROOT_DIR"

echo "=== Building native PyDOS compiler ==="
if ! make -f Makefile.mac compiler; then
    echo "ERROR: native compiler build failed" >&2
    exit 1
fi

mkdir -p "$BUILD_ROOT"

collect_enabled_tests() {
    local line
    local ignored_call
    local ignored_script
    local test_name
    local ignored_mode

    while IFS= read -r line; do
        read -r ignored_call ignored_script test_name ignored_mode _ <<< "$line"
        printf '%s\n' "$test_name"
    done < <(sed -n 's/^call runone\.bat /call runone.bat /p' runtests.bat)
}

if [[ $# -gt 0 ]]; then
    TESTS=("$@")
else
    mapfile -t TESTS < <(collect_enabled_tests)
fi

if [[ ${#TESTS[@]} -eq 0 ]]; then
    echo "ERROR: no tests selected" >&2
    exit 2
fi

build_runtime() {
    local target=$1
    local runtime_dir="$BUILD_ROOT/$target/runtime"
    local library
    local source
    local base_name
    local object
    local -a library_objects=()

    mkdir -p "$runtime_dir"

    if [[ "$target" == "8086" ]]; then
        library="$BUILD_ROOT/$target/PYDOSRT.LIB"
    else
        library="$BUILD_ROOT/$target/PDOS32RT.LIB"
    fi

    for source in runtime/pdos_*.c; do
        base_name=${source##*/}
        object="$runtime_dir/${base_name%.c}.obj"

        if [[ "$target" == "8086" ]]; then
            if ! wcc -0 -ml -ox -zq -fpc -we -fo="$object" "$source"; then
                echo "ERROR: failed to compile $source for $target" >&2
                return 1
            fi
        else
            if ! wcc386 -3s -mf -ox -zq -fpc -we -dPYDOS_32BIT \
                    -fo="$object" "$source"; then
                echo "ERROR: failed to compile $source for $target" >&2
                return 1
            fi
        fi

        library_objects+=("+$object")
    done

    rm -f "$library"
    if ! wlib -q -n "$library" "${library_objects[@]}"; then
        echo "ERROR: failed to create runtime library for $target" >&2
        return 1
    fi
}

compile_test() {
    local target=$1
    local test_name=$2
    local source_file="$ROOT_DIR/tests/$test_name.py"
    local expected_file="$ROOT_DIR/tests/$test_name.exp"
    local test_dir="$BUILD_ROOT/$target/tests/$test_name"
    local asm_file="$test_dir/$test_name.asm"
    local object_file="$test_dir/$test_name.obj"
    local exe_file="$test_dir/$test_name.exe"
    local build_log="$test_dir/build.log"
    local flags_file="$ROOT_DIR/tests/$test_name.flags"
    local -a compiler_flags=(--search-path tests --stdlib-idx bin/stdlib.idx)

    if [[ ! -f "$source_file" || ! -f "$expected_file" ]]; then
        echo "missing $source_file or $expected_file" > "$build_log"
        return 1
    fi

    mkdir -p "$test_dir"

    if [[ -f "$flags_file" ]]; then
        local -a file_flags=()
        read -r -a file_flags < "$flags_file"
        compiler_flags+=("${file_flags[@]}")
    fi

    # tests/<name>.split holds N: split the flat-386 output into N object
    # modules (the assembler is quadratic in file size, so a program that
    # links the whole TUI library otherwise times out).  8086 ignores it.
    local split_n=1
    local split_file="$ROOT_DIR/tests/$test_name.split"
    if [[ "$target" == "386" && -f "$split_file" ]]; then
        read -r split_n < "$split_file"
    fi

    if [[ "$target" == "386" ]]; then
        compiler_flags+=(-t 386)
        if [[ "$split_n" -gt 1 ]]; then
            compiler_flags+=(--split "$split_n")
        fi
    fi

    if ! bin/pydos "$source_file" -o "$asm_file" \
            "${compiler_flags[@]}" > "$build_log" 2>&1; then
        return 1
    fi

    if [[ "$target" == "8086" ]]; then
        if ! wasm -0 -ml -d0 -zq -we -fo="$object_file" "$asm_file" \
                >> "$build_log" 2>&1; then
            return 1
        fi

        if ! wlink option quiet system dos option stack=32768 option dosseg \
                option eliminate name "$exe_file" file "$object_file" \
                library "$BUILD_ROOT/8086/PYDOSRT.LIB" library clibl \
                library emu87 >> "$build_log" 2>&1; then
            return 1
        fi
    else
        # Assemble the base object plus any split fragments (_p1, _p2, ...)
        # and collect them into the link's file list.
        local -a asm_parts=("$asm_file")
        local part
        for ((part = 1; part < split_n; part++)); do
            asm_parts+=("$test_dir/${test_name}_p${part}.asm")
        done
        local -a link_files=()
        local a obj
        for a in "${asm_parts[@]}"; do
            obj="${a%.asm}.obj"
            if ! wasm -3 -mf -d0 -zq -we -fo="$obj" "$a" \
                    >> "$build_log" 2>&1; then
                return 1
            fi
            link_files+=(file "$obj")
        done

        if ! wlink option quiet system causeway option stack=65536 \
                option dosseg option eliminate name "$exe_file" \
                "${link_files[@]}" \
                library "$BUILD_ROOT/386/PDOS32RT.LIB" library clib3s \
                >> "$build_log" 2>&1; then
            return 1
        fi
    fi
}

run_test() {
    local target=$1
    local test_name=$2
    local test_dir="$BUILD_ROOT/$target/tests/$test_name"
    local raw_output="$test_dir/$test_name.raw"
    local output_file="$test_dir/$test_name.out"
    local diff_file="$test_dir/$test_name.diff"

    timeout "$TEST_TIMEOUT" \
        dosemu -quiet -3 -K "$test_dir" -E "$test_name.exe" \
        > "$raw_output"
    local run_status=$?
    if [[ $run_status -ne 0 ]]; then
        if [[ $run_status -eq 124 ]]; then
            echo "DOSEMU2 execution timed out after $TEST_TIMEOUT" > "$diff_file"
        else
            echo "DOSEMU2 execution failed with status $run_status" > "$diff_file"
        fi
        return 1
    fi

    sed 's/\r$//' "$raw_output" > "$output_file"

    if ! diff -u "tests/$test_name.exp" "$output_file" > "$diff_file"; then
        return 1
    fi

    return 0
}

run_target() {
    local target=$1
    local test_name
    local passed=0
    local failed=0
    local total=${#TESTS[@]}

    echo
    echo "=== Building $target runtime ==="
    if ! build_runtime "$target"; then
        return 1
    fi

    echo "=== Running $total integration tests for $target ==="
    for test_name in "${TESTS[@]}"; do
        # tests/<name>.nodos marks a test whose logic is fully covered by
        # the CPython golden run and adds no code-generation coverage
        # worth an emulated DOS execution (headless App event-loop
        # drivers).  Skipped on both targets.
        if [[ -f "$ROOT_DIR/tests/$test_name.nodos" ]]; then
            echo "  SKIP  $test_name (CPython-only: $(head -1 "$ROOT_DIR/tests/$test_name.nodos"))"
            total=$((total - 1))
            continue
        fi
        # tests/<name>.386 marks a test whose native executable exceeds
        # the 8086 640 KB budget (the VM execution mode is the planned
        # way to lift this); it still runs fully on the 386 target.
        if [[ "$target" == "8086" && -f "$ROOT_DIR/tests/$test_name.386" ]]; then
            echo "  SKIP  $test_name (386-only: $(head -1 "$ROOT_DIR/tests/$test_name.386"))"
            total=$((total - 1))
            continue
        fi
        if ! compile_test "$target" "$test_name"; then
            echo "  FAIL  $test_name (build)"
            sed -n '1,12p' "$BUILD_ROOT/$target/tests/$test_name/build.log" 2>/dev/null
            failed=$((failed + 1))
            continue
        fi

        if run_test "$target" "$test_name"; then
            echo "  PASS  $test_name"
            passed=$((passed + 1))
        else
            echo "  FAIL  $test_name (output)"
            sed -n '1,40p' "$BUILD_ROOT/$target/tests/$test_name/$test_name.diff" 2>/dev/null
            failed=$((failed + 1))
        fi
    done

    echo "=== $target results: $passed/$total passed, $failed failed ==="
    [[ $failed -eq 0 ]]
}

OVERALL_STATUS=0
for target in "${TARGETS[@]}"; do
    if ! run_target "$target"; then
        OVERALL_STATUS=1
    fi
done

exit "$OVERALL_STATUS"
