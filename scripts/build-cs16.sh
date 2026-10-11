#!/usr/bin/env bash
# Compila o CS16Client (branch vr-mod) SEM root e instala em game/cstrike/.
# Mesmo padrão do build-xash.sh (container steamcmd, --entrypoint bash).
set -euo pipefail

BASE="$(cd "$(dirname "$0")/.." && pwd)"
CS16="$BASE/src/cs16-client"
GAME="$BASE/game"
IMG="docker.io/steamcmd/steamcmd:latest"

podman run --rm --security-opt label=disable -v "$CS16":/src --entrypoint bash \
  "$IMG" -exc '
    apt-get update -qq
    apt-get install -y -qq build-essential cmake python3 libfontconfig-dev libfreetype-dev git >/dev/null
    cd /src
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    cmake --build build -j"$(nproc)"
  '

echo ">> instalando em game/cstrike/"
cp -a "$CS16/build/cl_dll/client_amd64.so"                          "$GAME/cstrike/cl_dlls/client_amd64.so"
cp -a "$CS16/build/3rdparty/mainui_cpp/menu_amd64.so"               "$GAME/cstrike/cl_dlls/menu_amd64.so"
cp -a "$CS16/build/3rdparty/ReGameDLL_CS/regamedll/cs_amd64.so"     "$GAME/cstrike/dlls/cs_amd64.so"
cp -a "$CS16/build/3rdparty/yapb/yapb_amd64.so"                     "$GAME/cstrike/dlls/yapb_amd64.so"
cp -a "$CS16/build/extras.pk3"                                      "$GAME/cstrike/extras.pk3"
echo ">> OK — CS16Client instalado em $GAME/cstrike/"
