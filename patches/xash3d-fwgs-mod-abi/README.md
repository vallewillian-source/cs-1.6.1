# Xash3D-FWGS — external Mod ABI (VR support)

This is the engine half of CS 1.6.1. Xash3D-FWGS has no plugin system: nothing outside the
engine can draw, run per frame, or read input. This patch adds exactly that, as a small
frozen C ABI, plus the render-side work the VR Mod needs.

| | |
|---|---|
| Upstream | <https://github.com/FWGS/xash3d-fwgs> (GPL-3.0) |
| Base revision | `590869d280aad5b3646ab39d956a9ea3ac40ba4c` |
| Working branch in this project | `vr-mod` |
| ABI version delivered | `MOD_API_VERSION 8` |
| Patch | [`mod-abi.patch`](mod-abi.patch) — 26 files, +1853/−31 |

## Apply

```bash
git clone https://github.com/FWGS/xash3d-fwgs
cd xash3d-fwgs
git checkout 590869d280aad5b3646ab39d956a9ea3ac40ba4c
git apply /path/to/mod-abi.patch      # or: patch -p1 < /path/to/mod-abi.patch
```

Then build it the way the project's `scripts/build-xash.sh` does. If `git apply` complains,
the upstream file moved: the patch is a snapshot, not a rebaseable series.

## What is in it

**The Mod system itself** (new files):

- `engine/common/mod_api.h` — the whole contract, and the only header a Mod includes. The
  engine exports three functions to a Mod (`Printf`, `DoubleTime`, `engine_version`) and
  then calls back into it through capability-gated hooks. A Mod declares what it needs
  (`MOD_CAP_*`); the engine refuses to load it if `api_version` does not match exactly.
- `engine/common/modloader.{c,h}` — `dlopen` of `mods/mod_<name>.so`, capability
  negotiation, and the call sites for the lifecycle and frame hooks.

**Why each engine area had to change:**

| Area | Files | Why |
|---|---|---|
| Per-eye render loop | `ref/gl/gl_fbo.c`, `ref/gl/gl_rmain.c`, `ref/gl/gl_local.h`, `engine/ref_api.h` | The Mod needs to render each eye itself, in its own viewport, into the eye's framebuffer |
| Asymmetric frustum | `ref/gl/gl_frustum.{c,h}` | Culling used a symmetric frustum built from `fov_x`/`fov_y`, while VR eyes are off-centre rectangles: geometry that lived only in the outer band was never submitted |
| Viewmodel hook | `engine/client/cl_view.c`, `ref/gl/gl_studio.c`, `common/ref_params.h` | Lets the Mod suppress the 1999 viewmodel (`RF_NO_VIEWMODEL` / `RF_VIEWMODEL_WORLD`) and take it over |
| Input | `engine/client/input/input.c`, `engine/client/input.h`, `engine/client/input/in_keys.c` | Exposes controller samples and absolute angles to the Mod, and lets a Mod **consume** a key before any engine handler sees it (the IPD calibration UI) |
| 2D menu canvas | `engine/client/cl_scrn.c`, `engine/client/dll_int/cl_gameui.c`, `engine/client/cl_main.c` | Captures the menu's 2D drawing into an offscreen target so the Mod can draw it on a panel in the headset while the monitor keeps showing the menu |
| Loader | `engine/common/host.c`, `engine/platform/sdl2/vid_sdl2.c`, `ref/gl/gl_context.c` | Init/shutdown order against a valid GL context |

Other platforms (`ref/null`, `ref/soft`) only received the no-op end of the new render
hooks, so those renderers still build.

## Read next

- [`docs/MODS.md`](../../docs/MODS.md) — the **authoritative** description of the ABI: the
  contract, every hook, every struct field, and the rules for writing a Mod.
- [`docs/HANDS.md`](../../docs/HANDS.md) — what the VR Mod does with it.
- [`mods/null/`](../../mods/null/) — the minimal Mod (log-only), a working template.
