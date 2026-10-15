# Working on CS 1.6.1

- Read `docs/MODS.md` for the SDK and `docs/BUILD.md` for builds. Project-owned
  code lives in `sdk/`, `mods/`, `tools/`, `scripts/`, and `patches/`.
- `src/` contains disposable upstream checkouts. Change generic integration there,
  then publish the changes in `patches/`. New host implementations and headers are
  canonical in `sdk/`; run `scripts/sync-mod-hosts.py` before compiling.
- A mod belongs in its own `mods/<id>/` package. Never branch on a mod id in a host.
  Installed packages live in `game/mods/<id>/`, mutable state in `game/mod-state/<id>/`.
- Headers under `sdk/include/` are the only native mod dependency. Keep the ABI C,
  versioned and sized. Never expose engine, SDL, ImGui, OpenXR or C++ types there.
- Gameplay/GL callbacks run on the main thread. Network calls belong in sidecars;
  shared logs and snapshots use the host's bounded background I/O services.
- Enable/disable at frame boundaries, release held input, and retain native
  libraries until exit. VR can have an uninterruptible OpenXR initialization call.
- Preserve user calibration files and existing local edits. Never commit Valve game
  content, keys, runtime state, generated binaries, or local virtual environments.
- Run `python3 -m unittest discover -s tests -v`, `scripts/build-mods.sh`, and the
  appropriate engine/server builds. Test flat with `-nomods` and the managed profile.
- Report measured verification and hardware limits honestly. No headset means that
  stereo regressions can be checked offline, but real headset behavior remains unverified.
