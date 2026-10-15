# Contributing to CS 1.6.1

Start with [docs/BUILD.md](docs/BUILD.md), [docs/MODS.md](docs/MODS.md) and
[AGENTS.md](AGENTS.md). New mods should be packages, without engine branches on
package IDs or imports between unrelated mods.

- Keep the SDK a sized, versioned C ABI. Client v9 and server v1 are the current
  contracts. SDK headers and host sources are canonical in `sdk/`; disposable
  `src/` integrations must also be represented in `patches/`.
- Use `mods/null/` for a small client example and `mods/x-ray-debug/` for a complete
  client/server example. Installed third-party packages require no core registry.
- Keep network calls outside the game process. Frame callbacks must be bounded;
  shared logging and snapshot services perform persistence in a worker.
- Define exclusive claims when replacing camera, input, pointer or bot control.
  Additive panels and overlays should coexist without claims.
- Disable cleanly, release owned input/resources and reject stale session data.
  Assume callbacks share the game process; native plugins are trusted code.
- Preserve user configuration and existing local edits. Never overwrite calibration
  during a build. Never commit Valve assets, keys, logs, binaries or virtualenvs.
- All project contributions are GPL-3.0. Record third-party licenses in CREDITS.md.
  Existing Portuguese comments do not need bulk translation.

Before proposing changes, run the relevant builds and tests, verify combinations
and the `-nomods` reference, update docs and state which hardware checks remain
unverified. `python3 scripts/verify-patches.py` checks integration reproduction.
Avoid unrelated cleanup in upstream code; fix structural problems in the owned
integration and record evidence in `docs/VALIDATION.md`.

VR's offline contact sheets remain available, using the production drawing path.
See `docs/HANDS.md` for modes, calibration and verification details.
