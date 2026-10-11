#!/usr/bin/env bash
# Builda o Mod null (template) e instala em game/mods/ — rootless via podman.
# Padrão para os outros Mods: só precisa de gcc + mod_api.h (engine common/).
set -euo pipefail

BASE="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$BASE/mods/null"
API="$BASE/src/xash3d-fwgs/engine/common"
OUT="$BASE/game/mods"

mkdir -p "$OUT"

podman run --rm --security-opt label=disable \
  -v "$SRC":/src \
  -v "$API":/api:ro \
  --entrypoint bash \
  docker.io/steamcmd/steamcmd:latest -c '
    apt-get update -qq
    apt-get install -y -qq gcc >/dev/null
    gcc -O2 -fPIC -shared -Wall -Wextra -I/api -o /src/mod_null.so /src/mod_null.c
  '

cp "$SRC/mod_null.so" "$OUT/mod_null.so"
echo ">> OK: game/mods/mod_null.so"
