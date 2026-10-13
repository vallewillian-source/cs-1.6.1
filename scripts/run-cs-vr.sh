#!/usr/bin/env bash
# Inicia o CS 1.6 em VR (mod_vr via OpenXR + WiVRn) — Fase 2 do PLAN.md.
# Sem headset/runtime: o mod fica inativo e o jogo roda flat (fallback).
#
# O servidor de bots (com o cérebro Jev — docs/BOTBRAIN.md) sobe junto e o
# cliente entra nele: um comando só, sem terminal extra. Se já houver um
# servidor na porta, ele apenas conecta (e não derruba o que não subiu).
#
# Uso:
#   ./scripts/run-cs-vr.sh                        # servidor de bots + entra em VR
#   CS_BOTS=0 ./scripts/run-cs-vr.sh              # sem bots (menu principal, como antes)
#   ./scripts/run-cs-vr.sh +connect outro:27015   # respeita o seu +connect (não sobe servidor)
#   CS_BOT_PORT=27016 ./scripts/run-cs-vr.sh
#   VR_DEBUG=1 VR_RESOLUTION_SCALE=0.8 ./scripts/run-cs-vr.sh
set -euo pipefail

BASE="$(cd "$(dirname "$0")/.." && pwd)"
GAME="$BASE/game"

[ -f "$GAME/mods/mod_vr.so" ] || { echo "ERRO: mods/vr/build.sh ainda não rodou (game/mods/mod_vr.so ausente)"; exit 1; }

# Pré-voo: o xrCreateInstance do OpenXR BLOQUEIA enquanto não houver headset
# conectado. Conta apenas servidores INDEPENDENTES: o wivrn-server gera um
# processo filho (worker) que compartilha o mesmo socket de IPC — dois
# processos na árvore são um servidor só, e avisar aí manda o usuário matar o
# servidor que está certo.
NSRV=0
for pid in $(pgrep -x wivrn-server 2>/dev/null || true); do
  ppid=$(ps -o ppid= -p "$pid" 2>/dev/null | tr -d ' ')
  pai=$(ps -o comm= -p "${ppid:-0}" 2>/dev/null || true)
  [ "$pai" = "wivrn-server" ] && continue  # worker do mesmo runtime
  NSRV=$((NSRV + 1))
done

if [ "$NSRV" = "0" ]; then
  echo "AVISO: nenhum wivrn-server rodando — abra o wivrn-dashboard e conecte o headset antes (o jogo vai abrir flat)."
elif [ "$NSRV" -gt 1 ]; then
  echo "AVISO: $NSRV wivrn-server independentes rodando ao mesmo tempo — o headset pode estar ligado ao errado e o jogo travar no init. Feche os extras (pkill -x wivrn-server) e reconecte o headset."
fi

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

# Video driver: o mod usa a plataforma Xlib/GLX do OpenXR
# (XR_KHR_opengl_enable), então precisa que o engine tenha uma janela X11.
# Numa sessão Wayland o SDL escolhe o driver Wayland por padrão, que usa EGL e
# não tem contexto GLX nenhum: o mod fica inativo ("sem contexto GLX
# (video=wayland)" no painel). XWayland dá conta — é o driver x11 aqui.
# Respeita um SDL_VIDEODRIVER já definido pelo usuário (ex.: wayland, para
# experimentar o caminho EGL quando o mod passar a suportá-lo).
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"

# Resolução de render por olho — o outro lado do orçamento de streaming.
# O runtime "recomenda" 2476x2594 por olho (e era isso que renderizávamos), mas
# o WiVRn NÃO transmite nesse tamanho: ele codifica 1280x1344 por olho (o que o
# app do headset pediu, conforme o log do servidor) e reduz antes de codificar.
# Renderizar 3,7x mais pixels do que sai no stream é trabalho jogado fora em
# duas pontas (render do engine + redução no compositor) sem mudar um pixel do
# que chega no headset. 1280/2476 = 0,517 → 0,52 entrega exatamente o que é
# transmitido. Ajuste fino: 0,6–0,7 mantém uma sobra de supersampling (imagem um
# pouco mais definida, mais custo); se um dia subir a resolução no app do WiVRn,
# suba esta escala na mesma proporção. Acompanha a linha `perf:` do mod.
export VR_RESOLUTION_SCALE="${VR_RESOLUTION_SCALE:-0.52}"

