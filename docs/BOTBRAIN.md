# BOTBRAIN.md — bots with a "slow brain" (Jev / TypeSafe)

> **Status: V1 (2026-10-13), de_dust2 only, defuse scenario.** Runs on the local
> dedicated server (`server/`), applies equally to VR and flat (bots live on the
> server). Outside the scope of the VR MVP — it is a separate track that uses the
> same server infrastructure.

## 1. The idea in one sentence

ReGameDLL's **CCSBot** remains the muscle (nav, vision, aim, weapons, combat); a
**Python sidecar** asks the **Jev** model (TypeSafe) what the *team's* plan is for
that round and writes the answer to a file; the DLL applies it to the bots. Jev
is never on the critical path — the game does not wait for it.

Why this way:

- **Bots live on the server** → VR and flat players face exactly the same bots;
  nothing changes on the client.
- **1 request per decision** is Jev's contract (compact state in, one choice from
  a closed set out). It is cheap, cacheable and does not require real-time
  latency: the decision lands on freeze or on an event, not on every tick.
- **File transport** → zero network inside `cs.so` (32-bit), zero new threads,
  crash-proof: dead sidecar = bots keep going on the algorithm.

```
server (podman, 32-bit)                      host (Python)
┌──────────────────────────────────┐        ┌───────────────────────────┐
│ cs.so (ReGameDLL)                │observe │ tools/botbrain            │
│  CCSBot  — the "muscle"          │───────►│  botbrain.py              │
│  cs_bot_advisor:                 │        │   · signature → cache     │
│   · writes observe_<t|ct>.txt    │◄───────│   · Jev /v1/systemone     │
│   · reads plan_<t|ct>.txt        │ plan   │   · writes plan_*.txt     │
│   · applies the intent to bots   │        │  traces/decisions.jsonl   │
└──────────────────────────────────┘        └───────────────────────────┘
              cstrike/advisor/
```

## 2. Files

| File | Role |
|---|---|
| `src/ReGameDLL_CS/regamedll/dlls/bot/advisor/cs_bot_advisor.{h,cpp}` | the module in the DLL: cvars, mailbox, round/event detection, intent→behavior |
| `patches/regamedll-bot-advisor/` | **canonical copy** of the module + `hooks.patch` (the 12 hook lines) — because `/src/` is ignored by the repo; `apply.sh` re-applies it (idempotent) and `build-regamedll.sh` calls it |
| `tools/botbrain/botbrain.py` | the daemon (mailbox → cache → Jev → plan) |
| `tools/botbrain/typesafe.py` | minimal Jev client (same protocol/credentials as the `jev-ultrafast` clone) |
| `tools/botbrain/intents.py` | closed set of intents + the "coach" rules |
| `tools/botbrain/run.sh` | brings up the daemon with jev-ultrafast's venv/`.env` |
| `scripts/run-botbrain.sh` | sidecar + server together |
| `scripts/build-regamedll.sh` | 32-bit build of `cs.so` in the container (host without multilib) |
| `server/cstrike/advisor.cfg` | bots (`bot_quota 8`…) + advisor cvars |
| `server/cstrike/advisor/` | runtime: `observe_*.txt`, `plan_*.txt`, `plan_cache.json` |

Hooks in the existing code (minimal edits):

- `CCSBotManager::RestartRound()` → `Advisor_OnRoundStart()`
- `CCSBotManager::StartFrame()` → `Advisor_FrameUpdate()`
- `CCSBot::Update()` (before the state machine dispatch) → `Advisor_ApplyIntent(bot)`
- `Bot_RegisterCVars()` → `Advisor_RegisterCVars()`

## 3. Running

```bash
scripts/build-regamedll.sh      # compiles and installs cs.so into the server
scripts/run-botbrain.sh         # sidecar + server (dust2)
```

or separately: `tools/botbrain/run.sh` against a server already up.

**In VR none of this is needed:** `scripts/run-cs-vr.sh` brings up the bot server
by itself, waits for the port to open, joins the match in VR and tears everything
down on exit (`CS_BOTS=0` turns it off; `CS_BOT_PORT` changes the port; if a
server is already on that port it just connects, and a `+connect` of yours makes
it not even start a local server).

Credentials: `~/Workspace/jev-ultrafast/.env` (`TYPESAFE_API_KEY`,
`TYPESAFE_MODEL`). Nothing is copied or modified in the jev clone.

## 4. Protocol (`key=value` files)

`cstrike/advisor/observe_<t|ct>.txt` (the DLL writes it, atomic via tmp+rename):

```
v=1  ts=412.50  team=T  round=7  phase=freeze   # freeze | live | postplant
score_us=3  score_them=4  alive_us=5  alive_them=4
players_us=5  players_them=5  money_avg_us=4100  money_avg_them=900
bomb=none        # none | carried | planted
bomb_zone=none   # A | B | none
history=RUSH_B,STACK_A
```

`cstrike/advisor/plan_<t|ct>.txt` (the sidecar writes it):

```
v=1  seq=91915040  round=7  intent=RUSH_B  source=jev   # jev | cache
```

The DLL only applies it if `round` is the current round **and** `seq` is greater
than the last consumed one — an old plan is never re-applied.

## 5. Closed set of intents (V1, de_dust2/defuse)

| Team | Intents |
|---|---|
| T | `RUSH_B`, `RUSH_A_LONG`, `RUSH_A_SHORT`, `SPLIT_A`, `HOLD_AFTER_PLANT`, `ECO_SAVE`, `DEFAULT` |
| CT | `STACK_A`, `STACK_B`, `RETAKE_A`, `RETAKE_B`, `ECO_SAVE`, `DEFAULT` |

Mapping (in `cs_bot_advisor.cpp`), using only what the map provides:

