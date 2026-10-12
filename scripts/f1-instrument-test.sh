#!/usr/bin/env bash
# Instrumenta e testa: cstrike.rc e userconfig.d executam?
set -uo pipefail
GAME=/home/willianvalle/Workspace/cs-opensource/game
cd "$GAME" || exit 1
rm -f f1conn.log
timeout -s INT 20 stdbuf -oL -eL ./xash3d -game cstrike -log f1conn.log +name F1Inst > /dev/null 2>&1
echo "--- markers ---"
grep -E 'USERCONFIG_OK|CSTRIKE_RC' f1conn.log | head -3
wc -l < f1conn.log
