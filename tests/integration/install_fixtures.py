#!/usr/bin/env python3
"""Install/remove disposable SDK-only client packages for live loader tests."""
from pathlib import Path
import json
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
IDS = ("test-bad-abi", "test-bad-size", "test-no-symbol", "test-refuse-init", "test-missing", "test-camera")
if "--remove" in sys.argv:
    for ident in IDS:
        shutil.rmtree(ROOT / "game/mods" / ident, ignore_errors=True)
    raise SystemExit(0)

for ident in IDS:
    folder = ROOT / "game/mods" / ident
    if folder.exists():
        raise SystemExit(f"Refusing to replace existing test package: {folder}")
    folder.mkdir()
    manifest = dict(manifest_version=1, id=ident, name=ident, version="1.0", client_abi=9,
                    components={"client": "client.so"}, claims=["camera"] if ident == "test-camera" else [],
                    defaults=dict(enabled=False, panel=False))
    (folder / "mod.json").write_text(json.dumps(manifest))
    if ident == "test-missing":
        continue
    code = '''#include "mod_api.h"
static const csmod_services_t *s;
static int init(const mod_engine_api_t *h){s=h->services;return REFUSE?0:1;}
static void shutdown(){if(s)s->Log(s->context,0,"partial initialization cleanup");}
static mod_api_t api;
extern "C" mod_api_t *ENTRY(){api.api_version=VERSION;api.size=SIZE;
api.mod_name=IDENTITY;api.Mod_Init=init;api.Mod_Shutdown=shutdown;api.capabilities=CAP;return &api;}
'''
    output = ROOT / "build/fixtures" / (ident + ".cpp")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(code)
    definitions = dict(REFUSE=int(ident == "test-refuse-init"), ENTRY="DifferentEntry" if ident == "test-no-symbol" else "ModGetAPI",
                       VERSION=8 if ident == "test-bad-abi" else 9, SIZE="0" if ident == "test-bad-size" else "sizeof(mod_api_t)",
                       IDENTITY='"'+ident+'"', CAP="MOD_CAP_DISPLAY" if ident == "test-camera" else "0")
    subprocess.run(["c++", "-std=c++17", "-fPIC", "-shared", "-I"+str(ROOT/"sdk/include"),
                    *[f"-D{k}={v}" for k, v in definitions.items()], str(output), "-o", str(folder/"client.so")], check=True)
print("Installed disposable test packages; restart before testing")
