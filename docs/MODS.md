# MODS.md — CS 1.6.1 Mod Architecture

> **Status: ABI v8 (2026-10-12).**
> v1 (Phase 1) frozen; **v2** added, at the END of the structs: `mod_view_t.viewport[4]`,
> `Mod_EyeCount()` and `Mod_AfterEyes()` (stereo — render passes per frame and pre-2D
> hook). **v3** added `Mod_Overlay2D()`: the last drawing point of the frame
> (HUD, menus, console and scoreboard ready; swap not yet done), used by the 2D
> screen debug panel of `mod_vr`. **v4** added `mod_view_t.fov_asym[4]`/`has_fov_asym`
> + the `RF_FOV_ASYM` flag in `ref_viewpass_t`: frustum **off-center per eye** — what
> the headset's canted optics require (see §"Per-eye frustum" below). **v5** (Phase 3)
> made `MOD_CAP_INPUT` leave inertia: `mod_input_sample_t` gained `move_up`,
> `viewangles_abs[3]` and `has_viewangles_abs`, and the engine started to **APPLY** the sample
> in the `usercmd` (see §"Input" below). **v6** added `Mod_ViewModel` +
> `mod_viewmodel_t`: the Mod takes the **weapon in hand** (overrides origin/angles of the
> viewmodel) and chooses the depth — `RF_VIEWMODEL_WORLD` (the weapon becomes **occluded
> by the world**) and `RF_NO_VIEWMODEL` (hide; for a Mod that draws its own
> weapon) — see §"Weapon in hand" below. **v7** added `Mod_Key` + `MOD_CAP_KEYS`:
> the Mod sees the keyboard event **before every engine handler** (OSK, console,
> menu, binds and client DLL) and can **consume it** — it is what enables a keyboard
> calibration UI (the IPD fine-tuning of `mod_vr` uses it) without the key
> leaking into the game. **v8** replaced the two never-wired stubs of `MOD_CAP_HUD`
> (`Mod_HudRedirect`/`Mod_HudPointer`) with **a per-frame exchange** —
> `Mod_Hud( mod_hud_t* )`: the engine delivers the **facts** (menu open, size of the
> 2D space, canvas texture) and the Mod decides the **policy** (does it want the 2D on an offscreen
> canvas? where is the menu cursor and when to click?). Along with it came the **2D canvas**
> in the ref, the **virtual cursor** that both engine cursor consumers
> read, and `R_InvalidateTextures` (see §"Menus in the headset"). Source of
> truth: `src/xash3d-fwgs/engine/common/mod_api.h` (branch `vr-mod`). Loader:
> `src/xash3d-fwgs/engine/common/modloader.c`. Mods:
> `mods/null/` (template) and `mods/vr/` (OpenXR — the first real Mod).
>
> **An old mod does not load in a new engine (and vice-versa):** the loader requires
> `api_version == MOD_API_VERSION` exactly — since the struct grew, a mod compiled
> against v3 would read shifted fields. When bumping the ABI, recompile `mods/*` (the
> `mods/null` is the first to complain).
>
> C++ build gotcha: `ModGetAPI` needs `extern "C"` (mangling), and the mod linking
> needs the source BEFORE the `-l` (Ubuntu uses `--as-needed` by default).

## 1. What a "Mod" is here

A **Mod** is a shared library (`mod_<name>.so`) that the engine
(Xash3D FWGS, branch `vr-mod`) loads at runtime and that extends the
game through **hooks** at defined extension points.

It is not a GoldSrc/Xash "game dir" (`-game cstrike` still exists and
remains the source of content). A Mod **does not duplicate content** — it is
code. The first Mod is **`vr`** (virtual reality via OpenXR).

## 2. Architecture decisions (recorded on 2026-10-11, Phase 0)

