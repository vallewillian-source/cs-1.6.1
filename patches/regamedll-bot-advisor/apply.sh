#!/usr/bin/env bash
# Compatibility entrypoint; the advisor now belongs to its package.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$ROOT/scripts/apply-patches.py"
