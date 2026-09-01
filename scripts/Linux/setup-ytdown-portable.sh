#!/usr/bin/env bash
# Setup portable yt-dlp for Linux packaging → ../packages/ytdown/bin/yt-dlp
# Counterpart to scripts/Windows/setup-ytdown-portable.ps1
#
# Usage:
#   ./scripts/Linux/setup-ytdown-portable.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=_ensure-console.sh
source "$SCRIPT_DIR/_ensure-console.sh"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CODING_ROOT="$(cd "$PROJECT_ROOT/.." && pwd)"
PACKAGES_ROOT="$CODING_ROOT/packages"
BIN_DIR="$PACKAGES_ROOT/ytdown/bin"
YTDLP_PATH="$BIN_DIR/yt-dlp"
URL="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux"

echo "Setting up portable yt-dlp in $BIN_DIR"
mkdir -p "$BIN_DIR"

TMP="$(mktemp)"
cleanup() { rm -f "$TMP"; }
trap cleanup EXIT

echo "Downloading $URL"
curl -fL --retry 3 -A "Mozilla/5.0" -o "$TMP" "$URL"
chmod +x "$TMP"

if ! file "$TMP" | grep -qi 'elf'; then
  echo "Downloaded file does not look like a Linux ELF binary." >&2
  file "$TMP" >&2 || true
  exit 1
fi

mv -f "$TMP" "$YTDLP_PATH"
chmod +x "$YTDLP_PATH"
trap - EXIT

echo "Verifying..."
"$YTDLP_PATH" --version

echo "Done: $YTDLP_PATH"
