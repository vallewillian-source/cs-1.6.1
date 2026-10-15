#!/usr/bin/env python3
"""Check fresh upstream reconstruction without touching working checkouts."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
for name, revision, patch in (
    ("xash3d-fwgs", "590869d280aad5b3646ab39d956a9ea3ac40ba4c", "patches/xash3d-fwgs-mod-abi/mod-abi.patch"),
    ("ReGameDLL_CS", "4a50c42e85fd3778c2b7d24731c7d30c83bbab01", "patches/regamedll-mod-host/hooks.patch"),
):
    with tempfile.TemporaryDirectory(prefix="cs161-patch-") as temp:
        # A checkout applies upstream .gitattributes (ReGameDLL uses CRLF).
        # git archive + a bare directory does not reproduce git apply semantics.
        subprocess.run(["git", "clone", "--shared", "--no-checkout", "--quiet", str(ROOT / "src" / name), temp], check=True)
        subprocess.run(["git", "checkout", "--quiet", "--detach", revision], cwd=temp, check=True)
        for arguments in (("--check",), (), ("--reverse", "--check")):
            subprocess.run(["git", "apply", *arguments, str(ROOT / patch)], cwd=temp, check=True)
    print(f"{name}: fresh apply and complete reverse verification passed")