| # | Decision | Rationale |
|---|---------|---------------|
| 1 | **C ABI, versioned struct**, a single exported symbol (`ModGetAPI`) | binary stability: engine and Mod compile separately, even with different compilers; new fields at the end + `api_version` = compatibility in both directions |
| 2 | **Loading via `dlopen`**, driven by `-mod <name>` (engine looks for `mod_<name>.so`) | zero cost when off; multiple Mods possible in the future without collision; clean unload on shutdown |
| 3 | **Declared capabilities** (bitmask: `display`, `input`, `hud`) | the engine asks, the Mod answers: only the hooks of what the Mod declares are called; allows Mods of different natures to coexist |
| 4 | **Hook not registered = default engine behavior** | total fallback: without `-mod`, the game is byte-for-byte the current one |
| 5 | **The engine never knows OpenXR** (nor any Mod-specific technology) | all VR knowledge lives 100% in `mod_vr.so`; the engine only knows the generic contract |
| 6 | **VR is client-side** — server (ReHLDS/ReGameDLL), protocol and content untouched | VR client and flat client play on the same server, indistinguishable |

## 3. Contract v1 (frozen — see `engine/common/mod_api.h`)

The complete struct is in the header (source of truth). Summary of what the Mod implements/exposes:

- **Mandatory export**: `mod_api_t *ModGetAPI( void )` — the only required symbol.
- **Identity**: `api_version` (must be exactly `MOD_API_VERSION`), `mod_name`, `capabilities` (`MOD_CAP_DISPLAY`, `MOD_CAP_INPUT`, `MOD_CAP_HUD`, `MOD_CAP_KEYS`).
- **Lifecycle**: `Mod_Init( mod_engine_api_t* )` (receives `Printf`, `DoubleTime`, `engine_version`; return 0 refuses the load) and `Mod_Shutdown()`.
- **Frame** (always called): `Mod_FramePre( time )` / `Mod_FramePost( time )`.
- **`MOD_CAP_DISPLAY`**: `Mod_Camera( mod_view_t* )` — per view pass, can override origin/angles/FOV (external projection is left for phase 2); `Mod_BeginFrame( mod_gl_ctx_t* )` (window + engine GL context); `Mod_EndFrame()`.
- **`MOD_CAP_INPUT`**: `Mod_Input( mod_input_sample_t* )` — 1x/frame. **In v1 the engine delivers the sample and does not apply the contents** (real application, via the keyboard/mouse route with prediction, arrives in Phase 3 with mod_vr).
- **`MOD_CAP_HUD`**: `Mod_Hud( mod_hud_t* )` (v8 — engine facts + Mod decisions per frame; see §"Menus in the headset"), `Mod_HudRedirect`/`Mod_HudPointer` (the never-wired stubs of v1 **left** in v8) and, in v3, `Mod_Overlay2D()`.
- **`MOD_CAP_KEYS`** (v7): `Mod_Key( key, down )` — keyboard event before every handler; return 1 = consumed (see §"Keyboard" below).

### `Mod_Overlay2D()` (v3, `MOD_CAP_HUD`)

Last drawing point of the frame: it is called at the end of `V_PostRender`, **after**
HUD, menus, console, scoreboard and `SCR_MakeScreenShot`, and **before** `R_EndFrame`
(which presents the render target and does the swap). Practical consequences:

- what the Mod draws here is **on top** of all the game UI;
- the target is the window's 2D framebuffer; in VR the eyes live in other swapchains
  (submitted to the runtime in `Mod_EndFrame`), so an overlay here **does not go to the
  headset** — only to the monitor;
- it does not enter game screenshots (`SCR_MakeScreenShot` runs before).

Use `glGetIntegerv( GL_VIEWPORT )` to know the coordinate space: the
`R_Set2DMode` has already fixed the viewport in the 2D target, so the overlay is
correct with or without the internal render target of `ref_gl` (see
`r_scene_scale`/`r_msaa`).

**Target trap (2026-10-12):** with VR active the **inherited binding is not the
window** — the frame ends with the FBO of the **last eye** image bound (the mirror
blit saves and restores the binding it found). Whoever draws 2D without saying where
they draw ends up writing **inside the image that goes to the headset**. That is how
the debug panel and the game HUD appeared on the HMD and not on the monitor. Two
fixes:

