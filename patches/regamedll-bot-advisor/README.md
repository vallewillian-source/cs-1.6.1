# patches/regamedll-bot-advisor

The `cs_bot_advisor` module (the bots' "slow brain" — see
[`docs/BOTBRAIN.md`](../../docs/BOTBRAIN.md)) lives inside the ReGameDLL
tree. Since `/src/` is ignored by the repository (it is a re-clonable
checkout), the module and its hooks live here and are reapplied before the
build.

| File | What it is |
|---|---|
| `cs_bot_advisor.{h,cpp}` | the module (canonical copy) |
| `hooks.patch` | the 12 hook lines in 4 existing files (CMakeLists + `cs_bot_init/manager/update.cpp`) |
| `apply.sh` | copies the module and applies the hooks; **idempotent** |

```bash
patches/regamedll-bot-advisor/apply.sh          # applies (or confirms already applied)
patches/regamedll-bot-advisor/apply.sh /caminho/src/ReGameDLL_CS
```

`scripts/build-regamedll.sh` calls `apply.sh` automatically, so a fresh clone
of `/src/` compiles again without manual steps.
