#!/usr/bin/env python3
"""Bounded Jev advisor. All network and filesystem work stays outside the game."""
from __future__ import annotations
import argparse
import json
import os
import signal
import time
from pathlib import Path
import intents
import mailbox
import typesafe


def signature(team: str, obs: dict) -> str:
    money = int(obs["money_avg_us"])
    return "|".join(map(str, (
        "v2", os.environ.get("TYPESAFE_MODEL", typesafe.DEFAULT_MODEL), obs["map"], team,
        obs["phase"], obs["bomb"], obs["bomb_zone"],
        min(obs["alive_us"], obs["alive_them"]) <= 2,
        "eco" if money < 1200 else "force" if money < 4000 else "full",
        (obs["score_us"] > obs["score_them"]) - (obs["score_us"] < obs["score_them"]),
        obs.get("history", "").split(",")[-1],
    )))


def validate_observation(obs: dict | None, session: str, team: str) -> bool:
    if not isinstance(obs, dict):
        return False
    try:
        return (obs["v"] == 2 and obs["session"] == session and obs["team"] == team
                and obs["map"] == "de_dust2" and type(obs["round"]) is int
                and type(obs["revision"]) is int
                and obs.get("players_us", 0) > 0 and obs.get("players_them", 0) > 0
                and obs["alive_us"] > 0
                and 0 <= time.monotonic() - float(obs["observed_at"]) <= 10
                and all(type(obs[k]) is int and obs[k] >= 0 for k in (
                    "alive_us", "alive_them", "money_avg_us", "money_avg_them", "score_us", "score_them")))
    except (KeyError, TypeError, ValueError):
        return False


