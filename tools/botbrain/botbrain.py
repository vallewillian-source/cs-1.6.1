#!/usr/bin/env python3
"""Compatibility CLI. The implementation belongs to the bots-jev package."""
import json
import os
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
state = ROOT / "game/mod-state/bots-jev"
if "--report" in sys.argv:
    path = state / "decisions.jsonl"
    rows = [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []
    calls = [r for r in rows if r.get("source") == "jev"]
    print(json.dumps(dict(decisions=len(rows), model_calls=len(calls),
                         input_tokens=sum((r.get("usage") or {}).get("input_tokens", 0) for r in calls),
                         output_tokens=sum((r.get("usage") or {}).get("output_tokens", 0) for r in calls)), indent=2))
    raise SystemExit(0)
control = json.loads((state.parent / "host/control.json").read_text())
os.environ.update(CS_MOD_STATE=str(state), CS_MOD_SESSION=control["session"],
                  CS_MOD_GENERATION=str(control["mods"]["bots-jev"]["generation"]))
python = state / ".venv/bin/python"
os.execv(str(python), [str(python), "-u", str(ROOT / "mods/bots-jev/sidecar/brain.py"), *sys.argv[1:]])
