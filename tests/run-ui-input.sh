#!/usr/bin/env bash
set -euo pipefail
BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
podman run --rm --security-opt label=disable -v "$BASE":/project -w /project \
  --entrypoint bash "${CS_BUILD_IMAGE:-localhost/cs161-build:1}" -c '
set -e
mkdir -p build/validation
c++ -std=c++17 -O0 -pthread -Isdk/host -Isdk/include -Ithird_party/json \
  -Ithird_party/imgui -Ithird_party/imgui/backends $(pkg-config --cflags sdl2) \
  tests/mods/ui_input.cpp sdk/host/runtime.cpp third_party/imgui/imgui.cpp \
  third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
  third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_sdl2.cpp \
  third_party/imgui/backends/imgui_impl_opengl2.cpp -lSDL2 -lGL -ldl -o build/validation/ui-input
task_state=$(mktemp -d)
trap '\''rm -rf "$task_state"'\'' EXIT
SDL_VIDEODRIVER=dummy build/validation/ui-input "$task_state"
echo "PASS SDL button clicks, Esc-menu interaction, typing, shortcut cleanup and window bounds"
'