- `ref/gl/gl_fbo.c` (`GL_BindScreenTarget`, called by `R_Set2DMode(true)`): without an
  internal render target the 2D draws **on the window**, and now this is said
  explicitly (`glBindFramebuffer( GL_FRAMEBUFFER, 0 )`) instead of trusting the
  inherited binding;
- `mod_vr` also **binds the window on its own** before painting the panel
  (and restores the binding afterwards), to depend neither on the engine nor on the
  hook order.

Rule for any Mod: before drawing in 2D, **bind the target explicitly**
(`GL_DRAW_FRAMEBUFFER_BINDING`, and save/restore if you don't own the frame).

Consumers today: the **2D screen debug panel** of `mod_vr` (FPS + VR loading
errors/warnings; `VR_DEBUG_PANEL`, `VR_PANEL_DUMP` — details in the header of
`mods/vr/mod_vr.cpp`).

### `mod_view_t.fov_asym[4]` / `has_fov_asym` (v4, `MOD_CAP_DISPLAY`)

`fov_x`/`fov_y` **can only express a centered frustum**: the renderer builds
`xMax = zNear·tan(fov_x/2)`, `xMin = -xMax` (same vertically). A headset with
**canted** optics does not deliver that — the runtime declares, per eye, four
independent and mirrored angles. Measured on the Quest 3 via WiVRn:

```
left eye:  L 54.0°  R 40.0°  U 44.0°  D 55.0°
right eye: L 40.0°  R 54.0°  U 44.0°  D 55.0°
```

They are 94° horizontal and 99° vertical **off-center** (an asymmetry of ±14° on the
horizontal and 11° on the vertical, with the sign swapped between the eyes). Drawing a
centered 54°×55° frustum and submitting the asymmetric one makes the compositor stretch
each eye's image by a different factor — the classic symptom is "each eye alone looks
good, the two together don't fuse".

With `has_fov_asym = 1` the Mod delivers the four angles in **radians and in the
OpenXR sign** (`angleLeft`/`angleDown` negative) and `ref_gl` builds the off-center
projection in `R_SetupProjectionMatrix` (`ref/gl/gl_rmain.c`); the flag
`RF_FOV_ASYM` travels in the `ref_viewpass_t` (`common/ref_params.h`).

**The culling also uses the four angles** (`GL_FrustumInitProjAsym`, called in
`R_SetupFrustum` when the flag is set): before, it was built from
`fov_x`/`fov_y`, which only describe a centered rectangle — and an asymmetric
frustum can only be covered by an envelope, which is too wide on the wide side and
**too narrow on the narrow side**. What is missing is not submitted for drawing:
geometry that lives only over that strip (distant walls, the sky, water, particles)
disappears from the image, and what appears in its place is what was behind — the map
"renders differently" at the periphery. Since `R_CullSurface` only discards a surface
whose **entire box** falls outside, large surfaces crossing the center continue being
drawn in full: that is what made the defect look "90% right".

`fov_x`/`fov_y` remain mandatory, but watch the **unit**: they are **total** apertures
in degrees (the engine divides by two internally — `tan(fov_x/2)` in the
projection, `SinCos(fov*0,5)` in the old frustum). Publishing a half-angle there counts
as if it were double and shrinks the culling to half the aperture — it was exactly the
bug this asymmetric path came to fix; with it, the value of `fov_x`/`fov_y`
stopped mattering for culling (but keep it correct: `right − left` and
`up − down` for any other consumer).

### Input: `mod_input_sample_t` applied to the `usercmd` (v5, `MOD_CAP_INPUT`)

In v1 the hook existed but was inert: `Mods_Input` delivered the zeroed struct and
**discarded** the response (old comment in `modloader.c`). v5 closed the path, in two
halves:

1. **Poll** — `Mods_Input()` is still called 1x/frame in `Host_Frame`
   (`host.c`, right after `Host_InputFrame`) and now **stores** the sample;
   `Mods_GetInput( &sample )` delivers it to whoever applies it.
2. **Application** — `IN_ModAppendMove( cmd, active )` (`engine/client/input/input.c`)
   is called in `CL_CreateCmd` (`cl_main.c`), **immediately after** the
   client's `CL_CreateMove` and `IN_EngineAppendMove`, and before
   `CL_PredictMovement`.

Why **after** the client DLL: it is the DLL that reads mouse/keyboard and writes
angles and `cmd->buttons` (by assignment, not OR). Applying afterwards makes the Mod
**beat the mouse** without turning it off — the mouse keeps navigating menus, because
both `IN_EngineAppendMove` and `IN_ModAppendMove` cut at
`cls.key_dest != key_game`. And since `CL_PredictMovement` comes next, what
the Mod writes is what goes in the `usercmd` **sent to the server** and in the prediction.

Fields v5 added (at the end of the struct, as always):

| Field | What for |
|---|---|
| `move_up` | vertical axis → `cmd->upmove` (on the ground pmove zeroes `wishvel[2]`, so it only counts where the game already uses it: water, spectator) |
| `viewangles_abs[3]` | **absolute** orientation (pitch, yaw, roll) |
| `has_viewangles_abs` | `1` = use the absolute; `0` = the `viewangles_delta` of v1 counts |

**Absolute, not delta.** The HMD pose already is an absolute orientation; with delta the
Mod would fight the mouse and the client's `CL_AdjustAngles`. Absolute is also
what makes "head-oriented movement" free: the Mod injects the HMD yaw and sends the stick
**raw** in `forwardmove`/`sidemove` — `pm_shared` builds the
`wishdir` with `AngleVectors(cmd.viewangles)` and ignores pitch on the ground, so
"looking up" does not change the speed and "turning the head" changes the walking
direction. No direction math in the Mod.

Application rules (in `IN_ModAppendMove`):

- Angles: copied to `cmd->viewangles` **and** to `cl.viewangles` (same as
  `IN_EngineAppendMove`, for prediction and the view model to follow). Pitch is
  clamped to ±89 as in the client's `CL_CreateMove` — the render may look further
  (`Mod_Camera` uses the raw pose), but what goes to the server stays within
  what the game accepts.
- Movement: only **taken over** when the stick left the deadzone (`value != 0`).
  With the stick centered what came from the keyboard remains — the keyboard stays as
  a fallback on any axis.
- `MOD_BTN_WALK` applies `cl_movespeedkey` (same factor as CS's `+speed`).
- Buttons → GoldSrc protocol `usercmd` bits, which **already exist** in
  `common/const.h`: `MOD_BTN_ATTACK→IN_ATTACK`, `ATTACK2→IN_ATTACK2`,
  `JUMP→IN_JUMP`, `USE→IN_USE`, `RELOAD→IN_RELOAD`, `CROUCH→IN_DUCK`,
  `SCORE→IN_SCORE`. `MOD_BTN_MENU` has no equivalent bit — the Mod handles it.
- The trigger gate is the Mod's: with the weapon out of hand (holster/ground) `MOD_BTN_ATTACK`
  **is not marked** — that is how "dropping the weapon" has a consequence without the ABI having a
  channel of inventory. See `docs/HANDS.md`.

### Per-hand grip: Mod actions, not the ABI's (2026-10-13)
The squeeze was bound to **one** action with both `squeeze/value` (`left` **and** `right`) and
`countSubactionPaths = 0`: the runtime returned a single, mixed value, and it was impossible to know
*which* hand squeezed — without that there is no per-hand grab/release nor a two-handed grip.
Today there are **two actions** (`grip_left`, `grip_right`, `FLOAT`, with the `subactionPath` of their hand, via
`vr_make_action_sub`), and `MOD_BTN_WALK` still comes from the **larger** of the two, as before.
It is the same pattern already used in the pose actions (`aim_left`/`aim_right`). The hand paths
started being resolved **before** the creation of the actions, because now they need them.

### Weapon in hand: `Mod_ViewModel` (v6, `MOD_CAP_DISPLAY`)
Until v5 the viewmodel was untouchable: the client DLL wrote origin and angles into
`clgame.viewent` (the **orientation** comes from `V_CalcGunAngle`, derived from the view; the
**origin** is the player's eye, with bob/smoothing) and the ref only drew — there was no
hook in between. v6 opens the hole with an **input-and-output** struct: the
engine pre-fills with what the DLL built and the Mod decides what changes.

| Field | Direction | What for |
|---|---|---|
| `eye` | input | eye of the current pass |
| `player_origin[3]` | input | **central** eye of the player in the world — without the roomscale offset and **without the separation between the eyes** |
| `origin[3]`, `angles[3]` | input/output | what the client DLL wrote; the Mod overrides and marks |
| `model_name[64]` | input | e.g.: `v_ak47.mdl` — lets the Mod fine-tune the fit per weapon class |
| `has_origin`, `has_angles` | output | apply `origin[]` / `angles[]` (the angles mirror into `curstate.angles` and `latched.prevangles`, as the DLL does) |
| `world_depth` | output | **normal** depth: the weapon is occluded by the world |
| `hide` | output | do not draw the viewmodel (the Mod draws its own) |

Three decisions that are not obvious:

1. **Called AFTER `Mods_Camera` and per eye.** After because `V_GetRefParams`
   **zeroes** `rvp->flags` (`cl_view.c`) — a flag requested before it would be erased.
   Per eye because `pfnCalcRefdef` runs again on every pass and rewrites the
   viewmodel: without reapplying, on the second eye the weapon would go back to the middle of the
   head. Contract consequence: the Mod must be **idempotent within the frame** (only
   read state).
2. **`player_origin` is the central eye, not the `rvp`'s `vieworigin`.** The vieworigin already
   left `Mods_Camera` with the roomscale offset **and** with the eye separation. The
   weapon is a single object in the world and both eyes have to see it in the same place: using
   the vieworigin would give ~1.3 cm of disparity per eye and the weapon would appear with the
   wrong depth.
3. **`RF_VIEWMODEL_WORLD` turns off a hack, it does not add a feature.** The
   `R_DrawViewModel` compresses the viewmodel's depth range (`VIEWMODEL_DEPTH_RANGE`)
   precisely so the weapon **never** cuts into the wall — correct behavior on a
   monitor and wrong in VR, where the weapon has to disappear behind geometry. Without
   requesting the flag, **nothing changes** for any Mod.

The debug panel remains 2D on the window: nothing here pushes it to the headset —
whoever handles menus/HUD on the HMD is `Mod_Hud` + the 2D canvas (v8, §"Menus in the headset").

### Keyboard: `Mod_Key` (v7, `MOD_CAP_KEYS`)

Called at the **top** of the engine's `Key_Event` (`engine/client/input/in_keys.c`),
right after `Key_Rotate` and before everything: virtual keyboard, console, menu,
binds and the client DLL's `pfnKey_Event`.

```c
int (*Mod_Key)( int key, int down );   // 1 = consumido
```

- **`key`** is the engine keynum (`engine/keydefs.h`): printable keys arrive
  as **lowercase ASCII** (`'a'`=97, `','`=44) and specials as constants
  (`K_F9`=143, `K_HOME`=151…). Since `mod_api.h` is self-contained on purpose, the
  specials that a debug UI usually wants are pinned in it as
  `MOD_KEY_*` — and the mapping is by **scancode**, not by layout: `'['`/`']'`
  are the keys where the US layout has them (on ABNT2 they are swapped).
- **Return 1 = consumed**: the `Key_Event` ends there and the key **does not reach the
  game** (nor the binds). Returning 0 follows the normal path.
- **Consume only what you use.** A Mod should return 0 when its UI is
  off — consuming always would erase the key from the game without the user asking for anything.
  Whoever wants to preserve the *down* and the *up* coherent (so the game doesn't see half a
  key) consumes both events.

Reference use: the IPD fine-tuning of `mod_vr` (`VR_IPD`/`mods/vr_ipd.cfg`)
turns the mode on with F9/Home/End and, while it is on, consumes `-`, `=`, `,`,
`.` and `0`. Outside the mode, no key is consumed.

### Menus in the headset: `Mod_Hud` + the 2D canvas (v8, `MOD_CAP_HUD`)

**The problem:** the game menu (pause, team, class, buy) is drawn by
`mainui`/client DLL with the engine's **2D functions**, straight into the window's
framebuffer. For it to exist in the headset you cannot "reimplement the menu": you must
**capture that drawing** and show it there. And for the pointer to work, the shortest
path is the reverse: let the engine **believe** the mouse is where the laser
points.

**Three pieces, each in a different place:**

1. **Facts** (`Mod_Hud`, 1x/frame): the engine delivers `menu_open` (`cls.key_dest ==
   key_menu` — the ESC menu and the client menus, team/class/buy, **all**
   mark `KEY_MENU` on opening), `ui_visible` (`UI_IsVisible()`), the size of the 2D
   space and the canvas texture; the Mod returns `want_canvas`,
   `has_pointer`/`x`/`y` and `buttons`. Called in `Host_Frame` **after**
   `Mods_FramePre` and **before** the input frame — it is this position that makes the cursor and
   the click count for the frame that is starting.
   The predicate is **not** `!= key_game` on purpose: that would include the console
   (`key_console`) and message mode, and opening the console would turn on the headset
   panel with no menu inside it (console and HUD stay out — the owner's decision
   on 2026-10-12: "only the menus").
2. **2D canvas** (`ref/gl/gl_fbo.c` + `engine/client/cl_view.c`): with
   `want_canvas` (and `menu_open` — double lock), `V_PostRender` diverts the frame's
   **menus** (`VGui_Paint` + `UI_UpdateMenu`) to an RGBA8 target the size of the 2D
   space, created on demand (`R_HudCanvasBegin`), and at the end returns
   the result to the **window** with blend (`R_HudCanvasEnd`) — without that the monitor
   would be left without a menu. The Mod receives the `texnum` and draws the canvas on the eyes.
   **The canvas does NOT capture the HUD** (nor the console, nor the debug overlays): they
   are drawn straight to the window, before `R_HudCanvasBegin`. That is a requirement,
   not a detail — the HUD is game content and, captured in the canvas, appeared **on
   top of the buy menu** inside the headset (the green "buy zone" icon,
   the radar, the crosshair and health/ammo floating over the items, hindering the
   reading). That is how the owner described the defect on 2026-10-12: "a blue/cyan
   element that is not the screen, but on top of it". Measured: without the HUD in the canvas, the
   buy menu canvas loses ~3 thousand opaque pixels (the icon) and the menu is
   identical.
3. **Virtual cursor** (`engine/client/input/input.c` + `cl_game.c`): the absolute
   position of the menu cursor has **two consumers** and both start reading
   `IN_GetCursorPos` — `IN_MouseMove` (→ `UI_MouseMove`, the engine UI) and the slot
   `GetMousePosition` of `cl_enginefunc_t` (→ `g_pMenu->MouseMove`, the **client DLL**
   menus). A single override point: if the Mod wrote to only one, the other
   would pull the cursor back and the hover would flicker. The click is
   `IN_SetVirtualButton` → `IN_MouseEvent( 0, down )` → in `key_menu` it lands in
   `Key_Event( K_MOUSE1 )` → `UI_KeyEvent` → the menu item (`K_MOUSE1` = 241).

**`R_InvalidateTextures()`** (`ref/gl/gl_fbo.c`): the Mod draws with **raw** textures
(`pglBindTexture`), which the ref's cache does not see — and the cache is consulted
by equality ("it's already that one, don't rebind"). Without invalidating after each point
where the Mod draws (`Mods_AfterEyes`, `Mods_Overlay2D`), the next engine `GL_Bind` may
**skip** the bind and the UI/3D come out with the Mod's texture.

**Panel orientation (mirror bug, 2026-10-12):** the anchor uses **horizontal** forward and
right (`fwd = (cy, sy, 0)`, `right = fwd × up = (sy, −cy, 0)`) and
the Mod projects the rectangle through each eye's asymmetric frustum. With the right
written as `(−sy, −cy, 0)` (only X inverted) the rectangle stopped being
perpendicular to the forward and the panel came out **mirrored** at every yaw with non-zero sine
and **sheared** at the others — it was the "mirrored menu" reported in the headset. The math
checks out: `right` must be `fwd × up`, as in the engine's `AngleVectors` with
pitch/roll zero. Verification without a headset: `/tmp/warp_panel.py` projects the real
canvas with the same math as the Mod and shows the panel as it comes out on the HMD —
comparing the two vectors, the old one comes out mirrored and the new one correct; a sweep
of 72 yaws shows 70 failures with the old vector (only 0° and 180° passed) and zero with
the fixed one.

**Debug without a headset:** the privileged cvar `hud_ptr_test` (default 0) forces the
canvas and drives the cursor; `hud_ptr_test 2` **opens and closes the menu on its own** (the
rest of the command-line `+commands` are lost during map loading, so
you can't chain `+togglemenu`) and `hud_ptr_test 3` opens the **real buy menu**
through the client command `showvguimenu 28` (`MENU_BUY`, the same path as the
`b` key) and leaves it open — it is the content the owner sees in the headset. With `-console`
(window mode only has a console with that flag: `Con_Printf` returns early without
`host.allow_console`) the trace `hud_ptr_test:`/`UI_MouseMove`/`UI_KeyEvent` appears
on stdout, and `-dev 1` also turns on the `Con_DPrintf` of `UI_MouseMove`/`UI_KeyEvent`.

The **exact content of the panel** comes out through `HUD_CANVAS_DUMP=/tmp/x.pam` (+
`HUD_CANVAS_DUMP_FRAME=<n>`, the n-th canvas frame — the 1st is usually too
early), with an alpha histogram on the console: it is what separates "the game painted" from
"the world leaked through the alpha" and it is what identified the HUD over the menu. The
**mod_vr** dump (`VR_PANEL_DUMP`) captures the **window** (the canvas already blended over the
mirror), so the two are complementary: canvas = panel content, window =
monitor path.

### Call sites in the engine (implemented in Phase 1)

| Call site | File | Hook |
|---|---|---|
| `Host_Init` (after executing the configs) | `engine/common/host.c` | `Mods_Init` — parse `-mod <name>` + load |
| `Host_Frame` | `engine/common/host.c` | `Mods_FramePre` → **`CL_ModHud`/`Mods_Hud` (v8)** → `Host_InputFrame` → `Mods_Input` → … → `Mods_FramePost` |
| `CL_CreateCmd` (after the client DLL) | `engine/client/cl_main.c` | `IN_ModAppendMove` (v5) — applies the Mod sample to the `usercmd` |
| `Host_ShutdownWithReason` | `engine/common/host.c` | `Mods_Shutdown` (clean unload) |
| `SCR_UpdateScreen` | `engine/client/cl_scrn.c` | `Mods_BeginFrame` (pre-3D), `Mods_EndFrame` (post-render); the old `Mods_HudFrame` left in v8 (became `CL_ModHud`, in `Host_Frame`) |
| `V_RenderView` (per view pass) | `engine/client/cl_view.c` | `Mods_Camera( &rvp, viewnum )` — can override the view |
| `V_RenderView` (per eye, **after** `Mods_Camera`) | `engine/client/cl_view.c` | `Mods_ViewModel` (v6) — weapon in hand; applies `clgame.viewent` + `RF_NO_VIEWMODEL`/`RF_VIEWMODEL_WORLD` to the `rvp` |
| `V_PostRender` (HUD) | `engine/client/cl_view.c` | HUD/`SCR_TileClear` draw **straight to the window** (they do not enter the headset canvas) |
| `V_PostRender` (start of the menus) | `engine/client/cl_view.c` | `R_HudCanvasBegin` (v8) — diverts the MENUS (`VGui_Paint`/`UI_UpdateMenu`) to the canvas when the Mod asks |
| `V_PostRender` (end of the menus, pre-`SCR_MakeScreenShot`) | `engine/client/cl_view.c` | `R_HudCanvasEnd` (v8) — returns the canvas to the window (with blend) |
| `V_PostRender` (end, pre-swap) | `engine/client/cl_view.c` | `Mods_Overlay2D` (v3) — 2D overlay on top of the UI; `R_InvalidateTextures` (v8) after |
| `IN_MouseMove` / `GetMousePosition` | `engine/client/input/input.c`, `engine/client/dll_int/cl_game.c` | `IN_GetCursorPos` (v8) — the Mod's virtual cursor, read by both consumers |
| `Key_Event` (top, after `Key_Rotate`) | `engine/client/input/in_keys.c` | `Mods_Key` (v7) — key before every handler; can consume |
| GL context ready | `engine/platform/sdl2/vid_sdl2.c` | `Mods_VideoInit( window, ctx )` |

### Loading and search

- Command line: `./xash3d -game cstrike -mod null`.
- Tested paths, in order: `<rootdir>/mods/mod_<name>.so`, `<rootdir>/mod_<name>.so` (rootdir = the `xash3d` folder, e.g.: `game/`).
- Console: `mod_info` shows the loaded mod, capabilities and hook counters.
- Errors (missing file, missing symbol, wrong `api_version`, `Mod_Init` refusing) refuse the load **without bringing down the game** — the engine continues without a mod.

## 4. Rules for Mod authors

1. All access to the engine goes through the `mod_engine_api` received in `Mod_Init` —
   no direct linking against engine symbols.
2. Never block: hooks run in the game frame; heavy work goes to its own thread.
3. Respect `capabilities`: the engine only calls what was declared.
4. A Mod cannot assume another Mod exists.
5. Version error (`api_version` different) = load refusal with a clear message.

## 5. Planned Mods

| Mod | Capabilities | Status |
|-----|--------------|--------|
| `vr` (OpenXR, WiVRn/Quest 3) | display + hud + input + keys | **under construction** — per-eye stereo (asymmetric frustum + runtime IPD), debug panel (`Mod_Overlay2D`), menus in the headset (`Mod_Hud`), input and weapon in hand (Phase 6), and the **own viewmodel** (arms+weapon with holster, fingers and two-handed grip) in the `pistol` class — see `docs/HANDS.md` |
| `null` (log-only template) | none (lifecycle + frame only) | **done (Phase 1)** — `mods/null/`, `game/mods/mod_null.so`; it is also the engine gating test |
| *future:* 6DoF weapon, HD pack, gameplay, accessibility | — | post-MVP, over the same ABI |

## 6. How to write a Mod (quick tutorial)

1. Copy `mods/null/` to `mods/<yourmod>/` and change the name in `mod_api.mod_name`.
2. Declare the capabilities you will use; implement the corresponding hooks.
3. Build: a `gcc -O2 -fPIC -shared -I<engine>/engine/common -o mod_<name>.so <source>` suffices
   (see `mods/null/build.sh` — the only include is `mod_api.h`, self-contained).
4. Install to `game/mods/mod_<name>.so` and run with `-mod <name>`.
5. The rules of section 4 apply; load errors do not bring down the game.

## 7. References

- `PLAN.md` at the workspace root — complete plan, phases and acceptance criteria.
- Engine base: `src/xash3d-fwgs` (branch `vr-mod`) · client: `src/cs16-client`
  (branch `vr-mod`) · flat binaries baseline: `backups/flat-baseline-20261011/`.