# Render/pacing — tudo por COMANDO DE CONSOLE (`+cvar valor`), não pela forma
# `-cvar valor`: o `-` é aplicado antes dos configs e o config.cfg do jogador
# sobrescreve depois. Isso já mordeu: com o vsync ligado, o loop inteiro
# (engine + VR) ficava travado no refresh do MONITOR (60 Hz) enquanto o HMD
# pede 90 Hz — 2 de cada 3 quadros faltando, o que aparece como travamento e
# áreas PRETAS nas partes novas do mapa ao virar a cabeça (o compositor não tem
# conteúdo para reprojetar). Medido aqui: 60 fps → 90 fps só trocando a forma.
#
# - gl_vsync 0: o relógio é o do HMD (xrWaitFrame), nunca o do monitor.
# - fps_max: teto de folga (o pacing real vem do runtime).
# - r_scene_scale 1 / gl_msaa 0: o scene target interno do ref_gl rebinda FBO
#   por cima do nosso e bagunça os olhos — precisa ficar desligado.
# - r_speeds 0: sem HUD de estatísticas do renderer (custo por frame).
#
# --- servidor de bots + cérebro Jev (ligado por padrão) ------------------
# O dedicado roda no podman (32-bit) e leva ~15 s para carregar mapa + bots; o
# cliente só entra quando a porta UDP já está de pé. docs/BOTBRAIN.md.
CS_BOTS="${CS_BOTS:-1}"
BOT_PORT="${CS_BOT_PORT:-27015}"
STARTED_SERVER=0
BOT_PID=""

ARGS=("$@")
USR_CONNECT=0
for a in "${ARGS[@]}"; do
  [ "$a" = "+connect" ] && USR_CONNECT=1
done

server_up() { ss -lun 2>/dev/null | grep -q ":$BOT_PORT "; }

CLEANED=0
cleanup() {
  [ "$CLEANED" = "1" ] && return          # INT/TERM e EXIT chamam os dois
  CLEANED=1
  if [ "$STARTED_SERVER" = "1" ]; then
    # O run-botbrain.sh tem trap próprio que derruba o sidecar ao receber TERM;
    # o container tem nome fixo, então removê-lo é o cinto de segurança.
    [ -n "$BOT_PID" ] && kill "$BOT_PID" 2>/dev/null || true
    podman rm -f cs16-server >/dev/null 2>&1 || true
    echo "bots: servidor e sidecar encerrados."
  fi
}
trap cleanup EXIT INT TERM

if [ "$CS_BOTS" = "0" ]; then
  echo "bots: desligados (CS_BOTS=0) — abrindo sem servidor."
elif [ "$USR_CONNECT" = "1" ]; then
  echo "bots: você passou um +connect próprio — não vou subir servidor local."
elif server_up; then
  echo "bots: já há um servidor em :$BOT_PORT — conectando nele (não subo nem derrubo)."
  ARGS+=("+connect" "127.0.0.1:$BOT_PORT")
else
  mkdir -p "$BASE/logs"
  echo "bots: subindo servidor + sidecar Jev (4v4 de_dust2)… log em logs/botbrain-run.log"
  "$BASE/scripts/run-botbrain.sh" > "$BASE/logs/botbrain-run.log" 2>&1 &
  BOT_PID=$!
  STARTED_SERVER=1

  for _ in $(seq 1 90); do
    if server_up; then break; fi
    if ! kill -0 "$BOT_PID" 2>/dev/null; then
      echo "ERRO: o servidor de bots morreu ao subir — veja logs/botbrain-run.log" >&2
      exit 1
    fi
    sleep 1
  done

  if server_up; then
    echo "bots: servidor de pé em :$BOT_PORT."
  else
    echo "AVISO: a porta $BOT_PORT não abriu em 90 s — entrando de todo modo." >&2
  fi
  ARGS+=("+connect" "127.0.0.1:$BOT_PORT")
fi

cd "$GAME"
env LD_LIBRARY_PATH="$GAME/mods${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ./xash3d -game cstrike -mod vr \
  +gl_vsync 0 +fps_max 200 \
  +r_scene_scale 1 +gl_msaa 0 +r_speeds 0 "${ARGS[@]}"
