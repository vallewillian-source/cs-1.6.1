# Migrated to the generic server mod host

The Jev advisor now lives in `mods/bots-jev/`. Generic ReGameDLL hooks and the
spawn-equipment fix are published in `../regamedll-mod-host/`. The old apply.sh
entrypoint forwards to the full integration patch verifier.
