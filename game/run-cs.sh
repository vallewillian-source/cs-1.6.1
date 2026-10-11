#!/bin/bash
# ============================================================
# Cliente CS 1.6 — Xash3D FWGS + CS16Client
# ------------------------------------------------------------
# Roda nativo no host (64-bit, usa o SDL2 do sistema).
#
# Uso:  ./run-cs.sh                (menu → New Game / Find Servers)
#       ./run-cs.sh +connect 127.0.0.1:27015   (entra no servidor local)
# ============================================================
set -euo pipefail
BASE="$(cd "$(dirname "$0")" && pwd)"
cd "$BASE"
exec ./xash3d -game cstrike "$@"
