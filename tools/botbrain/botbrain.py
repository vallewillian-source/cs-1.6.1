#!/usr/bin/env python3
"""botbrain -- the slow brain (Jev / TypeSafe sidecar).

Watches the mailbox the ReGameDLL advisor module writes
(``cstrike/advisor/observe_<t|ct>.txt``), asks the TypeSafe "Jev" model to pick
one intent from a closed set, and writes the answer back
(``cstrike/advisor/plan_<t|ct>.txt``) for the DLL to apply.

One request == one decision. Decisions are cached by a coarse situation
signature, so repeated rounds cost nothing. There is deliberately no fallback
intent on error: if the model fails we do not write a plan, and the DLL keeps
whatever it had (the stock AI at round start). The game never waits on this
process.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import intents  # noqa: E402
import typesafe  # noqa: E402

JEV_DIR = Path(os.environ.get("JEV_DIR", Path.home() / "Workspace" / "jev-ultrafast"))
EPOCH_BASE = 1_700_000_000  # keeps seq inside a 32-bit int for years to come


def parse_obs(path: Path) -> dict:
    out = {}
    for raw in path.read_text().splitlines():
        if "=" in raw:
            key, value = raw.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def _int(obs: dict, key: str, default: int = 0) -> int:
    try:
        return int(obs.get(key, default))
    except (TypeError, ValueError):
        return default


def buy_status(avg_money: int) -> str:
    """CS 1.6 economy in one word, computed here so the model doesn't guess.

    A rifle + armor + grenades is roughly 4000; a force buy (armor + SMG, or
    deagle + armor) is around 1200-2000. Below ~1200 the team really cannot
    contest, which is when saving is right.
    """
    if avg_money < 1200:
        return "eco"
    if avg_money < 4000:
        return "force"
    return "full"


def clutch(obs: dict) -> str:
    us = _int(obs, "alive_us")
    them = _int(obs, "alive_them")
    return "clutch" if min(us, them) <= 2 else "even"


def score_bucket(obs: dict) -> int:
    diff = _int(obs, "score_us") - _int(obs, "score_them")
    return (diff > 0) - (diff < 0)


def last_plans(obs: dict, n: int = 1) -> str:
    """Only the most recent plan: the full history changes every decision and
    would make the cache key (and thus the cache) useless."""
    hist = [h for h in obs.get("history", "").split(",") if h]
    return ",".join(hist[-n:])


def signature(team: str, obs: dict) -> str:
    """The decision key. Deliberately coarse: it changes at round start, at
    phase changes, when the bomb is planted, when a team is down to a clutch,
    and when the economy band or the score flips - not on every kill."""
    return "|".join([
        team,
        obs.get("phase", "?"),
        f"b{obs.get('bomb', '?')}",
        f"z{obs.get('bomb_zone', '?')}",
        clutch(obs),
        buy_status(_int(obs, "money_avg_us")),
        f"s{score_bucket(obs)}",
        f"h{last_plans(obs)}",
    ])


def state_text(team: str, obs: dict) -> str:
    money = _int(obs, "money_avg_us")
    return "\n".join([
        "map=de_dust2",
        f"we_are={team}",
        f"round={obs.get('round')}",
        f"phase={obs.get('phase')}",
        f"score_us={obs.get('score_us')} score_them={obs.get('score_them')}",
        f"alive_us={obs.get('alive_us')} alive_them={obs.get('alive_them')}",
        f"money_avg_us={money} money_avg_them={obs.get('money_avg_them')}",
        f"buy_status={buy_status(money)}",
        f"bomb={obs.get('bomb')} bomb_zone={obs.get('bomb_zone')}",
        f"recent_plans={obs.get('history', '')}",
    ])


class Cache:
    def __init__(self, path: Path):
        self.path = path
        self.data: dict = {}
        self._dirty = False
        if path.is_file():
            try:
                self.data = json.loads(path.read_text())
            except (ValueError, OSError):
                self.data = {}

    def get(self, sig: str):
        return self.data.get(sig)

    def put(self, sig: str, intent: str, source: str) -> None:
        self.data[sig] = {"intent": intent, "source": source, "ts": int(time.time())}
        self._dirty = True

    def save(self) -> None:
        if not self._dirty:
            return
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.data, indent=1, sort_keys=True))
        tmp.replace(self.path)
        self._dirty = False


class Brain:
    def __init__(self, args: argparse.Namespace):
        self.dir = Path(args.dir)
        self.interval = args.interval
        self.min_call = args.min_call_interval
        self.once = args.once
        self.no_model = args.no_model
        self.trace_dir = Path(args.trace_dir)
        self.trace_dir.mkdir(parents=True, exist_ok=True)
        self.cache = Cache(self.dir / "plan_cache.json")
        self.last_mtime = {"T": None, "CT": None}
        self.last_seq = {"T": 0, "CT": 0}
        self.last_call = {"T": 0.0, "CT": 0.0}
        self.stats = {"decisions": 0, "model_calls": 0, "cache_hits": 0, "errors": 0}
        self.tokens_in = 0
        self.tokens_out = 0

    def log(self, msg: str) -> None:
        print(f"[botbrain {time.strftime('%H:%M:%S')}] {msg}", flush=True)

    def run(self) -> None:
        self.log(f"watching {self.dir} (interval={self.interval}s min_call={self.min_call}s)")
        while True:
            self.tick()
            if self.once:
                break
            time.sleep(self.interval)
        self.finish()

    def finish(self) -> None:
        self.cache.save()
        self.log(f"sessão: {self.summary()}")

    def tick(self) -> None:
        for team, name in (("T", "observe_t.txt"), ("CT", "observe_ct.txt")):
            path = self.dir / name
            try:
                mtime = path.stat().st_mtime
            except OSError:
                continue
            if mtime == self.last_mtime[team]:
                continue
            self.last_mtime[team] = mtime
            try:
                obs = parse_obs(path)
            except OSError as exc:
                self.log(f"{team}: cannot read observation: {exc}")
                continue
            try:
                self.decide(team, obs)
            except Exception as exc:  # never let one bad decision kill the daemon
                self.stats["errors"] += 1
                self.log(f"{team}: decision failed: {exc}")

    def decide(self, team: str, obs: dict) -> None:
        rnd = _int(obs, "round", -1)
        if rnd < 0:
            return

        sig = signature(team, obs)

        hit = self.cache.get(sig)
        if hit:
            self.stats["cache_hits"] += 1
            self.write_plan(team, rnd, hit["intent"], "cache")
            self.trace(team, obs, sig, hit["intent"], "cache", None)
            return

        now = time.monotonic()
        if now - self.last_call[team] < self.min_call:
            return  # rate-limited; the next event will retry

        if self.no_model:
            self.log(f"{team}: cache miss but --no-model is set, no plan written")
            return

        self.log(f"{team}: asking Jev (round {rnd}, sig {sig})")
        try:
            result = typesafe.choose_operation(
                state_text=state_text(team, obs),
                criteria=intents.criteria_for(team),
                goal=intents.goal_for(team),
                rules=intents.rules_for(team),
            )
        except Exception as exc:
            self.stats["errors"] += 1
            self.log(f"{team}: model call failed, no plan written: {exc}")
            return

        self.last_call[team] = time.monotonic()
        self.stats["model_calls"] += 1
        self.session_tokens(result.get("usage"))

        intent = result["choice"]
        self.cache.put(sig, intent, "jev")
        self.cache.save()
        self.write_plan(team, rnd, intent, "jev")
        self.trace(team, obs, sig, intent, "jev", result)
        self.log(
            f"{team}: Jev -> {intent} "
            f"(conf={result.get('confidence')}, {result.get('latency_ms')}ms)"
        )

    def write_plan(self, team: str, rnd: int, intent: str, source: str) -> None:
        self.stats["decisions"] += 1
        seq = max(int(time.time()) - EPOCH_BASE, self.last_seq[team] + 1)
        self.last_seq[team] = seq

        suffix = "t" if team == "T" else "ct"
        path = self.dir / f"plan_{suffix}.txt"
        tmp = self.dir / f"plan_{suffix}.txt.tmp"
        tmp.write_text(f"v=1\nseq={seq}\nround={rnd}\nintent={intent}\nsource={source}\n")
        tmp.replace(path)

    def trace(self, team: str, obs: dict, sig: str, intent: str, source: str, result) -> None:
        line = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "team": team,
            "round": _int(obs, "round", -1),
            "phase": obs.get("phase"),
            "alive": f"{obs.get('alive_us')}v{obs.get('alive_them')}",
            "buy_status": buy_status(_int(obs, "money_avg_us")),
            "bomb": obs.get("bomb"),
            "bomb_zone": obs.get("bomb_zone"),
            "sig": sig,
            "intent": intent,
            "source": source,
            "confidence": (result or {}).get("confidence"),
            "latency_ms": (result or {}).get("latency_ms"),
            "usage": (result or {}).get("usage"),
        }
        with (self.trace_dir / "decisions.jsonl").open("a") as fh:
            fh.write(json.dumps(line, sort_keys=True) + "\n")

    def session_tokens(self, usage) -> None:
        """Accumulate the token cost reported by TypeSafe for this run."""
        if not isinstance(usage, dict):
            return
        self.tokens_in += int(usage.get("input_tokens") or 0)
        self.tokens_out += int(usage.get("output_tokens") or 0)

    def summary(self) -> str:
        s = self.stats
        return (
            f"decisões={s['decisions']} chamadas_ao_modelo={s['model_calls']} "
            f"cache={s['cache_hits']} erros={s['errors']} | "
            f"tokens_entrada={self.tokens_in} tokens_saída={self.tokens_out}"
        )


def default_dir() -> Path:
    repo_root = Path(__file__).resolve().parents[2]
    return repo_root / "server" / "cstrike" / "advisor"


# TypeSafe publishes Jev at $42 per billion input tokens (typesafe.ai). The
# output price is not published, so a report is an input-only floor.
USD_PER_INPUT_TOKEN = 42 / 1e9


def report(trace_path: Path, since: str | None = None) -> int:
    """Prints what a session actually cost, from the recorded usage."""
    if not trace_path.is_file():
        print(f"sem trace em {trace_path}")
        return 1

    rows = []
    for line in trace_path.read_text().splitlines():
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if since and row.get("ts", "") < since:
            continue
        rows.append(row)

    if not rows:
        print("nenhuma decisão no período")
        return 0

    jev = [r for r in rows if r.get("source") == "jev"]
    tin = sum(int((r.get("usage") or {}).get("input_tokens") or 0) for r in jev)
    tout = sum(int((r.get("usage") or {}).get("output_tokens") or 0) for r in jev)
    lat = [r["latency_ms"] for r in jev if r.get("latency_ms")]

    print(f"decisões:        {len(rows)}")
    print(f"chamadas ao Jev: {len(jev)}  (cache: {len(rows) - len(jev)})")
    print(f"período:         {rows[0].get('ts')} → {rows[-1].get('ts')}")
    if lat:
        print(f"latência:        média {sum(lat) / len(lat):.0f} ms · máx {max(lat)} ms")
    print(f"tokens:          {tin} entrada · {tout} saída")
    print(f"custo (entrada): US$ {tin * USD_PER_INPUT_TOKEN:.6f}")

    intents: dict = {}
    rounds = set()
    for row in rows:
        intents[row.get("intent")] = intents.get(row.get("intent"), 0) + 1
        if row.get("round") is not None:
            rounds.add(row["round"])
    print(f"rodadas:         {len(rounds)}")
    print("intents:         " + ", ".join(
        f"{k}×{v}" for k, v in sorted(intents.items(), key=lambda kv: -kv[1])))

    if jev and tin == 0:
        print("\n(dica: chamadas sem 'usage' são de antes da instrumentação;"
              " o trace só passou a gravar tokens depois.)")
    return 0


def _exit_via_signal(signum, frame):
    raise KeyboardInterrupt


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="CS 1.6 bots slow brain (Jev/TypeSafe sidecar)")
    parser.add_argument("--dir", default=os.environ.get("BOTBRAIN_DIR", str(default_dir())),
                        help="mailbox directory (default: server/cstrike/advisor)")
    parser.add_argument("--interval", type=float, default=0.25, help="poll interval in seconds")
    parser.add_argument("--min-call-interval", type=float, default=2.0,
                        help="minimum seconds between model calls per team")
    parser.add_argument("--trace-dir", default=str(Path(__file__).resolve().parent / "traces"))
    parser.add_argument("--once", action="store_true", help="process one tick and exit")
    parser.add_argument("--no-model", action="store_true",
                        help="never call the model (mailbox/cache testing)")
    parser.add_argument("--report", nargs="?", const="", metavar="DESDE",
                        help="só imprime o gasto/atividade do trace e sai "
                             "(opcional: desde 'AAAA-MM-DDTHH:MM')")
    args = parser.parse_args(argv)

    if args.report is not None:
        return report(Path(args.trace_dir) / "decisions.jsonl", args.report or None)

    typesafe.load_env(JEV_DIR / ".env")
    if not os.environ.get("TYPESAFE_API_KEY"):
        print(f"warning: TYPESAFE_API_KEY not set (looked in {JEV_DIR}/.env)", file=sys.stderr)

    # Ctrl+C and the launcher's SIGTERM both land on the same summary
    signal.signal(signal.SIGTERM, _exit_via_signal)
    signal.signal(signal.SIGINT, _exit_via_signal)

    brain = Brain(args)
    try:
        brain.run()
    except KeyboardInterrupt:
        brain.finish()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
