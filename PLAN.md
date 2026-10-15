# PLAN.md — VR Mod for the open-source CS 1.6.1 (MVP)

> Status: planning · Created on 2026-10-11
>
> Current stack (running and validated): client **Xash3D FWGS (64-bit) + cs16-client** in `game/`,
> dedicated server **ReHLDS + ReGameDLL_CS** in `server/` (podman), sources and builds in `src/`.
> Rootless Fedora environment (sudo unavailable): 32-bit server builds via podman; native client.

---

## 1. Objective

Play the current CS 1.6.1 (client in `game/`) in virtual reality on Fedora, with **Meta Quest 3 via WiVRn** (PCVR: the game runs on the PC and is streamed to the headset).

### 1.1 MVP definition of done

1. Stereoscopic game on the Quest 3, with 1:1 head look (pitch/yaw; camera at the player's eye height).
2. Locomotion and actions **entirely through the Quest controllers** (thumbsticks + buttons), with aim by the head.
3. **HUD and menus legible and navigable inside the headset** (virtual panel + laser pointer on the controllers), including buy menu, scoreboard, console and cs16-client menus.
4. A full match against bots (YAPB) on the local dedicated server, **with no change whatsoever on the server**.
5. The current flat mode remains 100% intact and is the fallback.
6. Everything implemented as a **VR Mod** — first user of a reusable **Mod architecture** (section 2).

### 1.2 Out of the MVP scope (future, gives context to the architecture)

- 6DoF weapon in the hands / realistic aim by controller — explicitly cut by the user.
- Hands, body, facial tracking, spatial audio, passthrough.
- VR-vs-flat multiplayer with balancing, new content, extreme performance (aggressive foveation etc.).

---

## 2. Proposed architecture

### 2.1 Principles

- **VR is 100% client-side.** Server (ReHLDS/ReGameDLL), network protocol and content do not change. VR client and flat client play on the same server, indistinguishable to it.
- **"Mod" ≠ "game dir".** A Mod is a library (`mod_<name>.so`) that the engine loads by parameter (`-mod <name>`), receiving hooks at defined extension points. Nothing duplicates the content of `cstrike/`; nothing changes in the game without the Mod.
- **Stable, versioned contract (C ABI).** The engine exposes a single interface; future Mods (VR is the first; then: 6DoF weapon, HD pack, gameplay, accessibility) use the same interface. The engine is **not** re-patched for each Mod.
- **Total fallback.** Without `-mod`, the behavior is byte-for-byte the current one. Hooks not configured = no-op.

### 2.2 Layers

```
┌────────────────────────────────────────────────────┐
│ cs16-client (client_amd64.so, menu_amd64.so)       │ HUD/gameplay — unchanged in the MVP*;
│                                                    │ draws via the engine's 2D API
├────────────────────────────────────────────────────┤
│ Xash3D FWGS — branch `vr-mod`                      │
│   + common/mod_api.h    Mod contract v1            │
│   + engine/mod_loader.c loader (dlopen) + hooks    │  ← ONLY structural patch
│   + ref_gl              renderer (minimal patch for│
│                         projection/external camera)│
├────────────────────────────────────────────────────┤
│ mod_vr.so  ← FIRST MOD (OpenXR)                    │
│   session+stereo · controller input ·              │
│   virtual HUD/menu panel · pacing                  │
├────────────────────────────────────────────────────┤
│ WiVRn — OpenXR runtime (Flatpak user, no root)     │
├────────────────────────────────────────────────────┤
│ Meta Quest 3 — WiVRn client (Meta Store), Wi-Fi    │
└────────────────────────────────────────────────────┘
 * if hooks in the engine are not enough for something in the HUD,
   the cs16-client is adjusted on the `vr-mod` branch (expected to be minimal).
```

### 2.3 Mod contract (`mod_api.h`) — design draft

Versioned struct, resolved through a single exported symbol (`ModGetAPI`). Unknown/`NULL` fields are ignored by the engine (forward and backward compatibility).

- **Identity**: `api_version`, `mod_name`, `capabilities` (bitmask: `display`, `input`, `hud`).
- **Lifecycle**: `Mod_Init(engine_api)`, `Mod_Shutdown()`.
- **Frame**: `Mod_FramePre()` / `Mod_FramePost()` — VR pacing (OpenXR frame sync) and pose collection.
- **Display** (capability `display`): `Mod_Camera(eye, view_t*)` — overrides origin/angles/projection **per eye**; `Mod_BeginFrame(gl_ctx)`, `Mod_EndFrame()` — swapchains, OpenXR submit; engine recognizes the Mod as "display owner" (turns off mouse grab, window vsync, software cursor).
- **Input** (capability `input`): `Mod_Input(input_sample_t*)` — injects movement/buttons/angles through the same route as keyboard/mouse (client prediction intact).
- **HUD** (capability `hud`): `Mod_HudRedirect(target)` — redirects 2D drawing to the Mod's canvas; `Mod_HudPointer(x, y, click)` — laser pointer injects mousemove/click in the 2D space.

Golden rule: **the engine never knows about OpenXR**. Everything that is VR lives in `mod_vr.so`.

### 2.4 Code layout

```
cs-opensource/
├── src/xash3d-fwgs/        branch `vr-mod`: common/mod_api.h, engine/mod_loader.c,
│                           hook call sites, minimal patch in ref_gl
├── src/cs16-client/        branch `vr-mod` (expected: empty or nearly so)
├── mods/
│   ├── vr/                 mod_vr sources (C++, OpenXR loader, GL)
│   └── null/               log-only Mod template (doc example + 2nd test Mod)
├── scripts/                build-openxr.sh · build-mods.sh · run-cs-vr.sh (new)
├── docs/MODS.md            ABI contract + "how to write a Mod" tutorial
├── PLAN.md                 this file
└── README.md               gains a VR section in Phase 5
```

---

## 3. Phases and tasks

Conventions:
- Every task is **atomic**: one responsibility, with a verifiable done criterion ("Done when:").
- No time estimates (team decision). Relative complexity indicated: 🟢 low · 🟡 medium · 🔴 high.
- Each phase ends with a demonstrable **milestone** before moving on.

### Phase 0 — VR infrastructure (burn the transport risk before coding the game)

> ✅ **COMPLETED on 2026-10-11.** 0.1/0.2 skipped by user decision (WiVRn already
> validated in the environment). Details: OpenXR-SDK **1.1.63** in `src/openxr-sdk/install/`
> (dynamic loader + headers); `vr-mod` branches created in both repos (base:
> xash3d `590869d`, cs16-client `6a49bcc`); flat baseline in
> `backups/flat-baseline-20261011/` (13 files + `MANIFEST.sha256`); rebuild of the
> branches validated with boot + connection to the dedicated server; `docs/MODS.md` versioned.
> Gotcha solved: the `steamcmd:latest` image changed base and started linking
> `libbz2.so.1.0` (Ubuntu soname) — `build-xash.sh` normalizes to `libbz2.so.1`
> (Fedora) with `patchelf` in the post-build.

- [x] 🟢 **0.1** *(skipped — WiVRn already works in the environment, per the user)* Install WiVRn via Flatpak *user* (`io.github.wivrn.wivrn`) and install the client on the Quest 3 through the dashboard wizard. *Done when:* headset connects, shows the virtual screen and **plays the PC audio in the headset**.
- [x] 🟢 **0.2** *(skipped — WiVRn already works in the environment, per the user)* Run an independent OpenXR test app (hello_xr or equivalent, GL backend) against the WiVRn runtime. *Done when:* stereo scene visible in the headset with head tracking — proves the Fedora + RTX 4060 + WiVRn + GL combo.
- [x] 🟡 **0.3** Create `scripts/build-openxr.sh` (rootless): download and build the OpenXR-SDK (loader + headers), in the podman container or vendored. *Done when:* `libopenxr_loader.so` + headers available for linking.
- [x] 🟢 **0.4** Create branch `vr-mod` in `src/xash3d-fwgs` and `src/cs16-client`; save a baseline of the current flat binaries (`.so` + `xash3d`). *Done when:* build of the `vr-mod` branch without changes reproduces the functional flat game (smoke test).
- [x] 🟢 **0.5** Create the skeleton of `docs/MODS.md` and record the ABI decision (versioned struct + dlopen + capabilities). *Done when:* document committed on the branch.

**Phase milestone:** Quest 3 ↔ Fedora transport proven end to end, rootless, with an app that is not ours.

### Phase 1 — Mod loader in the engine (foundation of the architecture)

> ✅ **COMPLETED on 2026-10-11.** Commit `8d12c80` on the xash3d-fwgs `vr-mod` branch;
> template in `mods/null/`; contract v1 frozen in `docs/MODS.md`.
> Architecture discovery: `COM_LoadLibrary`/`FS_FindLibrary` forces **lowercase** on the
> library path (`// always in lower case (why?)` in `filesystem/searchpath.c`) —
> the Mod loader uses `dlopen` directly (POSIX) and validates the path with `stat`, resolving the
> binary directory via `/proc/self/exe` (case guaranteed). Mod logs to its own file
> (`game/mods/mod_null.log`) because the engine's `Con_Printf` does not go to stdout in
> graphics mode (the reliable log channel is `-log <file>`, which requires a path starting with an alphanumeric).
> Content discovery: the command-line `+commands` (`+connect` etc.) only run if a
> `.rc` invokes `stuffcmds` — `valve/valve.rc` covers HL, but the HLDS CS does not ship `cstrike.rc`;
> we created `game/cstrike/cstrike.rc`. Dedicated logging turned on (`log on` in `server.cfg`) —
> without it no join/drop shows up anywhere (HLDS stdout is buffered).

- [x] 🟡 **1.1** Write `common/mod_api.h` v1 (contract from section 2.3: structs, hooks, capabilities). *Done when:* header compiles standalone and is consumed by a stub. — compiled with `-Wall -Wextra` on `mods/null`.
- [x] 🟡 **1.2** Implement the loader in the engine (`engine/common/modloader.c`): parse `-mod <name>`, `dlopen("mod_<name>.so")`, resolve `ModGetAPI`, validate `api_version`, store globals; clean unload on shutdown. *Done when:* `-mod null` loads and unloads without a crash, with clear logging. — validated in dedicated mode (stdout) with a full load→init→frames→unload cycle.
- [x] 🟡 **1.3** Insert hook call sites at the host's neutral points: frame start/end, camera pre-setup (per eye), post-HUD draw, input queue, GL context creation. *Done when:* each call site is a no-op without a Mod loaded. — `host.c` (frame/input/shutdown/init), `cl_scrn.c` (begin/hud/end), `cl_view.c` (camera per view pass), `vid_sdl2.c` (video init).
- [x] 🟢 **1.4** Implement the `null` Mod (template): logs each hook with counters and timestamp. *Done when:* 10+ min of flat gameplay with `-mod null`, no crash and no gameplay change; becomes the `MODS.md` example. — long 10.5 min test connected to the dedicated server.
- [x] 🟢 **1.5** Flat regression: compare behavior without `-mod` vs baseline (Phase 0.4) and with `-mod null`. *Done when:* zero functional difference. — boot + connect without `-mod` after the changes, identical to the functional baseline.

**Phase milestone:** the `null` Mod proves the architecture is generic — nothing in it is VR-specific.

### Phase 2 — VR core in mod_vr (OpenXR session + stereo render)

> ✅ **IMPLEMENTED on 2026-10-11 — code complete and tested without a headset; the final
> stereo validation requires the Quest 3 connected (WiVRn).** Engine commit on the
> `vr-mod` branch (ABI v2 + eye loop + viewport flag); mod in `mods/vr/` +
> `scripts/run-cs-vr.sh`.
>
> Discoveries this phase (all documented in the code):
> - WiVRn 26.9 (RPM) only accepts `xrCreateInstance` of API **1.1** (1.0 → `-51`) and **blocks
>   forever** without a connected headset (the system's `hello_xr` hangs the same way) → the
>   OpenXR init runs on a **separate thread with a 6 s timeout**; the game stays flat and VR
>   "turns itself on" when the headset connects. Runtime manifest: `/usr/share/openxr/1/openxr_wivrn.json`.
> - `ref_gl` has an internal *scene target* (`r_scene_scale`/MSAA) that rebinds an FBO over
>   ours — turned off via cvars in the launcher; and the viewport math assumes viewport ⊆ window
>   → new flag `RF_VIEWPORT_ABSOLUTE` (`ref_params.h`) with a branch in `R_SetupGL` (minimal patch).
> - Ubuntu links with `--as-needed`: when linking the mod, the source comes BEFORE the `-l`; and
>   `ModGetAPI` in C++ needs `extern "C"`.
> - The 2.5 fallback in effect: symmetric FOV = max per axis in the render; the **submit** uses the
>   real asymmetric FOV per eye (compositor does not distort).
> - The **swap happens inside `V_PostRender`** (`R_EndFrame` → `GL_SwapBuffers`), that
>   is, long before `Mod_EndFrame` (end of `SCR_UpdateScreen`): drawing in
>   `Mod_EndFrame` lands in the post-swap back buffer and never appears. A real 2D overlay
>   needs its own point just before `R_EndFrame` → **`Mod_Overlay2D` (ABI v3,
>   `MOD_CAP_HUD`, task 2.9)**. That is also why `Mod_AfterEyes` (pre-2D) serves only
>   the mirror: the game HUD draws over it.

- [x] 🟡 **2.1** mod_vr skeleton: OpenXR instance/session against WiVRn (via `XR_RUNTIME_JSON`), reference space `LOCAL`, head and hand spaces. *Done when:* log confirms session created and poses arriving (without render). — log in `game/mods/mod_vr.log`; poses require a headset.
- [x] 🟡 **2.2** OpenXR frame loop (`xrWaitFrame` → `xrBeginFrame` → `xrLocateViews` → `xrEndFrame`) coupled to `Mod_FramePre/Post`, with pacing by `predictedDisplayTime`. *Done when:* loop stable at the HMD refresh (72/90 Hz). — code ready; HMD stability pending a headset.
- [x] 🔴 **2.3** Per-eye GL swapchains (`XR_KHR_opengl_enable`): create swapchains, FBO per image, bind in the camera hook. *Done when:* distinct clear color per eye visible in the headset. — code ready (`VR_DEBUG_CLEAR=1` paints the eyes blue/red for testing).
- [x] 🔴 **2.4** Camera override + double render: HMD origin/angles → engine view, scene drawn **2× per frame** (one per eye), render-only offsets (physics/prediction untouched). Turn off desktop paths: window vsync. *Done when:* standing in the map, 1:1 look. — eye loop in `V_RenderView` (`Mods_EyeCount`), audio only on eye 0; pending a headset.
- [x] 🔴 **2.5** Per-eye projection: MVP fallback in effect (symmetric FOV = max per axis; real asymmetric submit). *Done when:* comfortable stereo, no severe clipping. — pending a headset; asymmetric matrix injection in ref_gl is left for post-MVP.
- [x] 🟡 **2.6** Submit + mirror: `xrEndFrame` with projection layers; left-eye mirror to the window via `Mod_AfterEyes` (blit before 2D — monitor shows the view + flat HUD). — pending a headset for real submit.
- [x] 🟢 **2.7** `VR_RESOLUTION_SCALE` (0.3–3.0, default 1.0) via env. *Done when:* the default scale sustains stable refresh on the RTX 4060. — fps measurement on the HMD pending a headset.
- [ ] 🟡 **2.8** (Conditional) Fixed foveation via the FB extension, if WiVRn exposes it. *Done when:* measurable gain in performance headroom. — not attempted (conditional; without a headset there is no way to measure).
- [x] 🟢 **2.9** **Debug panel on the 2D screen** (2026-10-12): FPS + every error/warning from the VR load, visible **only on the monitor** and only when the game runs with `-mod vr`. It required ABI **v3** (`Mod_Overlay2D`, capability `MOD_CAP_HUD`), called at the end of `V_PostRender` — after the HUD/menus and before the swap, the only point where a Mod paints over the UI (and, in VR, outside the eye swapchains). Embedded 8x16 bitmap font (`mods/vr/gen_font.py` → `vr_font.h`); `VR_DEBUG_PANEL=0` hides it, `VR_PANEL_DUMP=saida.ppm` saves a frame for diagnosis. *Done when:* with VR failing (no runtime/headset) the panel shows the translated `XrResult`, the runtime and the reason for the flat fallback. — validated without a headset (dump checked: panel over the menu, 1 error + 1 warning detailed).
- [x] 🔴 **2.10** **Fixes for VR to actually come up with WiVRn** (2026-10-12): the 2.9 panel showed that the VR path had never run end to end. Three root causes, all fixed in `mod_vr.cpp` + launcher:
  (a) **video driver**: in a Wayland session SDL opened on the `wayland` driver (EGL) and there is no GLX context — the mod is Xlib/GLX, so `run-cs-vr.sh` now exports `SDL_VIDEODRIVER=x11` (XWayland serves);
  (b) **graphics requirements**: `xrGetOpenGLGraphicsRequirementsKHR` was missing, mandatory before `xrCreateSession` with `XR_KHR_opengl_enable` — without it WiVRn returns `-50 XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING`;
  (c) **context thread**: the runtime's client compositor does `glXMakeCurrent` on the thread that creates the session, but the session was created on the init thread (the GL context belongs to the main thread) → `XR_ERROR_INITIALIZATION_FAILED`. Now only `xrCreateInstance` (which blocks without a headset) runs outside; `xrGetSystem`/requirements/session/reference space run on the main thread, with a retry every 1 s (headset connected later = VR turns itself on).
  Also fixed in passing: `glXQueryContext` returns **0 on success** (the check was inverted and the binding came out with `visual 0`/null `fbconfig`), the swapchain format now gets chosen from the runtime's list (it was `0`, invalid), and the launcher's "2 wivrn-server" warning no longer confuses the parent with the worker.
  *Done when:* initialization reaches `xrBeginSession` + per-eye swapchains with the WiVRn runtime. — **validated up to swapchains** without a headset (`sessão READY`, `xrBeginSession`, 2476x2594 per eye, RGBA8, 0 errors in the panel); still to confirm stereo with the Quest 3 connected.
- [x] 🔴 **2.11** **Black HMD even with everything initialized** (2026-10-12): with the Quest 3 on, the session stayed stuck in `READY` and the headset showed black, with no error anywhere. Cause: **frame pumping was missing**. The app only called `xrWaitFrame`/`xrBeginFrame` in `VISIBLE/FOCUSED`, but OpenXR requires it to keep running the frame loop already in `SYNCHRONIZED` (submitting an empty frame, `layerCount 0`) — without that the runtime does not advance and never delivers VISIBLE. Fixed with `xr_session_begun` (set on `xrBeginSession`, cleared on STOPPING/LOSS): the loop pumps from the begin and only renders the eyes when VISIBLE/FOCUSED. Verified with the headset connected: `READY → SYNCHRONIZED → VISIBLE → FOCUSED`, two eyes rendered, `xrEndFrame` with `camadas=1` (layers=1) and real content in the eyes.
- [x] 🟢 **2.12** **Stereo pipeline diagnosis** (2026-10-12): since the black HMD leaves no trace, the Mod now measures and logs what matters: completeness of each eye's FBO (`glCheckFramebufferStatus`), FBO binding after the eye render, **pixel readback of the swapchain image** (with content = the render happened; black = the problem is the render), `xrEndFrame` with the layer count and the result, `xrLocateViews` (pose flags) and aggregated GL errors. Traces of the first 12 stereo frames (`VR_TRACE=1` keeps it on); warnings every ~5 s saying *where* it stopped ("ainda nao VISIBLE" [not VISIBLE yet] vs "sem render 3D nos olhos" [no 3D render in the eyes]). `VR_PANEL_DUMP_AT=<s>` chooses the dump delay. *Lesson recorded:* the engine **has no hardware gamma ramp** (there is no `SetWindowGammaRamp` in the code), so what is in the framebuffer is what the monitor shows — there is no gamma difference between mirror and HMD.
- [x] 🟢 **2.13** **Performance: the loop was stuck on the monitor vsync** (2026-10-12): with VR already working, the game stuttered and **black areas appeared in the new parts of the map when turning the head**. The measurement (`perf`, one line per second with work per frame vs HMD period) showed the exact picture of the problem: **60 fps (16.7 ms) against a 90 Hz HMD (11.1 ms)** — 1 out of every 3 headset frames arrived without new content, and that is exactly what turns black whatever has just entered the field of view. Cause: the launcher's `-gl_vsync 0` **did not hold** (the `-cvar` form is applied before the configs and the player's `config.cfg` turns vsync back on), so the WINDOW buffer swap stalled the entire loop — engine + VR — at the monitor refresh. One-line fix in `run-cs-vr.sh`: pass it as a **console command** (`+gl_vsync 0 +fps_max 200 +r_scene_scale 1 +gl_msaa 0 +r_speeds 0`), which runs after the configs. Measured: **60 → 90 fps**, matching the HMD period. Headroom verified with `VR_RESOLUTION_SCALE`: 1.0 (2476x2594/eye) = 1.5 ms of work; 1.3 = 1.9 ms; 1.6 (3961x4150/eye) = 2.4 ms — that is, there is budget left to raise the resolution if you want sharpness, and margin to lower it if the GPU/encoder cannot keep up.

- [x] 🔴 **2.14** **Wrong stereo: the pair would not fuse ("each eye alone looks ok, the two together do not")** (2026-10-12). Three combined causes, all measured with the Quest 3 connected:

  1. **Asymmetric frustum per eye** (the biggest). The runtime declares, per eye, four independent and mirrored angles — on the Quest 3 via WiVRn: left `L54 R40 U44 D55`, right `L40 R54 U44 D55`. That is 94°×99° **off-center** (asymmetry ±14° horizontally, 11° vertically, with the sign flipped between the eyes). The engine only knew how to build a **centered** frustum (`fov_x`/`fov_y`) and the mod only had that: it rendered 54°×55° centered and submitted 94°×99° asymmetric — the compositor stretched each eye's image by a **different** factor, and that is exactly what prevents fusion. It required ABI **v4**: `mod_view_t.fov_asym[4]`/`has_fov_asym` → `RF_FOV_ASYM` in `ref_viewpass_t` → off-center projection in `R_SetupProjectionMatrix` (`Matrix4x4_CreateProjection` already accepted asymmetric bounds, it was the `ref` that always passed symmetric ones).
  2. **Mirrored XR→GoldSrc rotation.** The map `(x,y,z) → (-z, x, y)` has determinant **−1**, and both spaces are **right-handed** (OpenXR: X right/Y up/Z back; GoldSrc: X forward/Y left/Z up — checked via `right × up = -forward` of `AngleVectors`). Therefore the mapping was a **mirror**, not a rotation: the head turned the wrong way (yaw with flipped sign) and — worse for stereo — the head's "right" axis came out inverted, throwing the eye offset to the wrong side (pseudoscopic pair). The correct one is `(-z, -x, y)`. Checked numerically against the engine's `AngleVectors`/modelview (~1e-4 error on real WiVRn poses).
  3. **Guessed IPD.** The mod separated the cameras by a fixed 63 mm; the headset reports **68.3 mm** (with the user's calibrated IPD). Now the offset comes from the **real position of each eye** reported by the runtime (delta between the eyes, brought into the head reference frame): measured `(68,3 0,0 −0,0) mm` — pure separation along the head's right, parallel cameras. `VR_IPD=<m>` forces a manual value.

  The head **roll** also started being applied (`atan2(-right.z, up.z)`), because with an asymmetric frustum the optical center leaves the view axis and tilting the head would shift the image.

  *Objective verification (`VR_STEREO_DUMP=pref` writes the two images of the SAME frame):* the parallax measured between the eyes is **−632 px** against **−600 px** predicted by the declared geometry (the ~32 px difference is the scene parallax, ~2.2 m average depth); **vertical disparity of 1 px**; per-region correlation **0.93–0.97** (before, with the symmetric render against the asymmetric submit, there was no single displacement that aligned the two images). The mirror test gives a worse peak (0.32 at −1112 px), confirming the pair is not mirrored. Disposable measurement tool in `/tmp/pair_final.c`.

- [x] 🔴 **2.15** **The debug panel (and the game HUD) appeared in the headset, not on the monitor** (2026-10-12). Cause: **the 2D target at the end of the frame was not the window**. With VR active the frame ends with the last eye's image FBO bound — the mirror's `blit_mirror` saves and *restores* the binding it found — and `R_Set2DMode(true)` calls `GL_BindScreenTarget()`, which **returned without binding** when there is no internal render target (the VR case: `+r_scene_scale 1 +gl_msaa 0`). Result: debug panel, HUD, menus and console were drawn INSIDE the image sent to the compositor — 2D on the wrong screen, in one eye only and still distorted by the runtime warp. Two fixes: in the engine, `GL_BindScreenTarget` now binds the window explicitly when there is no internal target (`ref/gl/gl_fbo.c`); in the mod, the panel binds the window (FBO 0) on its own before painting and restores the binding afterward. Permanent diagnosis: if the 2D ever arrives again with FBO ≠ 0, `painel: o 2D chegou com FBO N ligado` (panel: the 2D arrived with FBO N bound) appears in `mods/mod_vr.log`. *Desired side effect:* the game HUD/menu also **stop leaking** into the HMD — the headset goes back to showing only the world until Phase 4.

**Phase milestone (biggest technical risk burned):** *dust2 in stereo, looking around, game 100% functional by keyboard/mouse.* → code complete and **stereo geometrically verified**; still missing the confirmation of visual comfort with the headset on the face (`scripts/run-cs-vr.sh` with WiVRn connected).

### Phase 3 — VR input (Quest controllers)

> Decisions made with the owner on 2026-10-12 before implementing: **aim by the
> head in this phase** (weapon in the hand comes later), **CS physics only** (no
> flight/noclip mode; the vertical axis uses the game's `upmove` and therefore only acts where
> it already acts — water, spectator), **snap turn only** (no smooth turning) and
> **roomscale on** (the real HMD position moves the camera; actually crouching
> becomes `IN_DUCK`).

- [x] 🟢 **3.1** OpenXR action set: thumbsticks, triggers, squeeze, A/B/X/Y, menu, haptic on `/user/hand/*`; `aim` poses per hand. Implemented in `mods/vr/mod_vr.cpp` (section "VR input"): action set `gameplay`, 14 actions, per-hand pose spaces and suggested bindings for `oculus/touch_controller`, `meta/touch_controller_plus` and the `khr/simple_controller` fallback (per-profile error is tolerated and logged). `xrAttachSessionActionSets` runs at the end of `xr_setup_session`, that is **before** `xrBeginSession`. *Done when:* the panel's `INPUT` block shows the per-hand interaction profile — it is what answers "did the controller match?" (if it comes up `(nenhum)` [none], no binding took).
- [x] 🟡 **3.2** Locomotion: left stick → forward/side **relative to the head yaw**, with a radial deadzone (which is a *floor*, not a step: the rest of the range is rescaled) and `walk` while holding grip (`cl_movespeedkey`, the same factor as `+speed`). Head orientation costs no computation in the Mod: it injects the **HMD yaw** as `viewangles_abs` and sends the raw stick — `pm_shared` builds the `wishdir` with `AngleVectors(cmd.viewangles)` and ignores pitch on the ground. With the stick centered, the keyboard still works (see 3.7). *Done when:* walking/strafing in the game with the controller alone.
- [x] 🟡 **3.3** Snap turn: right stick horizontal → step of `VR_TURN_SNAP_ANGLE` (default 30°), with a per-side **latch** (fires once and only re-arms when the stick returns to |x| < 0.3, otherwise it would spin in bursts with the stick held). **Deviation from the original plan:** it is not via reference space rotation — it is a **yaw offset** added to the HMD pose, applied **equally in the cmd and in the render** (`IN_ModAppendMove` and `VR_Camera`). Without that symmetry the body would shoot to one side and the camera would look to the other after the first step; and the offset is also what gives meaning to the roomscale offset (the world is the playspace rotated by the offset). The menu button **recenters** (zeroes the offset, recalibrates the height) and the runtime's `REFERENCE_SPACE_CHANGE_PENDING` event re-anchors on its own — without it a headset recenter would teleport the player. *Done when:* turning in steps without drift and without needing constant recentering.
- [x] 🟡 **3.4** Buttons → game actions, injected through the engine's input route: right/left trigger = fire/secondary attack, A = jump, B = reload, X = use, Y = **scoreboard** (hold) and left stick click = crouch (in addition to the physical crouch). **Two deliberate deviations from the plan:** `Y = flashlight` became scoreboard and `menu = virtual ESC` became **recenter** — opening the game menu would be useless now, because **the 2D still comes out on the monitor, not the headset** (that is Phase 4); recentering, yes, is needed precisely because of snap turn. Mapping that back is one line when Phase 4 arrives. *Done when:* all actions execute in the match.
- [x] 🟢 **3.5** Aim by the head: `viewangles_abs` = HMD pose, with pitch clamped at ±89 (the same clamp as the client's `CL_CreateMove` — the render can look further, what goes to the server cannot). The cmd's `roll` stays 0 on purpose: the game ignores roll and the render roll comes from `Mod_Camera`, which remains the only visual source. *Done when:* hitting fixed and moving targets consistently — **and the decisive test, which also proves the network route: shoot a wall and the hole appears where you are looking.**
- [ ] 🟢 **3.6** (Optional, cuttable) Short haptic on firing. The vibration action **already exists and is bound** (`/user/hand/*/output/haptic`), but `xrApplyHapticFeedback` is not called yet — it just needs to be wired up.
- [x] 🟢 **3.7** (New, came out of the design) **ABI v5 and application in the engine.** `MOD_CAP_INPUT` had existed since v1 but was inert (`Mods_Input` discarded the response). v5 added `move_up`/`viewangles_abs`/`has_viewangles_abs` and wired up the application: `IN_ModAppendMove` runs in `CL_CreateCmd` **after** the client DLL (that is why the Mod beats the mouse without disabling it, and the mouse keeps navigating menus) and **before** `CL_PredictMovement`. Details in `docs/MODS.md` §"Input". Flat regression guaranteed by construction: outside an active session the Mod returns the zeroed sample and the game stays identical to before.
- [x] 🟢 **3.8** (New) **Roomscale and physical crouch.** The HMD position enters the camera as a **horizontal** offset (rotated by the snap offset, limited by `VR_ROOMSCALE_MAX`) — leaning works. The **vertical** axis does **not** enter the camera: the head dropping beyond `VR_CROUCH_DROP` (with hysteresis) becomes `MOD_BTN_CROUCH`, and it is the game's duck that lowers the view. It is on purpose — adding the two would lower the view twice. `VR_CROUCH_MODE` covers those who play seated. *Accepted cost:* leaning moves the camera without moving the collision box, so you can "peek through" geometry — standard behavior in VR shooters.
- [x] 🟢 **3.9** (New) **Comfort vignette** (`VR_COMFORT_VIGNETTE`), drawn on the eye image itself before `xrReleaseSwapchainImage`. **Off by default** (0) — the plan called for a light intensity by default, but with snap turn (the most comfortable option) it is less necessary and touching the eye image is exactly where we already went wrong twice; it is one variable away after validation. GL state saved and restored, so the ref_gl cache stays valid.

**Status:** code complete, engine and mods compiled and installed, **ABI v5 validated end to end without a headset** (`mods/null`: `loaded "null" (api 5)` with 237 frames and paired hooks; `mods/vr`: `loaded "vr" (api 5, capabilities: display input hud)`). **Still missing the headset validation**, which is the only one that proves locomotion: walking while turning the head, snap without repeats, every button, physical crouch lowering only once, the bullet hole where you look, and the game menu (opened by keyboard) continuing to respond to the mouse with VR without moving the player.

**Phase milestone:** a match playable end to end **without keyboard or mouse** (spawn → walk → shoot → reload).

### Phase 4 — HUD and menus in VR

- [x] 🔴 **4.1** 2D capture layer: post-HUD hook redirects all 2D drawing (cs16-client HUD, menu_amd64 menus, console, engine text) to an offscreen FBO (virtual canvas). *Done when:* canvas contains rendered HUD and menus (verifiable via texture dump). — implemented as an on-demand **ref canvas** (`R_HudCanvasBegin/End/Info`, ABI **v8**): `V_PostRender` diverts the 2D to an RGBA8 target the size of the 2D space and, at the end, returns the result to the window with blend (the monitor keeps HUD/menu). The Mod receives the texnum in `Mod_Hud`. Canvas = window size (the same space in which the engine's mouse is measured). Validated in a flat run: the menu appears in the dump coming from the canvas, and the canvas **only exists when someone asks for it** (no creation in the normal game). **Clipping fixed on 12/10 (defect reported by the owner):** the canvas started capturing **only the menus** — HUD, console and debug overlays draw straight to the window — and the predicate became `cls.key_dest == key_menu` (not `!= key_game`, which turned the panel on in the console). Reason: with the HUD inside the canvas, the green "buy zone" icon (and, in a match, radar/aim/health/ammo) appeared **over the buy menu** in the headset — "a blue/cyan element that is not the screen, but on top of it". Proven with `HUD_CANVAS_DUMP` (RGBA PAM of the panel content + alpha histogram): before, the canvas had the icon; after, only the menu, with the menu identical.
- [x] 🟡 **4.2** Virtual panel: canvas → texture of a quad in the world; distance/scale calibratable. *Done when:* HUD and menus sharp and legible inside the headset. — implemented with a **world-frozen anchor** (in front and at eye height **at the instant the menu opens**, without following the head — owner's decision on 12/10), **subdivided** quad (6×6 grid: the projection of a rectangle is a homography and the grid kills the affine interpolation error), dark frame, drawn per eye in `Mod_AfterEyes` (before the mirror, so the monitor also shows the panel). Live calibration in `game/mods/vr_menu.cfg` (`dist_m`, `height_m`, `yaw_off_deg`, `y_off_m`, reread by mtime; saving already applies it, even with the menu open) + `VR_MENU_DIST`/`VR_MENU_HEIGHT`. The panel projection uses the **same formula as the engine** (validated numerically: 0.000000 px difference in 32 cases with the real asymmetric Quest 3 fov). **Mirror bug (12/10, reported by the owner: "it appears mirrored horizontally"):** the panel's "right" was written as `(−sy, −cy, 0)` — only X inverted — and the correct one is `fwd × up = (sy, −cy, 0)`, the same as `AngleVectors` with zero pitch/roll. With the wrong sign the rectangle stopped being perpendicular to the front: **mirrored** at every yaw with non-zero sine and **sheared** at the others (sweep of 72 yaws: 70 failures with the old vector — only 0° and 180° passed — and zero with the fixed one). Verified **without a headset** by projecting the real canvas with the Mod's math (`/tmp/warp_panel.py`): the old vector reproduces the mirrored menu, the new one comes out correct, at the same scale/position. **Waiting on the owner's eye** for legibility/position.
- [x] 🟡 **4.3** Laser pointer: ray from the controller's `aim` pose intersects the quad → maps to 2D canvas coordinates → injects mousemove/click. *Done when:* navigate the main menu, **buy menu** and console using only the controller. — implemented: **right-hand** laser (`xr_act_aim[1]`), intersection with the panel plane → canvas pixel; the engine gained the **virtual cursor** (`IN_GetCursorPos`) read by **both** cursor consumers (the engine's `UI_MouseMove` and the `GetMousePosition` that the client DLL uses in its own menus — without it one would pull the cursor back and the hover would flicker) and the click is `IN_MouseEvent` → `Key_Event(K_MOUSE1)` → `UI_KeyEvent`. The right trigger becomes a click (and not `ATTACK`) with the menu open; snap turn is blocked (owner's decision). Validated **without a headset** with the debug cvar `hud_ptr_test` (drives the cursor and the click; `2` opens/closes the menu by itself): the injected click **activated a menu item** (opened the Console: `key_dest: menu → console`), and the UI received the injected position (1336 `UI_MouseMove` calls). The numeric test caught a **flipped sign** in the ray's `t` before going to the headset. **Waiting on the owner's eye** for the comfort of pointing.
- [ ] 🟢 **4.4** Scoreboard and chat: confirm they flow through the same canvas (client HUD + engine text). *Done when:* legible in a match. — *design for the 12/10 clipping:* today the canvas **only turns on with the menu open**; turning it on always (HUD/scoreboard in the headset) is just changing the policy in the Mod and testing the cost.
- [ ] 🟢 **4.5** Mod debug overlay (HMD fps, poses, submit time) behind `vr_debug 1`. *Done when:* diagnosis possible without leaving the headset. — the 2D debug panel (in the window) already covers that partially and gained the two menu lines; in the headset, not yet.

**Phase milestone:** full flow in the headset — *menu → join the match → buy equipment → play a round → scoreboard.*

### Phase 5 — Integration, experience and MVP close-out

- [ ] 🟢 **5.1** `scripts/run-cs-vr.sh`: sets the WiVRn `XR_RUNTIME_JSON`, launches the engine with `-mod vr`; no headset detected → flat fallback with a warning. *Done when:* one command brings the game up in VR.
- [ ] 🟢 **5.2** Desktop entry "CS 1.6.1 VR" in GNOME (dedicated icon, its own `StartupWMClass`). *Done when:* launching from the GNOME menu validated with `gtk-launch`.
- [ ] 🟢 **5.3** Final user cvars (`vr_*`): snap turn, HUD distance/scale, resolution scale, recenter (button combo), panel mode — persisted in the config and documented. *Done when:* adjustable without a rebuild.
- [ ] 🟡 **5.4** End-to-end MP test: VR client connects to the local ReHLDS dedicated server and completes a match vs YAPB; server logs prove the VR client is indistinguishable from a flat client. *Done when:* complete clean match, no errors in the server log.
- [ ] 🟢 **5.5** Comfort checklist: 20+ min session, reachable recenter, motion sickness documentation (snap turn default, walk with grip, breaks). *Done when:* checklist documented and followed by the testers.
- [ ] 🟢 **5.6** README: VR section (prerequisites, 5/6 GHz network, troubleshooting) + `docs/MODS.md` finalized. *Done when:* a newcomer installs and runs following only the documentation.
- [ ] 🟢 **5.7** Final flat mode regression against the Phase 0.4 baseline. *Done when:* flat checklist passes in full.

**Phase milestone:** acceptance criteria of section 4 all green.

### Phase 6 — VR weapon gameplay, stage 1: weapon in hand and aim by hand

> **Ordering note:** this phase was implemented BEFORE Phases 4 and 5 (12/10) because
> it does not depend on them — it is all on the client, without server and without HUD. Its
> numbering stays at the end just to avoid renumbering the existing phases. The section 4 MVP is
> defined with "aim by the head", so VR weapons are explicitly **post-MVP**.
>
> **What makes it viable (measured in the code, not assumed):** the server computes the shot
> direction from `v_angle + punchangle` (`ReGameDLL_CS/…/weapons.cpp`) and the `v_angle` comes
> from the `usercmd` — the **same** channel the `pmove` uses to build the `wishdir`
> (`ReHLDS/…/sv_user.cpp`). Since the Mod already writes that channel since Phase 3, "shoot
> where the hand points" is choosing the right vector; and the viewmodel's **orientation**
> follows on its own, because the client DLL derives it from the view (`V_CalcGunAngle`). Only the
> **origin** needed a new hook.

- [x] 🟡 **6.1** ABI v6: `mod_viewmodel_t` + `Mod_ViewModel` + the flags `RF_VIEWMODEL_WORLD`/`RF_NO_VIEWMODEL` (which turn off the `VIEWMODEL_DEPTH_RANGE` hack and allow hiding the viewmodel). *Done when:* the engine applies the Mod's origin/angles/flags to the viewmodel. — done; the hook is called **after `Mods_Camera`** (`V_GetRefParams` zeroes the flags) and **per eye** (`pfnCalcRefdef` rewrites the viewmodel every pass), so the Mod is idempotent per frame. `mod_null` and `mod_vr` recompiled; `Mods: loaded "null" (api 6)`.
- [x] 🟡 **6.2** Mod: the weapon goes to the hand — origin = center eye + the **same** roomscale offset as the camera + hand delta (rotated by the snap) + fit **in the weapon's reference frame**. *Done when:* the weapon follows the hand 1:1 and is occluded by the world. — the fit comes from `game/mods/vr_weapon.cfg` (template versioned in `mods/vr/`), with **~1 s hot-reload** and a table per class; the panel gained the `arma` (weapon) line with the class of the model in use.
- [x] 🟢 **6.3** Mod: `VR_AIM_SOURCE=head|left|right` (default `right`) — the aim becomes the weapon's axis. *Done when:* shoot a wall and the hole appears where the weapon points. — implemented; still needs the headset (6.6).
- [x] 🟢 **6.4** Mod: locomotion stays on the head even with the aim on the hand — the stick is pre-rotated by (aim − head), which is the exact correction for the single angles channel. Side effect handled: with the `usercmd` pitch becoming the weapon's, `pmove` scales walking by `cos(pitch)`; `VR_MOVE_PITCH_COMP=1` compensates (default **off** = identical to the game). *Done when:* walking with the head turned follows the head, not the weapon.
- [x] 🟢 **6.5** Validation without a headset: ABI v6 loads both mods, `vr_weapon.cfg` is read and **reread live** (edited with the game running, the log showed `global 0 0 0` → `1 2 3`), and flat mode does not change (with VR inactive the Mod returns the struct without touching anything, so the flags stay at 0 and `R_DrawViewModel` runs the same path as before). *Done when:* without a headset, nothing breaks and the new path is observable.
- [ ] 🟡 **6.6** Validation with a headset, at two checkpoints on purpose: **(a)** `VR_AIM_SOURCE=head` — the weapon in the hand with the aim still by the gaze, which is where you calibrate the fit yourself; **(b)** `VR_AIM_SOURCE=right` — **shoot a wall and the hole appears where the weapon points**. *Done when:* the hole follows the weapon, turning (snap + physical) does not move the weapon relative to the hand, walking follows the head, touching the wall hides the weapon, and `head` restores the Phase 3 behavior.
- [x] 🟢 **6.7a Own viewmodel (pistol class)** — the game stops drawing the 1999
  viewmodel and the mod draws arms (WRAD ARMS, CC0) + pistol (Quaternius, CC0), with CPU
  skinning and **two-bone IK** on both arms up to the controllers. Occlusion by the world comes for
  free (the eye FBO already had depth). Only the `pistol` class; flat untouched. Doc:
  `docs/HANDS.md`; assets and licenses: `CREDITS.md` + `mods/vr/assets/SOURCES.md`.
- [x] 🟢 **6.7b VR weapon system (V2 of the own viewmodel, 13/10)** — what 6.7a
  left rough (weapon only in the right hand, sideways/backward, 4.2× bigger, no fingers) became a
  system in the mold of a modern VR game:
  - **fit RESOLVED**: per-asset axes in the cfg, HAND reference frame derived from the rig,
    grip anchored at the palm center (measured) and scale by the **real length in cm**
    — the pistol went from ~32 to **7.48 un = 19.0 cm**, with the barrel at **0.0°** from the hand's front
    and the grip at **0.00 un** from the palm;
  - **fingers that close**: flexion axis derived from rest (`cross(Y, palm-direction)`),
    30 bones, per-finger weights and a fist closed on the grip when holding;
  - **belly holster**, **draw with either hand**, **drop on the floor** (real gravity, the
    floor is the player's own), **pick up from the floor** and **two-handed grip** (the barrel goes from the support hand
    to the rear hand and the support hand's IK aims at the weapon); releasing one hand does not drop it;
  - **integration**: gated `MOD_BTN_ATTACK` (`enforce_armed`), **barrel aim**
    (`VR_AIM_FROM_WEAPON`, on), visual recoil in the weapon's reference frame, optional
    damped torso yaw (`VR_BODY_YAW_LAG`) so the holster does not turn with the head, and grip
    **per hand** with `subactionPath` (two XR actions, instead of one mixed);
  - **acceptance tool**: contact sheets (`VR_HANDS_SHEET=fit|weapon|curl|states|walk`)
    drawn on the SAME production path, with body-relative views, a `tools/ppm2png.py`
    converter and a numeric report in the log. The sheets (and the report) proved the
    fit and the `walk` script proved **8/8** state-machine transitions without a headset.
  - Doc: `docs/HANDS.md` (includes the bug notes and what is missing on the headset).
  - *Still needs the headset* (your end): the checks listed in `docs/HANDS.md`.
- [x] 🔴 **6.7b-fix The arm was posed right and the MESH was not (13/10, from the owner's headset
  report)** — *"the shoulder and the start of the arm are great, but at the forearm it got
  stretched and gigantic, so the hand ended up way out there."* The forearm mesh was stretched ~26
  un past the wrist, and **no bone had the wrong length**: the skeleton was correct, the numbers
  for the fit were correct, and the contact sheets still "looked like an arm" if you did not know
  what to look for. Two instances of the same mistake, both in the hand chain:
  - the **wrist bone's world matrix** was built as `inv(forearm) . P_hand . rest_world[wrist]`,
    with the rest matrix' **translation** included. Rig rest positions are absolute body
    coordinates (the wrist is ~6 un from the rig origin) and the skin is `cur_world . ibm`, so the
    translation does **not** cancel: the hand mesh landed at `wrist + scale . Rw . p_rest`. Fix:
    use the **rotation only** of the rest matrix (translation zeroed), which makes the skin exactly
    `T(wrist) . S(s) . Rw . T(-p_rest)` — rotate the hand about its own wrist and put the wrist
    where the IK put it;
  - the **palm centre was stored as an absolute rig coordinate** (`wrist + fwd*0.55d`), and the hand
    frame it feeds has its **origin at the wrist**; the ~6 units were added again, this time to the
    **weapon**, which is mounted on that frame — the pistol left the hand and floated ~27 un away.
    Fix: store it **relative to the wrist**.
  - **What made this findable** was refusing to judge by eye: the report now prints, on every
    sheet, the distance between **where the skinned mesh lands** and **where the math says it
    should** — `mao: wrist slip 0.00/0.00 un | palm centre 0.00/0.00 un` — plus `braco: target vs
    reach and elbow bend`, which separates "the arm is at full stretch because the target is far"
    from "the chain is wrong". Both must read `0.00`; they are the regression test for the class
    (mesh-in-the-wrong-place with a healthy skeleton), which no bone-length check would catch.
  - Also fixed: the **synthetic-hand fixture had two copies** (the drawing path and the state path),
    so they could disagree; now there is one `synth_hand_fill()`. And the fixture placed the support
    hand **outside the arm's reach**, forcing a straight arm that looks "stretched" in the sheets;
    `synth_dist` is now 16 un (reach is ~22), i.e. inside it.
  - Verified: `mao:` and `braco:` at `0.00`, `encaixe` unchanged (barrel 0.0°, grip 0.00 un, 7.48 un
    = 19.0 cm), `walk` still **8/8**, flat mode untouched, build warning count unchanged. The
    contact sheets were re-rendered: the arms are now geometry of normal thickness with a bent
    elbow, and the hand grips the pistol.
- [x] 🔴 **6.7b-fix3 The wrist was rolled by the CONTROLLER'S frame (13/10, from the owner's
  headset report)** — *"both the right and the left arm look as if I had rotated my wrist to the
  right; twice as much on the right."* Two independent causes, one modelling error and one
  diagnostic error:
  - **the modelling error**: the hand was `Rw = Bc . Br^T` (controller frame onto the palm frame
    measured in the rig's rest pose), which **forces** the two frames to coincide. Every degree of
    difference between the axes the runtime reports for a grip and the wrist this rig's rest pose
    has therefore became a **constant twist on screen** — the same map for both hands (so both
    looked twisted the same way), and it cannot be tuned away by any fit value. Measured in the
    fixture: the controller frame asks for **±115°** of wrist roll. The aim and the fit never looked
    broken because a roll **about** the barrel axis does not move the barrel;
  - **the fix splits the rotation by what each source knows**: the **controller supplies the
    direction** (`aim_dir`, including the two-hands blend) and the **arm supplies the roll** — the
    IK already resolved the forearm, and `R(Wf3) . R(rest_world[forearm])^T` is the rig-to-world
    rotation that carries the wrist's rest relationship. `wrist_roll` (default 0) blends back
    towards the controller's roll, and `1` reproduces the previous behaviour **exactly**, so this is
    reversible by cfg, without a rebuild;
  - **the diagnostic error** ("twice as much on the right"): the fixture had **both** synthetic
    hands squeezing, which put every sheet into the **two-hands** state — the rear (right) hand
    carried the barrel blend on top of everything else, and the support (left) arm was dragged to
    the weapon at near-full extension (`braco:` read **23.4 u / 5°**; now **19.6 u / 52°**). The
    fixture now squeezes with the rear hand only, so the sheets show the natural single-handed state,
    and the `states` cell for two hands stacks the hands (the support hand *in front*, as in a real
    two-handed grip) instead of side by side, which had made the barrel point sideways;
  - **new acceptance number**: `punho:` prints, live, how many degrees of roll the controller is
    asking for and how many are being applied (`0` with the default cfg) — the same idea as `mao:`:
    turn the complaint into a number that must read zero and cannot silently regress.
  - Verified: `encaixe` unchanged (0.0° / 0.00 un / 7.48 un = 19.0 cm), `mao:` 0.00/0.00, `punho` 0°
    applied, `walk` **8/8**, flat mode untouched (0 lines from the subsystem), same 2 build warnings.
    All sheets re-rendered; the three images in `docs/` were regenerated because the published ones
    came from the twisted build.
- [x] 🔴 **6.7b-fix4 The weapon was dropped on the floor at spawn (13/10, from the owner's headset
  report)** — *"since we implemented the new hand-closing scheme, no weapon appears anymore; I
  cannot see it in the hand nor in the holster."* The state machine released the weapon when "the
  grip is not pressed", and on the **first frame nobody has pressed anything yet**: the weapon was
  let go before the player could see it, so it was lying on the ground where the controller happened
  to be (reproduced without a headset with `synth_grip 0`: `arma largada no chao (16 -6 -27)`).
  Grabbing already required an **edge** (you must squeeze); releasing now does too, on the
  **aggregate** "somebody is holding it" (`g_ws.held`) instead of one hand's zero:
  - the spawn case cannot fire (nobody was holding), and a hand that never squeezed cannot drop the
    weapon either;
  - with two hands, `held` is the **union** of the holding hands, so letting go with the rear hand
    while the support hand still holds does **not** drop it. The first attempt (a per-hand edge)
    broke exactly that: `walk` step 6 read `na mao` instead of `no coldre` — the script caught it;
  - the log now prints the state the weapon is **born in** (`arma nasce NA MAO`), so this starts
    with the log instead of a guess.
  - Verified: with `synth_grip 0` the state stays `na mao` and the pistol renders in the hand
    (sheet + `encaixe` 0.0° / 0.00 un / 19.0 cm), `walk` back to **8/8** (ground → grab left →
    two hands → store in the holster → draw right), flat mode untouched.
  - *Why the sheets missed it:* the fixture squeezes the rear grip, which is exactly what keeps the
    weapon in the hand. The failing case is the **absence** of a squeeze, so `synth_grip 0` is part
    of the test from now on.
- [x] 🔴 **6.7b-fix5 The hand frame's "up" was the palm's normal, not the grip's axis (13/10, from
  the owner's headset report)** — *"the weapon is upside down and not sitting where it naturally
  would"*. **This is the root cause of 6.7b-fix3 as well** (the twisted wrists): one 90° error in the
  hand frame, with three visible faces.
  - the frame's `up` came from `cross(palm-forward, pinky→index)` with the sign chosen by "the palm
    faces the centre of the rig". For a rest pose with the palm horizontal (this rig) that test is
    **degenerate** — it decides on the small horizontal component — and it resolved **differently
    for the two hands** (`palma x esquerda` came out `+1.00` on one hand and `-1.00` on the other,
    impossible for a mirrored pair), so the controller map *and* the weapon's mount were 90° out,
    inconsistently;
  - **the axis that a grip uses is the KNUCKLE LINE** (pinky→index, continuing to the thumb): on a
    pistol the beavertail is between the thumb and the index and the magazine exits at the pinky, so
    the grip runs along that line and the slide's top faces the thumb side. It is also the axis a
    **grip-style controller reports as `up`** (Touch: `+Y` out of the top face, where the thumb is).
    Pairing the controller's `up` with the palm's normal throws the knuckle line into the horizontal,
    and a wrist whose knuckle line is horizontal is a wrist turned ~90°;
  - the weapon, mounted on the frame, inherited it: its **grip pointed out of the palm** (not into
    the fist) and its slide faced the wrong way. The `encaixe` line could never see this: it measures
    along the barrel, and a roll **about** the barrel leaves it at `0.0°`.
  - Fix: build the frame from anatomy and **check the anatomy instead of assuming it** — the load log
    prints the agreement between the `up` axis and the thumb's own position
    (`polegar +0.93 (tem de ser +)`), which caught a first attempt with the sign reversed
    (`polegar -0.93`). The palm's normal is no longer used for anything (printed only as a
    cross-check; its polarity is the unreliable part).
  - Consequences: the roll can come **from the controller** again (`wrist_roll 1`, the new default —
    the player holds the controller upright, so the weapon is upright, and there is no invented twist
    to remove). The `punho:` disagreement dropped from **±115°** to **−24/+38°**: that gap *was* the
    90° error. `support_side` flipped to −1.4 (its axis is now the grip's left, the side the palm
    faces). All sheets re-rendered (the three images in `docs/` again — they had been regenerated
    from the twisted build).
  - **New acceptance number**: the sheets print the weapon's **slide axis against the body's up**, and
    in the fixture it must read `+1.00 / 0.00 / 0.00` (`arma:` line). It read `+0.86 / −0.52` (a 31°
    cant) before. *A fit check that cannot see a roll cannot certify a grip.*
  - Verified: `encaixe` unchanged (0.0° / 0.00 un / 7.48 un = 19.0 cm), `mao:` 0.00/0.00, `arma:`
    +1.00/0.00/0.00, `walk` **8/8**, flat untouched (0 lines from the subsystem), build clean.
- [ ] 🟢 **6.7c Registered debt (out of this stage)**: **gesture-based reload/grenade pin**,
  wrist roll on the weapon (forearm twist), haptics, sound, buy menu in VR, dropped weapon
  collision and the **remaining weapon classes** (rifle/SMG etc., which stay on the game viewmodel).
  Recoil (`punchangle`) still deflects the shot and does **not** push the camera — good against
  motion sickness, but it removes the visual feedback, which now comes from the weapon.
- [x] 🟡 **6.8** Fine IPD adjustment via the keyboard (symptom: "near is hard to fuse, far looks right" — classic of camera separation being too large, because the disparity of what is far is tiny). ABI **v7**: `Mod_Key` + `MOD_CAP_KEYS` — the Mod sees the key **before every engine handler** and can **consume it**, which is what allows a calibration UI without the key leaking into the game. In `mod_vr`: F9/Home/End turns the mode on; `-`/`=` = ±1.0 mm, `,`/`.` = ±0.1 mm, `0` zeroes it; the adjustment is a **delta** over the base (runtime or `VR_IPD`), applied by **scaling** the measured vector between the eyes (preserves direction, including the cant); the number appears **written on both eyes** (zero disparity = read inside the headset), in the 2D panel (`IPD 74.6 mm = base 68.3 (runtime) +6.3 | posicao +63`) and in the log, and the delta persists in `game/mods/vr_ipd.cfg`. *Done when:* the adjustment changes the image monotonically, the value survives a restart and `0` returns to the base. — implemented and validated without a headset (ABI v7 loads; keys injected into the window reached the hook, the log recorded each step and the `.cfg` was written).
  - **Owner's result on the headset (12/10):** the best landed **at the floor of the adjustment** (the then `-40.0 mm` → 27.3 mm), and he wanted to go lower. The clamp was **mine** (`VR_IPD_DELTA_MAX_MM 40`), not physical → the limit became the **final IPD** in **[0, 110] mm** (0 = the cameras coincide, mono image; below 0 the eyes swap sides and the brain does not fuse).
  - **Owner's final calibration (12/10, with the new range):** he went down 1 mm at a time to the **floor** — `mods/vr_ipd.cfg` closed at **`-67.2 mm` = final IPD 0.0 mm (mono)**, and that is where the pair fuses best ("the less, the better" up to the end of the range). The compiled default became that one (`VR_IPD_DELTA_DEFAULT_MM -67.2`, overridden by `VR_IPD_ADJUST` and then by the `.cfg`). Since the taste went to the end of the range, what remains to investigate **does not shrink along with the IPD**: vertical/rotational misalignment of the pair (the Quest 3 cant) and the engine's fixed near plane (4 u ≈ 10 cm, which roomscale lets you reach).

- [x] 🔴 **6.9** **Periphery culling bug** ("the map renders wrong at the edges, it has always been like that"): the engine chose what to submit with a **symmetric** frustum built from `fov_x`/`fov_y` (`GL_FrustumInitProj`), and the mod published there the **half-angle** (54°/55°) while the engine fields are **full aperture** — the culling window came out with **half** the image aperture. `R_CullSurface` only discards a surface when the **entire box** is outside, so a large surface crossing the center kept being drawn in full and the defect looked "90% ok"; what disappeared was what lives **only** in the outer band (distant walls, the sky — culled before the `SURF_DRAWSKY` test (`gl_rsurf.c:3530`) —, water, particles, beams), letting you see what was behind. **Ideal solution:** `GL_FrustumInitProjAsym` (frustum from the four `fov_asym` angles, the SAME region as the render) called in `R_SetupFrustum` when `RF_FOV_ASYM`, and the mod went back to publishing `fov_x = right − left`, `fov_y = up − down` (the right unit). *Done when:* no visible direction is discarded and flat does not change. — validated without a headset: the new function matches the engine's on symmetric angles (0 divergences in 5 FOV pairs, including 170×120) and, in the Quest 3's left eye, the old frustum discarded **6,475 of the 9,500 visible directions** against **0** for the new one; flat renders the same (frame dump checked). **Waiting on the owner's eye** to confirm the periphery came out whole.

**Phase milestone:** a match with the weapon in hand — *aim with the hand, hit where the weapon points, walk by the head.*

---

## 4. MVP acceptance criteria (demo checklist)

- [ ] From the GNOME menu → stereoscopic game on the Quest 3 via WiVRn, with one command.
- [ ] 1:1 head look; walk, turn (snap), crouch and jump through the controllers.
- [ ] Shoot and hit with aim by the head.
- [ ] Buy weapons in the buy menu with the laser pointer, inside the headset.
- [ ] Full match vs YAPB on the local dedicated server, with no change on the server.
- [ ] Flat mode intact (identical baseline).
- [ ] A second Mod (`null` template) loads through the same ABI, proving the architecture.

---

## 5. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| OpenXR asymmetric projection vs symmetric frustum of the GoldSrc pipeline | distortion/clip at the edges | MVP accepts symmetric FOV with margin (T 2.5); per-eye projection as an upgrade |
| Performance: old GL at 90 Hz + WiVRn encode | jitter, reprojection, motion sickness | `vr_resolution_scale` (T 2.7), conditional foveation (T 2.8), pacing by `predictedDisplayTime` |
| Wi-Fi latency/stability | motion sickness, disconnects | 5/6 GHz and a nearby AP; risk burned in Phase 0 with a test app |
| Desktop assumptions in the engine (mouse grab, single window, vsync) | broken input in VR | Mod as "display owner" turns off desktop paths (T 2.4) |
| Engine `buildnum` vs cs16-client's `MIN_XASH_VERSION 4190` | `Host_Error` in the VR client | `vr-mod` branch keeps versioning ≥ master (already caused a crash before) |
| Unexpected WiVRn + GL + Fedora combo | blocker in Phase 2 | isolated hello_xr (T 0.2) validates before touching the engine |
| Motion sickness in a fast FPS (CS movement) | usability | snap turn default, walk with grip, comfort documentation (T 5.5); vignette is left for post-MVP |
| GPL if distributing binaries | legal obligation | xash3d-fwgs (GPL-3.0) + cs16-client (GPL-2.0): Mod source open if distributing; local use with no obligation |

---

## 6. Open decisions (resolve with a prototype in hand, not on paper)

1. Snap turn: reference space rotation vs adding to viewangles — test drift/recenter (T 3.3 decides).
2. Symmetric FOV with margin is already enough for the MVP, or is asymmetric projection mandatory from the start (T 2.5 decides).
3. HUD panel: default "follows the head" or "fixed in the world" — offer both, decide the default in testing (T 4.2).
4. Final grip binding: walk (locomotion) vs secondary fire (T 3.2 vs 3.4 — prototypes decide).
5. Will cs16-client need its own branch? Hypothesis: no — hooks in the engine are enough for the MVP (reassess in T 4.1).

---

## 7. Post-MVP direction (architecture context, not current scope)

- **Mod v2 — 6DoF weapon**: per-controller `aim` poses → viewmodel and aim decoupled from the body (uses the existing `Mod_Camera`/`Mod_Input`; first real adjustment in cs16-client). **Stage 1 done in Phase 6** (weapon in hand + aim by hand, on the client); what remains here is that of 6.7: two hands, visible hands and gestures.
- **Visible hands/body**, spatial audio, passthrough, comfort vignette.
- **Other Mods on the same ABI**: HD pack, gameplay overhaul, accessibility — foundation ready in Phase 1, with no new engine patch.

---

## 8. Licenses

- `xash3d-fwgs`: GPL-3.0 · `cs16-client`: GPL-2.0 → the VR Mod, by being loaded alongside, inherits the GPL obligations **if distributed as a binary** (internal/local use: no obligation).
- ReHLDS/ReGameDLL (MIT): the VR project does not touch them; the **bots track** (§9) adds its own module (`cs_bot_advisor`) to ReGameDLL, under the same upstream license (MIT) — see `docs/BOTBRAIN.md`.
- Valve content (`cstrike/`, `valve/`): remains outside any redistribution.

---

## 9. Parallel track — bots with a "slow brain" (Jev / TypeSafe)

> **Status: V1 done and verified (2026-10-13), de_dust2/defuse only.** It is not part
> of the VR MVP; it uses the same server infrastructure and applies equally to VR and
> flat, because bots live on the server. Full document: `docs/BOTBRAIN.md`.

**Shape:** ReGameDLL's CCSBot stays as the muscle (nav, vision, aim, combat);
a Python sidecar (`tools/botbrain/`) asks the **Jev** model (TypeSafe) what the
*team* plan is and returns it by file; the `cs_bot_advisor` module (inside
`cs.so`) applies it to the bots. The model is never on the critical path — the transport is
a mailbox on disk.

**Shape decisions (why not "LLM per tick"):** bots run at 10–30 Hz and
Jev makes 1 request per decision in hundreds of ms; so Jev decides **where/when
the team commits** (at freeze and on events) and the algorithms play. One decision
per team per moment, cache by coarse signature (economy/scoreboard/phase/clutch)
and total fallback to stock behavior.

**Intents (closed set):** T `RUSH_B|RUSH_A_LONG|RUSH_A_SHORT|SPLIT_A|`
`HOLD_AFTER_PLANT|ECO_SAVE|DEFAULT`; CT `STACK_A|STACK_B|RETAKE_A|RETAKE_B|`
`ECO_SAVE|DEFAULT`. The intent→behavior map uses only the map's own zone centers
(no dust2 coordinates in the code).

**Verified:** bots play dust2 (generated nav, 4v4, they plant/defuse); `RUSH_B`
plants at B and `RUSH_A_LONG` at A; the observe→Jev→plan→applied loop shows up in
the server log; and the decisions vary with the state (postplant→`RETAKE_*`/
`HOLD_AFTER_PLANT`; enemy repeating B→`STACK_A`).

**Out of V1:** other maps, Jev per bot, learned policy/distillation, generated
chatter, buy micro-management, socket transport, UI.


## 2026-10-15 — Independent packages and integrated diagnostics (implemented)

The multi-mod plan is now implemented. The canonical platform lives in `sdk/`:
client ABI v9, server ABI v1, generic sorted hosts, explicit exclusive ownership,
per-mod contexts, background mailboxes/logs and copied per-view snapshots. Native
libraries remain mapped through exit. `-nomods` is the reference path.

Packages install into `game/mods/<id>/`; mutable calibration, environments, logs
and caches live in `game/mod-state/<id>/`. The local supervisor owns only its
session's server/client/sidecars, verifies server identity/readiness, tracks
activation generations and shows confirmed component states. Legacy launchers
are compatibility wrappers. The previously local first-spawn equipment fix is
published in the new generic ReGameDLL integration patch.

VR diagnostics now use shared ImGui tools; implementation units separate input,
configuration, runtime and rendering while preserving the existing hand changes.
OpenXR initialization no longer blocks the game for six seconds. Jev policy,
closed vocabulary and private Python sidecar belong to `bots-jev`, with real plan
expiry and stock-AI fallback. The SDK-only `x-ray-debug` package required no core
identifier branch. Its construction exposed cross-architecture alignment,
per-view projection and renderer-state gaps, which were corrected generically.

F6 opens the manager, F7 hides/shows the selected panel set, F8 toggles the selected
mod, and direct shortcuts are configurable. F9 remains VR calibration. Independent
panels retain geometry/visibility, source/severity logs, search, pause and clear.
Final UI review caught text-entry shortcut interference, capture after title-bar
close and saved positions outside a smaller display; all three have regression
coverage and were fixed.

Validation: 17 contract/native tests; 25 initial live checks including invalid
packages and exclusive conflicts; 16 applicable live checks after removing test
packages; UI input/geometry harness; real Jev/cache; map changes away from and back
to dust2; flat without mods; offline VR contact sheet; clean patch application and
repeated verification; engine/server/package builds. Measured frame intervals and
coverage limits are recorded in `docs/VALIDATION.md`. A headset pass and long
memory soak remain explicitly unverified.

Delivery profile: `flat-dev`, de_dust2, eight bots, bots-jev + x-ray-debug enabled,
VR disabled, Bots and X-ray panels visible. See `docs/MODS.md`, `docs/BUILD.md`,
`docs/XRAY.md` and `AGENTS.md` for extending and reproducing the platform.

### Mouse input follow-up

Corrected debug interaction when the native Esc menu is open, prevented passive
panels from changing gameplay cursor visibility, and preserved authoritative
SDL button coordinates. Mouse acquisition cannot reactivate the game client while
the tools own input. Added an actual SDL/ImGui button press/release regression,
retained VR/native-menu pointer paths and rebuilt the published engine integration.
