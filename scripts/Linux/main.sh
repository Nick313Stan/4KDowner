#!/usr/bin/env bash
# One-shot Linux release packager:
#   portable folder + tar.gz archive → ../yCompiled/
#
# Usage:
#   ./scripts/Linux/main.sh
#
# Optional:
#   --skip-archive     skip tar.gz
#   --ensure-ytdown    run setup-ytdown-portable.sh if yt-dlp missing
#   --out-root DIR
#   --app-version VER  version segment (default: from CMakeLists.txt)
#   --build-dir DIR
#
# Single stage:
#   ./scripts/Linux/build-portable.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=_ensure-console.sh
source "$SCRIPT_DIR/_ensure-console.sh"
# shellcheck source=project-version.sh
source "$SCRIPT_DIR/project-version.sh"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CODING_ROOT="$(cd "$PROJECT_ROOT/.." && pwd)"

SKIP_ARCHIVE=0
ENSURE_YTDOWN=0
OUT_ROOT=""
APP_VERSION=""
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-linux}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-archive) SKIP_ARCHIVE=1; shift ;;
    --ensure-ytdown) ENSURE_YTDOWN=1; shift ;;
    --out-root) OUT_ROOT="${2:-}"; shift 2 ;;
    --app-version) APP_VERSION="${2:-}"; shift 2 ;;
    --build-dir) BUILD_DIR="${2:-}"; shift 2 ;;
    -h|--help)
      sed -n '2,18p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$APP_VERSION" ]]; then
  APP_VERSION="$(get_project_app_version "$PROJECT_ROOT")"
fi

RELEASE_NAME="4KDowner-${APP_VERSION}-linux-x64"
if [[ -z "$OUT_ROOT" ]]; then
  OUT_ROOT="$CODING_ROOT/yCompiled"
fi
PORTABLE_ROOT="$OUT_ROOT/$RELEASE_NAME"
ARCHIVE_PATH="$OUT_ROOT/${RELEASE_NAME}.tar.gz"

echo "=== 4KDowner Linux package ==="
echo "Project:  $PROJECT_ROOT"
echo "Release:  $RELEASE_NAME"
echo "Output:   $OUT_ROOT"
echo ""

PORTABLE_ARGS=(
  --out-root "$OUT_ROOT"
  --app-version "$APP_VERSION"
  --portable-root "$PORTABLE_ROOT"
  --build-dir "$BUILD_DIR"
)
if [[ "$ENSURE_YTDOWN" -eq 1 ]]; then
  PORTABLE_ARGS+=(--ensure-ytdown)
fi

"$SCRIPT_DIR/build-portable.sh" "${PORTABLE_ARGS[@]}"

if [[ "$SKIP_ARCHIVE" -eq 0 ]]; then
  echo "Setting archive output path..."
  cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    "-D4KDOWNER_PACKAGE_DIR=$PORTABLE_ROOT" \
    "-D4KDOWNER_ARCHIVE_PATH=$ARCHIVE_PATH"

  echo "Building portable archive..."
  cmake --build "$BUILD_DIR" --target package-archive -j"$(nproc 2>/dev/null || echo 4)"

  if [[ ! -f "$ARCHIVE_PATH" ]]; then
    echo "Archive missing: $ARCHIVE_PATH" >&2
    exit 1
  fi
fi

echo ""
echo "=== Done ==="
echo "Portable: $PORTABLE_ROOT"
if [[ "$SKIP_ARCHIVE" -eq 0 ]]; then
  echo "Archive:  $ARCHIVE_PATH"
fi
