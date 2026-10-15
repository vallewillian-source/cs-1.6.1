#!/usr/bin/env bash
# Build independent packages against the public SDK, then copy their directories.
set -euo pipefail
BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${CS_BUILD_IMAGE:-localhost/cs161-build:1}"
podman image exists "$IMAGE" || podman build -t "$IMAGE" -f "$BASE/scripts/Containerfile.mods" "$BASE/scripts"
podman run --rm --security-opt label=disable -v "$BASE":/project -w /project \
  --entrypoint python3 "$IMAGE" scripts/package-mods.py "$@"
