# x-ray-debug

A removable development mod using only the public SDK. `server.cpp` publishes
player positions and bounds at 20 Hz, including players outside the normal PVS.
`client.cpp` projects all eight bounding-box corners with the final view and draws
boxes and identification through walls.

No camera, input, pointer or bot-control claims are needed. Overlays run per eye
and support asymmetric frustums. Debug windows remain on the monitor.

The F6 manager controls activation and panel visibility independently; F8 toggles
the selected mod. Settings filter teams,
labels and distance and persist in `game/mod-state/x-ray-debug/settings.cfg`.
Local, dead/disconnected and behind-camera players are excluded. Data expires after
500 ms. The host rejects data on a different session, activation or connected endpoint.

Copy the built `game/mods/x-ray-debug/` directory to install; remove it to uninstall,
then restart. Neither operation requires engine registration or rebuilding the server.

## What implementing it revealed

- Client entities cannot reliably describe players hidden by PVS. The generic
  server SDK now exposes a complete copied match snapshot.
- i386 and x86_64 align doubles differently. Explicit padding and cross-architecture
  layout assertions make the match wire format stable.
- Overlay-only mods must not claim the camera. The SDK provides an additive per-view
  hook, separate from display ownership; HUD overlays cannot mask the VR pointer.
- Raw drawing can corrupt later mods. Plugins issue draw commands through the host,
  whose backend restores render state.

See `tests/mods/projection.cpp` and `docs/VALIDATION.md` for evidence.
