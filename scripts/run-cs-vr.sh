#!/usr/bin/env bash
# Inicia o CS 1.6 em VR (mod_vr via OpenXR + WiVRn) — Fase 2 do PLAN.md.
# Sem headset/runtime: o mod fica inativo e o jogo roda flat (fallback).
#
# Uso:
#   ./scripts/run-cs-vr.sh                        # menu principal em VR
#   ./scripts/run-cs-vr.sh +connect 127.0.0.1:27015
#   VR_DEBUG=1 VR_RESOLUTION_SCALE=0.8 ./scripts/run-cs-vr.sh
set -euo pipefail

BASE="$(cd "$(dirname "$0")/.." && pwd)"
GAME="$BASE/game"

[ -f "$GAME/mods/mod_vr.so" ] || { echo "ERRO: mods/vr/build.sh ainda não rodou (game/mods/mod_vr.so ausente)"; exit 1; }

# Runtime OpenXR: procura o WiVRn (instalação system, user e Flatpak);
# override manual com XR_RUNTIME_JSON=
if [ -z "${XR_RUNTIME_JSON:-}" ]; then
  for p in \
    "/usr/share/openxr/1/openxr_wivrn.json" \
    "$HOME/.local/share/openxr/1/openxr_wivrn.json" \
    "$HOME/.local/share/openxr/1/openxr_runtime.json" \
    "$HOME/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/"*.json \
    "/var/lib/flatpak/app/io.github.wivrn.wivrn/current/active/files/share/openxr/"*.json; do
    if [ -f "$p" ]; then XR_RUNTIME_JSON="$p"; break; fi
  done
  [ -n "${XR_RUNTIME_JSON:-}" ] && echo "runtime OpenXR: $XR_RUNTIME_JSON" || echo "aviso: runtime OpenXR do WiVRn não encontrado — o mod deve ficar inativo (flat)"
fi
export XR_RUNTIME_JSON

# Render: sem vsync (o pacing é o do HMD), sem render targets internos do
# ref_gl (scene target/MSAA bagunçariam os FBOs dos olhos), fps solto.
cd "$GAME"
exec env \
  LD_LIBRARY_PATH="$GAME/mods${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ./xash3d -game cstrike -mod vr \
  -gl_vsync 0 -r_scene_scale 1 -gl_msaa 0 -fps_max 200 "$@"
