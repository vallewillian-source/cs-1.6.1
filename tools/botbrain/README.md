# botbrain — the bots' "slow brain" (Jev / TypeSafe)

A Python sidecar that gives the CS 1.6.1 bots (ReGameDLL) on the local
dedicated server **one team decision at a time**. The DLL (`cs_bot_advisor`,
inside `cs.so`) writes the round's facts to a file, botbrain asks the **Jev**
model (TypeSafe) which intent to use, and writes the answer back. The DLL
applies it.

```
server (podman, 32-bit)                 host (Python)
┌─────────────────────────────┐          ┌──────────────────────────┐
│ cs.so  CCSBot (the "muscle") │ observe  │ botbrain.py              │
│ cs_bot_advisor ────────────►│─────────►│1. read observe_<t|ct>.txt│
│   · writes observe_*.txt    │          │  2. signature → cache?   │
│   · reads plan_*.txt ◄──────│◄─────────│  3. Jev /v1/systemone    │
│   · applies to bots         │  plan    │  4. writes plan_*.txt    │
└─────────────────────────────┘          │  traces/decisions.jsonl  │
        cstrike/advisor/                  └──────────────────────────┘
```

## Why this way

- **Bots live on the server** → VR and flat play against the same bots, with
  no extra work. Nothing here is client-side.
- **1 request per decision** — the same contract as Jev: a compact indexed
  state goes in, one choice from a closed set comes out. It's cheap and
  cacheable.
- **Never blocks the game**: the transport is a file. If this process dies,
  the bots keep playing (the sidecar doesn't write a plan; the DLL keeps what
  it had, and at round start it falls back to stock behavior).

## Running

```bash
# everything together (sidecar + server):
scripts/run-botbrain.sh

# only the sidecar, against a server that is already up:
tools/botbrain/run.sh
tools/botbrain/run.sh --once          # processes one tick and exits
tools/botbrain/run.sh --no-model      # doesn't call the model (mailbox/cache test)
```

Credentials: `~/Workspace/jev-ultrafast/.env` (`TYPESAFE_API_KEY`,
`TYPESAFE_MODEL`) and that project's venv (which already has `httpx[http2]`).
Nothing is copied or modified in the jev clone.

## File protocol

`cstrike/advisor/observe_<t|ct>.txt` (the DLL writes, `key=value`):

```
v=1
ts=412.50
team=T
round=7
phase=freeze          # freeze | live | postplant
score_us=3
score_them=4
alive_us=5
alive_them=4
players_us=5
players_them=5
money_avg_us=4100
money_avg_them=900
bomb=none             # none | carried | planted
bomb_zone=none        # A | B | none
history=RUSH_B,STACK_A
```

`cstrike/advisor/plan_<t|ct>.txt` (the sidecar writes):

```
v=1
seq=2712345           # monotonic: the DLL only applies a new seq
round=7              # must be the current round, otherwise it is discarded
intent=RUSH_B
source=jev           # jev | cache
```

## Intents (V1, de_dust2, defuse)

T: `RUSH_B`, `RUSH_A_LONG`, `RUSH_A_SHORT`, `SPLIT_A`, `HOLD_AFTER_PLANT`,
`ECO_SAVE`, `DEFAULT`. CT: `STACK_A`, `STACK_B`, `RETAKE_A`, `RETAKE_B`,
`ECO_SAVE`, `DEFAULT`. The closed list and the "coach" rules live in
`intents.py`; the names must also exist in
`src/ReGameDLL_CS/regamedll/dlls/bot/advisor/cs_bot_advisor.h`.

`DEFAULT` = **don't overwrite** anything (the CCSBot plays on its own). The
intent→behavior map uses the zone centers that the map itself provides
(`TheCSBots()->GetZone(i)->m_center`), so there are no hardcoded dust2
coordinates in the code.

## Cost — how much the match spent

The trace records the `usage` that TypeSafe returns on each call, and the
daemon closes each session with a summary. To see a session's spend:

```bash
tools/botbrain/run.sh --report                     # everything in the trace
tools/botbrain/run.sh --report 2026-10-13T15:52    # only since that instant
```

Typical output:

```
decisões:        159  (decisions)
chamadas ao Jev: 14  (cache: 145)  (Jev calls)
latência:        média 254 ms · máx 455 ms  (latency: avg · max)
tokens:          11193 entrada · 1101 saída  (input · output)
custo (entrada): US$ 0.000470  (cost, input)
intents:         ECO_SAVE×111, HOLD_AFTER_PLANT×25, RETAKE_B×23
```

On shutdown (Ctrl+C **or** SIGTERM from `run-botbrain.sh`), the summary also
goes to the end of the log:

```
sessão: decisões=159 chamadas_ao_modelo=14 cache=145 erros=0 | tokens_entrada=11193 tokens_saída=1101
```

Order of magnitude, measured: **~800 input tokens and ~80 output tokens per
call**. TypeSafe publishes **US$ 42 per billion input tokens** (≈ US$ 0.042 per
million), which puts a match of a few minutes in the ballpark of **less than one
cent of a dollar**. The **output** price **is not published** on the site, so
the report's `custo (entrada)` is a floor — the official number is the TypeSafe
console.

What keeps the spend low is the design: one decision per team per moment
(round start, phase change, bomb planted, clutch), with cache by signature — in
one measured session, **14 calls covered 159 decisions**.

### What keeps it cheap

The situation signature is **deliberately coarse** (phase, bomb/zone, clutch,
economy bracket, score, last plan), so similar rounds fall into the cache and
cost zero. In addition there is a `--min-call-interval` (default 2 s per team)
so it doesn't fire bursts.
