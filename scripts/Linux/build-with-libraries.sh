#!/usr/bin/env bash
# Bundle source + sibling packages/ for offline dev setup:
#   4KDowner-with-libraries-<version>.zip
#     4KDowner/   repo sources (no build trees, no .git)
#     packages/   ffmpeg, nodejs, raylib, tinyfiledialogs, ytdown, ...
#
# Usage:
#   ./scripts/Linux/build-with-libraries.sh
#
# Optional:
#   --out-root DIR       output root (default: ../yCompiled)
#   --app-version VER    version in archive name (default: from CMakeLists.txt)
#   --packages-root DIR  override ../packages
#   --keep-staging       do not delete temporary staging folder

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=_ensure-console.sh
source "$SCRIPT_DIR/_ensure-console.sh"
# shellcheck source=project-version.sh
source "$SCRIPT_DIR/project-version.sh"

PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CODING_ROOT="$(cd "$PROJECT_ROOT/.." && pwd)"
PACKAGES_ROOT="$CODING_ROOT/packages"
OUT_ROOT=""
APP_VERSION=""
KEEP_STAGING=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out-root) OUT_ROOT="${2:-}"; shift 2 ;;
    --app-version) APP_VERSION="${2:-}"; shift 2 ;;
    --packages-root) PACKAGES_ROOT="${2:-}"; shift 2 ;;
    --keep-staging) KEEP_STAGING=1; shift ;;
    -h|--help)
      sed -n '2,16p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "$APP_VERSION" ]]; then
  APP_VERSION="$(get_project_app_version "$PROJECT_ROOT")"
fi

if [[ -z "$OUT_ROOT" ]]; then
  OUT_ROOT="$CODING_ROOT/yCompiled"
fi

ARCHIVE_NAME="4KDowner-with-libraries-${APP_VERSION}.zip"
ARCHIVE_PATH="$OUT_ROOT/$ARCHIVE_NAME"
STAGING_ROOT="$OUT_ROOT/_staging-with-libraries-${APP_VERSION}"
STAGING_PROJECT="$STAGING_ROOT/4KDowner"
STAGING_PACKAGES="$STAGING_ROOT/packages"

REQUIRED_PACKAGES=(raylib tinyfiledialogs ffmpeg ytdown nodejs)

echo "=== 4KDowner with-libraries archive ==="
echo "Project:   $PROJECT_ROOT"
echo "Packages:  $PACKAGES_ROOT"
echo "Release:   $APP_VERSION"
echo "Archive:   $ARCHIVE_PATH"
echo ""

for pkg in "${REQUIRED_PACKAGES[@]}"; do
  if [[ ! -d "$PACKAGES_ROOT/$pkg" ]]; then
    echo "Required package folder missing: $PACKAGES_ROOT/$pkg" >&2
    exit 1
  fi
done

mkdir -p "$OUT_ROOT"
rm -rf "$STAGING_ROOT"
mkdir -p "$STAGING_ROOT"

echo "Staging project sources..."
rsync -a --delete \
  --exclude '/build/' \
  --exclude '/build-windows/' \
  --exclude '/build-linux/' \
  --exclude '/out/' \
  --exclude '/.vs/' \
  --exclude '/.git/' \
  --exclude '/.cursor/' \
  --exclude '/.cache/' \
  --exclude '/cache/' \
  --exclude '/scripts/Windows/msi/branding/' \
  --exclude '/compile_commands.json' \
  "$PROJECT_ROOT/" "$STAGING_PROJECT/"

echo "Staging packages..."
rsync -a --delete "$PACKAGES_ROOT/" "$STAGING_PACKAGES/"

rm -f "$ARCHIVE_PATH"

if command -v cmake >/dev/null 2>&1; then
  echo "Creating zip via cmake..."
  cmake -E chdir "$STAGING_ROOT" tar cf "$ARCHIVE_PATH" --format=zip "4KDowner" "packages"
else
  echo "cmake not found; using zip..."
  command -v zip >/dev/null 2>&1 || { echo "zip command required when cmake is missing" >&2; exit 1; }
  (
    cd "$STAGING_ROOT"
    zip -r -q "$ARCHIVE_PATH" "4KDowner" "packages"
  )
fi

if [[ ! -f "$ARCHIVE_PATH" ]]; then
  echo "Archive missing: $ARCHIVE_PATH" >&2
  exit 1
fi

if [[ "$KEEP_STAGING" -eq 0 ]]; then
  rm -rf "$STAGING_ROOT"
fi

archive_mb="$(du -m "$ARCHIVE_PATH" | awk '{print $1}')"
echo ""
echo "Done: $ARCHIVE_PATH (${archive_mb} MB)"
