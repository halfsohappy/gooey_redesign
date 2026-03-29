#!/usr/bin/env bash
# Build the gooey-server PyInstaller binary and place it where Tauri expects it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GOOEY_DIR="$(dirname "$SCRIPT_DIR")"
SPEC="$GOOEY_DIR/GooeyServer.spec"
DIST_DIR="$GOOEY_DIR/dist"
TARGET_DIR="$GOOEY_DIR/src-tauri/binaries"

TRIPLE=$(rustc -vV 2>/dev/null | grep '^host:' | awk '{print $2}')
echo "Building gooey-server sidecar for target: $TRIPLE"

cd "$GOOEY_DIR"
python -m PyInstaller "$SPEC"

SRC="$DIST_DIR/gooey-server"
DEST="$TARGET_DIR/gooey-server-$TRIPLE"

mkdir -p "$TARGET_DIR"
cp "$SRC" "$DEST"
chmod +x "$DEST"

echo "Sidecar placed at: $DEST"
