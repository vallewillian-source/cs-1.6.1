# Patches — the upstream changes this project needs

CS 1.6.1 is built **from source on top of four upstream projects**, and it modifies two of
them. Those modifications are not vendored into this repository (`src/` is in
`.gitignore` — it holds re-clonable checkouts), so they live here as patches that the build
re-applies to a clean checkout.

| Patch set | Upstream | What it adds |
|---|---|---|
| [`xash3d-fwgs-mod-abi/`](xash3d-fwgs-mod-abi/) | [FWGS/xash3d-fwgs](https://github.com/FWGS/xash3d-fwgs) | The external-**Mod** system: the `mod_api.h` contract, the loader, the per-eye render loop, the asymmetric frustum, the viewmodel and input hooks, the 2D menu canvas — everything the VR Mod needs from the engine |
| [`regamedll-bot-advisor/`](regamedll-bot-advisor/) | [rehlds/ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS) | The bot **advisor module** (`cs_bot_advisor`): the observe/plan/apply hooks that let the Jev brain drive bots without a fork of the bot AI |

The other two upstreams are used unmodified:

- **ReHLDS** — the 32-bit dedicated server engine. Built as-is.
- **CS16Client** — the game client. Built as-is: every client-side VR feature (barrel aim,
  snap turn, roomscale crouch, controller input) is implemented through the engine's Mod
  ABI, so the client DLL did not have to change.

Each patch directory has its own README with the exact base revision and apply
instructions. Applying them is part of [docs/BUILD.md](../docs/BUILD.md).
