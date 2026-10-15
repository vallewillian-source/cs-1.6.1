#!/usr/bin/env python3
"""Build and install the supplied example packages; runtime discovery is generic."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def compile_mod(ident, kind, files, extra=()):
    source = ROOT / "mods" / ident
    output = ROOT / "build/mods" / ident / (kind + ".so")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = ["g++", "-std=c++17", "-O2", "-g", "-fPIC", "-shared", "-fvisibility=hidden",
               "-Wall", "-Wextra", "-Wl,-z,defs", "-I" + str(ROOT / "sdk/include"),
               "-I" + str(ROOT / "third_party/json"), "-o", str(output)]
    if kind == "server":
        command.append("-m32")
    command += [str(source / f) for f in files] + list(extra)
    subprocess.run(command, check=True)
    return output


def install(ident, binaries):
    source, target = ROOT / "mods" / ident, ROOT / "game/mods" / ident
    target.mkdir(parents=True, exist_ok=True)
    for name in ("mod.json", "LICENSE", "requirements.txt"):
        if (source / name).is_file():
            shutil.copy2(source / name, target / name)
    for binary in binaries:
        temporary = target / (binary.name + ".new")
        shutil.copy2(binary, temporary)
        temporary.replace(target / binary.name)
    if (source / "sidecar").is_dir():
        shutil.copytree(source / "sidecar", target / "sidecar", dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("__pycache__"))
    if (source / "assets").is_dir():
        shutil.copytree(source / "assets", target / "assets", dirs_exist_ok=True)
    state = ROOT / "game/mod-state" / ident
    state.mkdir(parents=True, exist_ok=True)
    for config in source.glob("*.cfg"):
        old = ROOT / "game/mods" / config.name
        destination = state / config.name
        if not destination.exists():
            shutil.copy2(old if old.exists() else config, destination)
    if ident == "vr":
        for name in ("vr_menu.cfg", "vr_ipd.cfg"):
            old = ROOT / "game/mods" / name
            if old.is_file() and not (state / name).exists():
                shutil.copy2(old, state / name)


def main():
    selected = sys.argv[1:] or ["null", "bots-jev", "x-ray-debug", "vr"]
    for ident in selected:
        if ident == "null":
            binaries = [compile_mod(ident, "client", ["mod_null.c"])]
        elif ident == "bots-jev":
            binaries = [compile_mod(ident, "client", ["client/panel.cpp"]), compile_mod(ident, "server", ["server/advisor.cpp"])]
        elif ident == "x-ray-debug":
            binaries = [compile_mod(ident, "client", ["client.cpp"]), compile_mod(ident, "server", ["server.cpp"])]
        elif ident == "vr":
            oxr = ROOT / "src/openxr-sdk/install"
            binaries = [compile_mod(ident, "client", ["mod_vr.cpp", "vr_hands.cpp", "vr_paths.cpp", "vr_async.cpp"], (
                "-I" + str(oxr / "include"), "-L" + str(oxr / "lib"), "-lopenxr_loader", "-lX11", "-ldl", "-pthread", "-Wl,-rpath,$ORIGIN"))]
        else:
            raise SystemExit(f"Unknown supplied package: {ident}")
        install(ident, binaries)
        if ident == "vr":
            for library in (ROOT / "src/openxr-sdk/install/lib").glob("libopenxr_loader.so*"):
                shutil.copy2(library.resolve(), ROOT / "game/mods/vr" / library.name)
        print(f"Installed {ident}", flush=True)


if __name__ == "__main__":
    main()
