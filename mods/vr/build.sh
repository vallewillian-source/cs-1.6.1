#!/usr/bin/env bash
# Builda o Mod vr (OpenXR) e instala em game/mods/ — rootless via podman.
# Linka libopenxr_loader (buildado pelo scripts/build-openxr.sh) e copia a
# lib pro game/mods/ com rpath $ORIGIN (o mod é autossuficiente no host).
# Semeia também game/mods/vr_weapon.cfg (encaixe da arma) — só se ainda não
# existir: esse arquivo é para você calibrar, não para o build sobrescrever.
set -euo pipefail

BASE="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$BASE/mods/vr"
API="$BASE/src/xash3d-fwgs/engine/common"
OXR="$BASE/src/openxr-sdk/install"
OUT="$BASE/game/mods"
IMG="docker.io/steamcmd/steamcmd:latest"

[ -f "$OXR/lib/libopenxr_loader.so" ] || { echo "ERRO: OpenXR-SDK não buildado — rode scripts/build-openxr.sh antes"; exit 1; }
mkdir -p "$OUT"

podman run --rm --security-opt label=disable \
  -v "$SRC":/src \
  -v "$API":/api:ro \
  -v "$OXR":/oxr:ro \
  --entrypoint bash \
  "$IMG" -c '
    set -e
    apt-get update -qq
    apt-get install -y -qq g++ libx11-dev libgl1-mesa-dev >/dev/null
    g++ -O2 -fPIC -shared -Wall -Wextra -std=c++17 \
      -I/api -I/oxr/include \
      -o /src/mod_vr.so /src/mod_vr.cpp \
      -L/oxr/lib -lopenxr_loader -lX11 \
      -Wl,-rpath,"\$ORIGIN" -Wl,--no-as-needed
  '

cp "$SRC/mod_vr.so" "$OUT/mod_vr.so"
# loader do OpenXR ao lado do mod (rpath $ORIGIN resolve)
for f in libopenxr_loader.so libopenxr_loader.so.1 libopenxr_loader.so.1.1.63; do
  [ -e "$OXR/lib/$f" ] && cp -a "$OXR/lib/$f" "$OUT/$f"
done

# encaixe da arma: template só na primeira vez (nunca sobrescreve a sua calibração)
[ -e "$OUT/vr_weapon.cfg" ] || cp "$SRC/vr_weapon.cfg" "$OUT/vr_weapon.cfg"

echo ">> OK: game/mods/mod_vr.so"
