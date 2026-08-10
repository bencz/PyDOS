#!/usr/bin/env bash
# Build PyDOS sample projects into real DOS executables on a Linux host.

set -uo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_ROOT="$ROOT_DIR/build/samples"
LOCAL_WATCOM="$ROOT_DIR/toolchains/openwatcom/distribution"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 8086|386|all [sample ...]" >&2
    exit 2
fi

REQUESTED_TARGET=$1
shift
case "$REQUESTED_TARGET" in
    8086|386) TARGETS=("$REQUESTED_TARGET") ;;
    all) TARGETS=(8086 386) ;;
    *) echo "target must be 8086, 386 or all" >&2; exit 2 ;;
esac

if [[ -z "${WATCOM:-}" && -d "$LOCAL_WATCOM" ]]; then
    WATCOM=$LOCAL_WATCOM
fi
if [[ -z "${WATCOM:-}" ]]; then
    echo "OpenWatcom was not found; set WATCOM or install it locally." >&2
    exit 2
fi

WATCOM=$(cd "$WATCOM" 2>/dev/null && pwd) || exit 2
export WATCOM
export INCLUDE="$WATCOM/h"
export PATH="$ROOT_DIR/toolchains/openwatcom/hosts/linux/bin:$WATCOM/binl:$WATCOM/binw:$PATH"

if [[ $# -gt 0 ]]; then
    SAMPLES=("$@")
else
    SAMPLES=(hello_project tui_demo alley_cat edit)
fi

cd "$ROOT_DIR"
make -f Makefile.mac compiler || exit 1

build_runtime() {
    local target=$1
    local runtime_dir="$BUILD_ROOT/$target/runtime"
    local library
    local source
    local object
    local base_name
    local -a objects=()

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
            wcc -0 -ml -ox -zq -fpc -we -fo="$object" "$source" || return 1
        else
            wcc386 -3s -mf -ox -zq -fpc -we -dPYDOS_32BIT \
                -fo="$object" "$source" || return 1
        fi
        objects+=("+$object")
    done
    rm -f "$library"
    wlib -q -n "$library" "${objects[@]}" || return 1
}

build_sample() {
    local target=$1
    local sample=$2
    local sample_dir="$ROOT_DIR/samples/$sample"
    local output_dir="$BUILD_ROOT/$target/$sample"
    local asm_file="$output_dir/main.asm"
    local object_file="$output_dir/main.obj"
    local exe_file="$output_dir/$sample.exe"

    if [[ ! -f "$sample_dir/main.py" ]]; then
        echo "Unknown sample: $sample" >&2
        return 1
    fi
    mkdir -p "$output_dir"

    if [[ "$target" == "8086" ]]; then
        bin/pydos "$sample_dir/main.py" -o "$asm_file" \
            --search-path "$sample_dir" --stdlib-idx bin/stdlib.idx || return 1
        wasm -0 -ml -d0 -zq -we -fo="$object_file" "$asm_file" || return 1
        wlink option quiet system dos option stack=16384 option dosseg \
            option eliminate name "$exe_file" file "$object_file" \
            library "$BUILD_ROOT/8086/PYDOSRT.LIB" library clibl \
            library emu87 || return 1
    else
        bin/pydos "$sample_dir/main.py" -o "$asm_file" -t 386 \
            --search-path "$sample_dir" --stdlib-idx bin/stdlib.idx || return 1
        wasm -3 -mf -d0 -zq -we -fo="$object_file" "$asm_file" || return 1
        wlink option quiet system causeway option stack=65536 option dosseg \
            option eliminate name "$exe_file" file "$object_file" \
            library "$BUILD_ROOT/386/PDOS32RT.LIB" library clib3s || return 1
    fi
    echo "Built $target $sample -> $exe_file"
}

status=0
for target in "${TARGETS[@]}"; do
    echo "=== Runtime $target ==="
    if ! build_runtime "$target"; then
        status=1
        continue
    fi
    for sample in "${SAMPLES[@]}"; do
        echo "=== $target $sample ==="
        build_sample "$target" "$sample" || status=1
    done
done
exit "$status"
