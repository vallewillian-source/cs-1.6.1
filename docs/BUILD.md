# Building CS 1.6.1 from source

This is the long-form guide: everything that has to exist on disk before `game/run-cs.sh`
and `server/run-server.sh` can work. All of it was done on the reference machine (Fedora
Linux, rootless Podman, **no sudo**) and the steps are written the way they were actually
run, not the way a distro package would do it.

If you only want to play, you don't need this file — but you *do* need the game content,
which cannot be shipped here (see below).

---

## 0. What you are building

Four upstream projects, compiled from source, plus two Mods of this project:

```
server/       32-bit dedicated server   ReHLDS + ReGameDLL_CS (patched: bot advisor)
game/         64-bit client             Xash3D-FWGS (patched: Mod ABI) + CS16Client
game/mods/    our Mods                  mod_vr.so (OpenXR VR), mod_null.so (template)
```

## 1. Requirements

| Need | Why |
|---|---|
| Linux (tested on Fedora) | Everything is built and run here |
| **Podman, rootless** | The 32-bit server and every build run inside a container, so no `sudo` and no multilib packages on the host |
| `git`, ~15 GB free disk | Four source trees, two builds, and the game content |
| A GPU with working OpenGL (GLX) | The client is `ref_gl` |
| *For VR only:* an OpenXR runtime and a headset | The Mod is inactive (game runs flat) without one |

The container image used throughout is `docker.io/steamcmd/steamcmd:latest`: it is Ubuntu
with the SteamCMD client, and it conveniently carries both an i386 runtime (for the 32-bit
HLDS binaries) and a toolchain.

## 2. Game content (Valve, not distributed here)

Maps, models, sprites and sounds are Valve's property and are **not** in this repository.
Download them from Valve's own CDN with SteamCMD, anonymous login:

```bash
podman run --rm -v "$PWD/server":/data:Z docker.io/steamcmd/steamcmd:latest \
  +force_install_dir /data +login anonymous \
  +app_set_config 90 mod cstrike +app_update 90 -beta steam_legacy validate +quit
```

The **`steam_legacy` beta is essential**, and it is the reason this project exists in this
shape: the pre-anniversary HLDS build (engine 8684) still ships a `libsteam_api.so` that
exports `SteamGameServer_Init`, which ReHLDS requires. The current app 90 build removed
that symbol.

The client needs the same content; copy or re-run into `game/` (the `cstrike` and `valve`
directories).

## 3. Source trees and patches

```bash
mkdir -p src && cd src
git clone https://github.com/dreamstalker/rehlds      ReHLDS
git clone https://github.com/rehlds/ReGameDLL_CS      ReGameDLL_CS
git clone https://github.com/FWGS/xash3d-fwgs         xash3d-fwgs
git clone https://github.com/Velaron/cs16-client      cs16-client
```

Pin the revisions this project is known to build against:

| Tree | Revision | Note |
|---|---|---|
| `ReHLDS` | `550f2d6` | used unmodified |
| `ReGameDLL_CS` | `4a50c42` | needs the advisor patch |
| `xash3d-fwgs` | `590869d` | needs the Mod ABI patch |
| `cs16-client` | `6a49bcc` | used unmodified |

Apply the two patches — this is what makes it CS 1.6.1 and not stock CS 1.6:

```bash
# engine: the external Mod ABI + per-eye render loop (see patches/xash3d-fwgs-mod-abi/)
git -C xash3d-fwgs checkout 590869d
git -C xash3d-fwgs apply ../patches/xash3d-fwgs-mod-abi/mod-abi.patch

# game DLL: the bot advisor hooks (also applied automatically by build-regamedll.sh)
./patches/regamedll-bot-advisor/apply.sh src/ReGameDLL_CS
```

`apply.sh` is idempotent, and the ReGameDLL build script calls it for you.

## 4. Dedicated server (32-bit)

```bash
./scripts/build-regamedll.sh            # ReGameDLL_CS -> server/cstrike/dlls/cs.so
```

That script builds inside Podman with `gcc-multilib`, applies the advisor hooks first, and
installs the result. ReHLDS itself is built the same way, from its own tree:

