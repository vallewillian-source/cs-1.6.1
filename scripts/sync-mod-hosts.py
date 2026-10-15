#!/usr/bin/env python3
"""Install canonical SDK/host sources into disposable, patched upstream trees.

Engine/server integration call sites are published in patches/. This script copies
only owned sources and vendored dependencies; it never edits upstream call sites.
"""
from pathlib import Path
import os
import shutil

ROOT = Path(__file__).resolve().parents[1]


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and source.read_bytes() == destination.read_bytes():
        return
    temporary = destination.with_name(destination.name + f".sync-{os.getpid()}")
    shutil.copy2(source, temporary)
    temporary.replace(destination)


def main():
    engine = ROOT / "src/xash3d-fwgs"
    server = ROOT / "src/ReGameDLL_CS"
    if engine.is_dir():
        common = engine / "engine/common"
        for name in ("mod_api.h", "csmod_services.h"):
            copy(ROOT / "sdk/include" / name, common / name)
        copy(ROOT / "sdk/host/client.c", common / "modloader.c")
        copy(ROOT / "sdk/host/runtime.h", common / "runtime.h")
        copy(ROOT / "sdk/host/runtime.cpp", common / "csmod_runtime.cpp")
        copy(ROOT / "third_party/json/json.hpp", common / "json.hpp")
        copy(ROOT / "sdk/host/modui.cpp", engine / "engine/client/csmod_ui.cpp")
        for file in (ROOT / "third_party/imgui").rglob("*"):
            if file.is_file():
                copy(file, engine / "3rdparty/csmod-imgui" / file.relative_to(ROOT / "third_party/imgui"))
    if server.is_dir():
        target = server / "regamedll/dlls/modhost"
        for name in ("csmod_services.h", "csmod_server.h"):
            copy(ROOT / "sdk/include" / name, target / name)
        for name in ("runtime.cpp", "runtime.h", "server.cpp", "server.h"):
            copy(ROOT / "sdk/host" / name, target / name)
        copy(ROOT / "third_party/json/json.hpp", target / "json.hpp")


if __name__ == "__main__":
    main()
