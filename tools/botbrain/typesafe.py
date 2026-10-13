"""Minimal TypeSafe ("Jev") client -- the closed-choice protocol.

This mirrors the proven client in ``jev_ultrafast/model.py`` (a local clone of
github.com/browser-use/jev-ultrafast): a single POST to ``/v1/systemone``
carries a compact ``state`` plus a ``questions`` map whose ``operation`` head
is a multiple-choice over ``criteria``. The answer is validated before it is
used, so a malformed response can never drive a decision.

We deliberately do not import the clone (its package pulls browser deps); we
reuse its protocol, its credentials (``~/Workspace/jev-ultrafast/.env``) and
its retry policy. One request == one decision.
"""

from __future__ import annotations

import math
import os
import time
from pathlib import Path

import httpx

ENDPOINT = "https://api.typesafe.ai/v1/systemone"
DEFAULT_MODEL = "jev-latest"

_CLIENT = httpx.Client(http2=True, timeout=25)


def load_env(path: Path) -> None:
    """setdefault-style .env loader: real environment variables win."""
    if not path.is_file():
        return
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def _post_json(url: str, key: str, body: dict) -> dict:
    headers = {"Authorization": f"Bearer {key}"}
    for attempt in range(3):
        try:
            response = _CLIENT.post(url, json=body, headers=headers)
        except httpx.HTTPError as exc:
            raise RuntimeError(f"TypeSafe connection failed: {exc}") from None
        if response.status_code in {429, 529, 503} and attempt < 2:
            time.sleep(0.5 * 2**attempt)
            continue
        if response.is_error:
            raise RuntimeError(
                f"TypeSafe returned HTTP {response.status_code}: {response.text[:200]}"
            )
        return response.json()
    raise RuntimeError("TypeSafe unavailable")


def _validate_choice(answer: dict, ids: set[str]) -> str:
    choice = answer.get("choice")
    if choice not in ids:
        raise ValueError(f"choice {choice!r} not in {sorted(ids)}")

    probs = answer.get("probabilities")
    if not isinstance(probs, dict) or set(probs) != ids:
        raise ValueError("probabilities must cover exactly the criteria ids")

    values = list(probs.values()) + [answer.get("confidence")]
    for value in values:
        if not isinstance(value, (int, float)) or not math.isfinite(value) or not 0.0 <= value <= 1.0:
            raise ValueError("probability/confidence out of range")

    if abs(sum(probs.values()) - 1.0) > 0.02:
        raise ValueError("probabilities must sum to ~1")

    best = max(probs, key=probs.get)
    if probs[choice] < probs[best] - 1e-6:
        raise ValueError("choice is not the argmax")

    return choice


def choose_operation(
    *,
    state_text: str,
    criteria: dict[str, str],
    goal: str,
    rules: str,
    model: str | None = None,
) -> dict:
    """One request -> one intent.

    Returns {choice, confidence, probabilities, model, usage, latency_ms}.
    """
    key = os.environ.get("TYPESAFE_API_KEY")
    if not key:
        raise RuntimeError("TYPESAFE_API_KEY is not set (expected in ~/Workspace/jev-ultrafast/.env)")

    ids = set(criteria)
    body = {
        "model": model or os.environ.get("TYPESAFE_MODEL", DEFAULT_MODEL),
        "state": {
            "page": {
                "url": "game://cs16/de_dust2",
                "title": "Counter-Strike round state",
                "text": state_text,
            },
            "elements": [],
            "recent_actions": [],
        },
        "questions": {
            "operation": {
                "type": "choice",
                "criteria": criteria,
                "instructions": {"goal": goal, "rules": rules},
            }
        },
    }

    started = time.monotonic()
    result = _post_json(ENDPOINT, key, body)
    latency_ms = int((time.monotonic() - started) * 1000)

    answers = result.get("answers")
    if not isinstance(answers, dict) or "operation" not in answers:
        raise ValueError("response has no answers.operation")

    choice = _validate_choice(answers["operation"], ids)

    return {
        "choice": choice,
        "confidence": answers["operation"].get("confidence"),
        "probabilities": answers["operation"].get("probabilities"),
        "model": result.get("model"),
        "usage": result.get("usage", {}),
        "latency_ms": latency_ms,
    }
