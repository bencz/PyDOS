#!/usr/bin/env bash
# Build distributable PyDOS artifacts from a Unix-like host.

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TARGET=${1:-all}
LOCAL_WATCOM="$ROOT_DIR/toolchains/openwatcom/distribution"
BUILD_ROOT="$ROOT_DIR/build/release"
RELEASE_ROOT=${PYDOS_RELEASE_ROOT:-"$ROOT_DIR/release"}

case "$TARGET" in
    all|msdos) ;;
    *)
        echo "ERROR: unsupported release target: $TARGET" >&2
        echo "Available targets: all, msdos" >&2
        exit 2
        ;;
esac

if [[ -z "${WATCOM:-}" && -d "$LOCAL_WATCOM" ]]; then
    WATCOM=$LOCAL_WATCOM
fi
if [[ -z "${WATCOM:-}" ]]; then
    echo "ERROR: Open Watcom not found; set WATCOM or install it in toolchains/openwatcom/distribution" >&2
    exit 2
fi

WATCOM=$(cd "$WATCOM" && pwd)
export WATCOM
export INCLUDE="$WATCOM/h"
export PATH="$ROOT_DIR/toolchains/openwatcom/hosts/linux/bin:$WATCOM/binl:$WATCOM/binw:$PATH"

for tool in wpp386 wcc wcc386 wlib wlink; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required Open Watcom tool not found: $tool" >&2
        exit 2
    fi
done

