#!/usr/bin/env bash
#
# Re-applies the bot-advisor module into the ReGameDLL source tree.
#
# The repo's .gitignore excludes /src/ (it is a re-clonable checkout), so the
# module and its hooks live here as a patch and are (re)applied before the
# build. Idempotent: safe to run over an already-patched tree.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="${1:-$ROOT/src/ReGameDLL_CS}"

if [ ! -d "$SRC/regamedll/dlls/bot" ]; then
  echo "apply.sh: no ReGameDLL source at '$SRC'" >&2
  exit 1
fi

DEST="$SRC/regamedll/dlls/bot/advisor"
mkdir -p "$DEST"
install -m 0644 "$HERE/cs_bot_advisor.h" "$DEST/cs_bot_advisor.h"
install -m 0644 "$HERE/cs_bot_advisor.cpp" "$DEST/cs_bot_advisor.cpp"

# The include in cs_bot_init.cpp is the marker that the hooks are in place.
if grep -q 'advisor/cs_bot_advisor.h' "$SRC/regamedll/dlls/bot/cs_bot_init.cpp"; then
  echo "apply.sh: hooks already applied"
else
  ( cd "$SRC" && patch -p1 --forward --silent < "$HERE/hooks.patch" )
  echo "apply.sh: hooks applied"
fi
