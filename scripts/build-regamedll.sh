#!/usr/bin/env bash
#
# Builds the 32-bit ReGameDLL cs.so for the dedicated server.
#
# The Fedora host has no i386 multilib and no sudo, so the build runs inside
# the same podman steamcmd container the server itself uses (see
# server/run-server.sh). The source tree is bind-mounted at /src, so the
# build/ dir persists on the host and later runs are incremental.
#
# Usage:
#   scripts/build-regamedll.sh            # incremental
#   scripts/build-regamedll.sh --clean    # wipe build/ first
#
set -euo pipefail

BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$BASE/src/ReGameDLL_CS"
JOBS="${JOBS:-$(nproc)}"
CLEAN=0
[ "${1:-}" = "--clean" ] && CLEAN=1

echo "== ReGameDLL -> 32-bit cs.so (jobs=$JOBS, clean=$CLEAN) =="

if [ "$CLEAN" = "1" ]; then
  rm -rf "$SRC/build"
fi

# The advisor module and its hooks live in patches/ (because /src/ is a
# re-clonable checkout excluded from the repo). Re-apply them; idempotent.
bash "$BASE/patches/regamedll-bot-advisor/apply.sh" "$SRC"

INNER=$(cat <<'EOS'
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  build-essential cmake g++-multilib gcc-multilib git ca-certificates >/dev/null
mkdir -p /src/ReGameDLL_CS/build
cd /src/ReGameDLL_CS/build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 .. >/dev/null
make -j"$JOBS"
EOS
)

# The repo's src/ is mounted at /src so the existing build/ cache (generated
# with the source at /src/ReGameDLL_CS) stays valid and builds are incremental.
podman run --rm --security-opt label=disable \
  -v "$BASE/src":/src \
  -e JOBS="$JOBS" \
  --entrypoint bash docker.io/steamcmd/steamcmd:latest -c "$INNER"

install -m 0755 "$SRC/build/regamedll/cs.so" "$BASE/server/cstrike/dlls/cs.so"
echo "== installed: $BASE/server/cstrike/dlls/cs.so =="
file "$BASE/server/cstrike/dlls/cs.so"