build_msdos_compiler() {
    local output_dir="$BUILD_ROOT/msdos/compiler"
    local source
    local base_name
    local object
    local -a objects=()
    local -a link_args=(option quiet system causeway option stack=65536
                        option dosseg option eliminate)
    local output="$output_dir/PYDOS.EXE"

    mkdir -p "$output_dir"
    while IFS= read -r source; do
        base_name=${source##*/}
        object="$output_dir/${base_name%.cpp}.obj"
        wpp386 -3s -mf -ox -zq -fpc -we -fo="$object" "$source"
        objects+=("$object")
    done < <(make -s -f Makefile.mac print-compiler-sources)

    link_args+=(name "$output")
    for object in "${objects[@]}"; do
        link_args+=(file "$object")
    done
    wlink "${link_args[@]}"
}

build_msdos_runtime() {
    local architecture=$1
    local output_dir="$BUILD_ROOT/msdos/runtime/$architecture"
    local library
    local source
    local base_name
    local object
    local -a objects=()

    mkdir -p "$output_dir"
    if [[ "$architecture" == "8086" ]]; then
        library="$BUILD_ROOT/msdos/PYDOSRT.LIB"
    else
        library="$BUILD_ROOT/msdos/PDOS32RT.LIB"
    fi

    while IFS= read -r source; do
        base_name=${source##*/}
        object="$output_dir/${base_name%.c}.obj"
        if [[ "$architecture" == "8086" ]]; then
            wcc -0 -ml -ox -zq -fpc -we -fo="$object" "$source"
        else
            wcc386 -3s -mf -ox -zq -fpc -we -dPYDOS_32BIT \
                -fo="$object" "$source"
        fi
        objects+=("+$object")
    done < <(make -s -f Makefile.mac print-runtime-sources)

    if [[ -f "$library" ]]; then
        rm -f "$library"
    fi
    wlib -q -n "$library" "${objects[@]}"
}

write_release_files() {
    local stage
    local destination="$RELEASE_ROOT/msdos"
    local header
    local commit="unknown"

    mkdir -p "$RELEASE_ROOT"
    stage=$(mktemp -d "$RELEASE_ROOT/.msdos.XXXXXX")
    trap 'rm -rf "$stage"' RETURN
    mkdir -p "$stage/BIN" "$stage/LIB" "$stage/INCLUDE"

    install -m 0755 "$BUILD_ROOT/msdos/compiler/PYDOS.EXE" \
        "$stage/BIN/PYDOS.EXE"
    install -m 0644 "$ROOT_DIR/bin/stdlib.idx" "$stage/BIN/STDLIB.IDX"
    install -m 0644 "$BUILD_ROOT/msdos/PYDOSRT.LIB" \
        "$stage/LIB/PYDOSRT.LIB"
    install -m 0644 "$BUILD_ROOT/msdos/PDOS32RT.LIB" \
        "$stage/LIB/PDOS32RT.LIB"

    for header in "$ROOT_DIR"/runtime/*.h "$ROOT_DIR"/common/*.h; do
        install -m 0644 "$header" "$stage/INCLUDE/${header##*/}"
    done

    printf '%s\r\n' \
        '@ECHO OFF' \
        'IF "%WATCOM%"=="" GOTO NOWATCOM' \
        'SET INCLUDE=%WATCOM%\H' \
        'SET PATH=%WATCOM%\BINW;%PATH%' \
        'GOTO DONE' \
        ':NOWATCOM' \
        'ECHO Set WATCOM to the Open Watcom directory first.' \
        ':DONE' > "$stage/SETENV.BAT"

    printf '%s\r\n' \
        '@ECHO OFF' \
        'IF "%1"=="" GOTO USAGE' \
        'BIN\PYDOS.EXE %1.PY -o %1.ASM' \
        'IF ERRORLEVEL 1 GOTO FAILED' \
        'WASM -0 -ML -D0 -ZQ -WE -FO=%1.OBJ %1.ASM' \
        'IF ERRORLEVEL 1 GOTO FAILED' \
        'ECHO OPTION QUIET>%1.LNK' \
        'ECHO SYSTEM DOS>>%1.LNK' \
        'ECHO OPTION STACK=32768>>%1.LNK' \
        'ECHO OPTION DOSSEG>>%1.LNK' \
        'ECHO OPTION ELIMINATE>>%1.LNK' \
        'ECHO NAME %1.EXE>>%1.LNK' \
        'ECHO FILE %1.OBJ>>%1.LNK' \
        'ECHO LIBRARY LIB\PYDOSRT.LIB>>%1.LNK' \
        'ECHO LIBRARY CLIBL>>%1.LNK' \
        'ECHO LIBRARY EMU87>>%1.LNK' \
        'WLINK @%1.LNK' \
        'IF ERRORLEVEL 1 GOTO FAILED' \
        'ECHO Built %1.EXE for 8086.' \
        'GOTO DONE' \
        ':USAGE' \
        'ECHO Usage: BUILD86 name_without_py' \
        'GOTO DONE' \
        ':FAILED' \
        'ECHO Build failed.' \
        ':DONE' > "$stage/BUILD86.BAT"

    printf '%s\r\n' \
        '@ECHO OFF' \
        'IF "%1"=="" GOTO USAGE' \
        'BIN\PYDOS.EXE %1.PY -o %1.ASM -t 386' \
        'IF ERRORLEVEL 1 GOTO FAILED' \
        'WASM -3 -MF -D0 -ZQ -WE -FO=%1.OBJ %1.ASM' \
        'IF ERRORLEVEL 1 GOTO FAILED' \
        'ECHO OPTION QUIET>%1.LNK' \
        'ECHO SYSTEM CAUSEWAY>>%1.LNK' \
        'ECHO OPTION STACK=65536>>%1.LNK' \
        'ECHO OPTION DOSSEG>>%1.LNK' \
        'ECHO OPTION ELIMINATE>>%1.LNK' \
        'ECHO NAME %1.EXE>>%1.LNK' \
        'ECHO FILE %1.OBJ>>%1.LNK' \
        'ECHO LIBRARY LIB\PDOS32RT.LIB>>%1.LNK' \
        'ECHO LIBRARY CLIB3S>>%1.LNK' \
        'WLINK @%1.LNK' \
        'IF ERRORLEVEL 1 GOTO FAILED' \
        'ECHO Built %1.EXE for 386.' \
        'GOTO DONE' \
        ':USAGE' \
        'ECHO Usage: BUILD386 name_without_py' \
        'GOTO DONE' \
        ':FAILED' \
        'ECHO Build failed.' \
        ':DONE' > "$stage/BUILD386.BAT"

    if command -v git >/dev/null 2>&1; then
        commit=$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || printf unknown)
    fi
    printf '%s\r\n' \
        'PyDOS MS-DOS release' \
        "Source commit: $commit" \
        '' \
        'BIN\PYDOS.EXE is the protected-mode compiler.' \
        'BIN\STDLIB.IDX is loaded automatically beside PYDOS.EXE.' \
        'LIB\PYDOSRT.LIB targets 8086 real mode.' \
        'LIB\PDOS32RT.LIB targets 386 protected mode.' \
        '' \
        'Run SETENV.BAT after setting WATCOM.' \
        'Run BUILD86 NAME or BUILD386 NAME for NAME.PY.' \
        'All compiler, assembler and linker warnings are errors.' \
        > "$stage/RELEASE.TXT"

    if [[ -d "$destination" ]]; then
        rm -rf "$destination"
    fi
    mv "$stage" "$destination"
    trap - RETURN
    echo "MS-DOS release written to $destination"
}

cd "$ROOT_DIR"
if [[ "$TARGET" == "all" || "$TARGET" == "msdos" ]]; then
    echo "=== MS-DOS compiler ==="
    build_msdos_compiler
    echo "=== MS-DOS 8086 runtime ==="
    build_msdos_runtime 8086
    echo "=== MS-DOS 386 runtime ==="
    build_msdos_runtime 386
    write_release_files
fi
