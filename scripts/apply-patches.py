#!/usr/bin/env python3
"""Apply complete integration patches or verify that every hunk is present."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PATCHES = (
    ("xash3d-fwgs", "patches/xash3d-fwgs-mod-abi/mod-abi.patch"),
    ("ReGameDLL_CS", "patches/regamedll-mod-host/hooks.patch"),
)


def check(source, patch, reverse=False):
    args = ["git", "-C", str(source), "apply", "--check"]
    if reverse:
        args.append("--reverse")
    return subprocess.run([*args, str(patch)], capture_output=True).returncode == 0


def main():
    for name, relative in PATCHES:
        source, patch = ROOT / "src" / name, ROOT / relative
        if not source.is_dir():
            raise SystemExit(f"Missing upstream source: {source}")
        if check(source, patch, reverse=True):
            print(f"{name}: complete patch already present")
        elif check(source, patch):
            subprocess.run(["git", "-C", str(source), "apply", str(patch)], check=True)
            print(f"{name}: applied")
        else:
            raise SystemExit(f"{name}: source diverges or patch is partially applied. Preserve local changes and use the pinned base from docs/BUILD.md.")
    subprocess.run([sys.executable, str(ROOT / "scripts/sync-mod-hosts.py")], check=True)


if __name__ == "__main__":
    main()
