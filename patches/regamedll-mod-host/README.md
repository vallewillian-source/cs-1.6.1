# ReGameDLL generic mod host

Base: `4a50c42e85fd3778c2b7d24731c7d30c83bbab01`.

`hooks.patch` adds generic lifecycle/frame/round/bot hooks and fixes first-spawn
equipment ordering. No Jev policy is compiled into cs.so. Native host and SDK
sources are canonical in `sdk/` and copied by `scripts/sync-mod-hosts.py`.

From the repository root run `python3 scripts/apply-patches.py`. It checks every
hunk, detects partially applied patches, then syncs the owned sources. Builds do
this automatically. `python3 scripts/verify-patches.py` verifies fresh bases.
