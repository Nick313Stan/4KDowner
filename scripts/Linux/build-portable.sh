#!/usr/bin/env bash
# Build portable folder only → ../yCompiled/4KDownerCompiled/4KDowner-<ver>-linux-x64/
#
# Usage:
#   ./scripts/Linux/build-portable.sh
#
# Optional:
#   --ensure-ytdown   refresh packages/ytdown/bin/yt-dlp if missing
#   --out-root DIR    output root (default: ../yCompiled/4KDownerCompiled)
#   --app-version VER version segment in folder name (default: 1.1.0)
#   --portable-root DIR  override full portable folder path
#   --build-dir DIR   CMake build dir (default: <repo>/build-linux)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=_ensure-console.sh
source "$SCRIPT_DIR/_ensure-console.sh"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CODING_ROOT="$(cd "$PROJECT_ROOT/.." && pwd)"
PACKAGES_ROOT="$CODING_ROOT/packages"

ENSURE_YTDOWN=0
OUT_ROOT=""
APP_VERSION="1.1.0"
PORTABLE_ROOT=""
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-linux}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ensure-ytdown) ENSURE_YTDOWN=1; shift ;;
    --out-root) OUT_ROOT="${2:-}"; shift 2 ;;
    --app-version) APP_VERSION="${2:-}"; shift 2 ;;
    --portable-root) PORTABLE_ROOT="${2:-}"; shift 2 ;;
    --build-dir) BUILD_DIR="${2:-}"; shift 2 ;;
    -h|--help)
      sed -n '2,16p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

RELEASE_NAME="4KDowner-${APP_VERSION}-linux-x64"
if [[ -z "$OUT_ROOT" ]]; then
  OUT_ROOT="$CODING_ROOT/yCompiled/4KDownerCompiled"
fi
if [[ -z "$PORTABLE_ROOT" ]]; then
  PORTABLE_ROOT="$OUT_ROOT/$RELEASE_NAME"
fi

echo "=== 4KDowner portable folder (Linux) ==="
echo "Project:  $PROJECT_ROOT"
echo "Release:  $RELEASE_NAME"
echo "Build:    $BUILD_DIR"
echo "Output:   $PORTABLE_ROOT"
echo ""

mkdir -p "$OUT_ROOT"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  echo "Configuring CMake $BUILD_DIR ..."
  cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi

echo "Setting package output path..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
  "-D4KDOWNER_PACKAGE_DIR=$PORTABLE_ROOT"

YTDLP_BIN="$PACKAGES_ROOT/ytdown/bin/yt-dlp"
if [[ "$ENSURE_YTDOWN" -eq 1 || ! -x "$YTDLP_BIN" ]]; then
  if [[ ! -x "$YTDLP_BIN" ]]; then
    echo "Portable yt-dlp missing; running setup-ytdown-portable.sh ..."
  else
    echo "EnsureYtDown: refreshing portable yt-dlp ..."
  fi
  "$SCRIPT_DIR/setup-ytdown-portable.sh"
fi

echo "Building Release + portable..."
cmake --build "$BUILD_DIR" --config Release --target package-portable -j"$(nproc 2>/dev/null || echo 4)"

if [[ ! -f "$PORTABLE_ROOT/4KDowner" ]]; then
  echo "Portable binary missing: $PORTABLE_ROOT/4KDowner" >&2
  exit 1
fi
chmod +x "$PORTABLE_ROOT/4KDowner"
rm -f "$PORTABLE_ROOT/README.txt" "$PORTABLE_ROOT/README.md"

echo ""
echo "Done: $PORTABLE_ROOT"