class Brain:
    def __init__(self, state: Path, session: str, generation: int, no_model: bool = False):
        self.state, self.session, self.generation = state, session, generation
        self.channels = state / "channels"
        self.no_model = no_model
        self.cache_path = state / "plan_cache.json"
        try:
            saved = json.loads(self.cache_path.read_text())
            self.cache = saved if isinstance(saved, dict) else {}
        except (OSError, ValueError):
            self.cache = {}
        self.last_call = {"T": 0.0, "CT": 0.0}
        self.last_observation = {"T": None, "CT": None}
        self.recent = {}
        self.sequence = time.monotonic_ns()
        self.stats = dict(model_calls=0, cache_hits=0, errors=0, decisions=0,
                          input_tokens=0, output_tokens=0, latency_ms=0,
                          transport_errors=0, invalid_responses=0)
        self.last_status = 0.0

    def log(self, text: str, severity: int = 0) -> None:
        print(json.dumps(dict(message=text, severity=severity), ensure_ascii=False), flush=True)

    def observation(self, team: str):
        suffix = "t" if team == "T" else "ct"
        obs = mailbox.read(self.channels / f"observe-{suffix}.bin", self.session, self.generation)
        return obs if validate_observation(obs, self.session, team) else None

    def decide(self, team: str, obs: dict) -> bool:
        sig = signature(team, obs)
        criteria = intents.criteria_for(team)
        hit = self.cache.get(sig)
        result = None
        event = tuple(obs[k] for k in ("session", "map", "round", "revision"))
        recent = self.recent.get(team)
        if recent and recent[0] == event and recent[1] in criteria:
            intent, source = recent[1], "cache"
            self.stats["cache_hits"] += 1
        elif isinstance(hit, str) and hit in criteria:
            intent, source = hit, "cache"
            self.stats["cache_hits"] += 1
        else:
            now = time.monotonic()
            if now - self.last_call[team] < 2:
                return False  # Keep observation pending; retry even if its mtime does not change.
            if self.no_model:
                return True
            self.last_call[team] = now  # Also bound failure retries.
            self.stats["model_calls"] += 1
            try:
                money = obs["money_avg_us"]
                state = dict(obs, buy_status="eco" if money < 1200 else "force" if money < 4000 else "full")
                result = typesafe.choose_operation(
                    state_text=json.dumps(state, ensure_ascii=False), criteria=criteria,
                    goal=intents.goal_for(team), rules=intents.rules_for(team))
                intent, source = result["choice"], "jev"
                usage = result.get("usage") or {}
                self.stats["input_tokens"] += int(usage.get("input_tokens") or 0)
                self.stats["output_tokens"] += int(usage.get("output_tokens") or 0)
                self.stats["latency_ms"] = result["latency_ms"]
            except Exception as exc:
                self.stats["errors"] += 1
                invalid = isinstance(exc, (ValueError, TypeError, KeyError, AttributeError))
                self.stats["invalid_responses" if invalid else "transport_errors"] += 1
                kind = "resposta inválida" if invalid else "falha de transporte/configuração"
                self.log(f"{team}: {kind}: {exc}", 2)
                return False
            current = self.observation(team)
            if current is None or any(current[k] != obs[k] for k in ("session", "map", "round", "revision")):
                self.log(f"{team}: resposta tardia descartada; contexto da partida mudou", 1)
                return True
            self.cache[sig] = intent
            self.save_cache()

        # Re-check age immediately before publication, including slow cache/file operations.
        if not validate_observation(obs, self.session, team):
            return True
        self.sequence += 1
        plan = {key: obs[key] for key in ("session", "map", "round", "revision", "observed_at", "team")}
        plan.update(v=2, seq=self.sequence, intent=intent, source=source)
        suffix = "t" if team == "T" else "ct"
        mailbox.write(self.channels / f"plan-{suffix}.bin", plan, self.session, self.generation)
        self.recent[team] = (event, intent)
        self.stats["decisions"] += 1
        trace = dict(plan, timestamp=time.time(), usage=(result or {}).get("usage"),
                     latency_ms=(result or {}).get("latency_ms"), confidence=(result or {}).get("confidence"))
        path = self.state / "decisions.jsonl"
        if path.exists() and path.stat().st_size > 4 * 1024 * 1024:
            path.replace(path.with_suffix(".jsonl.1"))
        with path.open("a") as f:
            f.write(json.dumps(trace, allow_nan=False) + "\n")
        if source == "jev":
            self.log(f"{team}: Jev → {intent} ({self.stats['latency_ms']} ms)")
        return True

    def save_cache(self):
        if len(self.cache) > 4096:
            self.cache = dict(list(self.cache.items())[-4096:])
        tmp = self.cache_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.cache))
        tmp.replace(self.cache_path)

    def tick(self):
        for team in ("T", "CT"):
            obs = self.observation(team)
            if obs is None or obs["observed_at"] == self.last_observation[team]:
                continue
            try:
                if self.decide(team, obs):
                    self.last_observation[team] = obs["observed_at"]
            except (OSError, ValueError, TypeError) as exc:
                self.stats["errors"] += 1
                self.log(f"{team}: falha na decisão: {exc}", 2)
        if time.monotonic() - self.last_status > .5:
            mailbox.write(self.channels / "brain-status.bin", self.stats, self.session, self.generation)
            self.last_status = time.monotonic()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--state", type=Path, default=os.environ.get("CS_MOD_STATE"))
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--no-model", action="store_true")
    args = parser.parse_args()
    if args.state is None:
        parser.error("set CS_MOD_STATE or --state")
    # Explicit config first; compatibility only when no current configuration exists.
    config = os.environ.get("TYPESAFE_ENV_FILE")
    if config:
        typesafe.load_env(Path(config))
    else:
        local = args.state / "secrets.env"
        typesafe.load_env(local)
        if not os.environ.get("TYPESAFE_API_KEY"):
            typesafe.load_env(Path(os.environ.get("JEV_DIR", Path.home() / "Workspace/jev-ultrafast")) / ".env")
    if not os.environ.get("TYPESAFE_API_KEY") and not args.no_model:
        raise SystemExit("TYPESAFE_API_KEY ausente; configure secrets.env no estado do mod ou TYPESAFE_ENV_FILE")
    brain = Brain(args.state, os.environ["CS_MOD_SESSION"], int(os.environ["CS_MOD_GENERATION"]), args.no_model)
    def stop(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    brain.log("Sidecar Jev iniciado; decisões limitadas, cache e expiração de planos ativos")
    try:
        while True:
            brain.tick()
            if args.once:
                break
            time.sleep(.1)
    except KeyboardInterrupt:
        pass
    finally:
        brain.save_cache()
        brain.log("Sessão encerrada: " + json.dumps(brain.stats))


if __name__ == "__main__":
    main()
