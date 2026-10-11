#!/bin/bash
# ============================================================
# Servidor dedicado CS 1.6 — ReHLDS + ReGameDLL_CS
# ------------------------------------------------------------
# Os binários do HLDS/ReHLDS são 32-bit e o host não tem libs
# multilib (sem sudo), então o servidor roda dentro de um
# container podman (imagem do SteamCMD, que já tem runtime
# i386). Usa a rede do próprio host (--network host), então o
# servidor fica acessível em :27015 na sua LAN normalmente.
#
# Uso:  ./run-server.sh [mapa]     (ex.: ./run-server.sh cs_office)
#       PORT=27016 ./run-server.sh de_aztec
# ============================================================
set -euo pipefail
BASE="$(cd "$(dirname "$0")/.." && pwd)"
MAPA="${1:-de_dust2}"
PORTA="${PORT:-27015}"
MAXPLAYERS="${MAXPLAYERS:-16}"

exec podman run --rm -it \
  --name cs16-server \
  --network host \
  --security-opt label=disable \
  -v "$BASE/server":/server \
  --entrypoint bash \
  docker.io/steamcmd/steamcmd:latest \
  -c "cd /server && LD_LIBRARY_PATH=/server exec ./hlds_linux \
      -game cstrike -console -port $PORTA \
      +map $MAPA +maxplayers $MAXPLAYERS \
      -pingboost 2 +sys_ticrate 1000 -nobreakpad"
