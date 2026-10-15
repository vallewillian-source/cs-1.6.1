# Building CS 1.6.1

The supported development target is Linux x86_64 with SDL2/OpenGL; the local
ReHLDS/ReGameDLL server is i386 inside rootless Podman. No sudo is required.
Valve maps/models/sounds remain untracked and must be obtained through SteamCMD.

## Reproduce the stack

Clone upstreams under `src/` and check out these bases:

| Tree | Revision |
|---|---|
| ReHLDS | `550f2d6` |
| ReGameDLL_CS | `4a50c42e85fd3778c2b7d24731c7d30c83bbab01` |
| xash3d-fwgs | `590869d280aad5b3646ab39d956a9ea3ac40ba4c` |
| cs16-client | `6a49bcc` |

Sources: [ReHLDS](https://github.com/dreamstalker/rehlds),
[ReGameDLL](https://github.com/rehlds/ReGameDLL_CS),
[Xash3D](https://github.com/FWGS/xash3d-fwgs),
[CS16Client](https://github.com/Velaron/cs16-client).
Initialize the upstream submodules where required.

From the **project root**, run:

```bash
python3 scripts/apply-patches.py
scripts/build-regamedll.sh
scripts/build-xash.sh
scripts/build-cs16.sh
scripts/build-openxr.sh
scripts/build-mods.sh
scripts/setup-mods.sh
scripts/run-cs.sh
```

The patch script checks every hunk before applying and verifies already-patched
sources in reverse. It refuses partial/divergent integrations. Canonical owned
sources and dependencies are copied from `sdk/` and `third_party/` into disposable
upstream trees. Builds repeat this verification automatically. Test fresh patch
application with `python3 scripts/verify-patches.py`.

`scripts/Containerfile.mods` creates the reusable `localhost/cs161-build:1`
toolchain image. Override with `CS_BUILD_IMAGE` for another prepared image. Rebuild
that image deliberately when changing the toolchain; the upstreams, ImGui and JSON
sources are pinned. The base SteamCMD image is still tagged `latest`, so this is
source-reproducible rather than a bit-for-bit toolchain guarantee.

The engine launcher explicitly uses `-no-pie` on Linux to match upstream's
`-fno-pie` compilation on modern GCC. The managed server puts system i386 runtime
libraries ahead of SteamCMD's old bundled `libgcc`/`libstdc++` copies.

## Game assets

Obtain Valve assets through SteamCMD, not this repository:

```bash
podman run --rm -v "$PWD/server":/data:Z docker.io/steamcmd/steamcmd:latest \
  +force_install_dir /data +login anonymous \
  +app_set_config 90 mod cstrike +app_update 90 -beta steam_legacy validate +quit
```

Use the `steam_legacy` branch: ReHLDS needs the legacy server interfaces. Copy the
`cstrike` and `valve` content into `game/` as needed. ReHLDS itself is unchanged;
build it inside the toolchain with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` and install
`engine_i486.so` / `hlds_linux` into `server/`. CS16Client is unchanged and built
with the existing `build-cs16.sh` script.

## Packages and state

`scripts/build-mods.sh [null|vr|bots-jev|x-ray-debug …]` builds the supplied packages
using only the public SDK, installs whole directories in `game/mods/`, and seeds
missing configuration in `game/mod-state/<id>/`. Existing calibrations are never
overwritten. VR migration prefers existing legacy cfg files and resolves their
`mods/vr_assets/` paths into the new package's assets. Its OpenXR loader stays next
to `client.so` with an `$ORIGIN` rpath.

Python sidecars have separate environments installed from each package's
`requirements.txt`. Credentials are external; see `docs/BOTBRAIN.md`.

## Launch and test

```bash
scripts/run-cs.sh                          # flat development profile
scripts/run-cs.sh --restore                # previous activation choices
scripts/run-cs.sh --profile vr             # VR development profile
scripts/run-cs.sh --port 27016             # another local server port
scripts/run-cs.sh --server-only            # dedicated server and sidecars
scripts/run-cs.sh -- -windowed -width 1280 -height 720
(cd game && ./run-cs.sh -nomods)           # unmodified flat path
python3 -m unittest discover -s tests -v
```

The launcher uses a session lock, unique container name, session heartbeat and a
GoldSrc information query for readiness. It refuses an occupied port and only
stops processes it created. Closing the managed client ends its owned server and
sidecars. `scripts/run-cs-vr.sh` and `scripts/run-botbrain.sh` remain wrappers.

Changes to package contents require a restart; toggling installed mods does not.
See `docs/MODS.md` for installation, controls and extension interfaces.
