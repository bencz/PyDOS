#!/usr/bin/env bash
# Install the portable Open Watcom distribution inside this repository.

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TOOLCHAIN_ROOT="$ROOT_DIR/toolchains/openwatcom"
ARCHIVE="$TOOLCHAIN_ROOT/downloads/ow_portable_v2_stable.zip"
DISTRIBUTION="$TOOLCHAIN_ROOT/distribution"
DOWNLOAD_URL=https://downloads.openwatcom.org/ftp/source/ow_portable_v2_stable.zip

mkdir -p "$TOOLCHAIN_ROOT/downloads" "$DISTRIBUTION"

if [[ ! -s "$ARCHIVE" ]]; then
    echo "Downloading Open Watcom portable distribution..."
    curl -fL --retry 3 --connect-timeout 20 -o "$ARCHIVE" "$DOWNLOAD_URL"
fi

echo "Checking archive..."
unzip -tq "$ARCHIVE"

echo "Extracting to $DISTRIBUTION..."
unzip -oq "$ARCHIVE" -d "$DISTRIBUTION"

mkdir -p \
    "$TOOLCHAIN_ROOT/hosts/linux" \
    "$TOOLCHAIN_ROOT/hosts/windows" \
    "$TOOLCHAIN_ROOT/targets/dos" \
    "$TOOLCHAIN_ROOT/targets/os2"

ln -sfn ../../distribution/binl "$TOOLCHAIN_ROOT/hosts/linux/bin"
ln -sfn ../../distribution/binnt "$TOOLCHAIN_ROOT/hosts/windows/bin"
ln -sfn ../../distribution/binw "$TOOLCHAIN_ROOT/targets/dos/bin"
ln -sfn ../../distribution/binp "$TOOLCHAIN_ROOT/targets/os2/bin"

echo
echo "Open Watcom installed."
echo "  Linux host tools:  toolchains/openwatcom/hosts/linux/bin"
echo "  Windows host tools: toolchains/openwatcom/hosts/windows/bin"
echo "  DOS target tools:   toolchains/openwatcom/targets/dos/bin"
echo "  OS/2 target tools:  toolchains/openwatcom/targets/os2/bin"
echo "  macOS host tools:   not present in the portable distribution"
