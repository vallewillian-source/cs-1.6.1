# Upstream integrations

- `xash3d-fwgs-mod-abi/`: client ABI v9, generic multi-mod loader, rendering/input
  extension points and SDL2/OpenGL debug-tool integration.
- `regamedll-mod-host/`: generic server ABI v1, lifecycle/round/bot hooks and the
  first-spawn equipment fix. Advisor policy is in `mods/bots-jev/`.
- `regamedll-bot-advisor/`: compatibility apply entrypoint only.

Run `python3 scripts/apply-patches.py` from the repository root. It validates the
complete integration and copies canonical sources from `sdk/` and pinned
`third_party/` dependencies. `python3 scripts/verify-patches.py` checks fresh
upstream revisions and idempotence without changing working checkouts.

ReHLDS and CS16Client remain unmodified. Source revisions and builds are documented
in `docs/BUILD.md`.
