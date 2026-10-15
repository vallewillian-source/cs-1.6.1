# Mod platform validation — 2026-10-15

Tested on Fedora Linux, rootless Podman, NVIDIA RTX 4060, SDL2/X11/OpenGL.
The dedicated server is i386; the client is x86_64. Game assets and credentials
were already installed locally and remain outside version control.

## Reproduction commands

Run from the project root after following [BUILD.md](BUILD.md):

```bash
python3 -m unittest discover -s tests -v
bash tests/run-ui-input.sh
python3 scripts/verify-patches.py
scripts/build-regamedll.sh
scripts/build-xash.sh
scripts/build-mods.sh
scripts/setup-mods.sh
```

For live loader failure cases, install disposable packages, restart, then run:

```bash
python3 tests/integration/install_fixtures.py
scripts/run-cs.sh
python3 tests/integration/session_smoke.py --window <client-X11-window-id>
# Close the managed session before removing fixtures.
python3 tests/integration/install_fixtures.py --remove
scripts/run-cs.sh
```

The X11 id must belong to the newly launched client, checked against the PID in
`game/mod-state/host/session.json`. The test sends actual native keyboard events;
it also uses the same session-tagged control mailbox as the manager. Fixtures
are SDK-only packages installed by copying folders, without rebuilding a host.

## Results

| Area | Evidence and outcome |
|---|---|
| Contracts and failures | 17 Python/native tests passed. Actual C++ runtime, advisor, projection and X-ray code run in harnesses. |
| Loader | Live tests reject wrong ABI, wrong struct size, absent entry point, refused initialization and missing binary; other mods remain active. A camera-owner fixture blocks VR, then pending VR activates after the owner is disabled. Malformed/path-escaping manifests are covered by contract tests. |
| Lifecycle | Live F8 disables/re-enables Jev and its sidecar; eight stock bots continue moving while Jev is off. Repeated toggles of VR, null and X-ray return to the flat profile. Transitions sampled at roughly 0.1–0.2 s. |
| Real Jev | Authenticated requests and cache responses observed in live rounds, with tokens, latency and decisions per team. Representative requests took 211–350 ms. No model stub in the live test. |
| Advisor failures | Harnesses reject invalid probabilities, wrong session/map/round/revision, future or expired observations, late responses and invalid cache entries. Rate-limited attempts remain pending. Native expiry resets earlier bot interventions. Transport and invalid-response counters are separate. |
| X-ray | Actual overlay harness excludes local/dead/behind-camera/disconnected players, uses crouched bounds, filters teams/names and rejects expired/prior-session data. Live snapshots contain all eight bots independent of client PVS; screenshots show boxes through world geometry. |
| Map changes | A live server changed from de_dust2 to cs_office and back. Authoritative snapshots followed both maps; on return the client reconnected, all eight bots joined and both package stacks remained active. Jev policy is explicitly dust2-only. |
| Interface | Native F6/F7/F8, simultaneous panels and activation independent of visibility passed. Actual UI callback tests cover text-entry shortcut suppression, capture cancellation after closing, and restored-window bounds. Loss of focus and restoration released manager input in the live client. Captures at 1920×1080 and 1280×720 were inspected. Independent finish review found no remaining material UI issue. |
| Persistence and installation | Saved panel geometry restored across launches. A window position saved at 1920 was clamped fully into 1280 after a bug found during validation. The six fixture packages disappeared from discovery after folder removal/restart; state is retained separately. |
| Flat reference | `-nomods` opened de_dust2 at 1280×720 without the mod UI or overlays. |
| VR without headset | Runtime unavailable returns flat responsively; activation/deactivation stays usable. The migrated package rendered its fit contact sheet and framebuffer dump without a headset. Existing hand-fit edits and calibration files were preserved. |
| Reproduction | Fresh temporary checkouts of both published base revisions accepted all integration hunks and complete reverse checks. Repeated application checks passed. Current engine, server and all supplied packages rebuilt. The server build retains upstream overloaded-virtual warnings. |

The initial fixture run passed **25 live checks**. A subsequent run after removing
fixtures passed the **16 applicable checks**. UI callback and X-ray harnesses were
extended after those runs to cover issues found during final review.

## Performance and bounds

With the game capped at 144 FPS, the initial 30-sample comparison measured median
frame intervals of **6.95 ms with panels visible** and **6.92 ms hidden**; maxima
were 7.05 and 7.82 ms. A later windowed sample measured medians of 6.98 and 6.94 ms.
These are short end-to-end frame-interval samples, not an isolated measurement of
UI CPU/GPU cost or a guarantee on other hardware.

Runtime tests verify the 512-row log limit and snapshot expiry. Queues, channels,
payload sizes, caches and log files have explicit bounds. Network requests run
only in the Python sidecar; the engine's mailbox reads use cached memory. Shared
IPC/persistence runs in the host worker. This is not a multi-hour memory soak test.

## Evidence and remaining hardware checks

Generated captures/reports live in ignored `build/validation/`; logs and session
state stay untracked. Useful artifacts include `nomods.png`, `vr-fit.png`,
`panels-1280-fixed.png`, `manager-1280.png`, `session-smoke.json`, and
`final-flat.png`. No Valve content or credentials are added to source control.

A real headset was unavailable. Stereo composition, controller release during a
live OpenXR session, runtime reconnection and hands in-headset still require a
hardware pass. Offline render/projection checks do not establish those results.
The protocol and server controls in this release target the managed local server;
remote servers are not part of this validation. Fresh patch application and
rebuilds were verified, but an empty-machine Steam download/toolchain bootstrap
was not repeated. See [XRAY.md](XRAY.md) for SDK lessons and [MODS.md](MODS.md) for
extension boundaries.
