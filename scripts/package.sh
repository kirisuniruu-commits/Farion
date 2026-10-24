#!/usr/bin/env bash
# usage: package.sh <android-build-dir> <host-build-dir> <out-dir>
set -euo pipefail
ANDROID_BUILD="$1"; HOST_BUILD="$2"; OUT="$3"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$OUT/farion"      # directory name == runtime mod id
rm -rf "$OUT"; mkdir -p "$PKG/config"
cp "$ROOT/manifest.json" "$PKG/manifest.json"
cp "$ANDROID_BUILD/out/arm64-v8a/libfarion.so" "$PKG/"
cp "$ROOT/package/signatures.json" "$PKG/config/signatures.json"
"$HOST_BUILD/dh_config_gen" "$PKG"
( cd "$PKG" && zip -qr "../farion.levipack" . )
unzip -l "$OUT/farion.levipack"
