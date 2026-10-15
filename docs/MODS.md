# CS 1.6.1 Mod SDK — client v9 / server v1

Mods are independent packages. The engine hosts hooks and services; packages own
policy. VR, Jev and x-ray never import one another. The GoldSrc game directory
(`-game cstrike`) remains separate from this extension system.

## Install and run

Copy a complete package directory to `game/mods/<id>/`. Remove that directory to
uninstall. Discovery happens on restart; activation does not need a restart. Stop
a session before replacing native binaries. User configuration, logs, cache and
sidecar environments live separately in `game/mod-state/<id>/`.

```text
game/mods/example/
  mod.json
  LICENSE
  client.so           # optional: Linux x86_64
  server.so           # optional: Linux i386, ReGameDLL host
  sidecar/brain.py     # optional, declared by manifest
  requirements.txt    # optional Python dependencies
  assets/
game/mod-state/example/
  settings.cfg
  channels/           # session/generation-scoped snapshots
  logs/               # client.jsonl / server.jsonl / sidecar.jsonl
```

Start `scripts/run-cs.sh` for flat development. It owns one client, one local
server and enabled sidecars. `--profile vr` selects VR; `--restore` restores saved
activation choices. `game/run-cs.sh -nomods` is the flat reference without the
runtime or UI. `-mod <id>` remains a standalone package selection in the new layout.

## Package contract

```json
{
  "manifest_version": 1,
  "id": "example",
  "name": "Example",
  "version": "1.0.0",
  "client_abi": 9,
  "server_abi": 1,
  "components": { "client": "client.so" },
  "capabilities": ["debug-panel"],
  "claims": [],
  "defaults": { "enabled": false, "panel": false }
}
```

IDs use lowercase ASCII letters, numbers, `-` and `_`; the directory and manifest
must agree. Component paths stay inside the package, including after resolving
symlinks. A Python sidecar is declared as `"sidecar": ["python", "sidecar/brain.py"]`.
The launcher substitutes that package's private Python, never a shell. Run
`scripts/setup-mods.sh` after installing packages with Python requirements.

The capability list describes the package; native callback gating uses
`mod_api_t.capabilities`. Exclusive `claims` are `camera`, `input`, `pointer` and
`bots`. Display/input/HUD-pointer hooks require matching claims. Debug panels and
world overlays are additive. Activation fails visibly if another active package
owns the same resource. A broken package does not prevent other packages loading.

## Native interfaces

Use only `sdk/include/`. Headers are self-contained C. C++ must export the entry
point with `extern "C"` and default visibility; keep other symbols hidden. Versions
and sizes are checked before initialization. Old ABI v8 libraries are refused.

- Client exports `ModGetAPI()`, returning `mod_api_t` with `api_version=9`, the
  package ID as `mod_name`, and `size=sizeof(mod_api_t)`. Existing camera, input,
  viewmodel and HUD contracts remain. V9 appends `Mod_Enable`, `Mod_DebugPanel` and
  `Mod_WorldOverlay`. The engine supplies `mod_engine_api_t.services`.
- Server exports `CSModServerAPI()`, returning `csmod_server_api_t`, version 1 and
  its size. Callbacks cover initialization, enable/disable, frame, round start,
  bot updates and shutdown. Queries expose copied matches, bomb-site positions
  and bounded bot operations. Player IDs include their edict generation.
- `csmod_services_t` supplies per-package paths, monotonic time, thread-safe queued
  logging/metrics and cached snapshot exchange. `ReadSnapshot` returns bytes or
  zero for missing, expired or previous-generation data. Payloads are limited to
  64 KiB and sixteen published channels per package. One producer owns each channel.
- `csmod_ui_t` supplies text, checkbox, slider and separator widgets without
  exposing ImGui. `csmod_draw_t` supplies view-local line/text drawing;
  `csmod_view_t` supplies the final camera, viewport, asymmetric FOV and local slot.
  World callbacks run once per view before eye submission.

The match snapshot has explicit alignment shared by i386/x86_64. Custom payloads
must use fixed-width fields with explicit layout or UTF-8 JSON. Never serialize
pointers, C++ objects, `long` or architecture-sized fields.

## Lifecycle and isolation

Packages are sorted by ID. Initialization is lazy and occurs once. Enable/disable
transitions happen between frames. Disabled packages receive no gameplay hooks;
initialized packages may still render debug settings. Failed initialization must
tolerate `Shutdown` for partial cleanup. Initialization must not wait on hardware
or the network.

Input and pointer ownership clear on disable. Bot ownership resets to stock tasks
and disposition. Hosts pin native libraries until process exit, including failed
loads, so blocked workers cannot execute unmapped code. Mods must finish their
own workers/resources where possible. Native packages are trusted code, not
memory-isolated processes.

The worker performs shared IPC and log persistence every 50 ms. Frame callbacks
only copy cached data or queue publications. Put slow work in sidecars. Logs keep
512 records in memory and rotate at 4 MiB with two retained files. Hiding a panel
does not disable the mod or logging.

The bridge uses private local files, a unique session, activation generations and
confirmed component states. The client accepts world-debug data only on the managed
endpoint. Remote debugging and remote server control are outside this version.

## Controls

| Control | Action |
|---|---|
| F6 | Open manager / return to game |
| F7 | Hide/show the selected set of debug windows |
| F8 | Enable/disable the selected mod |
| Up/Down in manager | Select mod |
| Tab / Enter / Space | Operate focused widgets |
| F9 | Existing VR IPD calibration |
| Shortcut buttons | Assign a direct toggle/panel key; Esc clears |
| `mod_info` | Print installed mods and local state |
| `mod_toggle <id> [0|1]` | Request activation |
| `mod_panel <id> [0|1]` | Change panel visibility |

Panels are passive during gameplay. F6 enables interaction and releases gameplay
input. Opening the native game menu with Esc also makes the panels interactive;
clicks over a panel go to its controls, while the native menu keeps working outside
the panels. The engine owns cursor visibility during ordinary gameplay. Closing
the manager or losing focus releases captured input. Each panel has
independent visibility, filtering, severity, pause and clear controls. Window
geometry, panel visibility and shortcuts persist under `mod-state/host/`.

## Add a mod

1. Copy `mods/null/`, choose a new ID and update its manifest.
2. Implement only needed hooks. Analysis tools usually need panel/world hooks,
   without exclusive claims. `mods/x-ray-debug/` is a full SDK-only client/server example.
3. Compile with `-fPIC -shared -fvisibility=hidden -I sdk/include`; add `-m32` for
   the server. `scripts/package-mods.py` builds the supplied examples. Installed
   third-party mods need no host registry or build-script edit.
4. Copy the package into `game/mods/<id>/`, restart and activate in F6.
5. Test reactivation, stale data, map changes, failures and coexistence. Document evidence.

Render/input rationale from the old ABI remains in [the v8 archive](archive/MODS-v8.md).
