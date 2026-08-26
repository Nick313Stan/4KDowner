#!/usr/bin/env bash
# Read project(VERSION x.y.z) from CMakeLists.txt

get_project_app_version() {
  local project_root="$1"
  local cmake_lists="$project_root/CMakeLists.txt"
  if [[ ! -f "$cmake_lists" ]]; then
    echo "1.2.0"
    return
  fi
  local ver
  ver="$(grep -E '^[[:space:]]*VERSION[[:space:]]+[0-9.]+' "$cmake_lists" | head -n1 | sed -E 's/.*VERSION[[:space:]]+([0-9.]+).*/\1/')"
  if [[ -n "$ver" ]]; then
    echo "$ver"
  else
    echo "1.2.0"
  fi
}
