# Contributing to CS 1.6.1

This project exists to accumulate modifications on top of the open-source Counter-Strike 1.6
stack, and the two that exist today — the VR Mod and the Jev-driven bots — are meant to be
the first of several. Contributions are welcome, in that order of priority: **another Mod**,
**a fix**, **a document**.

## Ground rules

1. **The engine ABI is frozen.** A Mod talks to the engine through
   [`engine/common/mod_api.h`](patches/xash3d-fwgs-mod-abi/) and nothing else. Adding a
   capability is a deliberate, versioned change (`MOD_API_VERSION`); the engine refuses to
   load a Mod that does not match it exactly. Read
   [docs/MODS.md](docs/MODS.md) before touching that header.
2. **Flat mode must not change.** The game without a headset, without a Mod and without the
   bot advisor is the reference behaviour. Every feature here is gated: off by default, or
   active only when it applies (`VR_HANDS_*` only for the `pistol` class, the advisor only
   when `advisor_enable 1`, and so on). If your change alters stock behaviour, it is a bug.
3. **Measured claims only.** This repository's documents state numbers and say how they were
   obtained. If you cannot measure something, say so explicitly rather than describing it as
   working — "still needs the headset" is a valid and useful thing to write.
4. **Never commit game content.** Maps, models, sounds and sprites are Valve's; they are
   downloaded by SteamCMD and are excluded by `.gitignore`. Do not add them, not even one.
5. **Do not break the `.cfg` calibration loop.** The `game/mods/*.cfg` files are user
   calibration, re-read while the game runs. A build must never overwrite an existing one.

## Licensing of contributions

By contributing you agree your work is licensed under the project's **GPL-3.0** (see
[LICENSE](LICENSE)), inbound matching outbound. If your contribution embeds third-party
code, assets or fonts, add them to [CREDITS.md](CREDITS.md) with the license — assets must
be permissive (CC0, CC-BY, MIT or similar), and anything that requires shipping bulk
non-free data is out of scope.

## Source language

The code comments, log strings and `.cfg` comments are in **Portuguese**, the author's
language. New code may be written in either Portuguese or English; do not rewrite existing
comments in bulk as part of an unrelated change.

## Getting set up

Follow [docs/BUILD.md](docs/BUILD.md). For Mod work you need less than the full stack: the
engine header (`src/xash3d-fwgs/engine/common/mod_api.h`) and, for the VR Mod, the OpenXR
loader from `scripts/build-openxr.sh`.

## Making a new Mod

`mods/null/` is the template — a Mod that only logs, and the thing that proves the loader
gating works.

1. `cp -r mods/null mods/yourmod`, and rename the `mod_name` in `mod_api`.
2. Declare only the capabilities you implement (`MOD_CAP_*`). The engine calls a hook only
   if you declared its capability; declaring more than you implement is how you crash.
3. Implement the hooks. `Mod_Init` must be cheap and must not block: hooks run inside the
   game's frame, on the game's thread. Anything slow goes on your own thread.
4. Copy `mods/null/build.sh` and adjust it; it builds in rootless Podman with `gcc`.
5. Install into `game/mods/` and run the game with `-mod yourmod`.
6. Document it: a section in [docs/MODS.md](docs/MODS.md) if it extends the contract, and a
   document of its own if it is a feature (see HANDS.md and BOTBRAIN.md as models).

## Testing without a headset

This is the part worth copying from the existing work: features that need hardware to *see*
should still be verifiable without it. The VR Mod draws **contact sheets** — a grid of views
through the production drawing path — into a normal framebuffer dump:

```bash
cd game
VR_HANDS_SHEET=fit VR_DEBUG_PANEL=0 VR_PANEL_DUMP=/tmp/sheet.ppm VR_PANEL_DUMP_AT=12 \
  SDL_VIDEODRIVER=x11 ./xash3d -game cstrike -mod vr +map de_dust2
python3 ../mods/vr/tools/ppm2png.py /tmp/sheet.ppm /tmp/sheet.png
```

Along with the image, those modes print a numeric report to `game/mods/mod_vr.log`
(distances, angles, sizes) — image **and** number, so a fit can be judged instead of
guessed. Modes and what each proves: [docs/HANDS.md](docs/HANDS.md).

## Before you open a pull request

- It builds: `./mods/vr/build.sh` and `./scripts/build-xash.sh` (or whichever your change
  touches), with **no new warnings**.
- Flat mode is unchanged: run the client without the Mod and confirm the log has no new Mod
  lines and the game behaves as before.
- The documents match the code: cfg keys, defaults, file names and measured numbers all
  appear in `docs/`. A change that alters behaviour without updating the doc is incomplete.
- You said how it was verified, and what remains unverified on real hardware.

## Reporting a problem

Include, in this order: what you expected and what happened; the exact command line; the
relevant tail of `game/mods/mod_vr.log` (it prints one `perf:` line per second and the Mod's
own error lines); your OpenXR runtime and headset if it is VR; and whether flat mode is also
affected. If it is a fit/pose problem, attach a contact sheet — one image answers what a page
of prose cannot.
