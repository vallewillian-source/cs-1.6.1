#!/usr/bin/env bash
# Compatibility wrapper for the advisor package in the current managed session.
set -euo pipefail
BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$BASE/tools/botbrain/botbrain.py" "$@"
