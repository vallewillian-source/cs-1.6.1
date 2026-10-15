#!/usr/bin/env bash
# Compatibility entrypoint: local server + installed sidecars, without a client.
set -euo pipefail
BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$BASE/scripts/run-cs.sh" --server-only --profile flat-dev --port "${PORT:-27015}" --map "${MAP:-de_dust2}" "$@"