- `RUSH_*` / `RETAKE_*` → `MoveTo(random position in the site zone)` +
  `SetTask(SEEK_AND_DESTROY)` + `SetDisposition(ENGAGE_AND_INVESTIGATE)`.
- `STACK_*` / `HOLD_AFTER_PLANT` → `Hide(position in the zone)` +
  `SetTask(GUARD_BOMB_ZONE)` + `SetDisposition(OPPORTUNITY_FIRE)`.
- `ECO_SAVE` → `SetDisposition(SELF_DEFENSE)`.
- `DEFAULT` → **overrides nothing** (CCSBot plays on its own).

There is no dust2 coordinate hard-coded: the targets come from
`TheCSBots()->GetZone(i)->m_center` and `GetRandomPositionInZone()`. Which zone
index is site A or B comes from the **`func_bomb_target`'s `target`**
(`tgt_a`/`tgt_b`) — dust2 names by `target`, not `targetname`.

The intent is applied **once per change** (the DLL remembers what it already
applied per bot); after that CCSBot takes back control, including combat.

## 6. The coach side (economy and trigger)

- **`buy_status`** is computed in Python, not asked of the model: `<1200` = `eco`,
  `<4000` = `force`, otherwise `full`. CS 1.6 economy knowledge stays in the
  code, not in the prompt.
- **The signature is the trigger**: it only changes on round start, phase
  change, bomb planted, team reduced to clutch (≤2 alive), score flip, economy
  band and last plan. In other words, decisions happen at **significant
  moments**, not on every death.
- **Cache** by signature: repeated situations cost zero. Measured in game:
  ~400 decisions → ~15–25 model calls.
- **`--min-call-interval`** (2 s per team) avoids bursts.
- **No fallback on error**: if the model fails, no plan is written and the DLL
  keeps what it had (at round start, the stock behavior).

### What it costs (measured)

The trace records the `usage` that TypeSafe returns, the daemon closes each
session with a summary, and `tools/botbrain/run.sh --report [SINCE]` prints the
spend:

```
chamadas ao Jev: 14  (cache: 145)
tokens:          11193 entrada · 1101 saída
custo (entrada): US$ 0.000470
```

(Jev calls: 14 (cache: 145) · tokens: 11193 input · 1101 output · cost (input): US$ 0.000470)

Measured: **~800 input tokens and ~80 output tokens per call**. TypeSafe publishes
**US$ 42 per billion input tokens** (≈ US$ 0.042 per million) — that is, a match
of a few minutes costs **less than one cent of a dollar**. The **output price is
not published**, so the report gives an input floor; the official number is in
the TypeSafe console.

## 7. Server prerequisites (learned in practice)

1. **`bot_enable 1` in `server/cstrike/game_init.cfg`.** On a dedicated server
   the bots are gated by `bot_enable`, and ReGameDLL runs `game_init.cfg`
   **before** `Bot_RegisterCVars()` — it is the only window in which the cvar can
   be set. Without it, no `bot_*` command exists (the `game_init.cfg` was empty).
2. **`bot_join_after_player 0`.** With the default (`1`), with no human connected
   the desired quota is 0 and the server **kicks** the bots ("kicked to maintain
   quota"). Required for headless matches.
3. **`BotProfile.db` + `BotChatter.db` in `server/cstrike/`.**
   (`src/cs16-client/3rdparty/cs16client-extras/`).
4. **`server/cstrike/maps/de_dust2.nav`.** It does not exist ready-made in the
   repo: the nav-mesh is generated by the bot itself. With the map loaded and a
   bot alive, run `bot_nav_analyze` and after ~90 s `bot_nav_save` (the file is
   ~420 KB and from then on loads by itself).
5. **`exec advisor.cfg` in `server.cfg`.** `+exec` on the command line does not
   work: it runs before the game DLL registers the `bot_*` cvars.

## 8. Debugging

Module cvars: `advisor_enable` (0 = stock byte-for-byte), `advisor_debug`
(1 = plan/round/application; 2 = also each bot's goal),
`advisor_force_intent <NAME>` (drives the DLL half without the sidecar),
`advisor_poll_interval`, `advisor_heartbeat`.

```bash
grep ADVISOR server/...            # or the server's stdout
cat tools/botbrain/traces/decisions.jsonl | tail   # each decision + context
tools/botbrain/run.sh --report                     # how much the session spent
tools/botbrain/run.sh --once --no-model --dir server/cstrike/advisor   # tests the mailbox without spending
```

The module also logs, at the start of each round with debug on, the zone centers
and the site (`ADVISOR: zone 0 (B) target='tgt_b' …`).

## 9. What V1 proved

- Bots actually play dust2 (nav generated, 4v4, they plant and defuse).
- `RUSH_B` → bomb planted on **site B**; `RUSH_A_LONG` → **site A**
  (count by sampling `bomb_zone`).
- Full loop in game: observe → Jev → plan → applied to the bots
  (`ADVISOR: team CT <- RETAKE_B`, `applied RETAKE_B` ×…).
- Decision sensitivity (forged observations): full/even → `RUSH_B`;
  eco/losing → `ECO_SAVE`; postplant on B → `HOLD_AFTER_PLANT`; postplant on A
  (CT) → `RETAKE_A`; **enemy repeating B → `STACK_A`**.
- Cost: ~25 model calls in ~2 min of match (the rest comes from cache).

## 10. V1 limits (deliberate)

- `de_dust2` only; defuse scenario only.
- One decision per **team** per moment — there is no per-bot Jev.
- No learned policy/distillation, no generated chatter, no buy
  micro-management, no socket transport, no UI.
- The variety of intents depends on the match economy: with poor teams, Jev
  tends to `ECO_SAVE` — which is correct, but it makes the poor side attack less.
