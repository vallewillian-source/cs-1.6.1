#!/usr/bin/env bash
#
# Runs the botbrain sidecar (the "slow brain").
#
# It reuses the jev-ultrafast virtualenv (which already has httpx[http2]) and
# its .env credentials, so the same TypeSafe key powers both the browser agent
# and the game bots.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JEV_DIR="${JEV_DIR:-$HOME/Workspace/jev-ultrafast}"

PY="$JEV_DIR/.venv/bin/python"
if [ ! -x "$PY" ]; then
  PY="$(command -v python3)"
fi

exec "$PY" -u "$HERE/botbrain.py" "$@"
