#!/usr/bin/env bash
# Teste de conexão Fase 1: cliente gráfico com -mod null + +connect, A2S polling.
set -uo pipefail
GAME=/home/willianvalle/Workspace/cs-opensource/game
cd "$GAME" || exit 1

rm -f f1conn.log mods/mod_null.log
timeout -s INT 45 stdbuf -oL -eL ./xash3d -game cstrike -log f1conn.log \
  -mod null +name F1Fix +connect 127.0.0.1:27015 > /dev/null 2>&1 &
CPID=$!
sleep 10
echo "processos xash3d: $(pgrep -c xash3d)"
python3 /tmp/a2s.py
sleep 8
python3 /tmp/a2s.py
wait $CPID
echo "client_rc=$?"
echo "--- log do engine ---"
grep -E 'CSTRIKE_RC|Scoring|Connecting|Mods:' f1conn.log | head -6
echo "--- log do mod ---"
head -4 mods/mod_null.log 2>/dev/null
