#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Target release tag (default: latest release v3.1.1)
DXVK_TAG="${1:-v3.1.1}"
DXVK_VERSION="${DXVK_TAG#v}"

echo "========================================================"
echo "Downloading and Updating DXVK to $DXVK_TAG ($DXVK_VERSION)"
echo "Upstream: https://github.com/doitsujin/dxvk"
echo "========================================================"

RELEASES_DIR="$SCRIPT_DIR/releases"
mkdir -p "$RELEASES_DIR"

DXVK_TAR="dxvk-${DXVK_VERSION}.tar.gz"
DXVK_NATIVE_TAR="dxvk-native-${DXVK_VERSION}-steamrt-sniper.tar.gz"
DXVK_SRC_TAR="dxvk-${DXVK_VERSION}-src.tar.gz"

# 1. Download official GitHub release assets
echo "[1/4] Downloading official release archives..."
if [ ! -f "$RELEASES_DIR/$DXVK_TAR" ]; then
    echo "Downloading $DXVK_TAR..."
    curl -sSLf "https://github.com/doitsujin/dxvk/releases/download/${DXVK_TAG}/${DXVK_TAR}" -o "$RELEASES_DIR/$DXVK_TAR"
else
    echo "Found cached $DXVK_TAR."
fi

if [ ! -f "$RELEASES_DIR/$DXVK_NATIVE_TAR" ]; then
    echo "Downloading $DXVK_NATIVE_TAR..."
    curl -sSLf "https://github.com/doitsujin/dxvk/releases/download/${DXVK_TAG}/${DXVK_NATIVE_TAR}" -o "$RELEASES_DIR/$DXVK_NATIVE_TAR"
else
    echo "Found cached $DXVK_NATIVE_TAR."
fi

# 2. Download source archive corresponding to the release
echo "[2/4] Downloading DXVK source tree for $DXVK_TAG..."
if [ ! -f "$RELEASES_DIR/$DXVK_SRC_TAR" ]; then
    curl -sSLf "https://github.com/doitsujin/dxvk/archive/refs/tags/${DXVK_TAG}.tar.gz" -o "$RELEASES_DIR/$DXVK_SRC_TAR"
else
    echo "Found cached $DXVK_SRC_TAR."
fi

# 3. Unpack and verify contents
echo "[3/4] Unpacking and verifying release packages..."
mkdir -p "$SCRIPT_DIR/extracted"
tar -xzf "$RELEASES_DIR/$DXVK_TAR" -C "$SCRIPT_DIR/extracted"
mkdir -p "$SCRIPT_DIR/extracted/native"
tar -xzf "$RELEASES_DIR/$DXVK_NATIVE_TAR" -C "$SCRIPT_DIR/extracted/native"

echo "[4/4] Release assets downloaded and verified successfully:"
ls -lh "$RELEASES_DIR"
echo "Extracted files:"
ls -la "$SCRIPT_DIR/extracted"
echo "DXVK update download complete."
