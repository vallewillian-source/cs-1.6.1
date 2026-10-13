#!/usr/bin/env bash
#
# Runs the local dedicated CS 1.6 server with the bots' "slow brain" attached.
#
#   [sidecar botbrain]  <-- observe_<team>.txt / plan_<team>.txt --> [cs.so]
#
# Messages to the TypeSafe/Jev model are made by tools/botbrain (host side);
# the server runs in the podman steamcmd container like server/run-server.sh.
#
set -euo pipefail

BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAP="${MAP:-de_dust2}"
PORT="${PORT:-27015}"
MAXPLAYERS="${MAXPLAYERS:-12}"
ADVISOR_DIR="$BASE/server/cstrike/advisor"

mkdir -p "$ADVISOR_DIR" "$BASE/logs"

# start clean so a stale plan from a previous run cannot be applied
rm -f "$ADVISOR_DIR"/observe_*.txt "$ADVISOR_DIR"/plan_*.txt

echo "== sidecar: tools/botbrain -> $BASE/logs/botbrain.log =="
"$BASE/tools/botbrain/run.sh" > "$BASE/logs/botbrain.log" 2>&1 &
BRAIN=$!

cleanup() { kill "$BRAIN" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

# keep the server console interactive when we have a terminal, but still work
# when launched from a script/CI (no TTY)
TTY_FLAG="-i"
[ -t 0 ] && TTY_FLAG="-it"

echo "== server: $MAP :$PORT (podman) =="
# --replace: a leftover cs16-server from a previous run must not block the
# launch (and the previous instance cannot still be serving the same port)
podman run --rm --replace $TTY_FLAG \
  --name cs16-server \
  --network host \
  --security-opt label=disable \
  -v "$BASE/server":/server \
  --entrypoint bash \
  docker.io/steamcmd/steamcmd:latest \
  -c "cd /server && LD_LIBRARY_PATH=/server exec ./hlds_linux \
      -game cstrike -console -port $PORT \
      +map $MAP +maxplayers $MAXPLAYERS \
      +exec advisor.cfg \
      -pingboost 2 +sys_ticrate 1000 -nobreakpad"
