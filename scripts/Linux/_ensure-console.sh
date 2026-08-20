# Sourced by packaging scripts in this folder.
# Always opens a dedicated console window. When it closes, the script has finished.
#
# Skip:  FOURKDOWNER_NO_CONSOLE=1 ./main.sh

if [[ -n "${FOURKDOWNER_IN_TERMINAL:-}" ]]; then
  return 0
fi

if [[ -n "${CI:-}" || -n "${FOURKDOWNER_NO_CONSOLE:-}" ]]; then
  return 0
fi

if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
  echo "Error: no graphical display; cannot open a console window." >&2
  exit 1
fi

_caller="$(readlink -f "$0")"
export FOURKDOWNER_IN_TERMINAL=1

if command -v konsole >/dev/null 2>&1; then
  exec konsole -e bash "$_caller" "$@"
fi
if command -v xdg-terminal-exec >/dev/null 2>&1; then
  exec xdg-terminal-exec -- bash "$_caller" "$@"
fi
if command -v gnome-terminal >/dev/null 2>&1; then
  exec gnome-terminal --wait -- bash "$_caller" "$@"
fi
if command -v xfce4-terminal >/dev/null 2>&1; then
  exec xfce4-terminal -e "bash $(printf '%q ' "$_caller" "$@")"
fi
if command -v alacritty >/dev/null 2>&1; then
  exec alacritty -e bash "$_caller" "$@"
fi
if command -v kitty >/dev/null 2>&1; then
  exec kitty bash "$_caller" "$@"
fi
if command -v xterm >/dev/null 2>&1; then
  exec xterm -e bash "$_caller" "$@"
fi

echo "Error: no terminal emulator found (install konsole or xterm)." >&2
exit 1
