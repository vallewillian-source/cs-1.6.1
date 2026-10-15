#!/usr/bin/env bash
# Each Python sidecar gets its own environment, outside the installed package.
set -euo pipefail
BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for package in "$BASE/game/mods/"*; do
  [ -f "$package/requirements.txt" ] || continue
  state="$BASE/game/mod-state/$(basename "$package")"
  mkdir -p "$state"
  [ -x "$state/.venv/bin/python" ] || python3 -m venv "$state/.venv"
  "$state/.venv/bin/python" -m pip install --disable-pip-version-check -r "$package/requirements.txt"
done
