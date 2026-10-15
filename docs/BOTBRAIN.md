# Bots with Jev — independent package

`mods/bots-jev/` owns server policy, client diagnostics and the Python sidecar.
ReGameDLL contains a generic host and small bot operations, without Jev policy.
The model chooses a closed team intent; navigation, aim and combat remain stock.
The current vocabulary targets **de_dust2**.

## Run and configure

```bash
scripts/build-mods.sh bots-jev
scripts/setup-mods.sh
scripts/run-cs.sh                 # flat, eight bots, Jev and x-ray
scripts/run-botbrain.sh           # server + sidecars only
tools/botbrain/run.sh --report   # trace decisions and token usage
```

The private Python environment lives in `game/mod-state/bots-jev/.venv/`. Supply
`TYPESAFE_API_KEY` and optionally `TYPESAFE_MODEL` in the environment or through
`TYPESAFE_ENV_FILE`. Without an explicit file, the sidecar reads
`game/mod-state/bots-jev/secrets.env`. For compatibility, it falls back to
`$JEV_DIR/.env` or `~/Workspace/jev-ultrafast/.env` if no key is available. That
other project's code and virtual environment are not required or modified.

F6 opens the manager; F8 toggles the selected mod independently of panel visibility. Disabling stops the
sidecar and resets advisor-owned tasks/dispositions. Bots continue with stock AI.
The panel shows confirmed components, intentions, Jev/cache activity, failures,
latency, telemetry age and tokens. Transport/configuration failures and invalid
responses have separate counters and structured severities. After fixing credentials or dependencies, toggle off/on to retry.

## Protocol and failure behavior

The 88-byte host envelope carries magic, payload size, activation generation,
monotonic publication time and session. Payloads are UTF-8 JSON for `observe-t`,
`observe-ct`, `plan-t`, `plan-ct`, `advisor-status` and `brain-status`. See
`sidecar/mailbox.py` and `server/advisor.cpp` for the schemas.

Observations/plans carry v2, session, map, round, observation revision, team and
observation time. Plans add sequence, closed intent and source. The server rejects
wrong session/map/round/revision/team, replayed sequences, unknown intents, future
times and observations older than **ten seconds**. The sidecar rechecks context
after model calls. Changing activation generation invalidates previous data.

Observations refresh on significant state changes and every 500 ms. The sidecar
waits for players on both teams. Calls are limited to one per team every two
seconds, including failures; rate-limited observations stay pending. Cached intents
are checked against the current vocabulary. Keys include protocol, model, map and
coarse situation. Cache and trace storage are bounded.

Plans expire even if the sidecar disappears. Expiration, disable and round changes
clear interventions. Errors produce no replacement plan. Short HTTP timeouts and
bounded retries happen entirely outside the game process.

## Evidence

Current validation is in `docs/VALIDATION.md`. The sidecar records API-reported
input/output token usage in `game/mod-state/bots-jev/decisions.jsonl`; it does not
infer undocumented prices. Previous experiments remain in
[the archived notes](archive/BOTBRAIN-v8.md).