```bash
podman run --rm --security-opt label=disable -v "$PWD/src":/src --entrypoint bash \
  docker.io/steamcmd/steamcmd:latest -c '
    apt-get update -qq && apt-get install -y -qq build-essential cmake g++-multilib gcc-multilib git >/dev/null
    cd /src/ReHLDS && ./build.sh -j"$(nproc)" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5'
cp src/ReHLDS/build/rehlds/engine_i486.so        server/
cp src/ReHLDS/build/rehlds/dedicated/hlds_linux  server/
```

Two details that cost time if you don't know them:

- **`-DCMAKE_POLICY_VERSION_MINIMUM=3.5`** — CMake 4.x refuses projects that declare a
  minimum below 3.5, which both ReHLDS and ReGameDLL do.
- **32-bit is not optional.** The Steam client's game DLLs are 32-bit; the dedicated server
  must match them.

Run it with `server/run-server.sh` (it starts the container with `--network host`, so the
server is reachable on your LAN at `:27015`).

## 5. Client (64-bit)

```bash
./scripts/build-xash.sh     # Xash3D-FWGS  -> game/          (needs the ABI patch, step 3)
./scripts/build-cs16.sh     # CS16Client   -> game/cstrike/
```

Both run in Podman too. Notes that are not obvious:

- **Xash3D's `waf` needs `git` *inside the container*** to compute its build number. Without
  it the engine reports a build below 4190 and CS16Client refuses to load it.
- The build normalises the `bzip2` soname with `patchelf`: the container image links
  `libbz2.so.1.0` while Fedora only ships `libbz2.so.1`.
- CS16Client wants `libfontconfig-dev` / `libfreetype-dev`.

The 64-bit game DLL (`game/cstrike/dlls/cs_amd64.so`) and the YAPB bot library come out of
the CS16Client build, which vendors both — so the *client* side of the bot AI is stock. Only
the **server-side** ReGameDLL carries the advisor patch, which is where the Jev brain talks
to the bots. See [BOTBRAIN.md](BOTBRAIN.md).

## 6. OpenXR loader (VR only)

```bash
./scripts/build-openxr.sh            # default: OpenXR-SDK 1.1.63
```

Downloads and builds the Khronos OpenXR-SDK (loader + headers) into
`src/openxr-sdk/install/`. The VR Mod links against this loader and ships it next to itself
with an `$ORIGIN` rpath, so the Mod is self-contained on the host.

`BUILD_ALL_EXTENSIONS=OFF` is deliberate: the desktop path is X11/GLX, and requiring EGL
and OpenGLES would turn their absence into a fatal configure error.

## 7. The Mods

```bash
./mods/null/build.sh        # template Mod (log-only)  -> game/mods/mod_null.so
./mods/vr/build.sh          # the VR Mod               -> game/mods/mod_vr.so (+ vr_assets/)
```

Both need the engine header (`src/xash3d-fwgs/engine/common/mod_api.h`) and the VR Mod also
needs `src/openxr-sdk/install/`. `mods/vr/build.sh` copies its `.cfg` templates into
`game/mods/` **only if they don't exist yet**, so it never overwrites your calibration.

## 8. Run

```bash
cd game && ./run-cs.sh                            # menu
cd game && ./run-cs.sh +connect 127.0.0.1:27015    # join the local dedicated server
./scripts/run-cs-vr.sh                             # VR: bots + headset, one command
CS_BOTS=0 ./scripts/run-cs-vr.sh                   # VR without the bot server
```

`run-cs-vr.sh` is VR-specific and opinionated; what it does and why (vsync as a console
command, render resolution matched to what WiVRn actually transmits, `SDL_VIDEODRIVER=x11`
for the GLX context) is documented in the [main README](../README.md) and in
[HANDS.md](HANDS.md).

## 9. Fast rebuild loop

Every build script is incremental and safe to re-run:

| Script | Rebuilds |
|---|---|
| `scripts/build-regamedll.sh` | server game DLL |
| `scripts/build-xash.sh` | engine |
| `scripts/build-cs16.sh` | client |
| `scripts/build-openxr.sh` | OpenXR loader |
| `mods/vr/build.sh`, `mods/null/build.sh` | Mods |

Editing a Mod's `.cfg` under `game/mods/` needs **no rebuild at all**: `vr_hands.cfg` is
re-read while the game runs.
