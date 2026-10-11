#!/usr/bin/env bash
# Compila o Xash3D FWGS (branch vr-mod) SEM root e instala em game/.
#
# Notas:
# - Container podman (imagem steamcmd/steamcmd = Ubuntu c/ toolchain); preciso
#   de --entrypoint bash porque o ENTRYPOINT da imagem é o próprio steamcmd.
# - O waf exige git DENTRO do container para computar o buildnum; sem ele o
#   cs16-client recusa a engine (MIN_XASH_VERSION 4190).
# - patchelf normaliza o soname do bzip2: a imagem flutuou de base e passou a
#   linkar libbz2.so.1.0 (Ubuntu); o Fedora só tem libbz2.so.1. O
#   --replace-needed é no-op se o NEEDED já estiver certo.
set -euo pipefail

BASE="$(cd "$(dirname "$0")/.." && pwd)"
XASH="$BASE/src/xash3d-fwgs"
GAME="$BASE/game"
IMG="docker.io/steamcmd/steamcmd:latest"

podman run --rm --security-opt label=disable -v "$XASH":/src --entrypoint bash \
  "$IMG" -exc '
    apt-get update -qq
    apt-get install -y -qq build-essential python3 libsdl2-dev libfreetype-dev git patchelf >/dev/null
    cd /src
    ./waf configure -T release -8
    ./waf build -j"$(nproc)"
    patchelf --replace-needed libbz2.so.1.0 libbz2.so.1 build/engine/libxash.so || true
  '

echo ">> instalando em game/"
cp -a "$XASH/build/game_launch/xash3d"           "$GAME/xash3d"
cp -a "$XASH/build/engine/libxash.so"            "$GAME/libxash.so"
cp -a "$XASH/build/filesystem/filesystem_stdio.so" "$GAME/filesystem_stdio.so"
cp -a "$XASH/build/3rdparty/mainui/libmenu.so"   "$GAME/libmenu.so"
cp -a "$XASH/build/ref/gl/libref_gl.so"          "$GAME/libref_gl.so"
cp -a "$XASH/build/3rdparty/extras/extras.pk3"   "$GAME/valve/extras.pk3"
echo ">> OK — Xash3D instalado em $GAME"
