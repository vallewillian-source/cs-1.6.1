# Xash3D external mods and debug tools

Base: `590869d280aad5b3646ab39d956a9ea3ac40ba4c`; client ABI **v9**.

`mod-abi.patch` contains upstream integration: lifecycle, input, camera, per-eye
rendering, viewmodels, canvas, SDL event routing, debug draw points and build
integration. The Linux launcher explicitly disables PIE to match upstream code
compiled with -fno-pie on current GCC toolchains.

The SDK/loader/runtime/UI sources have one canonical copy in `sdk/`. Dear ImGui
and JSON dependencies are vendored with pinned revisions and licenses in
`third_party/`. They are copied into the disposable tree after patch application.

Run `python3 scripts/apply-patches.py` from the project root, then build. Do not
apply this patch alone and expect the generated host files to exist.
