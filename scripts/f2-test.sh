#!/usr/bin/env bash
# Testes da Fase 2:
#   T1: regressão -mod null v2 (dedicado → stdout com evidência de load)
#   T2: -mod vr sem XR_RUNTIME_JSON → fallback flat (dedicado)
#   T3: -mod vr gráfico COM runtime WiVRn → sessão criada, IDLE sem headset
set -uo pipefail
GAME=/home/willianvalle/Workspace/cs-opensource/game
cd "$GAME" || exit 1
rm -f mods/mod_vr.log mods/mod_null.log

echo "########## T1: dedicado -mod null (regressão v2)"
timeout -s INT 15 ./xash3d -dedicated -game cstrike -mod null +map de_dust2 > /tmp/f2-t1.log 2>&1
grep -E 'Mods:|mod_null' /tmp/f2-t1.log | head -4

echo "########## T2: dedicado -mod vr SEM runtime"
unset XR_RUNTIME_JSON
timeout -s INT 15 ./xash3d -dedicated -game cstrike -mod vr +map de_dust2 > /tmp/f2-t2.log 2>&1
grep -E 'mod_vr' /tmp/f2-t2.log | head -5

echo "########## T3: gráfico -mod vr COM runtime WiVRn (sessão IDLE esperada)"
export XR_RUNTIME_JSON="$HOME/.local/share/openxr/1/openxr_wivrn.json"
rm -f mods/mod_vr.log
timeout -s INT 22 stdbuf -oL -eL ./xash3d -game cstrike -log f2.log -mod vr +name F2T3 > /dev/null 2>&1
echo "rc=$?"
echo "--- mods/mod_vr.log ---"
cat mods/mod_vr.log 2>/dev/null | head -10
echo "--- engine log (runtime prints) ---"
grep -E 'mod_vr' f2.log | head -5
