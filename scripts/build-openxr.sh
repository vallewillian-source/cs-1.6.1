#!/usr/bin/env bash
# Fase 0.3 — Baixa e compila o OpenXR-SDK (loader + headers) SEM root, via podman.
#
# Saída:
#   src/openxr-sdk/OpenXR-SDK-Source-release-<ver>/   fontes
#   src/openxr-sdk/install/include/openxr/            headers (openxr.h, openxr_platform.h, ...)
#   src/openxr-sdk/install/lib/libopenxr_loader.so    loader dinâmico p/ linkar o mod_vr
#
# Uso:
#   ./scripts/build-openxr.sh            # versão pinada no default abaixo
#   ./scripts/build-openxr.sh 1.1.62     # versão específica
#   OPENXR_SDK_VERSION=latest ./scripts/build-openxr.sh   # pergunta à API do GitHub
set -euo pipefail

BASE="$(cd "$(dirname "$0")/.." && pwd)"
VER="${1:-${OPENXR_SDK_VERSION:-1.1.63}}"
SRC="$BASE/src/openxr-sdk"
IMG="docker.io/steamcmd/steamcmd:latest"

if [ "$VER" = "latest" ]; then
  VER="$(curl -sf https://api.github.com/repos/KhronosGroup/OpenXR-SDK-Source/releases/latest \
        | grep -m1 -o 'release-[0-9.]*' | sed 's/^release-//')"
  [ -n "$VER" ] || { echo "ERRO: não obtive a versão latest da API do GitHub" >&2; exit 1; }
fi

TAG="release-$VER"
DIR="$SRC/OpenXR-SDK-Source-$TAG"

if [ ! -d "$DIR" ]; then
  mkdir -p "$SRC"
  echo ">> baixando OpenXR-SDK-Source $TAG"
  curl -fL "https://codeload.github.com/KhronosGroup/OpenXR-SDK-Source/tar.gz/refs/tags/$TAG" \
    | tar -xz -C "$SRC"
fi

echo ">> compilando OpenXR $VER (loader dinâmico + headers) em container podman"
podman run --rm --security-opt label=disable \
  -v "$SRC":/src \
  "$IMG" bash -exc '
    apt-get update -qq
    apt-get install -y -qq build-essential cmake python3 libjsoncpp-dev >/dev/null
    cd /src/OpenXR-SDK-Source-*
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/src/install \
      -DBUILD_LOADER=ON -DDYNAMIC_LOADER=ON \
      -DBUILD_API_LAYERS=OFF -DBUILD_TESTS=OFF -DBUILD_CONFORMANCE_TESTS=OFF \
      -DBUILD_ALL_EXTENSIONS=ON
    cmake --build build -j"$(nproc)" --target install
  '

test -f "$SRC/install/lib/libopenxr_loader.so"
test -f "$SRC/install/include/openxr/openxr.h"
echo ">> OK: OpenXR $VER pronto"
ls -la "$SRC/install/lib/"libopenxr_loader* "$SRC/install/include/openxr/"
